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

/* Real-mode code in the DOS memory, run by an interpreter (Game_dosreal.h),
 * for a game that says GAME_DOS_REALMODE in Game_hooks.h; for the others the
 * functions do nothing, and an interrupt with no service stays unserved.
 *
 * A DOS4GW game may load a driver of 16-bit code into a DOS block (DPMI
 * 0100h), point a real-mode interrupt vector at it (DPMI 0201h) and call it
 * with INT n from protected mode: the extender passes an interrupt the
 * protected-mode code does not handle on to the real-mode vector.  The driver
 * then sets vectors of its own with INT 21h AH=25h (its handler of the sound
 * card's IRQ), and the IRQ, when the game's protected-mode handler passes it
 * on (Game_DosHardwareInterrupt), reaches that handler too.
 *
 * The interpreter is the 8086's instruction set with the 80186's additions
 * (PUSHA, ENTER, the shifts by an immediate, IMUL by an immediate, INS and
 * OUTS).  From DOS_PSP_ADDRESS to the end of the VGA window its memory is the
 * DOS memory at its own addresses (Game_dosmem.h: segment << 4 plus offset is
 * the guest address); below it is memory of its own, with the interrupt
 * vector table, which the game's code never sees (it reads zeros there through
 * its null pointers).  Its ports are those of the translated code
 * (X86_InPortProcedure, X86_OutPortProcedure).
 *
 * A vector the code has not set is F000:E000 + n, where the runtime's service
 * of INT n runs (INT 21h AH=25h and 35h on the vectors here) and returns as a
 * BIOS handler does.  A call from the host pushes an iret frame to F000:F000
 * and ends when the code returns there.  Its stack is 0F00:1000 down, in the
 * memory of its own; a call made while another one runs (the sound card's
 * IRQ, raised by a port the code writes) goes on the stack the code is on, as
 * the IRQ would.  An opcode it does not know (the 80386's 32-bit prefixes, the
 * FPU), a division by zero or a call still running after 50 million
 * instructions ends the call with a message. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Game_defs.h"
#include "Game_dosmem.h"
#include "Game_dosint.h"
#include "Game_dosreal.h"
#include "Game_hooks.h"

#ifndef GAME_DOS_REALMODE

void Game_DosRealSetVector(uint8_t vector, uint16_t segment, uint16_t offset)
{
    (void)vector;
    (void)segment;
    (void)offset;
}

uint32_t Game_DosRealGetVector(uint8_t vector)
{
    (void)vector;
    return 0;
}

void Game_DosRealReset(void)
{
}

int Game_DosRealInterrupt(uint8_t vector, Game_DosRegs *regs, int ds, int es)
{
    (void)vector;
    (void)regs;
    (void)ds;
    (void)es;
    return 0;
}

#else

extern "C" uint32_t CCALL X86_InPortProcedure(uint16_t port, uint32_t size);
extern "C" void CCALL X86_OutPortProcedure(uint16_t port, uint32_t size, uint32_t value);

#define LOW_END (DOS_VGA_ADDRESS + DOS_VGA_SIZE)

#define STUB_SEGMENT 0xf000u
#define STUB_OFFSET 0xe000u      /* + n: the runtime's service of INT n */
#define RETURN_OFFSET 0xf000u    /* the end of a call from the host */
#define STACK_SEGMENT 0x0f00u
#define STACK_TOP 0x1000u
/* a call that runs longer is stuck (a driver waiting for its own IRQ) */
#define MAX_INSTRUCTIONS 50000000u

enum { AX, CX, DX, BX, SP, BP, SI, DI };
enum { ES, CS, SS, DS };

#define CF 0x0001u
#define PF 0x0004u
#define AF 0x0010u
#define ZF 0x0040u
#define SF 0x0080u
#define TF 0x0100u
#define IF 0x0200u
#define DF 0x0400u
#define OF 0x0800u
#define ARITHMETIC (CF | PF | AF | ZF | SF | OF)

typedef struct {
    uint16_t r[8];
    uint16_t s[4];
    uint16_t ip;
    uint16_t flags;
    int segment;        /* the segment override of the instruction, -1: none */
    uint16_t start;     /* the IP of the instruction, for the messages */
    int stop;           /* a fault: the call ends */
} rm_cpu;

typedef struct {
    int mod, reg, rm;
    uint16_t offset;
    uint32_t address;
} rm_operand;

/* the DOS memory from 0 (the part below DOS_PSP_ADDRESS is not used) */
static uint8_t *memory;
/* below DOS_PSP_ADDRESS: the vector table, the BIOS's data, the stack */
static uint8_t low[DOS_PSP_ADDRESS];
/* the vectors as DPMI 0201h and INT 21h AH=25h set them */
static uint32_t vectors[256];
/* the call running, NULL between calls */
static rm_cpu *current;


static int trace_real(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv(GAME_ENV("TRACE_INT")) != NULL;
    return enabled;
}

static inline uint32_t linear(uint16_t segment, uint16_t offset)
{
    return (((uint32_t)segment << 4) + offset) & 0xfffffu;
}

static inline uint8_t read8(uint32_t address)
{
    if (address < DOS_PSP_ADDRESS) return low[address];
    return address < LOW_END ? memory[address] : 0xffu;
}

static inline uint16_t read16(uint32_t address)
{
    const uint16_t low = read8(address);
    return (uint16_t)(low | (read8((address + 1u) & 0xfffffu) << 8));
}

static inline void write8(uint32_t address, uint8_t value)
{
    if (address < DOS_PSP_ADDRESS) low[address] = value;
    else if (address < LOW_END) memory[address] = value;
}

static inline void write16(uint32_t address, uint16_t value)
{
    write8(address, (uint8_t)value);
    write8((address + 1u) & 0xfffffu, (uint8_t)(value >> 8));
}

static inline uint8_t fetch8(rm_cpu *c)
{
    const uint8_t value = read8(linear(c->s[CS], c->ip));
    c->ip++;
    return value;
}

static inline uint16_t fetch16(rm_cpu *c)
{
    const uint16_t low = fetch8(c);
    return (uint16_t)(low | (fetch8(c) << 8));
}

