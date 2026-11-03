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


#include "WinApi-registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "platform.h"
#include "ptr32.h"
#include "Game_hooks.h"
#include "Game_memory.h"
#include "Game_vfs.h"

#define ERROR_SUCCESS            0u
#define ERROR_FILE_NOT_FOUND     2u
#define ERROR_INVALID_PARAMETER  87u
#define ERROR_MORE_DATA          234u
#define ERROR_NO_MORE_ITEMS      259u
#define ERROR_BADKEY             1010u

#define REG_SZ                   1u
#define REG_DWORD                4u

#define MAX_PATH_LENGTH          512
#define MAX_HANDLES              64
#define HANDLE_BASE              0x00005400u    /* an open key: HANDLE_BASE + index; the predefined ones are 0x8000000x */

typedef struct {
    char *key;              /* full path, ROOT\a\b */
    char *name;
    uint32_t type;
    uint8_t *data;
    uint32_t size;
} reg_value;

static char **keys;         /* the keys that exist (all the parents of a created key too) */
static size_t key_count, key_capacity;
static reg_value *values;
static size_t value_count, value_capacity;
static char *handles[MAX_HANDLES];      /* the path of an open key */

static const char *const roots[] = { "HKCR", "HKCU", "HKLM", "HKU" };     /* 0x80000000 .. 0x80000003 */

/* <GAME>_REG_LOG set: what the program asks the registry, on stderr */
static int reg_logging(void)
{
    static int v = -1;
    if (v < 0) v = (getenv(GAME_ENV("REG_LOG")) != NULL);
    return v;
}
#define reg_log(...) do { if (reg_logging()) fprintf(stderr, __VA_ARGS__); } while (0)

static void reg_load(void);

static char *dup_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d != NULL) memcpy(d, s, n);
    return d;
}

static int key_exists(const char *path)
{
    size_t i;
    if (strchr(path, '\\') == NULL) return 1;    /* a root */
    for (i = 0; i < key_count; i++)
    {
        if (strcasecmp(keys[i], path) == 0) return 1;
    }
    return 0;
}

/* the key and every parent of it */
static void key_create(const char *path)
{
    char part[MAX_PATH_LENGTH];
    size_t i;

    for (i = 0; path[i] != 0 && i < sizeof(part) - 1; i++)
    {
        if ((path[i] == '\\') && (i > 0))
        {
            memcpy(part, path, i);
            part[i] = 0;
            if (!key_exists(part))
            {
                if (key_count == key_capacity)
                {
                    char **grown = (char **)realloc(keys, (key_capacity ? key_capacity * 2 : 32) * sizeof(char *));
                    if (grown == NULL) return;
                    keys = grown;
                    key_capacity = key_capacity ? key_capacity * 2 : 32;
                }
                keys[key_count++] = dup_string(part);
            }
        }
    }
    if (!key_exists(path))
    {
        if (key_count == key_capacity)
        {
            char **grown = (char **)realloc(keys, (key_capacity ? key_capacity * 2 : 32) * sizeof(char *));
            if (grown == NULL) return;
            keys = grown;
            key_capacity = key_capacity ? key_capacity * 2 : 32;
        }
        keys[key_count++] = dup_string(path);
    }
}

/* the path of a key handle and a sub key under it; 0 if the handle is not one */
static int resolve(uint32_t hkey, const char *subkey, char *out, size_t size)
{
    const char *base;
    size_t length;

    reg_load();
    if ((hkey & 0xfffffff8u) == 0x80000000u)
    {
        if ((hkey & 7) > 3) return 0;
        base = roots[hkey & 7];
    }
    else if ((hkey >= HANDLE_BASE) && (hkey < HANDLE_BASE + MAX_HANDLES) && (handles[hkey - HANDLE_BASE] != NULL))
    {
        base = handles[hkey - HANDLE_BASE];
    }
    else
    {
        return 0;
    }

    snprintf(out, size, "%s", base);
    if (subkey != NULL)
    {
        while (*subkey == '\\') subkey++;
        if (*subkey != 0)
        {
            length = strlen(out);
            snprintf(out + length, size - length, "\\%s", subkey);
        }
    }
    length = strlen(out);
    while ((length > 0) && (out[length - 1] == '\\')) out[--length] = 0;
    return 1;
}

