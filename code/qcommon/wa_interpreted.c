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

#include "wa_local.h"
#include "intreadwrite.h"

//
// Stack machine (byte code related functions)
//

// Setup a function
// Push params and locals on the stack and save a call frame on the call stack
// Sets new pc value for the start of the function
void WA_SetupCall(wa_t *m, uint32_t fidx)
{
    const wa_block_t  *func = &m->funcs[fidx];
    const wa_type_t   *type = func->type;

    // Push current frame on the call stack
    WA_ASSERT(m->csp < CALLSTACK_SIZE - 1, "Call stack overflow");

    wa_frame_t *frame = &m->callstack[++m->csp];
    frame->block = func;
    frame->sp = m->sp - type->num_params;
    frame->fp = m->fp;
    frame->ra = m->pc;

    // Push params (dropping extras)
    m->fp = m->sp - type->num_params + 1;
    WA_ASSERT(m->fp >= 0, "Stack underflow");

    // Push function locals
    WA_ASSERT(m->sp < STACK_SIZE - (int)func->num_locals, "Stack overflow");
    memset(m->stack + m->sp + 1, 0, sizeof(m->stack[0]) * func->num_locals);
    m->sp += func->num_locals;

    // Set program counter to start of function
    m->pc = m->code + func->start_addr;
}

// Call imported function.
// Pops params and pushes return value.
static void WA_ThunkOut(wa_t *m, uint32_t fidx)
{
    const wa_block_t  *func = &m->funcs[fidx];
    const wa_type_t   *type = func->type;

    int fp = m->sp - type->num_params + 1;
    WA_ASSERT(fp >= 0, "Stack underflow");

    // Pop params
    m->sp -= type->num_params;

    func->thunk(m, &m->stack[fp]);

    // Push return value
    WA_ASSERT(m->sp < STACK_SIZE - type->num_results, "Stack overflow");
    m->sp += type->num_results;
}

static uint16_t get_u16(wa_pc_t *pc)
{
    uint16_t v = RN16(*pc);
    *pc += 2;
    return v;
}

static uint32_t get_u32(wa_pc_t *pc)
{
    uint32_t v = RN32(*pc);
    *pc += 4;
    return v;
}

static uint64_t get_u64(wa_pc_t *pc)
{
    uint64_t v = RN64(*pc);
    *pc += 8;
    return v;
}

static uint32_t rotl32(uint32_t n, int c)
{
    c &= 31;
    return (n << c) | (n >> (32 - c));
}

static uint32_t rotr32(uint32_t n, int c)
{
    c &= 31;
    return (n >> c) | (n << (32 - c));
}

static uint64_t rotl64(uint64_t n, int c)
{
    c &= 63;
    return (n << c) | (n >> (64 - c));
}

static uint64_t rotr64(uint64_t n, int c)
{
    c &= 63;
    return (n >> c) | (n << (64 - c));
}

#define clz32(x) ((x) ? __builtin_clz(x) : 32)
#define ctz32(x) ((x) ? __builtin_ctz(x) : 32)

#define clz64(x) ((x) ? __builtin_clzll(x) : 64)
#define ctz64(x) ((x) ? __builtin_ctzll(x) : 64)

#define have(n) \
    WA_ASSERT(cur_sp >= n - 1, "Stack underflow")

#define need(n) \
    WA_ASSERT(cur_sp < STACK_SIZE - n, "Stack overflow")

#define save_reg \
    m->pc = cur_pc; \
    m->sp = cur_sp

#define load_reg \
    cur_pc = m->pc; \
    cur_sp = m->sp

#define fetch_op \
    opcode = *cur_pc++

#define dispatch_op \
    goto *dispatch_table[opcode]

#define dispatch \
    fetch_op; \
    dispatch_op

#define LOAD_OP(op, ty, size, read) \
    do_##op: \
    offset = get_u32(&cur_pc); \
    fetch_op; \
    have(1); \
    addr = stack[cur_sp].u32; \
    WA_ASSERT((uint64_t)addr + (uint64_t)offset + size <= msize, "Memory load out of bounds"); \
    maddr = m->memory.bytes + offset + addr; \
    stack[cur_sp].ty = read(maddr); \
    dispatch_op;