static inline void push(rm_cpu *c, uint16_t value)
{
    c->r[SP] -= 2;
    write16(linear(c->s[SS], c->r[SP]), value);
}

static inline uint16_t pop(rm_cpu *c)
{
    const uint16_t value = read16(linear(c->s[SS], c->r[SP]));
    c->r[SP] += 2;
    return value;
}

static inline uint8_t get8(const rm_cpu *c, int index)
{
    return index < 4 ? (uint8_t)c->r[index] : (uint8_t)(c->r[index - 4] >> 8);
}

static inline void set8(rm_cpu *c, int index, uint8_t value)
{
    if (index < 4) c->r[index] = (uint16_t)((c->r[index] & 0xff00u) | value);
    else c->r[index - 4] = (uint16_t)((c->r[index - 4] & 0x00ffu) | (value << 8));
}

static inline uint16_t data_segment(const rm_cpu *c)
{
    return c->s[c->segment >= 0 ? c->segment : DS];
}

static void decode(rm_cpu *c, rm_operand *m)
{
    const uint8_t byte = fetch8(c);
    int segment = DS;
    uint16_t offset = 0;

    m->mod = byte >> 6;
    m->reg = (byte >> 3) & 7;
    m->rm = byte & 7;
    if (m->mod == 3) return;
    switch (m->rm) {
        case 0: offset = (uint16_t)(c->r[BX] + c->r[SI]); break;
        case 1: offset = (uint16_t)(c->r[BX] + c->r[DI]); break;
        case 2: offset = (uint16_t)(c->r[BP] + c->r[SI]); segment = SS; break;
        case 3: offset = (uint16_t)(c->r[BP] + c->r[DI]); segment = SS; break;
        case 4: offset = c->r[SI]; break;
        case 5: offset = c->r[DI]; break;
        case 6:
            if (m->mod == 0) offset = fetch16(c);
            else { offset = c->r[BP]; segment = SS; }
            break;
        default: offset = c->r[BX]; break;
    }
    if (m->mod == 1) offset = (uint16_t)(offset + (uint16_t)(int8_t)fetch8(c));
    else if (m->mod == 2) offset = (uint16_t)(offset + fetch16(c));
    if (c->segment >= 0) segment = c->segment;
    m->offset = offset;
    m->address = linear(c->s[segment], offset);
}

static inline uint16_t get_rm(const rm_cpu *c, const rm_operand *m, int word)
{
    if (m->mod == 3) return word ? c->r[m->rm] : get8(c, m->rm);
    return word ? read16(m->address) : read8(m->address);
}

static inline void set_rm(rm_cpu *c, const rm_operand *m, int word, uint16_t value)
{
    if (m->mod == 3) {
        if (word) c->r[m->rm] = value;
        else set8(c, m->rm, (uint8_t)value);
    } else if (word) write16(m->address, value);
    else write8(m->address, (uint8_t)value);
}

static inline uint16_t get_reg(const rm_cpu *c, int index, int word)
{
    return word ? c->r[index] : get8(c, index);
}

static inline void set_reg(rm_cpu *c, int index, int word, uint16_t value)
{
    if (word) c->r[index] = value;
    else set8(c, index, (uint8_t)value);
}

static void szp(rm_cpu *c, uint32_t result, int word)
{
    uint8_t low = (uint8_t)result;

    result &= word ? 0xffffu : 0xffu;
    c->flags &= (uint16_t)~(ZF | SF | PF);
    if (result == 0) c->flags |= ZF;
    if (result & (word ? 0x8000u : 0x80u)) c->flags |= SF;
    low ^= (uint8_t)(low >> 4);
    if (((0x6996u >> (low & 0x0fu)) & 1u) == 0) c->flags |= PF;
}

/* ADD OR ADC SBB AND SUB XOR CMP, by the opcode's bits 3-5 */
static uint16_t alu(rm_cpu *c, int op, uint16_t a, uint16_t b, int word)
{
    const uint32_t mask = word ? 0xffffu : 0xffu;
    const uint32_t sign = word ? 0x8000u : 0x80u;
    uint32_t result, carry;

    switch (op) {
        case 0:
        case 2:
            carry = (op == 2) ? (c->flags & CF) : 0;
            result = (uint32_t)a + b + carry;
            c->flags &= (uint16_t)~(CF | OF | AF);
            if (result > mask) c->flags |= CF;
            if ((a ^ result) & (b ^ result) & sign) c->flags |= OF;
            if ((a ^ b ^ result) & 0x10u) c->flags |= AF;
            break;
        case 3:
        case 5:
        case 7:
            carry = (op == 3) ? (c->flags & CF) : 0;
            result = (uint32_t)a - b - carry;
            c->flags &= (uint16_t)~(CF | OF | AF);
            if ((uint32_t)a < (uint32_t)b + carry) c->flags |= CF;
            if ((a ^ b) & (a ^ result) & sign) c->flags |= OF;
            if ((a ^ b ^ result) & 0x10u) c->flags |= AF;
            break;
        case 1: result = (uint32_t)(a | b); c->flags &= (uint16_t)~(CF | OF | AF); break;
        case 4: result = (uint32_t)(a & b); c->flags &= (uint16_t)~(CF | OF | AF); break;
        default: result = (uint32_t)(a ^ b); c->flags &= (uint16_t)~(CF | OF | AF); break;
    }
    szp(c, result, word);
    return (uint16_t)(result & mask);
}

static uint16_t inc_dec(rm_cpu *c, uint16_t value, int decrement, int word)
{
    const uint16_t carry = c->flags & CF;
    const uint16_t result = alu(c, decrement ? 5 : 0, value, 1, word);
    c->flags = (uint16_t)((c->flags & ~CF) | carry);
    return result;
}

/* ROL ROR RCL RCR SHL SHR SAL SAR, by the reg field; the count as the 80186
 * takes it, modulo 32 */
