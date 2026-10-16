/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

/* Guest threads and semaphores (kernel32).
 *
 * A guest thread is an SDL thread that runs the recompiled start routine on a
 * CPU of its own (asm-cpu.c keeps one per host thread).  The functions take the
 * raw 32-bit guest values the import bridges of tools/gen_import_shim.py pass,
 * like the stubs of WinApi-stubs.c they replace.
 *
 * SuspendThread cannot stop a host thread from the outside: a suspended thread
 * stops at its next wait, Sleep or ReleaseSemaphore (Winapi_ThreadCheckpoint),
 * which is where a program that hands work over by semaphores is between two
 * pieces of work.
 *
 * WaitForSingleObject on a handle this layer does not know (0 from a stub such
 * as CreateEventA) returns WAIT_OBJECT_0, as the stub did. */

#include <stdint.h>
#include <stdlib.h>
#include <SDL.h>
#include "WinApi.h"
#include "WinApi-guest.h"
#include "Game_memory.h"

#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 0x102
#define INFINITE 0xffffffffu
#define CREATE_SUSPENDED 4
#define NORMAL_PRIORITY_CLASS 0x20
#define THREAD_PRIORITY_ERROR_RETURN 0x7fffffff
#define ERROR_TOO_MANY_POSTS 298

#define PSEUDO_CURRENT_PROCESS 0xffffffffu
#define PSEUDO_CURRENT_THREAD 0xfffffffeu

#if defined(__cplusplus)
#define THREAD_LOCAL thread_local
#else
#define THREAD_LOCAL _Thread_local
#endif

#ifdef __cplusplus
extern "C" {
#endif

void x86_deinitialize_cpu(void);

struct winapi_thread {
    SDL_mutex *lock;
    SDL_cond *cond;
    int refs;               /* the handle and the running thread */
    int suspend;            /* SuspendThread count */
    int32_t finished;
    uint32_t exit_code;
    int32_t priority;
    uint32_t start;
    uint32_t param;
};

struct winapi_semaphore {
    SDL_mutex *lock;
    SDL_cond *cond;
    int32_t count;
    int32_t max;
};

static THREAD_LOCAL struct winapi_thread *current_thread = NULL;
static SDL_atomic_t next_thread_id = { 1 };

static handle guest_handle(uint32_t value, handle_types type)
{
    handle h;

    if ((value == 0) || (value == PSEUDO_CURRENT_PROCESS) || (value == PSEUDO_CURRENT_THREAD)) return NULL;
    h = (handle)GAME_GUEST32_TO_PTR(value);
    return (h->handle_type == type) ? h : NULL;
}

static void thread_release(struct winapi_thread *t)
{
    int refs;

    SDL_LockMutex(t->lock);
    refs = --t->refs;
    SDL_UnlockMutex(t->lock);
    if (refs != 0) return;

    SDL_DestroyCond(t->cond);
    SDL_DestroyMutex(t->lock);
    free(t);
}

void Winapi_ThreadCheckpoint(void)
{
    struct winapi_thread *t = current_thread;

    if (t == NULL) return;
    SDL_LockMutex(t->lock);
    while (t->suspend > 0) SDL_CondWait(t->cond, t->lock);
    SDL_UnlockMutex(t->lock);
}

static int SDLCALL thread_main(void *data)
{
    struct winapi_thread *t = (struct winapi_thread *)data;
    uint32_t arg, code;

    current_thread = t;
    Winapi_ThreadCheckpoint();  /* CREATE_SUSPENDED */

    arg = t->param;
    code = Winapi_CallGuest(t->start, 1, &arg);
    x86_deinitialize_cpu();

    SDL_LockMutex(t->lock);
    t->finished = 1;
    t->exit_code = code;
    SDL_CondBroadcast(t->cond);
    SDL_UnlockMutex(t->lock);

    current_thread = NULL;
    thread_release(t);
    return 0;
}

uint32_t CCALL CreateThread_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    struct winapi_thread *t;
    SDL_Thread *thread;
    handle h;
    uint32_t id;

    (void)a0; (void)a1;     /* security attributes, stack size: the guest stack is 1 MB */
    if (a2 == 0) return 0;

    t = (struct winapi_thread *)calloc(1, sizeof(*t));
    if (t == NULL) return 0;
    h = Winapi_AllocHandle();
    if (h == NULL)
    {
        free(t);
        return 0;
    }
    t->lock = SDL_CreateMutex();
    t->cond = SDL_CreateCond();
    t->refs = 2;
    t->suspend = (a4 & CREATE_SUSPENDED) ? 1 : 0;
    t->start = a2;
    t->param = a3;

    h->oh.handle_type = HT_THREAD;
    h->oh.obj = t;

    id = (uint32_t)SDL_AtomicAdd(&next_thread_id, 1) + 1;
    thread = SDL_CreateThread(thread_main, "guest", t);
    if (thread == NULL)
    {
        SDL_DestroyCond(t->cond);
        SDL_DestroyMutex(t->lock);
        free(t);
        x86_free(h);
        return 0;
    }
    SDL_DetachThread(thread);

    if (a5 != 0) *(uint32_t *)GAME_GUEST32_TO_PTR(a5) = id;
    return GAME_PTR_TO_GUEST32(h);
}

