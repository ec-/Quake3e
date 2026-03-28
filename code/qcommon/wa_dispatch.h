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
*/

#define OP(op) [OP_##op] = &&do_##op

static const void *dispatch_table[] = {
    OP(Unreachable),
    OP(Br),
    OP(BrIf),
    OP(BrUnless),
    OP(BrTable),
    OP(Return),
    OP(Call),
    OP(CallIndirect),
    OP(Drop),
    OP(Select),
    OP(LocalGet),
    OP(LocalSet),
    OP(LocalTee),
    OP(GlobalGet),
    OP(GlobalSet),

    OP(I32_Load),
    OP(I64_Load),
    OP(I32_Load8_s),
    OP(I32_Load8_u),
    OP(I32_Load16_s),
    OP(I32_Load16_u),
    OP(I64_Load8_s),
    OP(I64_Load8_u),
    OP(I64_Load16_s),
    OP(I64_Load16_u),
    OP(I64_Load32_s),
    OP(I64_Load32_u),

    OP(I32_Store),
    OP(I64_Store),
    OP(I32_Store8),
    OP(I32_Store16),
    OP(I64_Store8),
    OP(I64_Store16),
    OP(I64_Store32),

    OP(MemorySize),
    OP(MemoryGrow),
    OP(MemoryCopy),
    OP(MemoryFill),

    OP(I32_Const),
    OP(I64_Const),

    OP(I32_Eqz),
    OP(I32_Eq),
    OP(I32_Ne),
    OP(I32_Lt_s),
    OP(I32_Lt_u),
    OP(I32_Gt_s),
    OP(I32_Gt_u),
    OP(I32_Le_s),
    OP(I32_Le_u),
    OP(I32_Ge_s),
    OP(I32_Ge_u),

    OP(I64_Eqz),
    OP(I64_Eq),
    OP(I64_Ne),
    OP(I64_Lt_s),
    OP(I64_Lt_u),
    OP(I64_Gt_s),
    OP(I64_Gt_u),
    OP(I64_Le_s),
    OP(I64_Le_u),
    OP(I64_Ge_s),
    OP(I64_Ge_u),

    OP(F32_Eq),
    OP(F32_Ne),
    OP(F32_Lt),
    OP(F32_Gt),
    OP(F32_Le),
    OP(F32_Ge),

    OP(F64_Eq),
    OP(F64_Ne),
    OP(F64_Lt),
    OP(F64_Gt),
    OP(F64_Le),
    OP(F64_Ge),

    OP(I32_Clz),
    OP(I32_Ctz),
    OP(I32_Popcnt),

    OP(I32_Add),
    OP(I32_Sub),
    OP(I32_Mul),
    OP(I32_Div_s),
    OP(I32_Div_u),
    OP(I32_Rem_s),
    OP(I32_Rem_u),
    OP(I32_And),
    OP(I32_Or),
    OP(I32_Xor),
    OP(I32_Shl),
    OP(I32_Shr_s),
    OP(I32_Shr_u),
    OP(I32_Rotl),
    OP(I32_Rotr),

    OP(I64_Clz),
    OP(I64_Ctz),
    OP(I64_Popcnt),

    OP(I64_Add),
    OP(I64_Sub),
    OP(I64_Mul),
    OP(I64_Div_s),
    OP(I64_Div_u),
    OP(I64_Rem_s),
    OP(I64_Rem_u),
    OP(I64_And),
    OP(I64_Or),
    OP(I64_Xor),
    OP(I64_Shl),
    OP(I64_Shr_s),
    OP(I64_Shr_u),
    OP(I64_Rotl),
    OP(I64_Rotr),

    OP(F32_Abs),
    OP(F32_Neg),
    OP(F32_Ceil),
    OP(F32_Floor),
    OP(F32_Trunc),
    OP(F32_Nearest),
    OP(F32_Sqrt),

    OP(F32_Add),
    OP(F32_Sub),
    OP(F32_Mul),
    OP(F32_Div),
    OP(F32_Min),
    OP(F32_Max),
    OP(F32_Copysign),

    OP(F64_Abs),
    OP(F64_Neg),
    OP(F64_Ceil),
    OP(F64_Floor),
    OP(F64_Trunc),
    OP(F64_Nearest),
    OP(F64_Sqrt),

    OP(F64_Add),
    OP(F64_Sub),
    OP(F64_Mul),
    OP(F64_Div),
    OP(F64_Min),
    OP(F64_Max),
    OP(F64_Copysign),

    OP(I32_Wrap_i64),
    OP(I32_Trunc_f32_s),
    OP(I32_Trunc_f32_u),
    OP(I32_Trunc_f64_s),
    OP(I32_Trunc_f64_u),

    OP(I64_Extend_i32_s),
    OP(I64_Extend_i32_u),
    OP(I64_Trunc_f32_s),
    OP(I64_Trunc_f32_u),
    OP(I64_Trunc_f64_s),
    OP(I64_Trunc_f64_u),

    OP(F32_Convert_i32_s),
    OP(F32_Convert_i32_u),
    OP(F32_Convert_i64_s),
    OP(F32_Convert_i64_u),
    OP(F32_Demote_f64),

    OP(F64_Convert_i32_s),
    OP(F64_Convert_i32_u),
    OP(F64_Convert_i64_s),
    OP(F64_Convert_i64_u),
    OP(F64_Promote_f32),

    OP(I32_Extend8_s),
    OP(I32_Extend16_s),
    OP(I64_Extend8_s),
    OP(I64_Extend16_s),
    OP(I64_Extend32_s),
};

#undef OP