static uint16_t shift(rm_cpu *c, int op, uint16_t value, unsigned int count, int word)
{
    const unsigned int bits = word ? 16u : 8u;
    const uint32_t mask = word ? 0xffffu : 0xffu;
    uint32_t v = value & mask;
    uint32_t carry = c->flags & CF;
    unsigned int index;

    count &= 0x1fu;
    if (count == 0) return value;
    switch (op) {
        case 0:
            for (index = 0; index < count; index++) {
                carry = (v >> (bits - 1)) & 1u;
                v = ((v << 1) | carry) & mask;
            }
            c->flags = (uint16_t)((c->flags & ~(CF | OF)) | (carry ? CF : 0));
            if (((v >> (bits - 1)) & 1u) ^ carry) c->flags |= OF;
            return (uint16_t)v;
        case 1:
            for (index = 0; index < count; index++) {
                carry = v & 1u;
                v = (v >> 1) | (carry << (bits - 1));
            }
            c->flags = (uint16_t)((c->flags & ~(CF | OF)) | (carry ? CF : 0));
            if (((v >> (bits - 1)) ^ (v >> (bits - 2))) & 1u) c->flags |= OF;
            return (uint16_t)v;
        case 2:
            for (index = 0; index < count; index++) {
                const uint32_t out = (v >> (bits - 1)) & 1u;
                v = ((v << 1) | carry) & mask;
                carry = out;
            }
            c->flags = (uint16_t)((c->flags & ~(CF | OF)) | (carry ? CF : 0));
            if (((v >> (bits - 1)) & 1u) ^ carry) c->flags |= OF;
            return (uint16_t)v;
        case 3:
            c->flags &= (uint16_t)~OF;
            if ((((v >> (bits - 1)) & 1u) ^ carry) != 0) c->flags |= OF;
            for (index = 0; index < count; index++) {
                const uint32_t out = v & 1u;
                v = (v >> 1) | (carry << (bits - 1));
                carry = out;
            }
            c->flags = (uint16_t)((c->flags & ~CF) | (carry ? CF : 0));
            return (uint16_t)v;
        case 4:
        case 6:
            for (index = 0; index < count; index++) {
                carry = (v >> (bits - 1)) & 1u;
                v = (v << 1) & mask;
            }
            c->flags = (uint16_t)((c->flags & ~(CF | OF | AF)) | (carry ? CF : 0));
            if (((v >> (bits - 1)) & 1u) ^ carry) c->flags |= OF;
            szp(c, v, word);
            return (uint16_t)v;
        case 5:
            c->flags &= (uint16_t)~(OF | AF);
            if ((v >> (bits - 1)) & 1u) c->flags |= OF;
            for (index = 0; index < count; index++) {
                carry = v & 1u;
                v >>= 1;
            }
            c->flags = (uint16_t)((c->flags & ~CF) | (carry ? CF : 0));
            szp(c, v, word);
            return (uint16_t)v;
        default: {
            const uint32_t sign = 1u << (bits - 1);
            for (index = 0; index < count; index++) {
                carry = v & 1u;
                v = (v >> 1) | (v & sign);
            }
            c->flags = (uint16_t)((c->flags & ~(CF | OF | AF)) | (carry ? CF : 0));
            szp(c, v, word);
            return (uint16_t)v;
        }
    }
}

static int condition(const rm_cpu *c, int code)
{
    const uint16_t f = c->flags;
    int result;

    switch (code >> 1) {
        case 0: result = (f & OF) != 0; break;
        case 1: result = (f & CF) != 0; break;
        case 2: result = (f & ZF) != 0; break;
        case 3: result = (f & (CF | ZF)) != 0; break;
        case 4: result = (f & SF) != 0; break;
        case 5: result = (f & PF) != 0; break;
        case 6: result = ((f & SF) != 0) != ((f & OF) != 0); break;
        default: result = (f & ZF) != 0 || ((f & SF) != 0) != ((f & OF) != 0); break;
    }
    return result ^ (code & 1);
}

static void fault(rm_cpu *c, const char *what)
{
    static uint32_t reported[8];
    static unsigned int count;
    const uint32_t where = linear(c->s[CS], c->start);

    c->stop = 1;
    for (unsigned int index = 0; index < count; index++) if (reported[index] == where) return;
    if (count < sizeof(reported) / sizeof(reported[0])) reported[count++] = where;
    fprintf(stderr, GAME_NAME ": real-mode code at %04X:%04X: %s (bytes %02X %02X %02X %02X)\n",
            c->s[CS], c->start, what, read8(where), read8(where + 1u), read8(where + 2u), read8(where + 3u));
}

static uint16_t port_in(uint16_t port, int word)
{
    return (uint16_t)X86_InPortProcedure(port, word ? 2u : 1u);
}

static void port_out(uint16_t port, int word, uint16_t value)
{
    /* a byte is AL alone */
    X86_OutPortProcedure(port, word ? 2u : 1u, word ? value : (uint32_t)(value & 0xffu));
}

static int vector_in_dos_memory(uint32_t vector)
{
    const uint32_t address = linear((uint16_t)(vector >> 16), (uint16_t)vector);
    return vector != 0 && address >= DOS_PSP_ADDRESS && address < DOS_MEMORY_END;
}

/* the table at 0000:0000, which the code may also write itself */
static uint32_t ivt_read(uint8_t vector)
{
    const uint8_t *entry = low + (unsigned int)vector * 4u;
    return (uint32_t)(entry[0] | (entry[1] << 8)) | ((uint32_t)(entry[2] | (entry[3] << 8)) << 16);
}

static void ivt_write(uint8_t vector, uint32_t value)
{
    uint8_t *entry = low + (unsigned int)vector * 4u;
    entry[0] = (uint8_t)value;
    entry[1] = (uint8_t)(value >> 8);
    entry[2] = (uint8_t)(value >> 16);
    entry[3] = (uint8_t)(value >> 24);
}

/* where INT n goes: the code's handler, or the stub of the runtime's service */
static uint32_t vector_target(uint8_t vector)
{
    const uint32_t target = ivt_read(vector);
    if (vector_in_dos_memory(target)) return target;
    return (STUB_SEGMENT << 16) | (STUB_OFFSET + vector);
}

/* The runtime's service of INT n for the real-mode code, in place: its
 * registers, the flags as the service leaves them.  The pointers of INT 21h
 * are DS:DX, which the runtime's services (made for the flat code) do not
 * read, so of INT 21h only what takes none is passed on. */