static uint32_t new_handle(const char *path, uint32_t *phk)
{
    uint32_t i;

    for (i = 0; i < MAX_HANDLES; i++)
    {
        if (handles[i] == NULL)
        {
            handles[i] = dup_string(path);
            *phk = HANDLE_BASE + i;
            return ERROR_SUCCESS;
        }
    }
    return 8u;  /* ERROR_NOT_ENOUGH_MEMORY */
}

static reg_value *find_value(const char *key, const char *name)
{
    size_t i;
    for (i = 0; i < value_count; i++)
    {
        if ((strcasecmp(values[i].key, key) == 0) && (strcasecmp(values[i].name, name) == 0)) return &values[i];
    }
    return NULL;
}

static void set_value(const char *key, const char *name, uint32_t type, const void *data, uint32_t size);

/* What the program writes stays from one run to the next (a game keeps its
 * options there): the whole registry in <program>.reg of the write root, a
 * text file, rewritten after every change the program makes.  It is read at
 * the first use of the registry, so what the port sets before the program
 * starts (Winapi_RegistrySet*) wins over the file. */
#define REG_FILE_NAME GAME_PROGRAM_NAME ".reg"

static int reg_loaded;

static int reg_file(char *path, size_t size)
{
    const char *root = Game_WriteRoot();
    if (root == NULL) return 0;
    return snprintf(path, size, "%s/%s", root, REG_FILE_NAME) < (int)size;
}

static void reg_load(void)
{
    char path[1024], line[4096];
    FILE *f;

    if (reg_loaded) return;
    if (!reg_file(path, sizeof(path))) return;      /* not yet: no write root */
    reg_loaded = 1;
    f = fopen(path, "rt");
    if (f == NULL) return;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *field[5];
        int n = 0;
        char *p = line;

        line[strcspn(line, "\r\n")] = 0;
        if ((line[0] == 0) || (line[0] == '#')) continue;
        while ((n < 5) && (p != NULL))
        {
            field[n++] = p;
            p = strchr(p, '\t');
            if (p != NULL) *p++ = 0;
        }
        if ((n == 2) && (strcmp(field[0], "key") == 0))
        {
            key_create(field[1]);
        }
        else if ((n == 5) && (strcmp(field[0], "value") == 0))
        {
            size_t length = strlen(field[4]) / 2, i;
            uint8_t *data = (uint8_t *)malloc(length ? length : 1);
            if (data == NULL) continue;
            for (i = 0; i < length; i++)
            {
                unsigned byte = 0;
                sscanf(field[4] + 2 * i, "%2x", &byte);
                data[i] = (uint8_t)byte;
            }
            set_value(field[1], field[2], (uint32_t)strtoul(field[3], NULL, 10), data, (uint32_t)length);
            free(data);
        }
    }
    fclose(f);
    reg_log("registry: read %s\n", path);
}

static void reg_save(void)
{
    char path[1024], temp[1040];
    FILE *f;
    size_t i;
    uint32_t j;

    if (!reg_file(path, sizeof(path))) return;
    snprintf(temp, sizeof(temp), "%s.new", path);
    f = fopen(temp, "wt");
    if (f == NULL) return;
    fprintf(f, "# what the program keeps in its registry (the port reads it at the start)\n");
    for (i = 0; i < key_count; i++) fprintf(f, "key\t%s\n", keys[i]);
    for (i = 0; i < value_count; i++)
    {
        fprintf(f, "value\t%s\t%s\t%u\t", values[i].key, values[i].name, (unsigned)values[i].type);
        for (j = 0; j < values[i].size; j++) fprintf(f, "%02x", values[i].data[j]);
        fprintf(f, "\n");
    }
    if (fclose(f) != 0) { remove(temp); return; }
    remove(path);       /* Windows' rename does not replace */
    rename(temp, path);
}

