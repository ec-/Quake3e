/*
Copyright (C) 2026 skullernet

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

This file incorporates work covered by the following copyright and
permission notice:

Copyright (C) Joel Martin <github@martintribe.org>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "q_shared.h"
#include "qcommon.h"
#include "sizebuf.h"
#include "wa_opcodes.h"

#define WA_Malloc(size)    Hunk_Alloc(size, h_high)

#define WA_ASSERT(cond, msg) \
    do { if (!(cond)) Com_Error(ERR_DROP, "%s: %s", __func__, msg); } while (0)

#define WA_ASSERT2(cond, msg) \
    do { if (!(cond)) Com_Error(ERR_DROP, "%s: %s", func, msg); } while (0)

#define ASSERT(cond, msg, ...) \
    do { if (!(cond)) { Com_Printf( S_COLOR_RED msg "\n", ##__VA_ARGS__ ); return qfalse; } } while (0)

#define WA_MAGIC    0x6d736100
#define WA_VERSION  0x01

#define WA_PAGE_SIZE    0x10000
#define STACK_SIZE      0x10000     // 65536
#define BLOCKSTACK_SIZE 0x1000      // 4096
#define CALLSTACK_SIZE  0x1000      // 4096
#define BR_TABLE_SIZE   0x10000     // 65536
#define MAX_BLOCKS      0x10000
#define MAX_FUNCS       0x10000
#define MAX_LOCALS      0x1000
#define MAX_GLOBALS     0x10000
#define MAX_TYPES       0x10000
#define MAX_RESULTS     1

#define I32         0x7f    // -0x01
#define I64         0x7e    // -0x02
#define F32         0x7d    // -0x03
#define F64         0x7c    // -0x04
#define FUNCREF     0x70    // -0x10
#define FUNC        0x60    // -0x20
#define BLOCK       0x40    // -0x40

#define KIND_FUNCTION   0
#define KIND_TABLE      1
#define KIND_MEMORY     2
#define KIND_GLOBAL     3

typedef union {
    uint32_t   u32;
    int32_t    i32;
    uint64_t   u64;
    int64_t    i64;
    float      f32;
    double     f64;
} wa_value_t;

typedef struct {
    uint32_t    initial;    // initial size (64K pages)
    uint32_t    maximum;    // maximum size (64K pages)
    uint32_t    pages;      // current size (64K pages)
    uint32_t    bytesize;   // current size (bytes)
    uint8_t    *bytes;      // memory area
} wa_memory_t;

struct wa_s;
typedef void (*wa_thunk_t)(const struct wa_s *, wa_value_t *);

typedef struct {
    const char *name;
    const char *mask;
    wa_thunk_t thunk;
} wa_import_t;

typedef struct {
    uint32_t  form;
    uint32_t  num_params;
    uint32_t *params;
    uint32_t  num_results;
    uint32_t  results[MAX_RESULTS];
} wa_type_t;

// A block or function
typedef struct {
    uint32_t   opcode;          // 0x00: function, 0x02: block, 0x03: loop, 0x04: if
    uint32_t   num_locals;      // function only
    union {
        uint32_t  *locals;      // function only
        wa_thunk_t thunk;       // function only (imported)
    };
    uint32_t   start_addr;      // else branch addr for if block
    uint32_t   end_addr;        // branch addr
    const wa_type_t *type;      // params/results type
} wa_block_t;

typedef const uint8_t *wa_pc_t;

typedef struct {
    const wa_block_t  *block;
    // Saved state
    int sp, fp;
    wa_pc_t ra;
} wa_frame_t;

typedef struct {
    uint32_t    initial;     // initial table size
    uint32_t    maximum;     // maximum table size
    uint32_t    size;        // current table size
    uint32_t   *entries;
} wa_table_t;

typedef struct {
    char       *data;
    uint32_t    len;
} wa_string_t;

typedef struct {
    uint32_t    kind;
    wa_string_t name;
    void       *value;
} wa_export_t;

typedef struct wa_s {
    struct vm_s    *vm;

    uint32_t    num_bytes;      // number of bytes in the module
    uint8_t    *code;           // module content/bytes

    uint32_t    num_types;      // number of function types
    wa_type_t  *types;          // function types

    uint32_t    num_imports;    // number of leading imports in functions
    uint32_t    num_funcs;      // number of function (including imports)
    wa_block_t  *funcs;         // imported and locally defined functions

    wa_table_t  table;
    wa_memory_t memory;

    wa_value_t *llvm_stack_pointer;
    wa_value_t  llvm_stack_start;
    wa_block_t *vmMain;

    uint32_t    num_globals;    // number of globals
    wa_value_t  *globals;       // globals

    uint32_t    num_exports;    // number of exports
    wa_export_t *exports;

    // Runtime state
    wa_pc_t     pc;             // program counter
    int         sp;             // operand stack pointer
    int         fp;             // current frame pointer into stack
    int         csp;            // callstack pointer
    wa_value_t  *stack;         // [STACK_SIZE] main operand stack
    wa_frame_t  *callstack;     // [CALLSTACK_SIZE] callstack
} wa_t;

extern const wa_import_t wa_stdlib[];

static inline void *WA_GetPointer(const wa_t *m, uint32_t ptr, uint32_t size,
                                  uint32_t nmemb, uint32_t align, const char *func)
{
    WA_ASSERT2(ptr, "Null VM pointer");
    WA_ASSERT2(!(ptr & (align - 1)), "Misaligned VM pointer");
    WA_ASSERT2((uint64_t)ptr + (uint64_t)size * nmemb <= m->memory.bytesize, "Out of bounds VM pointer");
    return m->memory.bytes + ptr;
}

#define WA_PTR_CNT(arg, type, cnt) \
    ((type *)WA_GetPointer(m, WA_U32(arg), sizeof(type), cnt, __alignof__(type), __func__))

#define WA_PTR_NULL_CNT(arg, type, cnt) \
    (WA_U32(arg) ? WA_PTR_CNT(arg, type, cnt) : NULL)

#define WA_PTR_NULL(arg, type) WA_PTR_NULL_CNT(arg, type, 1)

#define WA_VEC3(arg) WA_PTR_CNT(arg, vec_t, 3)
#define WA_VEC3_NULL(arg) WA_PTR_NULL_CNT(arg, vec_t, 3)

#define WA_PTR(arg, type) WA_PTR_CNT(arg, type, 1)

#define WA_STR_BUF(arg, siz) \
    ((WA_U32(arg) || WA_U32(siz)) ? WA_PTR_CNT(arg, char, WA_U32(siz)) : NULL)

#define WA_VEC3_BUF(arg, siz) \
    ((WA_U32(arg) || WA_U32(siz)) ? WA_PTR_CNT(arg, vec3_t, WA_U32(siz)) : NULL)

#define WA_STR_NULL(arg) WA_PTR_NULL(arg, char)
#define WA_STR(arg) WA_PTR(arg, char)

#define WA_U32(arg) stack[arg].u32
#define WA_I32(arg) stack[arg].i32
#define WA_U64(arg) stack[arg].u64
#define WA_I64(arg) stack[arg].i64
#define WA_F32(arg) stack[arg].f32
#define WA_F64(arg) stack[arg].f64

#define WA_THUNK(x) \
    static void thunk_##x(const wa_t *m, wa_value_t *stack)

#define WA_IMPORT(name, mask) \
    { #name, mask, thunk_##name }

void WA_SetupCall(wa_t *m, uint32_t fidx);
void WA_Interpret(wa_t *m);
const wa_type_t *WA_GetBlockType(uint32_t value_type);
qboolean WA_PrepareInterpreter(wa_t *m, sizebuf_t *sz);
qboolean WA_LoadModule(vm_t *vm, void *data, uint32_t size, qboolean alloc);
int32_t WA_CallInterpreted(wa_t *m, int nargs, int32_t *args);
void WA_Reset(wa_t *m);