static void service(rm_cpu *c, uint8_t vector)
{
    const uint8_t ah = (uint8_t)(c->r[AX] >> 8);
    Game_DosRegs regs;

    if (vector == 0x21 && ah == 0x25) {
        vectors[(uint8_t)c->r[AX]] = ((uint32_t)c->s[DS] << 16) | c->r[DX];
        ivt_write((uint8_t)c->r[AX], vectors[(uint8_t)c->r[AX]]);
        if (trace_real()) fprintf(stderr, "  real-mode vector %02x = %04x:%04x\n", (uint8_t)c->r[AX], c->s[DS], c->r[DX]);
        return;
    }
    if (vector == 0x21 && ah == 0x35) {
        const uint32_t target = vector_target((uint8_t)c->r[AX]);
        c->s[ES] = (uint16_t)(target >> 16);
        c->r[BX] = (uint16_t)target;
        return;
    }
    if (vector != 0x10 && vector != 0x16 && vector != 0x1a && vector != 0x21 && vector != 0x2f && vector != 0x33) {
        /* the BIOS's handler of an IRQ, the user's timer tick: an iret */
        if (trace_real()) fprintf(stderr, "  real-mode int %02x: no handler\n", vector);
        return;
    }
    if (vector == 0x21 && ah != 0x2a && ah != 0x2c && ah != 0x30) {
        static uint8_t reported[256];
        if (!reported[ah]) {
            reported[ah] = 1;
            fprintf(stderr, GAME_NAME ": real-mode INT 21h AH=%02Xh is not served\n", ah);
        }
        c->flags |= CF;
        return;
    }
    memset(&regs, 0, sizeof(regs));
    regs.eax = c->r[AX];
    regs.ebx = c->r[BX];
    regs.ecx = c->r[CX];
    regs.edx = c->r[DX];
    regs.esi = c->r[SI];
    regs.edi = c->r[DI];
    regs.ebp = c->r[BP];
    regs.eflags = c->flags;
    Game_DosDispatch(vector, &regs);
    c->r[AX] = (uint16_t)regs.eax;
    c->r[BX] = (uint16_t)regs.ebx;
    c->r[CX] = (uint16_t)regs.ecx;
    c->r[DX] = (uint16_t)regs.edx;
    c->r[SI] = (uint16_t)regs.esi;
    c->r[DI] = (uint16_t)regs.edi;
    c->r[BP] = (uint16_t)regs.ebp;
    c->flags = (uint16_t)((c->flags & ~ARITHMETIC) | (regs.eflags & ARITHMETIC));
}

static void interrupt(rm_cpu *c, uint8_t vector)
{
    const uint32_t target = vector_target(vector);

    if ((target >> 16) == STUB_SEGMENT) {
        service(c, vector);
        return;
    }
    push(c, c->flags);
    push(c, c->s[CS]);
    push(c, c->ip);
    c->flags &= (uint16_t)~(IF | TF);
    c->s[CS] = (uint16_t)(target >> 16);
    c->ip = (uint16_t)target;
}

static void string_op(rm_cpu *c, uint8_t op, uint8_t rep)
{
    const int word = op & 1;
    const uint16_t delta = (uint16_t)((c->flags & DF) ? (word ? -2 : -1) : (word ? 2 : 1));
    const int compares = (op & 0xf6) == 0xa6;      /* CMPS, SCAS */

    for (;;) {
        if (rep && c->r[CX] == 0) break;
        switch (op) {
            case 0xa4:
            case 0xa5: {
                const uint32_t from = linear(data_segment(c), c->r[SI]);
                const uint32_t to = linear(c->s[ES], c->r[DI]);
                if (word) write16(to, read16(from));
                else write8(to, read8(from));
                c->r[SI] = (uint16_t)(c->r[SI] + delta);
                c->r[DI] = (uint16_t)(c->r[DI] + delta);
                break;
            }
            case 0xa6:
            case 0xa7: {
                const uint32_t from = linear(data_segment(c), c->r[SI]);
                const uint32_t to = linear(c->s[ES], c->r[DI]);
                alu(c, 7, word ? read16(from) : read8(from), word ? read16(to) : read8(to), word);
                c->r[SI] = (uint16_t)(c->r[SI] + delta);
                c->r[DI] = (uint16_t)(c->r[DI] + delta);
                break;
            }
            case 0xaa:
            case 0xab: {
                const uint32_t to = linear(c->s[ES], c->r[DI]);
                if (word) write16(to, c->r[AX]);
                else write8(to, (uint8_t)c->r[AX]);
                c->r[DI] = (uint16_t)(c->r[DI] + delta);
                break;
            }
            case 0xac:
            case 0xad: {
                const uint32_t from = linear(data_segment(c), c->r[SI]);
                set_reg(c, AX, word, word ? read16(from) : read8(from));
                c->r[SI] = (uint16_t)(c->r[SI] + delta);
                break;
            }
            case 0xae:
            case 0xaf: {
                const uint32_t to = linear(c->s[ES], c->r[DI]);
                alu(c, 7, get_reg(c, AX, word), word ? read16(to) : read8(to), word);
                c->r[DI] = (uint16_t)(c->r[DI] + delta);
                break;
            }
            case 0x6c:
            case 0x6d: {
                const uint32_t to = linear(c->s[ES], c->r[DI]);
                const uint16_t value = port_in(c->r[DX], word);
                if (word) write16(to, value);
                else write8(to, (uint8_t)value);
                c->r[DI] = (uint16_t)(c->r[DI] + delta);
                break;
            }
            default: {
                const uint32_t from = linear(data_segment(c), c->r[SI]);
                port_out(c->r[DX], word, word ? read16(from) : read8(from));
                c->r[SI] = (uint16_t)(c->r[SI] + delta);
                break;
            }
        }
        if (!rep) break;
        c->r[CX]--;
        if (compares && rep == 0xf3 && (c->flags & ZF) == 0) break;
        if (compares && rep == 0xf2 && (c->flags & ZF) != 0) break;
    }
}