uint32_t CCALL SuspendThread_c(uint32_t a0)
{
    handle h = guest_handle(a0, HT_THREAD);
    struct winapi_thread *t;
    int prev;

    if (h == NULL) return 0xffffffffu;
    t = (struct winapi_thread *)h->oh.obj;
    SDL_LockMutex(t->lock);
    prev = t->suspend++;
    SDL_UnlockMutex(t->lock);
    return (uint32_t)prev;
}

uint32_t CCALL ResumeThread_c(uint32_t a0)
{
    handle h = guest_handle(a0, HT_THREAD);
    struct winapi_thread *t;
    int prev;

    if (h == NULL) return 0xffffffffu;
    t = (struct winapi_thread *)h->oh.obj;
    SDL_LockMutex(t->lock);
    prev = t->suspend;
    if (t->suspend > 0) t->suspend--;
    if (t->suspend == 0) SDL_CondBroadcast(t->cond);
    SDL_UnlockMutex(t->lock);
    return (uint32_t)prev;
}

uint32_t Winapi_SetThreadPriority(handle hThread, int32_t nPriority)
{
    struct winapi_thread *t;

    t = (hThread != NULL) ? (struct winapi_thread *)hThread->oh.obj : current_thread;
    if (t != NULL) t->priority = nPriority;
    /* the host priority is left as it is */
    return 1;
}

uint32_t CCALL GetThreadPriority_c(uint32_t a0)
{
    handle h;
    struct winapi_thread *t;

    if (a0 == PSEUDO_CURRENT_THREAD)
    {
        t = current_thread;
    }
    else
    {
        h = guest_handle(a0, HT_THREAD);
        if (h == NULL) return THREAD_PRIORITY_ERROR_RETURN;
        t = (struct winapi_thread *)h->oh.obj;
    }
    return (t != NULL) ? (uint32_t)t->priority : 0;
}

uint32_t CCALL GetPriorityClass_c(uint32_t a0)
{
    (void)a0;
    return NORMAL_PRIORITY_CLASS;
}

uint32_t CCALL CreateSemaphoreA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    struct winapi_semaphore *s;
    handle h;

    (void)a0; (void)a3;     /* security attributes; a named semaphore is not shared */
    if (((int32_t)a2 <= 0) || ((int32_t)a1 < 0) || ((int32_t)a1 > (int32_t)a2))
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    s = (struct winapi_semaphore *)calloc(1, sizeof(*s));
    if (s == NULL) return 0;
    h = Winapi_AllocHandle();
    if (h == NULL)
    {
        free(s);
        return 0;
    }
    s->lock = SDL_CreateMutex();
    s->cond = SDL_CreateCond();
    s->count = (int32_t)a1;
    s->max = (int32_t)a2;

    h->oh.handle_type = HT_SEMAPHORE;
    h->oh.obj = s;
    return GAME_PTR_TO_GUEST32(h);
}

