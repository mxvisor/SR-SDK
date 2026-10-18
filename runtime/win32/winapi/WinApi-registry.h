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

#if !defined(_WINAPI_REGISTRY_H_INCLUDED_)
#define _WINAPI_REGISTRY_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The registry of the port: a tree of keys with values, kept in memory, that a program
 * reads and writes through the Reg* functions of advapi32.  There is no registry behind
 * it, so a program finds only what the port put there; the port's own code does that with
 * these functions before the program starts (from the game's configuration).
 * `key` is a path like "HKLM\\SOFTWARE\\Vendor\\Game" (HKCR, HKCU, HKLM and HKU are the roots);
 * the key is made if it does not exist. */
void Winapi_RegistrySetDword(const char *key, const char *name, uint32_t value);
void Winapi_RegistrySetString(const char *key, const char *name, const char *value);

#ifdef __cplusplus
}
#endif

#endif