/* F6h/F7h: TEST NOT NEG MUL IMUL DIV IDIV */
static void group3(rm_cpu *c, int word)
{
    rm_operand m;
    uint16_t value;

    decode(c, &m);
    value = get_rm(c, &m, word);
    switch (m.reg) {
        case 0:
        case 1:
            alu(c, 4, value, word ? fetch16(c) : fetch8(c), word);
            break;
        case 2:
            set_rm(c, &m, word, (uint16_t)~value);
            break;
        case 3:
            set_rm(c, &m, word, alu(c, 5, 0, value, word));
            break;
        case 4:
            if (word) {
                const uint32_t product = (uint32_t)c->r[AX] * value;
                c->r[AX] = (uint16_t)product;
                c->r[DX] = (uint16_t)(product >> 16);
                c->flags = (uint16_t)((c->flags & ~(CF | OF)) | (c->r[DX] ? (CF | OF) : 0));
            } else {
                c->r[AX] = (uint16_t)((uint8_t)c->r[AX] * value);
                c->flags = (uint16_t)((c->flags & ~(CF | OF)) | ((c->r[AX] >> 8) ? (CF | OF) : 0));
            }
            break;
        case 5:
            if (word) {
                const int32_t product = (int32_t)(int16_t)c->r[AX] * (int16_t)value;
                c->r[AX] = (uint16_t)product;
                c->r[DX] = (uint16_t)((uint32_t)product >> 16);
                c->flags = (uint16_t)((c->flags & ~(CF | OF)) | (product != (int16_t)product ? (CF | OF) : 0));
            } else {
                const int16_t product = (int16_t)((int8_t)c->r[AX] * (int8_t)value);
                c->r[AX] = (uint16_t)product;
                c->flags = (uint16_t)((c->flags & ~(CF | OF)) | (product != (int8_t)product ? (CF | OF) : 0));
            }
            break;
        case 6:
            if (word) {
                const uint32_t dividend = ((uint32_t)c->r[DX] << 16) | c->r[AX];
                if (value == 0 || dividend / value > 0xffffu) { fault(c, "divide error"); return; }
                c->r[AX] = (uint16_t)(dividend / value);
                c->r[DX] = (uint16_t)(dividend % value);
            } else {
                const uint16_t dividend = c->r[AX];
                if (value == 0 || dividend / value > 0xffu) { fault(c, "divide error"); return; }
                c->r[AX] = (uint16_t)(((dividend % value) << 8) | (dividend / value));
            }
            break;
        default:
            if (word) {
                const int32_t dividend = (int32_t)(((uint32_t)c->r[DX] << 16) | c->r[AX]);
                int32_t quotient;
                if ((int16_t)value == 0) { fault(c, "divide error"); return; }
                quotient = dividend / (int16_t)value;
                if (quotient > 32767 || quotient < -32768) { fault(c, "divide error"); return; }
                c->r[AX] = (uint16_t)quotient;
                c->r[DX] = (uint16_t)(dividend % (int16_t)value);
            } else {
                const int16_t dividend = (int16_t)c->r[AX];
                int16_t quotient;
                if ((int8_t)value == 0) { fault(c, "divide error"); return; }
                quotient = (int16_t)(dividend / (int8_t)value);
                if (quotient > 127 || quotient < -128) { fault(c, "divide error"); return; }
                c->r[AX] = (uint16_t)((((uint8_t)(dividend % (int8_t)value)) << 8) | (uint8_t)quotient);
            }
            break;
    }
}

static void adjust(rm_cpu *c, uint8_t op)
{
    uint8_t al = (uint8_t)c->r[AX];
    const uint8_t old_al = al;
    const int old_cf = (c->flags & CF) != 0;

    switch (op) {
        case 0x27:      /* DAA */
        case 0x2f:      /* DAS */
            c->flags &= (uint16_t)~CF;
            if ((al & 0x0f) > 9 || (c->flags & AF)) {
                al = (uint8_t)(op == 0x27 ? al + 6 : al - 6);
                c->flags |= AF;
                if (op == 0x27 ? old_al > 0xf9 : old_al < 6) c->flags |= CF;
                if (old_cf) c->flags |= CF;
            } else c->flags &= (uint16_t)~AF;
            if (old_al > 0x99 || old_cf) {
                al = (uint8_t)(op == 0x27 ? al + 0x60 : al - 0x60);
                c->flags |= CF;
            }
            set8(c, AX, al);
            szp(c, al, 0);
            break;
        default:        /* AAA, AAS */
            if ((al & 0x0f) > 9 || (c->flags & AF)) {
                if (op == 0x37) c->r[AX] = (uint16_t)(c->r[AX] + 0x106);
                else {
                    set8(c, AX, (uint8_t)(al - 6));
                    set8(c, 4, (uint8_t)((c->r[AX] >> 8) - 1));
                }
                c->flags |= AF | CF;
            } else c->flags &= (uint16_t)~(AF | CF);
            set8(c, AX, (uint8_t)(c->r[AX] & 0x0f));
            break;
    }
}

