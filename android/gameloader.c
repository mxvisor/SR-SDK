/* Loads libmain.so for GameActivity, into a block of its own.
 *
 * The game keeps code pointers as (address - pointer_offset) in 32 bits, so
 * the image has to be within 4 GB above the guest's memory.  System.loadLibrary
 * puts it wherever the linker likes, and ART keeps gigabytes of its own around
 * it: what is free next to it differs by device and by boot.  So one block is
 * reserved first, the guest's 2 GB and the image right above them, and the
 * linker is told to use its upper part.  Game_AndroidGuestRegion takes the
 * lower part from the environment. */

#include <dlfcn.h>
#include <jni.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <android/dlext.h>

#define GUEST_SIZE ((size_t)0x80000000)
#define IMAGE_SIZE ((size_t)0x20000000)

static void *image;

JNIEXPORT jstring JNICALL
Java_io_github_mxvisor_srsdk_GameActivity_nativeLoadMain(JNIEnv *env, jclass cls, jstring path)
{
    android_dlextinfo info = { 0 };
    char value[32];
    const char *name;
    char *block;

    (void)cls;
    if (image != NULL) return NULL; /* a new activity in the same process */

    block = (char *)mmap(NULL, GUEST_SIZE + IMAGE_SIZE, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (block == (char *)MAP_FAILED)
    {
        return (*env)->NewStringUTF(env, "Cannot reserve the address space of the game");
    }

    info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    info.reserved_addr = block + GUEST_SIZE;
    info.reserved_size = IMAGE_SIZE;
    name = (*env)->GetStringUTFChars(env, path, NULL);
    image = android_dlopen_ext(name, RTLD_NOW | RTLD_GLOBAL, &info);
    (*env)->ReleaseStringUTFChars(env, path, name);
    if (image == NULL)
    {
        munmap(block, GUEST_SIZE + IMAGE_SIZE);
        return (*env)->NewStringUTF(env, dlerror());
    }

    snprintf(value, sizeof(value), "%llx", (unsigned long long)(uintptr_t)block);
    setenv("SR_SDK_GUEST_REGION", value, 1);
    return NULL;
}