#define STOR_OP(op, ty, size, writ) \
    do_##op: \
    offset = get_u32(&cur_pc); \
    fetch_op; \
    have(2); \
    sval = stack[cur_sp--]; \
    addr = stack[cur_sp--].u32; \
    WA_ASSERT((uint64_t)addr + (uint64_t)offset + size <= msize, "Memory store out of bounds"); \
    maddr = m->memory.bytes + offset + addr; \
    writ(maddr, sval.ty); \
    dispatch_op;

#define UN_OP(op, ty, func) \
    do_##op: \
    fetch_op; \
    have(1); \
    stack[cur_sp].ty = func(stack[cur_sp].ty); \
    dispatch_op;

#define BIN_OP(ty1, ty2, op, stmt) \
    do_##op: { \
    fetch_op; \
    have(2); \
    __typeof__(stack[0].ty1) a = stack[cur_sp - 1].ty1; \
    __typeof__(stack[0].ty1) b = stack[cur_sp].ty1; \
    stack[--cur_sp].ty2 = ({ stmt; }); \
    dispatch_op; }

#define CMP_U32(op, stmt) BIN_OP(u32, u32, op, stmt)
#define CMP_I32(op, stmt) BIN_OP(i32, u32, op, stmt)
#define CMP_U64(op, stmt) BIN_OP(u64, u32, op, stmt)
#define CMP_I64(op, stmt) BIN_OP(i64, u32, op, stmt)
#define CMP_F32(op, stmt) BIN_OP(f32, u32, op, stmt)
#define CMP_F64(op, stmt) BIN_OP(f64, u32, op, stmt)

#define BOP_U32(op, stmt) BIN_OP(u32, u32, op, stmt)
#define BOP_I32(op, stmt) BIN_OP(i32, i32, op, stmt)
#define BOP_U64(op, stmt) BIN_OP(u64, u64, op, stmt)
#define BOP_I64(op, stmt) BIN_OP(i64, i64, op, stmt)
#define BOP_F32(op, stmt) BIN_OP(f32, f32, op, stmt)
#define BOP_F64(op, stmt) BIN_OP(f64, f64, op, stmt)

#define CNV_OP(op, ty1, ty2) \
    do_##op: \
    fetch_op; \
    have(1); \
    stack[cur_sp].ty1 = stack[cur_sp].ty2; \
    dispatch_op;