static void step(rm_cpu *c)
{
    rm_operand m;
    uint8_t op, rep = 0;
    int word;

    c->segment = -1;
    c->start = c->ip;
    for (;;) {
        op = fetch8(c);
        if (op == 0x26) c->segment = ES;
        else if (op == 0x2e) c->segment = CS;
        else if (op == 0x36) c->segment = SS;
        else if (op == 0x3e) c->segment = DS;
        else if (op == 0xf2 || op == 0xf3) rep = op;
        else if (op != 0xf0) break;
    }
    word = op & 1;

    if (op < 0x40 && (op & 7) < 6) {
        const int kind = op >> 3;
        uint16_t result;
        switch (op & 7) {
            case 0:
            case 1:
                decode(c, &m);
                result = alu(c, kind, get_rm(c, &m, word), get_reg(c, m.reg, word), word);
                if (kind != 7) set_rm(c, &m, word, result);
                break;
            case 2:
            case 3:
                decode(c, &m);
                result = alu(c, kind, get_reg(c, m.reg, word), get_rm(c, &m, word), word);
                if (kind != 7) set_reg(c, m.reg, word, result);
                break;
            case 4:
                result = alu(c, kind, (uint8_t)c->r[AX], fetch8(c), 0);
                if (kind != 7) set8(c, AX, (uint8_t)result);
                break;
            default:
                result = alu(c, kind, c->r[AX], fetch16(c), 1);
                if (kind != 7) c->r[AX] = result;
                break;
        }
        return;
    }

    switch (op) {
        case 0x06: case 0x0e: case 0x16: case 0x1e:
            push(c, c->s[op >> 3]);
            break;
        case 0x07: case 0x17: case 0x1f:
            c->s[op >> 3] = pop(c);
            break;
        case 0x27: case 0x2f: case 0x37: case 0x3f:
            adjust(c, op);
            break;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
            c->r[op & 7] = inc_dec(c, c->r[op & 7], 0, 1);
            break;
        case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
            c->r[op & 7] = inc_dec(c, c->r[op & 7], 1, 1);
            break;
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: {
            const uint16_t value = c->r[op & 7];
            push(c, value);
            break;
        }
        case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: {
            const uint16_t value = pop(c);
            c->r[op & 7] = value;
            break;
        }
        case 0x60: {
            const uint16_t sp = c->r[SP];
            push(c, c->r[AX]); push(c, c->r[CX]); push(c, c->r[DX]); push(c, c->r[BX]);
            push(c, sp); push(c, c->r[BP]); push(c, c->r[SI]); push(c, c->r[DI]);
            break;
        }
        case 0x61:
            c->r[DI] = pop(c); c->r[SI] = pop(c); c->r[BP] = pop(c); pop(c);
            c->r[BX] = pop(c); c->r[DX] = pop(c); c->r[CX] = pop(c); c->r[AX] = pop(c);
            break;
        case 0x68:
            push(c, fetch16(c));
            break;
        case 0x6a:
            push(c, (uint16_t)(int8_t)fetch8(c));
            break;
        case 0x69:
        case 0x6b: {
            int32_t product;
            int16_t factor;
            decode(c, &m);
            factor = (op == 0x69) ? (int16_t)fetch16(c) : (int16_t)(int8_t)fetch8(c);
            product = (int32_t)(int16_t)get_rm(c, &m, 1) * factor;
            c->r[m.reg] = (uint16_t)product;
            c->flags = (uint16_t)((c->flags & ~(CF | OF)) | (product != (int16_t)product ? (CF | OF) : 0));
            break;
        }
        case 0x6c: case 0x6d: case 0x6e: case 0x6f:
        case 0xa4: case 0xa5: case 0xa6: case 0xa7:
        case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
            string_op(c, op, rep);
            break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f: {
            const int8_t delta = (int8_t)fetch8(c);
            if (condition(c, op & 15)) c->ip = (uint16_t)(c->ip + delta);
            break;
        }
        case 0x80: case 0x81: case 0x82: case 0x83: {
            uint16_t operand, result;
            decode(c, &m);
            operand = (op == 0x81) ? fetch16(c) : (op == 0x83) ? (uint16_t)(int8_t)fetch8(c) : fetch8(c);
            result = alu(c, m.reg, get_rm(c, &m, word), operand, word);
            if (m.reg != 7) set_rm(c, &m, word, result);
            break;
        }
        case 0x84: case 0x85:
            decode(c, &m);
            alu(c, 4, get_rm(c, &m, word), get_reg(c, m.reg, word), word);
            break;
        case 0x86: case 0x87: {
            uint16_t value;
            decode(c, &m);
            value = get_rm(c, &m, word);
            set_rm(c, &m, word, get_reg(c, m.reg, word));
            set_reg(c, m.reg, word, value);
            break;
        }
        case 0x88: case 0x89:
            decode(c, &m);
            set_rm(c, &m, word, get_reg(c, m.reg, word));
            break;
        case 0x8a: case 0x8b:
            decode(c, &m);
            set_reg(c, m.reg, word, get_rm(c, &m, word));
            break;
        case 0x8c:
            decode(c, &m);
            set_rm(c, &m, 1, c->s[m.reg & 3]);
            break;
        case 0x8d:
            decode(c, &m);
            c->r[m.reg] = m.offset;
            break;
        case 0x8e:
            decode(c, &m);
            c->s[m.reg & 3] = get_rm(c, &m, 1);
            break;
        case 0x8f: {
            uint16_t value;
            decode(c, &m);
            value = pop(c);
            set_rm(c, &m, 1, value);
            break;
        }
        case 0x90:
            break;
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
            const uint16_t value = c->r[op & 7];
            c->r[op & 7] = c->r[AX];
            c->r[AX] = value;
            break;
        }
        case 0x98:
            c->r[AX] = (uint16_t)(int8_t)c->r[AX];
            break;
        case 0x99:
            c->r[DX] = (c->r[AX] & 0x8000u) ? 0xffffu : 0;
            break;
        case 0x9a: {
            const uint16_t offset = fetch16(c);
            const uint16_t segment = fetch16(c);
            push(c, c->s[CS]);
            push(c, c->ip);
            c->s[CS] = segment;
            c->ip = offset;
            break;
        }
        case 0x9b:
            break;
        case 0x9c:
            push(c, (uint16_t)(c->flags | 0xf002u));
            break;
        case 0x9d:
            c->flags = (uint16_t)((pop(c) & 0x0fd5u) | 0x0002u);
            break;
        case 0x9e:
            c->flags = (uint16_t)((c->flags & 0xff00u) | ((c->r[AX] >> 8) & 0xd5u) | 0x0002u);
            break;
        case 0x9f:
            set8(c, 4, (uint8_t)c->flags);
            break;
        case 0xa0: case 0xa1: {
            const uint32_t address = linear(data_segment(c), fetch16(c));
            set_reg(c, AX, word, word ? read16(address) : read8(address));
            break;
        }
        case 0xa2: case 0xa3: {
            const uint32_t address = linear(data_segment(c), fetch16(c));
            if (word) write16(address, c->r[AX]);
            else write8(address, (uint8_t)c->r[AX]);
            break;
        }
        case 0xa8:
            alu(c, 4, (uint8_t)c->r[AX], fetch8(c), 0);
            break;
        case 0xa9:
            alu(c, 4, c->r[AX], fetch16(c), 1);
            break;
        case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
            set8(c, op & 7, fetch8(c));
            break;
        case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
            c->r[op & 7] = fetch16(c);
            break;
        case 0xc0: case 0xc1: {
            unsigned int count;
            decode(c, &m);
            count = fetch8(c);
            set_rm(c, &m, word, shift(c, m.reg, get_rm(c, &m, word), count, word));
            break;
        }
        case 0xd0: case 0xd1: case 0xd2: case 0xd3:
            decode(c, &m);
            set_rm(c, &m, word, shift(c, m.reg, get_rm(c, &m, word), op < 0xd2 ? 1u : (uint8_t)c->r[CX], word));
            break;
        case 0xc2: {
            const uint16_t bytes = fetch16(c);
            c->ip = pop(c);
            c->r[SP] = (uint16_t)(c->r[SP] + bytes);
            break;
        }
        case 0xc3:
            c->ip = pop(c);
            break;
        case 0xc4: case 0xc5:
            decode(c, &m);
            if (m.mod == 3) { fault(c, "LES/LDS of a register"); break; }
            c->r[m.reg] = read16(m.address);
            c->s[op == 0xc4 ? ES : DS] = read16((m.address + 2u) & 0xfffffu);
            break;
        case 0xc6: case 0xc7:
            decode(c, &m);
            set_rm(c, &m, word, word ? fetch16(c) : fetch8(c));
            break;
        case 0xc8: {
            const uint16_t bytes = fetch16(c);
            const unsigned int level = fetch8(c) & 0x1fu;
            uint16_t frame;
            push(c, c->r[BP]);
            frame = c->r[SP];
            for (unsigned int index = 1; index < level; index++) {
                c->r[BP] = (uint16_t)(c->r[BP] - 2);
                push(c, read16(linear(c->s[SS], c->r[BP])));
            }
            if (level > 0) push(c, frame);
            c->r[BP] = frame;
            c->r[SP] = (uint16_t)(c->r[SP] - bytes);
            break;
        }
        case 0xc9:
            c->r[SP] = c->r[BP];
            c->r[BP] = pop(c);
            break;
        case 0xca: {
            const uint16_t bytes = fetch16(c);
            c->ip = pop(c);
            c->s[CS] = pop(c);
            c->r[SP] = (uint16_t)(c->r[SP] + bytes);
            break;
        }
        case 0xcb:
            c->ip = pop(c);
            c->s[CS] = pop(c);
            break;
        case 0xcc:
            interrupt(c, 3);
            break;
        case 0xcd:
            interrupt(c, fetch8(c));
            break;
        case 0xce:
            if (c->flags & OF) interrupt(c, 4);
            break;
        case 0xcf:
            c->ip = pop(c);
            c->s[CS] = pop(c);
            c->flags = (uint16_t)((pop(c) & 0x0fd5u) | 0x0002u);
            break;
        case 0xd4: {
            const uint8_t base = fetch8(c);
            const uint8_t al = (uint8_t)c->r[AX];
            if (base == 0) { fault(c, "divide error"); break; }
            c->r[AX] = (uint16_t)(((al / base) << 8) | (al % base));
            szp(c, c->r[AX], 0);
            break;
        }
        case 0xd5: {
            const uint8_t base = fetch8(c);
            const uint8_t al = (uint8_t)((uint8_t)c->r[AX] + (uint8_t)(c->r[AX] >> 8) * base);
            c->r[AX] = al;
            szp(c, al, 0);
            break;
        }
        case 0xd6:
            set8(c, AX, (c->flags & CF) ? 0xffu : 0);
            break;
        case 0xd7:
            set8(c, AX, read8(linear(data_segment(c), (uint16_t)(c->r[BX] + (uint8_t)c->r[AX]))));
            break;
        case 0xe0: case 0xe1: case 0xe2: case 0xe3: {
            const int8_t delta = (int8_t)fetch8(c);
            int jump;
            if (op == 0xe3) jump = c->r[CX] == 0;
            else {
                c->r[CX]--;
                jump = c->r[CX] != 0;
                if (op == 0xe0) jump = jump && (c->flags & ZF) == 0;
                else if (op == 0xe1) jump = jump && (c->flags & ZF) != 0;
            }
            if (jump) c->ip = (uint16_t)(c->ip + delta);
            break;
        }
        case 0xe4: case 0xe5:
            set_reg(c, AX, word, port_in(fetch8(c), word));
            break;
        case 0xe6: case 0xe7:
            port_out(fetch8(c), word, c->r[AX]);
            break;
        case 0xec: case 0xed:
            set_reg(c, AX, word, port_in(c->r[DX], word));
            break;
        case 0xee: case 0xef:
            port_out(c->r[DX], word, c->r[AX]);
            break;
        case 0xe8: {
            const uint16_t delta = fetch16(c);
            push(c, c->ip);
            c->ip = (uint16_t)(c->ip + delta);
            break;
        }
        case 0xe9: {
            const uint16_t delta = fetch16(c);
            c->ip = (uint16_t)(c->ip + delta);
            break;
        }
        case 0xea: {
            const uint16_t offset = fetch16(c);
            c->s[CS] = fetch16(c);
            c->ip = offset;
            break;
        }
        case 0xeb: {
            const int8_t delta = (int8_t)fetch8(c);
            c->ip = (uint16_t)(c->ip + delta);
            break;
        }
        case 0xf5: c->flags ^= CF; break;
        case 0xf8: c->flags &= (uint16_t)~CF; break;
        case 0xf9: c->flags |= CF; break;
        case 0xfa: c->flags &= (uint16_t)~IF; break;
        case 0xfb: c->flags |= IF; break;
        case 0xfc: c->flags &= (uint16_t)~DF; break;
        case 0xfd: c->flags |= DF; break;
        case 0xf6: case 0xf7:
            group3(c, word);
            break;
        case 0xfe:
            decode(c, &m);
            if (m.reg > 1) { fault(c, "unknown opcode"); break; }
            set_rm(c, &m, 0, inc_dec(c, get_rm(c, &m, 0), m.reg, 0));
            break;
        case 0xff:
            decode(c, &m);
            switch (m.reg) {
                case 0:
                case 1:
                    set_rm(c, &m, 1, inc_dec(c, get_rm(c, &m, 1), m.reg, 1));
                    break;
                case 2: {
                    const uint16_t target = get_rm(c, &m, 1);
                    push(c, c->ip);
                    c->ip = target;
                    break;
                }
                case 3:
                case 5: {
                    uint16_t offset, segment;
                    if (m.mod == 3) { fault(c, "far call or jump through a register"); break; }
                    offset = read16(m.address);
                    segment = read16((m.address + 2u) & 0xfffffu);
                    if (m.reg == 3) {
                        push(c, c->s[CS]);
                        push(c, c->ip);
                    }
                    c->s[CS] = segment;
                    c->ip = offset;
                    break;
                }
                case 4:
                    c->ip = get_rm(c, &m, 1);
                    break;
                case 6:
                    push(c, get_rm(c, &m, 1));
                    break;
                default:
                    fault(c, "unknown opcode");
                    break;
            }
            break;
        case 0xf4:
            fault(c, "HLT");
            break;
        default:
            fault(c, (op == 0x66 || op == 0x67 || op == 0x0f) ? "an 80386 instruction" :
                     (op >= 0xd8 && op <= 0xdf) ? "an FPU instruction" : "unknown opcode");
            break;
    }
}