static void set_value(const char *key, const char *name, uint32_t type, const void *data, uint32_t size)
{
    reg_value *v = find_value(key, name);

    if (v == NULL)
    {
        if (value_count == value_capacity)
        {
            reg_value *grown = (reg_value *)realloc(values, (value_capacity ? value_capacity * 2 : 32) * sizeof(reg_value));
            if (grown == NULL) return;
            values = grown;
            value_capacity = value_capacity ? value_capacity * 2 : 32;
        }
        v = &values[value_count++];
        v->key = dup_string(key);
        v->name = dup_string(name);
        v->data = NULL;
    }
    free(v->data);
    v->data = (uint8_t *)malloc(size ? size : 1);
    if (v->data != NULL && size != 0) memcpy(v->data, data, size);
    v->type = type;
    v->size = size;
    key_create(key);
}

void Winapi_RegistrySetDword(const char *key, const char *name, uint32_t value)
{
    reg_load();
    set_value(key, name, REG_DWORD, &value, 4);
}

void Winapi_RegistrySetString(const char *key, const char *name, const char *value)
{
    reg_load();
    set_value(key, name, REG_SZ, value, (uint32_t)strlen(value) + 1);
}

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL RegOpenKeyExA_c(uint32_t hKey, uint32_t lpSubKey, uint32_t ulOptions, uint32_t samDesired, uint32_t phkResult)
{
    char path[MAX_PATH_LENGTH];

    (void)ulOptions; (void)samDesired;
    if (phkResult == 0) return ERROR_INVALID_PARAMETER;
    if (!resolve(hKey, (const char *)GUEST_MEM(lpSubKey), path, sizeof(path))) return ERROR_BADKEY;
    reg_log("registry: open %s: %s\n", path, key_exists(path) ? "found" : "missing");
    if (!key_exists(path)) return ERROR_FILE_NOT_FOUND;
    return new_handle(path, (uint32_t *)GUEST_MEM(phkResult));
}

uint32_t CCALL RegCreateKeyA_c(uint32_t hKey, uint32_t lpSubKey, uint32_t phkResult)
{
    char path[MAX_PATH_LENGTH];

    if (phkResult == 0) return ERROR_INVALID_PARAMETER;
    if (!resolve(hKey, (const char *)GUEST_MEM(lpSubKey), path, sizeof(path))) return ERROR_BADKEY;
    reg_log("registry: create %s\n", path);
    if (!key_exists(path))
    {
        key_create(path);
        reg_save();
    }
    return new_handle(path, (uint32_t *)GUEST_MEM(phkResult));
}

uint32_t CCALL RegCloseKey_c(uint32_t hKey)
{
    if ((hKey >= HANDLE_BASE) && (hKey < HANDLE_BASE + MAX_HANDLES) && (handles[hKey - HANDLE_BASE] != NULL))
    {
        free(handles[hKey - HANDLE_BASE]);
        handles[hKey - HANDLE_BASE] = NULL;
        return ERROR_SUCCESS;
    }
    return ((hKey & 0xfffffff8u) == 0x80000000u) ? ERROR_SUCCESS : ERROR_BADKEY;
}

uint32_t CCALL RegFlushKey_c(uint32_t hKey)
{
    (void)hKey;
    return ERROR_SUCCESS;
}