#define SEX_OP(op, ty, b) \
    do_##op: \
    fetch_op; \
    have(1); \
    stack[cur_sp].ty = (int##b##_t)stack[cur_sp].ty; \
    dispatch_op;

void WA_Interpret(wa_t *m)
{
    wa_value_t *const   stack = m->stack;
    const uint64_t      msize = m->memory.bytesize;
    const int    enter_csp = m->csp;
    uint32_t     arg, val, fidx, tidx, cond, count, index;
    uint32_t     offset, addr, dst, src, n;
    uint8_t     *maddr;
    wa_value_t   sval;
    uint32_t     opcode;
    wa_pc_t      cur_pc = m->pc;
    intptr_t     cur_sp = m->sp;

    WA_ASSERT(enter_csp >= 0, "Call stack underflow");

#include "wa_dispatch.h"

    dispatch;

    //
    // Control flow operators
    //
    do_Unreachable:
        WA_ASSERT(0, "Unreachable instruction");

    do_Br:
        addr = get_u32(&cur_pc);
        cur_pc = m->code + addr;
        dispatch;

    do_BrIf:
        addr = get_u32(&cur_pc);
        have(1);
        cond = stack[cur_sp--].u32;
        if (cond) // if true
            cur_pc = m->code + addr;
        dispatch;

    do_BrUnless:
        addr = get_u32(&cur_pc);
        have(1);
        cond = stack[cur_sp--].u32;
        if (!cond) // if false
            cur_pc = m->code + addr;
        dispatch;

    do_BrTable:
        count = get_u16(&cur_pc);
        cur_pc += -(uintptr_t)cur_pc & 3;
        const uint32_t *br_table = (const uint32_t *)cur_pc;
        have(1);
        index = stack[cur_sp--].u32;
        cur_pc = m->code + br_table[MIN(index, count)];
        dispatch;

    do_Return:
        WA_ASSERT(m->csp >= 0, "Call stack underflow");

        const wa_frame_t *frame = &m->callstack[m->csp--];
        const wa_type_t  *type = frame->block->type;

        // Restore stack pointer
        if (type->num_results == 1) {
            // Save top value as result
            if (frame->sp < cur_sp) {
                stack[frame->sp + 1] = stack[cur_sp];
                cur_sp = frame->sp + 1;
            }
        } else {
            if (frame->sp < cur_sp) {
                cur_sp = frame->sp;
            }
        }

        // Restore frame pointer and set pc to return address
        m->fp = frame->fp;
        cur_pc = frame->ra;

        if (m->csp < enter_csp) {
            save_reg;
            return; // return to top-level from function
        }
        dispatch;

    //
    // Call operators
    //
    do_Call:
        fidx = get_u16(&cur_pc);
    do_Call2:
        save_reg;
        if (fidx < m->num_imports)
            WA_ThunkOut(m, fidx);   // import/thunk call
        else
            WA_SetupCall(m, fidx);  // regular function call
        load_reg;
        dispatch;

    do_CallIndirect:
        tidx = get_u16(&cur_pc);
        have(1);
        val = stack[cur_sp--].u32;
        WA_ASSERT(val < m->table.maximum, "Undefined element in table");
        fidx = m->table.entries[val];
        WA_ASSERT(fidx < m->num_funcs, "Bad function index");
        WA_ASSERT(m->funcs[fidx].type == &m->types[tidx], "Indirect call function type differ");
        goto do_Call2;

    //
    // Parametric operators
    //
    do_Drop:
        fetch_op;
        have(1);
        cur_sp--;
        dispatch_op;

    do_Select:
        fetch_op;
        have(3);
        cond = stack[cur_sp--].u32;
        cur_sp--;
        if (!cond)  // use a instead of b
            stack[cur_sp] = stack[cur_sp + 1];
        dispatch_op;

    //
    // Variable access
    //
    do_LocalGet:
        arg = get_u16(&cur_pc);
        fetch_op;
        need(1);
        stack[++cur_sp] = stack[m->fp + arg];
        dispatch_op;

    do_LocalSet:
        arg = get_u16(&cur_pc);
        fetch_op;
        have(1);
        stack[m->fp + arg] = stack[cur_sp--];
        dispatch_op;

    do_LocalTee:
        arg = get_u16(&cur_pc);
        fetch_op;
        have(1);
        stack[m->fp + arg] = stack[cur_sp];
        dispatch_op;

    do_GlobalGet:
        arg = get_u16(&cur_pc);
        fetch_op;
        need(1);
        stack[++cur_sp] = m->globals[arg];
        dispatch_op;

    do_GlobalSet:
        arg = get_u16(&cur_pc);
        fetch_op;
        have(1);
        m->globals[arg] = stack[cur_sp--];
        dispatch_op;

    //
    // Memory-related operators
    //
    do_MemorySize:
        fetch_op;
        need(1);
        stack[++cur_sp].u32 = m->memory.pages;
        dispatch_op;

    do_MemoryGrow:
        fetch_op;
        have(1);
        uint32_t prev_pages = m->memory.pages;
        uint32_t delta = stack[cur_sp].u32;
        stack[cur_sp].u32 = prev_pages;
        if (delta)
            stack[cur_sp].u32 = -1; // resize not supported
        dispatch_op;

    do_MemoryCopy:
        fetch_op;
        have(3);
        dst = stack[cur_sp - 2].u32;
        src = stack[cur_sp - 1].u32;
        n   = stack[cur_sp    ].u32;
        WA_ASSERT((uint64_t)dst + n <= msize &&
                  (uint64_t)src + n <= msize, "Memory copy out of bounds");
        memmove(m->memory.bytes + dst, m->memory.bytes + src, n);
        cur_sp -= 3;
        dispatch_op;

    do_MemoryFill:
        fetch_op;
        have(3);
        dst = stack[cur_sp - 2].u32;
        src = stack[cur_sp - 1].u32;
        n   = stack[cur_sp    ].u32;
        WA_ASSERT((uint64_t)dst + n <= msize, "Memory fill out of bounds");
        memset(m->memory.bytes + dst, src, n);
        cur_sp -= 3;
        dispatch_op;

    //
    // Memory load operators
    //
    LOAD_OP(I32_Load,     u32, 4, RN32)
    LOAD_OP(I64_Load,     u64, 8, RN64)
    LOAD_OP(I32_Load8_s,  i32, 1, RN8S)
    LOAD_OP(I32_Load8_u,  u32, 1, RN8)
    LOAD_OP(I32_Load16_s, i32, 2, RN16S)
    LOAD_OP(I32_Load16_u, u32, 2, RN16)
    LOAD_OP(I64_Load8_s,  i64, 1, RN8S)
    LOAD_OP(I64_Load8_u,  u64, 1, RN8)
    LOAD_OP(I64_Load16_s, i64, 2, RN16S)
    LOAD_OP(I64_Load16_u, u64, 2, RN16)
    LOAD_OP(I64_Load32_s, i64, 4, RN32S)
    LOAD_OP(I64_Load32_u, u64, 4, RN32)

    //
    // Memory store operators
    //
    STOR_OP(I32_Store,   u32, 4, WN32)
    STOR_OP(I64_Store,   u64, 8, WN64)
    STOR_OP(I32_Store8,  u32, 1, WN8)
    STOR_OP(I32_Store16, u32, 2, WN16)
    STOR_OP(I64_Store8,  u64, 1, WN8)
    STOR_OP(I64_Store16, u64, 2, WN16)
    STOR_OP(I64_Store32, u64, 4, WN32)

    //
    // Constants
    //
    do_I32_Const:
        need(1);
        stack[++cur_sp].u32 = get_u32(&cur_pc);
        dispatch;

    do_I64_Const:
        need(1);
        stack[++cur_sp].u64 = get_u64(&cur_pc);
        dispatch;

    //
    // Comparison operators
    //

    // unary
    do_I32_Eqz:
        fetch_op;
        have(1);
        stack[cur_sp].u32 = stack[cur_sp].u32 == 0;
        dispatch_op;

    do_I64_Eqz:
        fetch_op;
        have(1);
        stack[cur_sp].u32 = stack[cur_sp].u64 == 0;
        dispatch_op;

    // binary i32
    CMP_U32(I32_Eq,   a == b)
    CMP_U32(I32_Ne,   a != b)
    CMP_I32(I32_Lt_s, a <  b)
    CMP_U32(I32_Lt_u, a <  b)
    CMP_I32(I32_Gt_s, a >  b)
    CMP_U32(I32_Gt_u, a >  b)
    CMP_I32(I32_Le_s, a <= b)
    CMP_U32(I32_Le_u, a <= b)
    CMP_I32(I32_Ge_s, a >= b)
    CMP_U32(I32_Ge_u, a >= b)

    // binary i64
    CMP_U64(I64_Eq,   a == b)
    CMP_U64(I64_Ne,   a != b)
    CMP_I64(I64_Lt_s, a <  b)
    CMP_U64(I64_Lt_u, a <  b)
    CMP_I64(I64_Gt_s, a >  b)
    CMP_U64(I64_Gt_u, a >  b)
    CMP_I64(I64_Le_s, a <= b)
    CMP_U64(I64_Le_u, a <= b)
    CMP_I64(I64_Ge_s, a >= b)
    CMP_U64(I64_Ge_u, a >= b)

    // binary f32
    CMP_F32(F32_Eq, a == b)
    CMP_F32(F32_Ne, a != b)
    CMP_F32(F32_Lt, a <  b)
    CMP_F32(F32_Gt, a >  b)
    CMP_F32(F32_Le, a <= b)
    CMP_F32(F32_Ge, a >= b)

    // binary f64
    CMP_F64(F64_Eq, a == b)
    CMP_F64(F64_Ne, a != b)
    CMP_F64(F64_Lt, a <  b)
    CMP_F64(F64_Gt, a >  b)
    CMP_F64(F64_Le, a <= b)
    CMP_F64(F64_Ge, a >= b)

    //
    // Numeric operators
    //

    // unary i32
    UN_OP(I32_Clz,    u32, clz32)
    UN_OP(I32_Ctz,    u32, ctz32)
    UN_OP(I32_Popcnt, u32, __builtin_popcount)

    // unary i64
    UN_OP(I64_Clz,    u64, clz64)
    UN_OP(I64_Ctz,    u64, ctz64)
    UN_OP(I64_Popcnt, u64, __builtin_popcountll)

    // unary f32
    UN_OP(F32_Abs,     f32, fabsf)
    UN_OP(F32_Neg,     f32, -)
    UN_OP(F32_Ceil,    f32, ceilf)
    UN_OP(F32_Floor,   f32, floorf)
    UN_OP(F32_Trunc,   f32, truncf)
    UN_OP(F32_Nearest, f32, rintf)
    UN_OP(F32_Sqrt,    f32, sqrtf)

    // unary f64
    UN_OP(F64_Abs,     f64, fabs)
    UN_OP(F64_Neg,     f64, -)
    UN_OP(F64_Ceil,    f64, ceil)
    UN_OP(F64_Floor,   f64, floor)
    UN_OP(F64_Trunc,   f64, trunc)
    UN_OP(F64_Nearest, f64, rint)
    UN_OP(F64_Sqrt,    f64, sqrt)

    // binary i32
    BOP_U32(I32_Add, a + b)
    BOP_U32(I32_Sub, a - b)
    BOP_U32(I32_Mul, a * b)
    BOP_I32(I32_Div_s,
        WA_ASSERT(b, "Integer divide by zero");
        WA_ASSERT(!(a == INT32_MIN && b == -1), "Integer overflow");
        a / b
    )
    BOP_U32(I32_Div_u,
        WA_ASSERT(b, "Integer divide by zero");
        a / b
    )
    BOP_I32(I32_Rem_s,
        WA_ASSERT(b, "Integer divide by zero");
        !(a == INT32_MIN && b == -1) ? a % b : 0
    )
    BOP_U32(I32_Rem_u,
        WA_ASSERT(b, "Integer divide by zero");
        a % b
    )
    BOP_U32(I32_And,   a & b)
    BOP_U32(I32_Or,    a | b)
    BOP_U32(I32_Xor,   a ^ b)
    BOP_U32(I32_Shl,   a << (b & 31))
    BOP_I32(I32_Shr_s, a >> (b & 31))
    BOP_U32(I32_Shr_u, a >> (b & 31))
    BOP_U32(I32_Rotl,  rotl32(a, b))
    BOP_U32(I32_Rotr,  rotr32(a, b))

    // binary i64
    BOP_U64(I64_Add, a + b)
    BOP_U64(I64_Sub, a - b)
    BOP_U64(I64_Mul, a * b)
    BOP_I64(I64_Div_s,
        WA_ASSERT(b, "Integer divide by zero");
        WA_ASSERT(!(a == INT64_MIN && b == -1), "Integer overflow");
        a / b
    )
    BOP_U64(I64_Div_u,
        WA_ASSERT(b, "Integer divide by zero");
        a / b
    )
    BOP_I64(I64_Rem_s,
        WA_ASSERT(b, "Integer divide by zero");
        !(a == INT64_MIN && b == -1) ? a % b : 0
    )
    BOP_U64(I64_Rem_u,
        WA_ASSERT(b, "Integer divide by zero");
        a % b
    )
    BOP_U64(I64_And,   a & b)
    BOP_U64(I64_Or,    a | b)
    BOP_U64(I64_Xor,   a ^ b)
    BOP_U64(I64_Shl,   a << (b & 63))
    BOP_I64(I64_Shr_s, a >> (b & 63))
    BOP_U64(I64_Shr_u, a >> (b & 63))
    BOP_U64(I64_Rotl,  rotl64(a, b))
    BOP_U64(I64_Rotr,  rotr64(a, b))

    // binary f32
    BOP_F32(F32_Add, a + b)
    BOP_F32(F32_Sub, a - b)
    BOP_F32(F32_Mul, a * b)
    BOP_F32(F32_Div, a / b)
    BOP_F32(F32_Min, fminf(a, b))
    BOP_F32(F32_Max, fmaxf(a, b))
    BOP_F32(F32_Copysign, copysignf(a, b))

    // binary f64
    BOP_F64(F64_Add, a + b)
    BOP_F64(F64_Sub, a - b)
    BOP_F64(F64_Mul, a * b)
    BOP_F64(F64_Div, a / b)
    BOP_F64(F64_Min, fmin(a, b))
    BOP_F64(F64_Max, fmax(a, b))
    BOP_F64(F64_Copysign, copysign(a, b))

    // conversion operations
    CNV_OP(I32_Wrap_i64,      u32, u64)
    CNV_OP(I32_Trunc_f32_s,   i32, f32)
    CNV_OP(I32_Trunc_f32_u,   u32, f32)
    CNV_OP(I32_Trunc_f64_s,   i32, f64)
    CNV_OP(I32_Trunc_f64_u,   u32, f64)
    CNV_OP(I64_Extend_i32_s,  i64, i32)
    CNV_OP(I64_Extend_i32_u,  u64, u32)
    CNV_OP(I64_Trunc_f32_s,   i64, f32)
    CNV_OP(I64_Trunc_f32_u,   u64, f32)
    CNV_OP(I64_Trunc_f64_s,   i64, f64)
    CNV_OP(I64_Trunc_f64_u,   u64, f64)
    CNV_OP(F32_Convert_i32_s, f32, i32)
    CNV_OP(F32_Convert_i32_u, f32, u32)
    CNV_OP(F32_Convert_i64_s, f32, i64)
    CNV_OP(F32_Convert_i64_u, f32, u64)
    CNV_OP(F32_Demote_f64,    f32, f64)
    CNV_OP(F64_Convert_i32_s, f64, i32)
    CNV_OP(F64_Convert_i32_u, f64, u32)
    CNV_OP(F64_Convert_i64_s, f64, i64)
    CNV_OP(F64_Convert_i64_u, f64, u64)
    CNV_OP(F64_Promote_f32,   f64, f32)

    // sign extensions
    SEX_OP(I32_Extend8_s,  i32,  8)
    SEX_OP(I32_Extend16_s, i32, 16)
    SEX_OP(I64_Extend8_s,  i64,  8)
    SEX_OP(I64_Extend16_s, i64, 16)
    SEX_OP(I64_Extend32_s, i64, 32)
}

static wa_interpreter_opcode_t extended_opcode(wa_extended_opcode_t opcode)
{
    switch (opcode) {
        // We pretend to support saturating conversions
        // to avoid costly runtime checks by the compiler
        case I32_Trunc_sat_f32_s: return OP_I32_Trunc_f32_s;
        case I32_Trunc_sat_f32_u: return OP_I32_Trunc_f32_u;
        case I32_Trunc_sat_f64_s: return OP_I32_Trunc_f64_s;
        case I32_Trunc_sat_f64_u: return OP_I32_Trunc_f64_u;
        case I64_Trunc_sat_f32_s: return OP_I64_Trunc_f32_s;
        case I64_Trunc_sat_f32_u: return OP_I64_Trunc_f32_u;
        case I64_Trunc_sat_f64_s: return OP_I64_Trunc_f64_s;
        case I64_Trunc_sat_f64_u: return OP_I64_Trunc_f64_u;
        case MemoryCopy: return OP_MemoryCopy;
        case MemoryFill: return OP_MemoryFill;
        default: ASSERT(0, "Unrecognized extended opcode %#x", opcode);
    }
}

static wa_interpreter_opcode_t memload_opcode(wa_opcode_t opcode)
{
    switch (opcode) {
        // We don't check types on stack, so no special opcodes
        // for floats are required
        case I32_Load: return OP_I32_Load;
        case I64_Load: return OP_I64_Load;
        case F32_Load: return OP_I32_Load;
        case F64_Load: return OP_I64_Load;
        case I32_Load8_s ... I64_Load32_u:
            return opcode + OP_I32_Load8_s - I32_Load8_s;
        case I32_Store: return OP_I32_Store;
        case I64_Store: return OP_I64_Store;
        case F32_Store: return OP_I32_Store;
        case F64_Store: return OP_I64_Store;
        case I32_Store8 ... I64_Store32:
            return opcode + OP_I32_Store8 - I32_Store8;
        default: return OP_Unreachable;
    }
}

static void put_u8(sizebuf_t *out, uint8_t v)
{
    WN8(SZ_GetSpace(out, 1), v);
}

static void put_u16(sizebuf_t *out, uint16_t v)
{
    WN16(SZ_GetSpace(out, 2), v);
}

static void put_u32(sizebuf_t *out, uint32_t v)
{
    WN32(SZ_GetSpace(out, 4), v);
}

static void put_u64(sizebuf_t *out, uint64_t v)
{
    WN64(SZ_GetSpace(out, 8), v);
}

// Converts from raw WASM opcodes to interpreter internal opcodes.
// Returns 0 on error, 1 if single pass is required, ≥ 2 if two
// passes are required.
static int WA_PrepareFunction(wa_t *m, wa_block_t *func, uint32_t func_end_addr,
                              sizebuf_t *in, sizebuf_t *out, wa_block_t *blocks, int pass)
{
    wa_block_t  *block;
    wa_block_t  *blockstack[BLOCKSTACK_SIZE];
    int          top = -1;
    int          num_blocks = 0;
    wa_opcode_t  opcode = Unreachable;
    uint32_t     count, index;

    func->start_addr = out->cursize;
    while (in->readcount <= func_end_addr) {
        uint32_t pos = in->readcount;

        opcode = SZ_ReadByte(in);
        switch (opcode) {
        case Unreachable:
            put_u8(out, OP_Unreachable);
            break;

        case Nop:
            break;

        case Block:
        case Loop:
        case If:
            ASSERT(num_blocks < BLOCKSTACK_SIZE, "Too many blocks");
            block = &blocks[num_blocks++];

            // For speed, we are not managing block stack at runtime. Instead
            // we ignore block type and directly convert blocks to jumps. This
            // works well for Clang generated bytecode.
            index = SZ_ReadLeb(in);
            if (pass == 0) {
                memset(block, 0, sizeof(*block));
                block->opcode = opcode;
                block->type = WA_GetBlockType(index);   // unused, but validated
                if (!block->type)
                    return qfalse;
            } else {
                assert(block->opcode == opcode);
            }

            ASSERT(top < BLOCKSTACK_SIZE - 1, "Blockstack overflow");
            blockstack[++top] = block;

            if (opcode == Loop)
                block->end_addr = out->cursize;    // loop: label after start

            if (opcode == If) {
                put_u8(out, OP_BrUnless);
                // branch to else block or after end of if
                if (block->start_addr == 0)
                    put_u32(out, block->end_addr);
                else
                    put_u32(out, block->start_addr);
            }
            break;

        case Else:
            ASSERT(top >= 0, "Blockstack underflow");
            block = blockstack[top];
            ASSERT(block->opcode == If, "Else not matched with if");
            put_u8(out, OP_Br);
            put_u32(out, block->end_addr);
            block->start_addr = out->cursize;
            break;

        case End:
            if (pos == func_end_addr) {
                put_u8(out, OP_Return);
                break;
            }
            ASSERT(top >= 0, "Blockstack underflow");
            block = blockstack[top--];
            if (block->opcode != Loop)
                block->end_addr = out->cursize; // block, if: label at end
            break;

        case Return:
            put_u8(out, OP_Return);
            break;

        case Br:
        case BrIf:
            ASSERT(top >= 0, "Blockstack underflow");
            index = SZ_ReadLeb(in);
            ASSERT(index <= top, "Bad label");
            put_u8(out, opcode + OP_Br - Br);
            put_u32(out, blockstack[top - index]->end_addr);
            break;

        case BrTable:
            ASSERT(top >= 0, "Blockstack underflow");

            count = SZ_ReadLeb(in); // target count
            ASSERT(count < BR_TABLE_SIZE, "BrTable size too big");

            put_u8(out, OP_BrTable);
            put_u16(out, count);

            out->cursize += -out->cursize & 3;
            for (uint32_t i = 0; i < count; i++) {
                index = SZ_ReadLeb(in);
                ASSERT(index <= top, "Bad label");
                put_u32(out, blockstack[top - index]->end_addr);
            }

            index = SZ_ReadLeb(in); // default target
            ASSERT(index <= top, "Bad label");
            put_u32(out, blockstack[top - index]->end_addr);
            break;

        case Call:
            index = SZ_ReadLeb(in);
            ASSERT(index < m->num_funcs, "Bad function index");
            put_u8(out, OP_Call);
            put_u16(out, index);
            break;

        case CallIndirect:
            index = SZ_ReadLeb(in);
            ASSERT(index < m->num_types, "Bad type index");
            put_u8(out, OP_CallIndirect);
            put_u16(out, index);
            index = SZ_ReadLeb(in);
            ASSERT(index == 0, "Only 1 default table supported");
            break;

        case Drop:
            put_u8(out, OP_Drop);
            break;

        case Select:
            put_u8(out, OP_Select);
            break;

        case LocalGet:
        case LocalSet:
        case LocalTee:
            index = SZ_ReadLeb(in);
            ASSERT(index < func->type->num_params + func->num_locals, "Bad local index");
            put_u8(out, opcode + OP_LocalGet - LocalGet);
            put_u16(out, index);
            break;

        case GlobalGet:
        case GlobalSet:
            index = SZ_ReadLeb(in);
            ASSERT(index < m->num_globals, "Bad global index");
            put_u8(out, opcode + OP_GlobalGet - GlobalGet);
            put_u16(out, index);
            break;

        case MemorySize:
        case MemoryGrow:
            in->readcount++;
            put_u8(out, opcode + OP_MemorySize - MemorySize);
            break;

        case I32_Load ... I64_Store32:
            SZ_ReadLeb(in); // skip flags
            put_u8(out, memload_opcode(opcode));
            put_u32(out, SZ_ReadLeb(in));
            break;

        case I32_Const:
            put_u8(out, OP_I32_Const);
            put_u32(out, SZ_ReadSignedLeb(in, 32));
            break;

        case I64_Const:
            put_u8(out, OP_I64_Const);
            put_u64(out, SZ_ReadSignedLeb(in, 64));
            break;

        case F32_Const:
            put_u8(out, OP_I32_Const);
            put_u32(out, SZ_ReadLong(in));
            break;

        case F64_Const:
            put_u8(out, OP_I64_Const);
            put_u64(out, SZ_ReadLong64(in));
            break;

        case I32_Eqz ... F64_Promote_f32:
            put_u8(out, opcode + OP_I32_Eqz - I32_Eqz);
            break;

        case I32_Reinterpret_f32 ... F64_Reinterpret_i64:
            break;

        case I32_Extend8_s ... I64_Extend32_s:
            put_u8(out, opcode + OP_I32_Extend8_s - I32_Extend8_s);
            break;

        case Extended:
            index = extended_opcode(SZ_ReadLeb(in));
            if (!index)
                return qfalse;
            put_u8(out, index);
            switch (index) {
            case OP_MemoryCopy:
                in->readcount += 2;
                break;
            case OP_MemoryFill:
                in->readcount += 1;
                break;
            }
            break;

        default:
            ASSERT(0, "Unrecognized opcode %#x", opcode);
        }
    }

    func->end_addr = out->cursize - 1;

    ASSERT(!out->overflowed, "Output buffer overflowed");
    ASSERT(top == -1, "Function ended in middle of block");
    ASSERT(opcode == End, "Function block doesn't end with End opcode");

    return num_blocks + 1;
}

qboolean WA_PrepareInterpreter(wa_t *m, sizebuf_t *in)
{
    wa_block_t  blocks[BLOCKSTACK_SIZE];
    sizebuf_t   out;

    m->code = WA_Malloc(in->cursize * 2);
    SZ_InitWrite(&out, m->code, in->cursize * 2);

    for (uint32_t f = m->num_imports; f < m->num_funcs; f++) {
        wa_block_t *func = &m->funcs[f];
        uint32_t start_addr = func->start_addr;
        uint32_t end_addr   = func->end_addr;
        uint32_t pos        = out.cursize;

        for (int pass = 0; pass < 2; pass++) {
            in->readcount = start_addr;
            out.cursize   = pos;
            int ret = WA_PrepareFunction(m, func, end_addr, in, &out, blocks, pass);
            if (!ret)
                return qfalse;
            if (ret == 1)
                break;
        }
    }

    // TODO: shrink hunk allocation somehow?
    m->num_bytes = out.cursize;

    return qtrue;
}