/* Runs until the code returns to F000:F000.  A jump into a stub (a handler
 * that chains to the vector it found) is the service and its iret. */
static int run(rm_cpu *c)
{
    for (uint32_t count = 0; ; count++) {
        if (c->s[CS] == STUB_SEGMENT) {
            if (c->ip == RETURN_OFFSET) return 1;
            if (c->ip >= STUB_OFFSET && c->ip < STUB_OFFSET + 0x100u) {
                uint16_t flags;
                service(c, (uint8_t)(c->ip - STUB_OFFSET));
                c->ip = pop(c);
                c->s[CS] = pop(c);
                flags = pop(c);
                c->flags = (uint16_t)((flags & ~ARITHMETIC & 0x0fffu) | (c->flags & ARITHMETIC) | 0x0002u);
                continue;
            }
            c->start = c->ip;
            fault(c, "a jump into the BIOS");
            return 0;
        }
        if (count >= MAX_INSTRUCTIONS) {
            c->start = c->ip;
            fault(c, "still running after 50 million instructions");
            return 0;
        }
        step(c);
        if (c->stop) return 0;
    }
}

void Game_DosRealSetVector(uint8_t vector, uint16_t segment, uint16_t offset)
{
    vectors[vector] = ((uint32_t)segment << 16) | offset;
    ivt_write(vector, vectors[vector]);
}

