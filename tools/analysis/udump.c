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

/* udump - decode a raw section with the very udis86 build SR uses.
 *
 * build: cc -O2 -include string.h -ISR/SRW/udis86-1.7.2 -ISR/SRW/udis86-1.7.2/libudis86 udump.c SR/SRW/udis86-1.7.2/libudis86/*.c -o udump
 *        (the udis86 directory of the SR the game builds with; paths from SR-SDK/)
 *
 * usage: udump <bin> <base_hex> <start_hex> [<end_hex>]
 *
 * prints one line per instruction: <addr> <len> <mnemonic_enum> <asm>
 * the mnemonic enum matches the UD_Ixxx values of itab.h, so coverage
 * against SET_FLAGS()/case lists can be checked mechanically.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "udis86.h"

int main(int argc, char **argv)
{
    FILE *f;
    long size, base, pos, end;
    unsigned char *buf;
    ud_t ud;

    if (argc < 4)
    {
        fprintf(stderr, "usage: udump <bin> <base_hex> <start_hex> [<end_hex>]\n");
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (f == NULL)
    {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *) malloc(size);
    if (fread(buf, 1, size, f) != (size_t) size)
    {
        fclose(f);
        return 1;
    }
    fclose(f);

    base = strtol(argv[2], NULL, 16);
    pos = strtol(argv[3], NULL, 16) - base;
    end = (argc > 4) ? (strtol(argv[4], NULL, 16) - base) : size;

    ud_init(&ud);
    ud_set_mode(&ud, 32);
    ud_set_syntax(&ud, UD_SYN_INTEL);

    while (pos >= 0 && pos < size && pos < end)
    {
        ud_set_input_buffer(&ud, buf + pos, size - pos);
        ud_set_pc(&ud, base + pos);

        if (ud_disassemble(&ud) == 0)
        {
            printf("%08lx 00 -1 --undecodable--\n", (unsigned long) (base + pos));
            break;
        }

        printf("%08lx %02d %d %s\n", (unsigned long) (base + pos),
               (int) ud_insn_len(&ud), (int) ud.mnemonic, ud_insn_asm(&ud));

        pos += ud_insn_len(&ud);
    }

    free(buf);
    return 0;
}