uint32_t CCALL ReleaseSemaphore_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    handle h = guest_handle(a0, HT_SEMAPHORE);
    struct winapi_semaphore *s;
    int32_t prev;

    Winapi_ThreadCheckpoint();
    if (h == NULL)
    {
        Winapi_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    s = (struct winapi_semaphore *)h->oh.obj;

    SDL_LockMutex(s->lock);
    prev = s->count;
    if (((int32_t)a1 <= 0) || ((int32_t)a1 > s->max - s->count))
    {
        SDL_UnlockMutex(s->lock);
        Winapi_SetLastError(ERROR_TOO_MANY_POSTS);
        return 0;
    }
    s->count += (int32_t)a1;
    SDL_CondBroadcast(s->cond);
    SDL_UnlockMutex(s->lock);

    if (a2 != 0) *(int32_t *)GAME_GUEST32_TO_PTR(a2) = prev;
    return 1;
}

/* wait on cond, lock held, while *value is 0 or less: at most ms milliseconds */
static int wait_positive(SDL_cond *cond, SDL_mutex *lock, const int32_t *value, uint32_t ms)
{
    uint32_t start, elapsed;

    start = SDL_GetTicks();
    while (*value <= 0)
    {
        if (ms == INFINITE)
        {
            SDL_CondWait(cond, lock);
            continue;
        }
        elapsed = SDL_GetTicks() - start;
        if (elapsed >= ms) return 0;
        SDL_CondWaitTimeout(cond, lock, ms - elapsed);
    }
    return 1;
}

uint32_t CCALL WaitForSingleObject_c(uint32_t a0, uint32_t a1)
{
    handle h;
    uint32_t start;

    Winapi_ThreadCheckpoint();
    if ((a0 == 0) || (a0 == PSEUDO_CURRENT_PROCESS) || (a0 == PSEUDO_CURRENT_THREAD)) return WAIT_OBJECT_0;
    h = (handle)GAME_GUEST32_TO_PTR(a0);

    switch (h->handle_type)
    {
        case HT_SEMAPHORE:
        {
            struct winapi_semaphore *s = (struct winapi_semaphore *)h->oh.obj;
            int done;

            SDL_LockMutex(s->lock);
            done = wait_positive(s->cond, s->lock, &s->count, a1);
            if (done) s->count--;
            SDL_UnlockMutex(s->lock);
            return done ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
        }
        case HT_THREAD:
        {
            struct winapi_thread *t = (struct winapi_thread *)h->oh.obj;
            int done;

            SDL_LockMutex(t->lock);
            done = wait_positive(t->cond, t->lock, &t->finished, a1);
            SDL_UnlockMutex(t->lock);
            return done ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
        }
        case HT_EVENT:
            start = SDL_GetTicks();
            while (!h->eh.signaled)
            {
                if ((a1 != INFINITE) && (SDL_GetTicks() - start >= a1)) return WAIT_TIMEOUT;
                SDL_Delay(1);
            }
            if (!h->eh.manual_reset) h->eh.signaled = 0;
            return WAIT_OBJECT_0;
        default:
            return WAIT_OBJECT_0;
    }
}

uint32_t Winapi_CloseObject(handle hObject)
{
    if (hObject->handle_type == HT_THREAD)
    {
        thread_release((struct winapi_thread *)hObject->oh.obj);
    }
    else
    {
        struct winapi_semaphore *s = (struct winapi_semaphore *)hObject->oh.obj;

        SDL_DestroyCond(s->cond);
        SDL_DestroyMutex(s->lock);
        free(s);
    }
    x86_free(hObject);
    return 1;
}

#ifdef __cplusplus
}
#endif