uint32_t Game_DosRealGetVector(uint8_t vector)
{
    /* what the code wrote into the table itself counts too */
    if (vector_in_dos_memory(ivt_read(vector))) return ivt_read(vector);
    return vectors[vector];
}

void Game_DosRealReset(void)
{
    memset(vectors, 0, sizeof(vectors));
    memset(low, 0, sizeof(low));
}

int Game_DosRealInterrupt(uint8_t vector, Game_DosRegs *regs, int ds, int es)
{
    const uint32_t target = ivt_read(vector);
    rm_cpu cpu;
    int ran;

    if (!vector_in_dos_memory(target)) return 0;
    if (memory == NULL) memory = Game_DosMemPtr(0, LOW_END);
    if (memory == NULL) return 0;

    memset(&cpu, 0, sizeof(cpu));
    cpu.r[AX] = (uint16_t)regs->eax;
    cpu.r[BX] = (uint16_t)regs->ebx;
    cpu.r[CX] = (uint16_t)regs->ecx;
    cpu.r[DX] = (uint16_t)regs->edx;
    cpu.r[SI] = (uint16_t)regs->esi;
    cpu.r[DI] = (uint16_t)regs->edi;
    cpu.r[BP] = (uint16_t)regs->ebp;
    cpu.s[DS] = (uint16_t)(ds < 0 ? 0 : ds);
    cpu.s[ES] = (uint16_t)(es < 0 ? 0 : es);
    /* a call inside another one (an IRQ) goes on the stack the code is on */
    cpu.s[SS] = current != NULL ? current->s[SS] : (uint16_t)STACK_SEGMENT;
    cpu.r[SP] = current != NULL ? current->r[SP] : (uint16_t)STACK_TOP;
    cpu.flags = (uint16_t)((regs->eflags & 0x0fd5u) | IF | 0x0002u);
    cpu.s[CS] = STUB_SEGMENT;
    cpu.ip = RETURN_OFFSET;
    interrupt(&cpu, vector);

    if (trace_real()) fprintf(stderr, "  real-mode int %02x at %04x:%04x ax=%04x bx=%04x cx=%04x dx=%04x\n",
                              vector, cpu.s[CS], cpu.ip, cpu.r[AX], cpu.r[BX], cpu.r[CX], cpu.r[DX]);
    {
        rm_cpu *const outer = current;
        current = &cpu;
        ran = run(&cpu);
        current = outer;
    }
    if (trace_real()) fprintf(stderr, "    -> ax=%04x bx=%04x cx=%04x dx=%04x flags=%04x%s\n",
                              cpu.r[AX], cpu.r[BX], cpu.r[CX], cpu.r[DX], cpu.flags, ran ? "" : " (stopped)");

    regs->eax = (regs->eax & 0xffff0000u) | cpu.r[AX];
    regs->ebx = (regs->ebx & 0xffff0000u) | cpu.r[BX];
    regs->ecx = (regs->ecx & 0xffff0000u) | cpu.r[CX];
    regs->edx = (regs->edx & 0xffff0000u) | cpu.r[DX];
    regs->esi = (regs->esi & 0xffff0000u) | cpu.r[SI];
    regs->edi = (regs->edi & 0xffff0000u) | cpu.r[DI];
    regs->ebp = (regs->ebp & 0xffff0000u) | cpu.r[BP];
    regs->eflags = (regs->eflags & ~ARITHMETIC) | (cpu.flags & ARITHMETIC);
    return 1;
}

#endif /* GAME_DOS_REALMODE */