uint32_t CCALL RegQueryValueExA_c(uint32_t hKey, uint32_t lpValueName, uint32_t lpReserved, uint32_t lpType, uint32_t lpData, uint32_t lpcbData)
{
    char path[MAX_PATH_LENGTH];
    const char *name = (const char *)GUEST_MEM(lpValueName);
    uint32_t *pcb = (uint32_t *)GUEST_MEM(lpcbData);
    reg_value *v;

    (void)lpReserved;
    if (!resolve(hKey, NULL, path, sizeof(path))) return ERROR_BADKEY;
    v = find_value(path, (name != NULL) ? name : "");
    reg_log("registry: query %s \\ %s: %s\n", path, (name != NULL) ? name : "", (v != NULL) ? "found" : "missing");
    if (v == NULL) return ERROR_FILE_NOT_FOUND;

    if (lpType != 0) *(uint32_t *)GUEST_MEM(lpType) = v->type;
    if (lpData == 0)
    {
        if (pcb != NULL) *pcb = v->size;
        return ERROR_SUCCESS;
    }
    if (pcb == NULL) return ERROR_INVALID_PARAMETER;
    if (*pcb < v->size)
    {
        *pcb = v->size;
        return ERROR_MORE_DATA;
    }
    memcpy(GUEST_MEM(lpData), v->data, v->size);
    *pcb = v->size;
    return ERROR_SUCCESS;
}

uint32_t CCALL RegSetValueExA_c(uint32_t hKey, uint32_t lpValueName, uint32_t Reserved, uint32_t dwType, uint32_t lpData, uint32_t cbData)
{
    char path[MAX_PATH_LENGTH];
    const char *name = (const char *)GUEST_MEM(lpValueName);

    (void)Reserved;
    if (!resolve(hKey, NULL, path, sizeof(path))) return ERROR_BADKEY;
    if ((lpData == 0) && (cbData != 0)) return ERROR_INVALID_PARAMETER;
    reg_log("registry: set %s \\ %s\n", path, (name != NULL) ? name : "");
    set_value(path, (name != NULL) ? name : "", dwType, GUEST_MEM(lpData), cbData);
    reg_save();
    return ERROR_SUCCESS;
}

/* the key and everything under it */
uint32_t CCALL RegDeleteKeyA_c(uint32_t hKey, uint32_t lpSubKey)
{
    char path[MAX_PATH_LENGTH];
    size_t length, i, kept;

    if (!resolve(hKey, (const char *)GUEST_MEM(lpSubKey), path, sizeof(path))) return ERROR_BADKEY;
    if (!key_exists(path) || (strchr(path, '\\') == NULL)) return ERROR_FILE_NOT_FOUND;
    length = strlen(path);

    for (i = 0, kept = 0; i < key_count; i++)
    {
        if ((strncasecmp(keys[i], path, length) == 0) && ((keys[i][length] == 0) || (keys[i][length] == '\\'))) free(keys[i]);
        else keys[kept++] = keys[i];
    }
    key_count = kept;
    for (i = 0, kept = 0; i < value_count; i++)
    {
        if ((strncasecmp(values[i].key, path, length) == 0) && ((values[i].key[length] == 0) || (values[i].key[length] == '\\')))
        {
            free(values[i].key); free(values[i].name); free(values[i].data);
        }
        else values[kept++] = values[i];
    }
    value_count = kept;
    reg_save();
    return ERROR_SUCCESS;
}

/* the dwIndex-th sub key directly under the key */
uint32_t CCALL RegEnumKeyA_c(uint32_t hKey, uint32_t dwIndex, uint32_t lpName, uint32_t cchName)
{
    char path[MAX_PATH_LENGTH];
    size_t length, i;
    uint32_t seen = 0;

    if (!resolve(hKey, NULL, path, sizeof(path))) return ERROR_BADKEY;
    length = strlen(path);
    for (i = 0; i < key_count; i++)
    {
        const char *rest;

        if ((strncasecmp(keys[i], path, length) != 0) || (keys[i][length] != '\\')) continue;
        rest = keys[i] + length + 1;
        if (strchr(rest, '\\') != NULL) continue;      /* deeper than a child */
        if (seen++ == dwIndex)
        {
            if (strlen(rest) + 1 > cchName) return ERROR_MORE_DATA;
            strcpy((char *)GUEST_MEM(lpName), rest);
            return ERROR_SUCCESS;
        }
    }
    return ERROR_NO_MORE_ITEMS;
}

#ifdef __cplusplus
}
#endif
