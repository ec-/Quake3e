/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2020-2021 Quake3e project

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// load time compiler and execution environment for PPC64 (ppc64le)
// using ELFv2 ABI, without dynamic register allocation

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#include "vm_local.h"

#define NUM_PASSES 1

// additional integrity checks
#define DEBUG_VM

#define FUNC_ALIGN 16

//#define DUMP_CODE

// =========================================================================
// PPC64 ELFv2 ABI Register Usage
// =========================================================================
// r0       scratch (cannot be base register in D-form loads/stores)
// r1       stack pointer (must be 16-byte aligned)
// r2       TOC pointer (reserved, do not touch)
// r3-r10   argument/return/scratch registers
// r11-r12  scratch (r12 = target addr for indirect calls in ELFv2)
// r13      small data area pointer (reserved, do not touch)
// r14-r31  callee-saved (non-volatile)
// f0       scratch
// f1-f13   argument/return/scratch
// f14-f31  callee-saved
// LR       link register (saved/restored via mflr/mtlr)
// CR       condition register (CR0-CR1 volatile, CR2-CR4 non-volatile)
// CTR      count register (volatile)

// GPR definitions
#define R0	0
#define R1	1	// SP
#define R2	2	// TOC - reserved
#define R3	3	// arg/return, scratch
#define R4	4	// arg, scratch
#define R5	5	// arg, scratch
#define R6	6	// arg, scratch
#define R7	7	// arg, scratch
#define R8	8	// arg, scratch
#define R9	9	// arg, scratch
#define R10	10	// arg, scratch
#define R11	11	// scratch
#define R12	12	// scratch (must hold target addr before bctrl in ELFv2)
#define R13	13	// small data area - reserved
#define R14	14	// callee-saved
#define R15	15	// callee-saved
#define R16	16	// callee-saved
#define R17	17	// callee-saved
#define R18	18	// callee-saved
#define R19	19	// callee-saved
#define R20	20	// callee-saved
#define R21	21	// callee-saved
#define R22	22	// callee-saved
#define R23	23	// callee-saved
#define R24	24	// callee-saved
#define R25	25	// callee-saved
#define R26	26	// callee-saved
#define R27	27	// callee-saved
#define R28	28	// callee-saved
#define R29	29	// callee-saved
#define R30	30	// callee-saved
#define R31	31	// callee-saved

// FPR definitions
#define F0	0	// scratch
#define F1	1	// scratch
#define F2	2	// scratch

// VM state registers (callee-saved)
#define rVMBASE			R14		// pointer to vm_t struct
#define rOPSTACK		R15		// opStack pointer (int32_t*)
#define rOPSTACKTOP		R16		// opStackTop pointer
#define rINSPOINTERS	R17		// instructionPointers (intptr_t*)
#define rPSTACK			R18		// programStack (int32_t value)
#define rPSTACKBOTTOM	R19		// stackBottom
#define rDATABASE		R20		// dataBase pointer
#define rDATAMASK		R21		// dataMask
#define rPROCBASE		R22		// procBase = dataBase + programStack (for current function)

typedef enum
{
	FUNC_ENTR,
	FUNC_BCPY,
	FUNC_CALL,
	FUNC_SYSC,
	FUNC_SYSF,
	FUNC_PSOF,
	FUNC_OSOF,
	FUNC_BADJ,
	FUNC_OUTJ,
	FUNC_BADR,
	FUNC_BADW,
	OFFSET_T_LAST
} offset_t;


static uint32_t *code;
static uint32_t compiledOfs;

static instruction_t *inst = NULL;

static uint32_t ip;
static uint32_t pass;

static uint32_t savedOffset[ OFFSET_T_LAST ];

// opstack tracking (simplified - no dynamic register allocation)
static int opstack;

static void VM_FreeBuffers( void )
{
	if ( inst ) {
		Z_Free( inst );
		inst = NULL;
	}
}


// =========================================================================
// PPC64 Instruction Encoding Macros
// =========================================================================
//
// PPC instructions are all 32-bit, big-endian bit numbering (bit 0 = MSB).
// The encoding macros construct native instruction words.
//
// Instruction forms:
//   D-form:  [opcode(6)][RT(5)][RA(5)][D(16)]
//   DS-form: [opcode(6)][RT(5)][RA(5)][DS(14)][XO(2)]
//   X-form:  [opcode(6)][RT(5)][RA(5)][RB(5)][XO(10)][Rc(1)]
//   XO-form: [opcode(6)][RT(5)][RA(5)][RB(5)][OE(1)][XO(9)][Rc(1)]
//   XFX-form:[opcode(6)][RT(5)][spr(10)][XO(10)][0]
//   I-form:  [opcode(6)][LI(24)][AA(1)][LK(1)]
//   B-form:  [opcode(6)][BO(5)][BI(5)][BD(14)][AA(1)][LK(1)]
//   XL-form: [opcode(6)][BO(5)][BI(5)][0(5)][XO(10)][LK(1)]
//   M-form:  [opcode(6)][RS(5)][RA(5)][SH(5)][MB(5)][ME(5)][Rc(1)]
//   MD-form: [opcode(6)][RS(5)][RA(5)][sh(5)][mb(6)][XO(3)][sh2(1)][Rc(1)]
//   A-form:  [opcode(6)][FRT(5)][FRA(5)][FRB(5)][FRC(5)][XO(5)][Rc(1)]

// ---- D-form ----
// addi, addis, lwz, stw, lbz, sth, lhz, etc.
#define PPC_D(op, rt, ra, d) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(rt)&0x1F)<<21) | \
	  (((unsigned)(ra)&0x1F)<<16) | ((unsigned)(d)&0xFFFF) )

// ---- DS-form ----
// ld, std, lwa
// The DS field is 14 bits; actual byte displacement = DS << 2
// So we pass the byte offset and shift right by 2 for encoding.
// The byte offset MUST be a multiple of 4.
#define PPC_DS(op, rt, ra, ds, xo) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(rt)&0x1F)<<21) | \
	  (((unsigned)(ra)&0x1F)<<16) | ((unsigned)(ds)&0xFFFC) | \
	  ((unsigned)(xo)&0x3) )

// ---- X-form ----
// and, or, xor, cmp, etc.
#define PPC_X(op, rs, ra, rb, xo, rc) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(rs)&0x1F)<<21) | \
	  (((unsigned)(ra)&0x1F)<<16) | (((unsigned)(rb)&0x1F)<<11) | \
	  (((unsigned)(xo)&0x3FF)<<1) | ((unsigned)(rc)&1) )

// ---- XO-form ----
// add, subf, mullw, divw, etc.
#define PPC_XO(op, rt, ra, rb, oe, xo, rc) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(rt)&0x1F)<<21) | \
	  (((unsigned)(ra)&0x1F)<<16) | (((unsigned)(rb)&0x1F)<<11) | \
	  (((unsigned)(oe)&1)<<10) | (((unsigned)(xo)&0x1FF)<<1) | \
	  ((unsigned)(rc)&1) )

// ---- XFX-form ----
// mtspr, mfspr
#define PPC_XFX(op, rt, spr, xo) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(rt)&0x1F)<<21) | \
	  (((unsigned)(spr)&0x3FF)<<11) | (((unsigned)(xo)&0x3FF)<<1) )

// ---- I-form ----
// b, bl
#define PPC_I(op, li, aa, lk) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(li)&0xFFFFFF)<<2) | \
	  (((unsigned)(aa)&1)<<1) | ((unsigned)(lk)&1) )

// ---- B-form ----
// bc, bcl (B-form encoding — use PPC_BC convenience macro instead)
#define PPC_B_RAW(op, bo, bi, bd, aa, lk) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(bo)&0x1F)<<21) | \
	  (((unsigned)(bi)&0x1F)<<16) | (((unsigned)(bd)&0x3FFF)<<2) | \
	  (((unsigned)(aa)&1)<<1) | ((unsigned)(lk)&1) )

// ---- XL-form ----
// bclr, bcctr
#define PPC_XL(op, bo, bi, xo, lk) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(bo)&0x1F)<<21) | \
	  (((unsigned)(bi)&0x1F)<<16) | (((unsigned)(xo)&0x3FF)<<1) | \
	  ((unsigned)(lk)&1) )

// ---- M-form ----
// rlwinm, rlwimi
#define PPC_M(op, rs, ra, sh, mb, me, rc) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(rs)&0x1F)<<21) | \
	  (((unsigned)(ra)&0x1F)<<16) | (((unsigned)(sh)&0x1F)<<11) | \
	  (((unsigned)(mb)&0x1F)<<6) | (((unsigned)(me)&0x1F)<<1) | \
	  ((unsigned)(rc)&1) )

// ---- MD-form ----
// rldicl, rldicr, rldic, rldimi
// Layout (PPC big-endian bit numbering, but we use LSB=bit0):
//   bits 31-26: opcode (6)
//   bits 25-21: RS (5)
//   bits 20-16: RA (5)
//   bits 15-11: sh[0:4] (5)
//   bits 10-6:  mb[0:4] (5)
//   bit  5:     mb[5] (1)
//   bits 4-2:   XO (3)
//   bit  1:     sh[5] (1)
//   bit  0:     Rc (1)
#define PPC_MD(op, rs, ra, sh, mb, xo3, rc) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(rs)&0x1F)<<21) | \
	  (((unsigned)(ra)&0x1F)<<16) | (((unsigned)(sh)&0x1F)<<11) | \
	  (((unsigned)(mb)&0x1F)<<6) | ((((unsigned)(mb)>>5)&1)<<5) | \
	  (((unsigned)(xo3)&0x7)<<2) | ((((unsigned)(sh)>>5)&1)<<1) | \
	  ((unsigned)(rc)&1) )

// sldi ra, rs, n  =>  rldicr ra, rs, n, 63-n  (XO=1 for rldicr)
#define PPC_SLDI(ra, rs, n)		PPC_MD(30, rs, ra, n, 63-(n), 1, 0)
// srdi ra, rs, n  =>  rldicl ra, rs, 64-n, n  (XO=0 for rldicl)
#define PPC_SRDI(ra, rs, n)		PPC_MD(30, rs, ra, 64-(n), n, 0, 0)
// clrldi ra, rs, n => rldicl ra, rs, 0, n
#define PPC_CLRLDI(ra, rs, n)	PPC_MD(30, rs, ra, 0, n, 0, 0)


// =========================================================================
// PPC64 Instruction Mnemonics
// =========================================================================

// -- Arithmetic (D-form) --
// addi rt, ra, si  (li rt, si when ra=0)
#define PPC_ADDI(rt, ra, si)	PPC_D(14, rt, ra, si)
// addis rt, ra, si  (lis rt, si when ra=0)
#define PPC_ADDIS(rt, ra, si)	PPC_D(15, rt, ra, si)
// li rt, si  (pseudo for addi rt, 0, si)
#define PPC_LI(rt, si)			PPC_ADDI(rt, R0, si)
// lis rt, si  (pseudo for addis rt, 0, si)
#define PPC_LIS(rt, si)			PPC_ADDIS(rt, R0, si)
// subfic rt, ra, si
#define PPC_SUBFIC(rt, ra, si)	PPC_D(8, rt, ra, si)
// mulli rt, ra, si
#define PPC_MULLI(rt, ra, si)	PPC_D(7, rt, ra, si)

// -- Arithmetic (XO-form) --
// add rt, ra, rb
#define PPC_ADD(rt, ra, rb)		PPC_XO(31, rt, ra, rb, 0, 266, 0)
// subf rt, ra, rb  (rt = rb - ra)
#define PPC_SUBF(rt, ra, rb)	PPC_XO(31, rt, ra, rb, 0, 40, 0)
// sub rt, ra, rb  (pseudo: subf rt, rb, ra => rt = ra - rb)
// NOTE: PPC subf is rt = rb - ra, so sub(rt,a,b) = subf(rt,b,a)
#define PPC_SUB(rt, ra, rb)		PPC_SUBF(rt, rb, ra)
// neg rt, ra
#define PPC_NEG(rt, ra)			PPC_XO(31, rt, ra, 0, 0, 104, 0)
// mullw rt, ra, rb
#define PPC_MULLW(rt, ra, rb)	PPC_XO(31, rt, ra, rb, 0, 235, 0)
// mulhw rt, ra, rb (signed high word)
#define PPC_MULHW(rt, ra, rb)	PPC_X(31, rt, ra, rb, 75, 0)
// mulhwu rt, ra, rb (unsigned high word)
#define PPC_MULHWU(rt, ra, rb)	PPC_X(31, rt, ra, rb, 11, 0)
// divw rt, ra, rb (signed)
#define PPC_DIVW(rt, ra, rb)	PPC_XO(31, rt, ra, rb, 0, 491, 0)
// divwu rt, ra, rb (unsigned)
#define PPC_DIVWU(rt, ra, rb)	PPC_XO(31, rt, ra, rb, 0, 459, 0)

// -- Logical (X-form, note: PPC X-form logicals use RS,RA,RB encoding) --
// and ra, rs, rb
#define PPC_AND(ra, rs, rb)		PPC_X(31, rs, ra, rb, 28, 0)
// or ra, rs, rb
#define PPC_OR(ra, rs, rb)		PPC_X(31, rs, ra, rb, 444, 0)
// xor ra, rs, rb
#define PPC_XOR(ra, rs, rb)		PPC_X(31, rs, ra, rb, 316, 0)
// nor ra, rs, rb
#define PPC_NOR(ra, rs, rb)		PPC_X(31, rs, ra, rb, 124, 0)
// not ra, rs  (pseudo for nor ra, rs, rs)
#define PPC_NOT(ra, rs)			PPC_NOR(ra, rs, rs)
// mr ra, rs  (pseudo for or ra, rs, rs)
#define PPC_MR(ra, rs)			PPC_OR(ra, rs, rs)

// andi. ra, rs, ui  (always sets CR0)
#define PPC_ANDI(ra, rs, ui)	PPC_D(28, rs, ra, ui)
// ori ra, rs, ui
#define PPC_ORI(ra, rs, ui)		PPC_D(24, rs, ra, ui)
// oris ra, rs, ui
#define PPC_ORIS(ra, rs, ui)	PPC_D(25, rs, ra, ui)
// xori ra, rs, ui
#define PPC_XORI(ra, rs, ui)	PPC_D(26, rs, ra, ui)
// xoris ra, rs, ui
#define PPC_XORIS(ra, rs, ui)	PPC_D(27, rs, ra, ui)
// nop (ori 0, 0, 0)
#define PPC_NOP()				PPC_ORI(R0, R0, 0)

// -- Shift (X-form, with RS,RA encoding) --
// slw ra, rs, rb
#define PPC_SLW(ra, rs, rb)		PPC_X(31, rs, ra, rb, 24, 0)
// srw ra, rs, rb
#define PPC_SRW(ra, rs, rb)		PPC_X(31, rs, ra, rb, 536, 0)
// sraw ra, rs, rb
#define PPC_SRAW(ra, rs, rb)	PPC_X(31, rs, ra, rb, 792, 0)
// srawi ra, rs, sh
#define PPC_SRAWI(ra, rs, sh)	PPC_X(31, rs, ra, sh, 824, 0)

// -- Shift 64-bit --
// sld ra, rs, rb
#define PPC_SLD(ra, rs, rb)		PPC_X(31, rs, ra, rb, 27, 0)
// srd ra, rs, rb
#define PPC_SRD(ra, rs, rb)		PPC_X(31, rs, ra, rb, 539, 0)

// -- Rotate/Mask --
// rlwinm ra, rs, sh, mb, me  (rotate left word immediate then AND with mask)
#define PPC_RLWINM(ra, rs, sh, mb, me)	PPC_M(21, rs, ra, sh, mb, me, 0)
// clrlwi ra, rs, n  (clear left n bits) = rlwinm ra, rs, 0, n, 31
#define PPC_CLRLWI(ra, rs, n)			PPC_RLWINM(ra, rs, 0, n, 31)


// -- Compare (D-form and X-form) --
// cmpwi cr, ra, si  (signed word compare immediate)
// L=0 for 32-bit compare, cr field in bits 21-23
#define PPC_CMPWI(cr, ra, si)	PPC_D(11, ((cr)<<2), ra, si)
// cmplwi cr, ra, ui  (unsigned word compare immediate)
#define PPC_CMPLWI(cr, ra, ui)	PPC_D(10, ((cr)<<2), ra, ui)
// cmpw cr, ra, rb  (signed word compare)
#define PPC_CMPW(cr, ra, rb)	PPC_X(31, ((cr)<<2), ra, rb, 0, 0)
// cmplw cr, ra, rb  (unsigned word compare)
#define PPC_CMPLW(cr, ra, rb)	PPC_X(31, ((cr)<<2), ra, rb, 32, 0)
// cmpdi cr, ra, si  (signed doubleword compare immediate, L=1)
#define PPC_CMPDI(cr, ra, si)	PPC_D(11, ((cr)<<2)|1, ra, si)
// cmpd cr, ra, rb  (signed doubleword compare)
#define PPC_CMPD(cr, ra, rb)	PPC_X(31, ((cr)<<2)|1, ra, rb, 0, 0)
// cmpld cr, ra, rb  (unsigned doubleword compare)
#define PPC_CMPLD(cr, ra, rb)	PPC_X(31, ((cr)<<2)|1, ra, rb, 32, 0)

// -- Load (D-form) --
// lwz rt, d(ra)
#define PPC_LWZ(rt, d, ra)		PPC_D(32, rt, ra, d)
// lbz rt, d(ra)
#define PPC_LBZ(rt, d, ra)		PPC_D(34, rt, ra, d)
// lhz rt, d(ra)
#define PPC_LHZ(rt, d, ra)		PPC_D(40, rt, ra, d)
// lha rt, d(ra)  (halfword algebraic / sign-extend)
#define PPC_LHA(rt, d, ra)		PPC_D(42, rt, ra, d)

// -- Load (DS-form, 64-bit) --
// ld rt, ds(ra)
#define PPC_LD(rt, ds, ra)		PPC_DS(58, rt, ra, ds, 0)
// lwa rt, ds(ra) (word algebraic = sign-extend to 64-bit)
#define PPC_LWA(rt, ds, ra)		PPC_DS(58, rt, ra, ds, 2)

// -- Load indexed (X-form) --
// lwzx rt, ra, rb
#define PPC_LWZX(rt, ra, rb)	PPC_X(31, rt, ra, rb, 23, 0)
// lbzx rt, ra, rb
#define PPC_LBZX(rt, ra, rb)	PPC_X(31, rt, ra, rb, 87, 0)
// lhzx rt, ra, rb
#define PPC_LHZX(rt, ra, rb)	PPC_X(31, rt, ra, rb, 279, 0)
// ldx rt, ra, rb
#define PPC_LDX(rt, ra, rb)		PPC_X(31, rt, ra, rb, 21, 0)
// lwax rt, ra, rb (load word algebraic indexed)
#define PPC_LWAX(rt, ra, rb)	PPC_X(31, rt, ra, rb, 341, 0)

// -- Store (D-form) --
// stw rs, d(ra)
#define PPC_STW(rs, d, ra)		PPC_D(36, rs, ra, d)
// stb rs, d(ra)
#define PPC_STB(rs, d, ra)		PPC_D(38, rs, ra, d)
// sth rs, d(ra)
#define PPC_STH(rs, d, ra)		PPC_D(44, rs, ra, d)

// -- Store (DS-form, 64-bit) --
// std rs, ds(ra)
#define PPC_STD(rs, ds, ra)		PPC_DS(62, rs, ra, ds, 0)

// -- Store indexed (X-form) --
// stwx rs, ra, rb
#define PPC_STWX(rs, ra, rb)	PPC_X(31, rs, ra, rb, 151, 0)
// stbx rs, ra, rb
#define PPC_STBX(rs, ra, rb)	PPC_X(31, rs, ra, rb, 215, 0)
// sthx rs, ra, rb
#define PPC_STHX(rs, ra, rb)	PPC_X(31, rs, ra, rb, 407, 0)
// stdx rs, ra, rb
#define PPC_STDX(rs, ra, rb)	PPC_X(31, rs, ra, rb, 149, 0)

// -- Sign extension (X-form) --
// extsb ra, rs (sign-extend byte)
#define PPC_EXTSB(ra, rs)		PPC_X(31, rs, ra, 0, 954, 0)
// extsh ra, rs (sign-extend halfword)
#define PPC_EXTSH(ra, rs)		PPC_X(31, rs, ra, 0, 922, 0)
// extsw ra, rs (sign-extend word to doubleword)
#define PPC_EXTSW(ra, rs)		PPC_X(31, rs, ra, 0, 986, 0)

// -- Branch (I-form) --
// b target  (relative, target is byte offset)
#define PPC_B(off)				( 0x48000000 | ((unsigned)(off) & 0x03FFFFFC) )
// bl target  (relative branch and link)
#define PPC_BL(off)				( 0x48000001 | ((unsigned)(off) & 0x03FFFFFC) )

// -- Branch Conditional (B-form) --
// Condition Register bit encoding for CR0:
//   BI=0: LT, BI=1: GT, BI=2: EQ, BI=3: SO/UN
// Branch hints: BO field
//   BO=12 (0b01100): branch if CR[BI] is set (true)
//   BO=4  (0b00100): branch if CR[BI] is clear (false)
//   BO=20 (0b10100): always (unconditional via bc)
#define BO_TRUE		12
#define BO_FALSE	4
#define BO_ALWAYS	20

#define BI_LT	0	// CR0 LT
#define BI_GT	1	// CR0 GT
#define BI_EQ	2	// CR0 EQ
#define BI_SO	3	// CR0 SO (summary overflow / unordered for FP)

// bc bo, bi, target  (relative, target is byte offset, must be 4-byte aligned)
#define PPC_BC(bo, bi, off) \
	( 0x40000000 | (((unsigned)(bo)&0x1F)<<21) | (((unsigned)(bi)&0x1F)<<16) | \
	  ((unsigned)(off) & 0xFFFC) )

// Convenience: branch if CR0 condition
// beq target
#define PPC_BEQ(off)		PPC_BC(BO_TRUE, BI_EQ, off)
// bne target
#define PPC_BNE(off)		PPC_BC(BO_FALSE, BI_EQ, off)
// blt target
#define PPC_BLT(off)		PPC_BC(BO_TRUE, BI_LT, off)
// bge target (not LT)
#define PPC_BGE(off)		PPC_BC(BO_FALSE, BI_LT, off)
// bgt target
#define PPC_BGT(off)		PPC_BC(BO_TRUE, BI_GT, off)
// ble target (not GT)
#define PPC_BLE(off)		PPC_BC(BO_FALSE, BI_GT, off)
// bso target (summary overflow / unordered)
#define PPC_BSO(off)		PPC_BC(BO_TRUE, BI_SO, off)
// bns target (not SO)
#define PPC_BNS(off)		PPC_BC(BO_FALSE, BI_SO, off)

// -- Branch to LR/CTR (XL-form) --
// blr  (branch to LR)
#define PPC_BLR()			PPC_XL(19, BO_ALWAYS, 0, 16, 0)
// bctr (branch to CTR)
#define PPC_BCTR()			PPC_XL(19, BO_ALWAYS, 0, 528, 0)
// bctrl (branch to CTR and link)
#define PPC_BCTRL()			PPC_XL(19, BO_ALWAYS, 0, 528, 1)
// blrl (branch to LR and link)
#define PPC_BLRL()			PPC_XL(19, BO_ALWAYS, 0, 16, 1)

// -- Special Purpose Registers --
// SPR encoding: PPC encodes SPR as two 5-bit halves swapped
// LR = SPR 8 -> encoded as (8 & 0x1F) | ((8 >> 5) << 5) -> but XFX field is (spr5:9 || spr0:4)
// Actually for mtspr/mfspr, the SPR field is split: bits 16-20 = spr[0:4], bits 11-15 = spr[5:9]
// So SPR 8 (LR) = spr[0:4]=8, spr[5:9]=0 => encoded field = (0 << 5) | 8 = 8
// But in the instruction, the halves are swapped: (spr[5:9] << 5) | spr[0:4]
// For SPR 8:  (0 << 5) | 8 = 8
// For SPR 9 (CTR):  (0 << 5) | 9 = 9
// For SPR 256 (VRSAVE): spr[0:4]=0, spr[5:9]=8 => (8 << 5) | 0 = 256
// The macro must swap the halves:
#define SPR_ENCODE(spr) ( (((spr) & 0x1F) << 5) | (((spr) >> 5) & 0x1F) )

#define SPR_LR		8
#define SPR_CTR		9

// mflr rt
#define PPC_MFLR(rt)		PPC_XFX(31, rt, SPR_ENCODE(SPR_LR), 339)
// mtlr rs
#define PPC_MTLR(rs)		PPC_XFX(31, rs, SPR_ENCODE(SPR_LR), 467)
// mfctr rt
#define PPC_MFCTR(rt)		PPC_XFX(31, rt, SPR_ENCODE(SPR_CTR), 339)
// mtctr rs
#define PPC_MTCTR(rs)		PPC_XFX(31, rs, SPR_ENCODE(SPR_CTR), 467)

// -- Floating-Point Load/Store (D-form) --
// lfs frt, d(ra)  (load float single)
#define PPC_LFS(frt, d, ra)		PPC_D(48, frt, ra, d)
// lfd frt, d(ra)  (load float double)
#define PPC_LFD(frt, d, ra)		PPC_D(50, frt, ra, d)
// stfs frs, d(ra) (store float single)
#define PPC_STFS(frs, d, ra)	PPC_D(52, frs, ra, d)
// stfd frs, d(ra) (store float double)
#define PPC_STFD(frs, d, ra)	PPC_D(54, frs, ra, d)

// -- Floating-Point Load/Store indexed (X-form) --
// lfsx frt, ra, rb
#define PPC_LFSX(frt, ra, rb)	PPC_X(31, frt, ra, rb, 535, 0)
// lfdx frt, ra, rb
#define PPC_LFDX(frt, ra, rb)	PPC_X(31, frt, ra, rb, 599, 0)
// stfsx frs, ra, rb
#define PPC_STFSX(frs, ra, rb)	PPC_X(31, frs, ra, rb, 663, 0)
// stfdx frs, ra, rb
#define PPC_STFDX(frs, ra, rb)	PPC_X(31, frs, ra, rb, 727, 0)

// -- Floating-Point Arithmetic (A-form) --
// A-form: [opcode(6)][FRT(5)][FRA(5)][FRB(5)][FRC(5)][XO(5)][Rc(1)]
#define PPC_A(op, frt, fra, frb, frc, xo, rc) \
	( (((unsigned)(op)&0x3F)<<26) | (((unsigned)(frt)&0x1F)<<21) | \
	  (((unsigned)(fra)&0x1F)<<16) | (((unsigned)(frb)&0x1F)<<11) | \
	  (((unsigned)(frc)&0x1F)<<6) | (((unsigned)(xo)&0x1F)<<1) | \
	  ((unsigned)(rc)&1) )

// fadds frt, fra, frb
#define PPC_FADDS(frt, fra, frb)	PPC_A(59, frt, fra, frb, 0, 21, 0)
// fsubs frt, fra, frb
#define PPC_FSUBS(frt, fra, frb)	PPC_A(59, frt, fra, frb, 0, 20, 0)
// fmuls frt, fra, frc  (note: multiplier is in FRC field, FRB=0)
#define PPC_FMULS(frt, fra, frc)	PPC_A(59, frt, fra, 0, frc, 25, 0)
// fdivs frt, fra, frb
#define PPC_FDIVS(frt, fra, frb)	PPC_A(59, frt, fra, frb, 0, 18, 0)
// fneg frt, frb  (fra=0)
#define PPC_FNEG(frt, frb)			PPC_X(63, frt, 0, frb, 40, 0)
// fmr frt, frb  (fra=0)
#define PPC_FMR(frt, frb)			PPC_X(63, frt, 0, frb, 72, 0)

// fadd (double) frt, fra, frb
#define PPC_FADD(frt, fra, frb)		PPC_A(63, frt, fra, frb, 0, 21, 0)
// fsub (double) frt, fra, frb
#define PPC_FSUB(frt, fra, frb)		PPC_A(63, frt, fra, frb, 0, 20, 0)

// -- Floating-Point Compare (X-form) --
// fcmpu cr, fra, frb
#define PPC_FCMPU(cr, fra, frb)		PPC_X(63, ((cr)<<2), fra, frb, 0, 0)

// -- Floating-Point Conversion --
// fctiwz frt, frb  (convert to integer word, round toward zero)
#define PPC_FCTIWZ(frt, frb)		PPC_X(63, frt, 0, frb, 15, 0)
// fcfids frt, frb  (convert from integer doubleword to single, Power ISA 2.06+)
// This is the modern way to do int-to-float on Power7+
#define PPC_FCFIDS(frt, frb)		PPC_X(59, frt, 0, frb, 846, 0)

// -- Floating-Point round to single (frsp) --
// frsp frt, frb  (round to single precision)
#define PPC_FRSP(frt, frb)			PPC_X(63, frt, 0, frb, 12, 0)

// -- Trap --
// trap  (tw 31, 0, 0)
#define PPC_TRAP()			PPC_X(31, 31, 0, 0, 4, 0)

// -- stwu/stdu for stack frame creation --
// stwu rs, d(ra)
#define PPC_STWU(rs, d, ra)		PPC_D(37, rs, ra, d)
// stdu rs, ds(ra)
#define PPC_STDU(rs, ds, ra)	PPC_DS(62, rs, ra, ds, 1)


// =========================================================================
// Code emission
// =========================================================================

static void emit( uint32_t isn )
{
	if ( code )
	{
		code[ compiledOfs >> 2 ] = isn;
	}
	compiledOfs += 4;
}


static void emitAlign( int align )
{
	while ( compiledOfs & ( align - 1 ) )
		emit( PPC_NOP() );
}


// =========================================================================
// Opstack helpers (simplified, no dynamic register allocation)
// =========================================================================
//
// The operand stack is tracked as an offset from rOPSTACK.
// Values are always stored to/loaded from memory at rOPSTACK + opstack*4.
// This is the simplest approach: every operation loads from opStack memory,
// does the operation, and stores back. No register caching.

static void init_opstack( void )
{
	opstack = 0;
}

static void inc_opstack( void )
{
	opstack += 4;
}

static void dec_opstack( void )
{
	opstack -= 4;
}

// Load the top of opstack into a GPR
// rt = *(rOPSTACK + opstack)
static void load_opstack( int rt )
{
	emit( PPC_LWZ( rt, opstack, rOPSTACK ) );
}

// Store a GPR to the top of opstack
// *(rOPSTACK + opstack) = rs
static void store_opstack( int rs )
{
	emit( PPC_STW( rs, opstack, rOPSTACK ) );
}


// =========================================================================
// 64-bit immediate loading
// =========================================================================
// Load a full 64-bit immediate into a register.
// Uses up to 5 instructions for arbitrary 64-bit values.
// PPC64 sequence: lis + ori + sldi + oris + ori

static void emit_MOVi64( int rt, uint64_t imm )
{
	if ( imm == (uint64_t)(int16_t)imm ) {
		// fits in signed 16-bit
		emit( PPC_LI( rt, imm & 0xFFFF ) );
		return;
	}

	if ( imm == (uint64_t)(int32_t)imm ) {
		// fits in signed 32-bit
		uint16_t hi = (imm >> 16) & 0xFFFF;
		uint16_t lo = imm & 0xFFFF;
		emit( PPC_LIS( rt, hi ) );
		if ( lo )
			emit( PPC_ORI( rt, rt, lo ) );
		return;
	}

	// full 64-bit
	uint16_t w3 = (imm >> 48) & 0xFFFF;
	uint16_t w2 = (imm >> 32) & 0xFFFF;
	uint16_t w1 = (imm >> 16) & 0xFFFF;
	uint16_t w0 = imm & 0xFFFF;

	if ( w3 ) {
		emit( PPC_LIS( rt, w3 ) );
		if ( w2 )
			emit( PPC_ORI( rt, rt, w2 ) );
	} else if ( w2 & 0x8000 ) {
		// w2 has high bit set — can't use PPC_LI (would sign-extend)
		// Use: li rt, 0; ori rt, rt, w2
		emit( PPC_LI( rt, 0 ) );
		emit( PPC_ORI( rt, rt, w2 ) );
	} else {
		emit( PPC_LI( rt, w2 ) );
	}
	// shift left 32 bits
	emit( PPC_SLDI( rt, rt, 32 ) );
	if ( w1 )
		emit( PPC_ORIS( rt, rt, w1 ) );
	if ( w0 )
		emit( PPC_ORI( rt, rt, w0 ) );
}

// Load a 32-bit value into a register (zero-extended to 64-bit for unsigned,
// or sign-extended for signed). For VM values these are 32-bit.
static void emit_MOVi32( int rt, uint32_t imm )
{
	if ( (int32_t)imm >= -32768 && (int32_t)imm <= 32767 ) {
		emit( PPC_LI( rt, imm & 0xFFFF ) );
		return;
	}

	uint16_t hi = (imm >> 16) & 0xFFFF;
	uint16_t lo = imm & 0xFFFF;
	emit( PPC_LIS( rt, hi ) );
	if ( lo )
		emit( PPC_ORI( rt, rt, lo ) );
}


// =========================================================================
// Helper offset calls
// =========================================================================

static void emitFuncOffset( vm_t *vm, offset_t func )
{
	int32_t offset = savedOffset[ func ] - compiledOfs;
	emit( PPC_BL( offset ) );
}


// =========================================================================
// Error handler functions
// =========================================================================

static void VM_Destroy_Compiled( vm_t *vm )
{
	if ( vm->codeBase.ptr )
	{
		if ( munmap( vm->codeBase.ptr, vm->codeLength ) )
			Com_Printf( S_COLOR_RED "%s(): memory unmap failed, possible memory leak!\n", __func__ );
	}
	vm->codeBase.ptr = NULL;
}


static void __attribute__((__noreturn__)) OutJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code outside VM" );
}


static void __attribute__((__noreturn__)) BadJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code at bad location inside VM" );
}


static void __attribute__((__noreturn__)) ErrBadProgramStack( void )
{
	Com_Error( ERR_DROP, "program tried to overflow programStack" );
}


static void __attribute__((__noreturn__)) ErrBadOpStack( void )
{
	Com_Error( ERR_DROP, "program tried to overflow opStack" );
}


static void __attribute__((__noreturn__)) ErrBadDataRead( void )
{
	Com_Error( ERR_DROP, "program tried to read out of data segment" );
}


static void __attribute__((__noreturn__)) ErrBadDataWrite( void )
{
	Com_Error( ERR_DROP, "program tried to write out of data segment" );
}


// =========================================================================
// Runtime check emission
// =========================================================================

// Data access check: either mask the address or check bounds and call error
// reg contains the address to check, it will be masked/checked in place
static void emit_CheckReg( vm_t *vm, int reg, offset_t func )
{
	if ( vm->forceDataMask || !( vm_rtChecks->integer & VM_RTCHECK_DATA ) ) {
		// just mask it
		emit( PPC_AND( reg, reg, rDATAMASK ) );
		return;
	}
	// compare and branch to error if out of range
	emit( PPC_CMPLW( 0, reg, rDATAMASK ) );
	// if reg <= dataMask, skip error (unsigned)
	emit( PPC_BLE( +8 ) );
	emitFuncOffset( vm, func );
}


// Jump target check
static void emit_CheckJump( vm_t *vm, int reg, int proc_base, int proc_len )
{
	if ( ( vm_rtChecks->integer & VM_RTCHECK_JUMP ) == 0 ) {
		return;
	}

	if ( proc_base != -1 ) {
		// allow jump within local function scope only
		if ( (int16_t)proc_base == proc_base ) {
			emit( PPC_ADDI( R11, reg, -proc_base ) ); // r11 = reg - proc_base
		} else {
			emit_MOVi32( R11, proc_base );
			emit( PPC_SUB( R11, reg, R11 ) );
		}
		// check if r11 > proc_len (unsigned, so negative wraps to large)
		emit_MOVi32( R12, proc_len );
		emit( PPC_CMPLW( 0, R11, R12 ) );
		emit( PPC_BLE( +8 ) ); // unsigned <=
		emitFuncOffset( vm, FUNC_OUTJ );
	} else {
		// generic check: reg >= instructionCount
		emit_MOVi32( R11, vm->instructionCount );
		emit( PPC_CMPLW( 0, reg, R11 ) );
		emit( PPC_BLT( +8 ) ); // unsigned <
		emitFuncOffset( vm, FUNC_OUTJ );
	}
}


// Program stack overflow check
static void emit_CheckProc( vm_t *vm, instruction_t *ins )
{
	// programStack overflow check
	if ( vm_rtChecks->integer & VM_RTCHECK_PSTACK ) {
		emit( PPC_CMPW( 0, rPSTACK, rPSTACKBOTTOM ) );
		emit( PPC_BGE( +8 ) ); // signed >=
		emitFuncOffset( vm, FUNC_PSOF );
	}

	// opStack overflow check
	if ( vm_rtChecks->integer & VM_RTCHECK_OPSTACK ) {
		uint32_t n = ins->opStack;
		emit_MOVi32( R11, n );
		emit( PPC_ADD( R11, rOPSTACK, R11 ) );
		emit( PPC_CMPLD( 0, R11, rOPSTACKTOP ) );
		emit( PPC_BLE( +8 ) ); // unsigned <=
		emitFuncOffset( vm, FUNC_OSOF );
	}
}


// =========================================================================
// Call dispatch helper emission
// =========================================================================

static void emitCallFunc( vm_t *vm )
{
	int i;

	init_opstack();

savedOffset[ FUNC_CALL ] = compiledOfs;

	// R3 holds the call number (loaded by OP_CALL)
	// if callnum < 0 => syscall
	emit( PPC_CMPWI( 0, R3, 0 ) );
	emit( PPC_BLT( savedOffset[ FUNC_SYSC ] - compiledOfs ) );

	// check if R3 >= instructionCount
	emit_CheckJump( vm, R3, -1, 0 );

	// local function call: branch to instructionPointers[R3]
	// R11 = R3 << 3 (intptr_t is 8 bytes on ppc64)
	emit( PPC_RLWINM( R11, R3, 3, 0, 28 ) );  // R11 = R3 << 3, clearing upper bits
	emit( PPC_LDX( R11, rINSPOINTERS, R11 ) ); // R11 = instructionPointers[R3]
	emit( PPC_MTCTR( R11 ) );
	emit( PPC_BCTR() );

savedOffset[ FUNC_SYSC ] = compiledOfs;

	// syscall: negate callnum
	// R3 = ~R3 (not R3, R3)
	emit( PPC_NOT( R3, R3 ) );

savedOffset[ FUNC_SYSF ] = compiledOfs;

	// ELFv2 ABI stack frame layout:
	//   SP+0:    back chain (8 bytes, set by stdu)
	//   SP+8:    CR save area (8 bytes)
	//   SP+16:   LR save area (8 bytes, callee saves our LR here)
	//   SP+24:   TOC save area (8 bytes, reserved)
	//   SP+32:   args[0..15] (16 * 8 = 128 bytes) -- parameter/local area
	//   SP+160:  our saved LR (8 bytes)
	//   SP+168:  our saved rOPSTACK (8 bytes)
	//   Total:   176 bytes (16-byte aligned)
	#define SYSC_FRAME 176
	#define SYSC_ARGS  32      // offset to args array within frame
	#define SYSC_LR    160     // offset to our saved LR
	#define SYSC_OPST  168     // offset to our saved rOPSTACK

	// Allocate stack space
	emit( PPC_STDU( R1, -SYSC_FRAME, R1 ) );

	// Save LR (will be clobbered by bctrl)
	emit( PPC_MFLR( R0 ) );
	emit( PPC_STD( R0, SYSC_LR, R1 ) );

	// Save opStack on stack too
	emit( PPC_STD( rOPSTACK, SYSC_OPST, R1 ) );

	// modify VM stack pointer for recursive VM entry
	// vm->programStack = pstack - 8
	emit( PPC_ADDI( R4, rPSTACK, -8 ) );
	emit( PPC_STW( R4, offsetof(vm_t, programStack), rVMBASE ) );

	// Sign-extend 32-bit args from procBase to intptr_t and store in args array
	// args[0] = R3 (syscall number, already set)
	// args[1..15] from procBase+8, procBase+12, ..., procBase+68
	emit( PPC_EXTSW( R3, R3 ) );  // sign-extend syscall number
	emit( PPC_STD( R3, SYSC_ARGS + 0, R1 ) );

	for ( i = 1; i < 16; i++ ) {
		emit( PPC_LWA( R0, 4 + i * 4, rPROCBASE ) );
		emit( PPC_STD( R0, SYSC_ARGS + i * 8, R1 ) );
	}

	// R3 = pointer to args array
	emit( PPC_ADDI( R3, R1, SYSC_ARGS ) );

	// Load vm->systemCall into R12 and call it
	// ELFv2 ABI requires R12 = target address before bctrl
	emit( PPC_LD( R12, offsetof(vm_t, systemCall), rVMBASE ) );
	emit( PPC_MTCTR( R12 ) );
	emit( PPC_BCTRL() );

	// Return value is in R3, store it to opstack+4
	emit( PPC_STW( R3, 4, rOPSTACK ) );

	// Restore opStack
	emit( PPC_LD( rOPSTACK, SYSC_OPST, R1 ) );

	// Restore LR
	emit( PPC_LD( R0, SYSC_LR, R1 ) );
	emit( PPC_MTLR( R0 ) );

	// Destroy stack frame
	emit( PPC_ADDI( R1, R1, SYSC_FRAME ) );

	emit( PPC_BLR() );
}


// =========================================================================
// Block copy helper
// =========================================================================
// On entry: R3 = src offset, R4 = dst offset, R5 = count

static void emitBlockCopyFunc( vm_t *vm )
{
	// Mask src and dst
	emit( PPC_AND( R3, R3, rDATAMASK ) );
	emit( PPC_AND( R4, R4, rDATAMASK ) );

	// Clamp count: src + count must not exceed dataMask+1
	emit( PPC_ADD( R6, R3, R5 ) );         // R6 = src + count
	emit( PPC_AND( R6, R6, rDATAMASK ) );  // R6 &= dataMask
	emit( PPC_SUB( R5, R6, R3 ) );         // count = R6 - src

	emit( PPC_ADD( R6, R4, R5 ) );         // R6 = dst + count
	emit( PPC_AND( R6, R6, rDATAMASK ) );  // R6 &= dataMask
	emit( PPC_SUB( R5, R6, R4 ) );         // count = R6 - dst

	// Convert offsets to pointers
	emit( PPC_ADD( R3, R3, rDATABASE ) );  // src = src + dataBase
	emit( PPC_ADD( R4, R4, rDATABASE ) );  // dst = dst + dataBase

	// Byte copy loop
	// while (count > 0) { *dst++ = *src++; count--; }
	emit( PPC_CMPWI( 0, R5, 0 ) );
	emit( PPC_BLE( +24 ) );  // skip if count <= 0

	// loop:
	emit( PPC_LBZ( R6, 0, R3 ) );         // R6 = *src
	emit( PPC_STB( R6, 0, R4 ) );         // *dst = R6
	emit( PPC_ADDI( R3, R3, 1 ) );        // src++
	emit( PPC_ADDI( R4, R4, 1 ) );        // dst++
	emit( PPC_ADDI( R5, R5, -1 ) );       // count--
	emit( PPC_CMPWI( 0, R5, 0 ) );
	emit( PPC_BGT( -24 ) );               // loop if count > 0

	emit( PPC_BLR() );
}


// =========================================================================
// Condition code mapping for integer comparisons
// =========================================================================

// For OP_EQ..OP_GEU, map opcode to the PPC branch instruction to take
// when the condition is TRUE.
// After cmpw/cmplw, CR0 bits are: LT, GT, EQ, SO
//
// PPC bc (B-form) has only a 14-bit BD field => +-32KB range.
// Since the two-pass compilation model requires both passes to produce
// identical instruction counts, and forward branch targets are unknown
// during the sizing pass, we always use the long-branch pattern:
//   bc <inverted_cond>, +8   ; skip over the 'b' if condition is FALSE
//   b  target                ; unconditional branch to real target (+-128MB)
// This costs one extra instruction per conditional branch but is safe
// for any VM size.

// Map an opcode to (BO, BI) for the TRUE condition
static void get_branch_cond( int op, int *bo, int *bi )
{
	switch ( op ) {
		case OP_EQ:  case OP_EQF: *bo = BO_TRUE;  *bi = BI_EQ; break;
		case OP_NE:  case OP_NEF: *bo = BO_FALSE; *bi = BI_EQ; break;
		case OP_LTI: case OP_LTU: case OP_LTF: *bo = BO_TRUE;  *bi = BI_LT; break;
		case OP_LEI: case OP_LEU: case OP_LEF: *bo = BO_FALSE; *bi = BI_GT; break;
		case OP_GTI: case OP_GTU: case OP_GTF: *bo = BO_TRUE;  *bi = BI_GT; break;
		case OP_GEI: case OP_GEU: case OP_GEF: *bo = BO_FALSE; *bi = BI_LT; break;
		default:     *bo = -1; *bi = -1; break;
	}
}

static void emit_branchConditional( vm_t *vm, instruction_t *ci, int op )
{
	int32_t target = ci->value;  // target instruction index
	int32_t targetOfs = vm->instructionPointers[ target ] - compiledOfs;
	int bo, bi;

	get_branch_cond( op, &bo, &bi );
	if ( bo == -1 ) {
		emit( PPC_TRAP() );
		return;
	}

	// Long form: inverted bc skips over unconditional b
	// Invert: BO_TRUE(12) <-> BO_FALSE(4)
	{
		int inv_bo = ( bo == BO_TRUE ) ? BO_FALSE : BO_TRUE;
		emit( PPC_BC( inv_bo, bi, +8 ) );  // skip next instruction if NOT condition
		emit( PPC_B( targetOfs - 4 ) );    // -4 because compiledOfs advanced by 4
	}
}


// =========================================================================
// VM_Compile
// =========================================================================

qboolean VM_Compile( vm_t *vm, vmHeader_t *header )
{
	const char *errMsg;
	instruction_t *ci;
	int proc_base;
	int proc_len;
	int proc_end;
	int i;

	inst = (instruction_t*)Z_Malloc( (header->instructionCount + 8) * sizeof( instruction_t ) );

	errMsg = VM_LoadInstructions( (byte *)header + header->codeOffset, header->codeLength, header->instructionCount, inst );
	if ( !errMsg ) {
		errMsg = VM_CheckInstructions( inst, vm->instructionCount, vm->jumpTableTargets, vm->numJumpTableTargets, vm->exactDataLength );
	}

	if ( errMsg ) {
		VM_FreeBuffers();
		Com_Printf( S_COLOR_YELLOW "%s(%s) error: %s\n", __func__, vm->name, errMsg );
		return qfalse;
	}

	if ( !vm->instructionPointers ) {
		vm->instructionPointers = Hunk_Alloc( header->instructionCount * sizeof(vm->instructionPointers[0]), h_high );
	}

	VM_ReplaceInstructions( vm, inst );

	memset( savedOffset, 0, sizeof( savedOffset ) );

	code = NULL;
	vm->codeBase.ptr = NULL;

	for ( pass = 0; pass < NUM_PASSES; pass++ ) {
__recompile:

	// translate all instructions
	ip = 0;
	compiledOfs = 0;

	proc_base = -1;
	proc_len = 0;
	proc_end = 0;

	init_opstack();

	// ===== PROLOGUE =====
	// ELFv2 ABI: create stack frame, save callee-saved registers and LR
	// We need to save: LR, R14-R22 (9 GPRs), F14 (1 FPR for scratch save area)
	// Stack frame: 16-byte aligned
	// Layout (from SP):
	//   SP+0:    back chain (8 bytes)
	//   SP+8:    CR save (8 bytes)
	//   SP+16:   LR save (stored by us, not in standard position but fine since we restore it)
	//   SP+24:   R14
	//   SP+32:   R15
	//   SP+40:   R16
	//   SP+48:   R17
	//   SP+56:   R18
	//   SP+64:   R19
	//   SP+72:   R20
	//   SP+80:   R21
	//   SP+88:   R22
	//   SP+96:   F14 (8 bytes, for int<->float conversion scratch)
	//   Total: 104, rounded up to 112 (16-byte aligned)
	// Actually, ELFv2 ABI: LR is saved by callee at caller's SP+16
	// But since we control everything, we'll use a simpler layout.

	#define FRAME_SIZE 160  // generous, 16-byte aligned

	// Create stack frame
	emit( PPC_STDU( R1, -FRAME_SIZE, R1 ) );

	// Save LR
	emit( PPC_MFLR( R0 ) );
	emit( PPC_STD( R0, FRAME_SIZE + 16, R1 ) );  // save LR in caller's frame (standard ELFv2 position)

	// Save callee-saved GPRs
	emit( PPC_STD( R14, 48, R1 ) );
	emit( PPC_STD( R15, 56, R1 ) );
	emit( PPC_STD( R16, 64, R1 ) );
	emit( PPC_STD( R17, 72, R1 ) );
	emit( PPC_STD( R18, 80, R1 ) );
	emit( PPC_STD( R19, 88, R1 ) );
	emit( PPC_STD( R20, 96, R1 ) );
	emit( PPC_STD( R21, 104, R1 ) );
	emit( PPC_STD( R22, 112, R1 ) );

	// Load VM state into dedicated registers
	emit_MOVi64( rVMBASE, (intptr_t)vm );
	emit_MOVi64( rINSPOINTERS, (intptr_t)vm->instructionPointers );
	emit_MOVi64( rDATABASE, (intptr_t)vm->dataBase );

	emit_MOVi32( rDATAMASK, vm->dataMask );
	emit_MOVi32( rPSTACKBOTTOM, vm->stackBottom );

	// Load volatile VM state from vm_t struct
	emit( PPC_LD( rOPSTACK, offsetof(vm_t, opStack), rVMBASE ) );
	emit( PPC_LD( rOPSTACKTOP, offsetof(vm_t, opStackTop), rVMBASE ) );
	emit( PPC_LWZ( rPSTACK, offsetof(vm_t, programStack), rVMBASE ) );

	// Branch to FUNC_ENTR (vmMain entry point)
	emitFuncOffset( vm, FUNC_ENTR );

	// ===== EPILOGUE =====
#ifdef DEBUG_VM
	// Store programStack back for debugging
	emit( PPC_STW( rPSTACK, offsetof(vm_t, programStack), rVMBASE ) );
#endif

	// Restore callee-saved GPRs
	emit( PPC_LD( R14, 48, R1 ) );
	emit( PPC_LD( R15, 56, R1 ) );
	emit( PPC_LD( R16, 64, R1 ) );
	emit( PPC_LD( R17, 72, R1 ) );
	emit( PPC_LD( R18, 80, R1 ) );
	emit( PPC_LD( R19, 88, R1 ) );
	emit( PPC_LD( R20, 96, R1 ) );
	emit( PPC_LD( R21, 104, R1 ) );
	emit( PPC_LD( R22, 112, R1 ) );

	// Restore LR
	emit( PPC_LD( R0, FRAME_SIZE + 16, R1 ) );
	emit( PPC_MTLR( R0 ) );

	// Destroy stack frame
	emit( PPC_ADDI( R1, R1, FRAME_SIZE ) );

	// Return
	emit( PPC_BLR() );

#ifdef FUNC_ALIGN
	emitAlign( FUNC_ALIGN );
#endif

	savedOffset[ FUNC_ENTR ] = compiledOfs; // offset to vmMain() entry point

	// ===== MAIN COMPILATION LOOP =====
	while ( ip < header->instructionCount ) {

		ci = &inst[ ip ];

		vm->instructionPointers[ ip ] = compiledOfs;
		ip++;

		switch ( ci->op )
		{
			case OP_UNDEF:
				emit( PPC_TRAP() );
				break;

			case OP_IGNORE:
				break;

			case OP_BREAK:
				emit( PPC_TRAP() );
				break;

			case OP_ENTER:
#ifdef FUNC_ALIGN
				emitAlign( FUNC_ALIGN );
#endif
				vm->instructionPointers[ ip - 1 ] = compiledOfs;

				proc_base = ip; // next instruction after OP_ENTER
				// locate endproc
				for ( proc_len = -1, i = ip; i < header->instructionCount; i++ ) {
					if ( inst[ i ].op == OP_PUSH && inst[ i + 1 ].op == OP_LEAVE ) {
						proc_len = i - proc_base;
						proc_end = i + 1;
						break;
					}
				}

				if ( proc_len == 0 ) {
					// empty function, just return
					emit( PPC_BLR() );
					ip += 2; // skip OP_PUSH + OP_LEAVE
					break;
				}

				// Save LR, opStack, pStack, procBase on native stack
				emit( PPC_MFLR( R0 ) );
				emit( PPC_STDU( R1, -64, R1 ) );	// create local frame
				emit( PPC_STD( R0, 64 + 16, R1 ) );	// save LR in caller's frame
				emit( PPC_STD( rOPSTACK, 32, R1 ) );
				emit( PPC_STW( rPSTACK, 40, R1 ) );
				emit( PPC_STD( rPROCBASE, 48, R1 ) );

				// Subtract frame size from programStack
				if ( (int16_t)ci->value == ci->value ) {
					emit( PPC_ADDI( rPSTACK, rPSTACK, -ci->value ) );
				} else {
					emit_MOVi32( R11, ci->value );
					emit( PPC_SUB( rPSTACK, rPSTACK, R11 ) );
				}

				emit_CheckProc( vm, ci );

				// procBase = dataBase + programStack
				emit( PPC_EXTSW( R11, rPSTACK ) );           // sign-extend pStack to 64-bit
				emit( PPC_ADD( rPROCBASE, R11, rDATABASE ) ); // procBase = pStack + dataBase
				break;

			case OP_LEAVE:
				dec_opstack(); // opstack -= 4
#ifdef DEBUG_VM
				if ( opstack != 0 ) {
					Com_Error( ERR_DROP, "VM: opStack corrupted on OP_LEAVE" );
				}
#endif
				if ( !ci->endp && proc_base >= 0 ) {
					// jump to last OP_LEAVE in this function
					if ( inst[ ip ].op == OP_PUSH && inst[ ip + 1 ].op == OP_LEAVE ) {
						// next instruction is proc_end, fall through
					} else {
						emit( PPC_B( vm->instructionPointers[ proc_end ] - compiledOfs ) );
					}
					break;
				}

				// Restore saved state
				emit( PPC_LD( rPROCBASE, 48, R1 ) );
				emit( PPC_LWZ( rPSTACK, 40, R1 ) );
				emit( PPC_LD( rOPSTACK, 32, R1 ) );
				emit( PPC_LD( R0, 64 + 16, R1 ) );  // restore LR
				emit( PPC_MTLR( R0 ) );
				emit( PPC_ADDI( R1, R1, 64 ) );     // destroy local frame
				emit( PPC_BLR() );
				break;

			case OP_CALL:
				// Load callnum from opstack
				load_opstack( R3 );  // R3 = *(rOPSTACK + opstack)
				// Adjust rOPSTACK so the helper stores return value at the right place.
				// The helper stores the return value at rOPSTACK+4 (for syscalls).
				// We want that to land at rOPSTACK + opstack (replacing the call number).
				// So we adjust rOPSTACK by (opstack - 4) before calling, and restore after.
				if ( opstack != 4 ) {
					emit( PPC_ADDI( rOPSTACK, rOPSTACK, opstack - 4 ) );
					emitFuncOffset( vm, FUNC_CALL );
					emit( PPC_ADDI( rOPSTACK, rOPSTACK, -(opstack - 4) ) );
				} else {
					emitFuncOffset( vm, FUNC_CALL );
				}
				// OP_CALL pops the call number but the return value takes its place,
				// so the opstack level remains unchanged (same as aarch64).
				break;

			case OP_PUSH:
				inc_opstack();
				if ( (ci + 1)->op == OP_LEAVE ) {
					proc_base = -1;
				}
				break;

			case OP_POP:
				dec_opstack();
				break;

			case OP_CONST:
				inc_opstack();
				// Store constant to opstack
				emit_MOVi32( R3, ci->value );
				store_opstack( R3 );
				break;

			case OP_LOCAL:
				inc_opstack();
				// local address = programStack + value
				if ( (int16_t)ci->value == ci->value ) {
					emit( PPC_ADDI( R3, rPSTACK, ci->value ) );
				} else {
					emit_MOVi32( R3, ci->value );
					emit( PPC_ADD( R3, rPSTACK, R3 ) );
				}
				store_opstack( R3 );
				break;

			case OP_JUMP:
				// indirect jump: target = *opstack
				load_opstack( R3 );
				dec_opstack();
				emit_CheckJump( vm, R3, proc_base, proc_len );
				// Load target address from instructionPointers[R3]
				// R11 = R3 << 3
				emit( PPC_RLWINM( R11, R3, 3, 0, 28 ) ); // shift left 3, mask to 32-bit
				emit( PPC_LDX( R11, rINSPOINTERS, R11 ) );
				emit( PPC_MTCTR( R11 ) );
				emit( PPC_BCTR() );
				break;

			// ---- Integer comparisons ----
			case OP_EQ:
			case OP_NE:
			case OP_LTI:
			case OP_LEI:
			case OP_GTI:
			case OP_GEI:
				// pop two, compare (signed), branch
				load_opstack( R4 );        // R4 = top
				dec_opstack();
				load_opstack( R3 );        // R3 = second
				dec_opstack();
				emit( PPC_CMPW( 0, R3, R4 ) );
				emit_branchConditional( vm, ci, ci->op );
				break;

			case OP_LTU:
			case OP_LEU:
			case OP_GTU:
			case OP_GEU:
				// pop two, compare (unsigned), branch
				load_opstack( R4 );        // R4 = top
				dec_opstack();
				load_opstack( R3 );        // R3 = second
				dec_opstack();
				emit( PPC_CMPLW( 0, R3, R4 ) );
				emit_branchConditional( vm, ci, ci->op );
				break;

			// ---- Float comparisons ----
			case OP_EQF:
			case OP_NEF:
			case OP_LTF:
			case OP_LEF:
			case OP_GTF:
			case OP_GEF:
				// pop two floats from opstack, compare, branch
				// Load as 32-bit words into FPRs via memory
				// We use lfs which loads a single-precision float and converts to double in the FPR
				// opstack values are stored as 32-bit IEEE 754 floats
				emit( PPC_LFS( F1, opstack, rOPSTACK ) );  // F1 = top (float)
				dec_opstack();
				emit( PPC_LFS( F0, opstack, rOPSTACK ) );  // F0 = second (float)
				dec_opstack();
				emit( PPC_FCMPU( 0, F0, F1 ) );
				emit_branchConditional( vm, ci, ci->op );
				break;

			// ---- Load operations ----
			case OP_LOAD1:
				load_opstack( R3 );  // R3 = address (from opstack top)
				emit_CheckReg( vm, R3, FUNC_BADR );
				emit( PPC_LBZX( R3, rDATABASE, R3 ) );  // R3 = dataBase[R3] (byte)
				store_opstack( R3 );
				break;

			case OP_LOAD2:
				load_opstack( R3 );
				emit_CheckReg( vm, R3, FUNC_BADR );
				emit( PPC_LHZX( R3, rDATABASE, R3 ) );  // R3 = dataBase[R3] (halfword)
				store_opstack( R3 );
				break;

			case OP_LOAD4:
				load_opstack( R3 );
				emit_CheckReg( vm, R3, FUNC_BADR );
				emit( PPC_LWZX( R3, rDATABASE, R3 ) );  // R3 = dataBase[R3] (word)
				store_opstack( R3 );
				break;

			// ---- Store operations ----
			case OP_STORE1:
				// *(stack[top-1]) = stack[top] (byte)
				load_opstack( R4 );        // R4 = value (top)
				dec_opstack();
				load_opstack( R3 );        // R3 = address (second)
				dec_opstack();
				if ( !ci->safe )
					emit_CheckReg( vm, R3, FUNC_BADW );
				emit( PPC_STBX( R4, rDATABASE, R3 ) );
				break;

			case OP_STORE2:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				dec_opstack();
				if ( !ci->safe )
					emit_CheckReg( vm, R3, FUNC_BADW );
				emit( PPC_STHX( R4, rDATABASE, R3 ) );
				break;

			case OP_STORE4:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				dec_opstack();
				if ( !ci->safe )
					emit_CheckReg( vm, R3, FUNC_BADW );
				emit( PPC_STWX( R4, rDATABASE, R3 ) );
				break;

			case OP_ARG:
				// Store opstack top to procBase + ci->value
				load_opstack( R3 );
				dec_opstack();
				emit( PPC_STW( R3, ci->value, rPROCBASE ) );
				break;

			case OP_BLOCK_COPY:
				// R3 = src (second), R4 = dst (top), R5 = count (immediate)
				// Actually: *(second) <- *(top), count = ci->value
				// Wait — let me check the semantics:
				// OP_BLOCK_COPY: src = stack[top], dst = stack[top-1], count = ci->value
				// Actually from vm_interpreted2.c: src = opStack[opStackOfs], dst = opStack[opStackOfs-1]
				// i.e., top = src addr, second = dst addr
				load_opstack( R3 );        // R3 = src (top)
				dec_opstack();
				load_opstack( R4 );        // R4 = dst (second)
				dec_opstack();
				emit_MOVi32( R5, ci->value );  // R5 = count
				emitFuncOffset( vm, FUNC_BCPY );
				break;

			// ---- Sign extension ----
			case OP_SEX8:
				load_opstack( R3 );
				emit( PPC_EXTSB( R3, R3 ) );
				store_opstack( R3 );
				break;

			case OP_SEX16:
				load_opstack( R3 );
				emit( PPC_EXTSH( R3, R3 ) );
				store_opstack( R3 );
				break;

			// ---- Integer arithmetic ----
			case OP_NEGI:
				load_opstack( R3 );
				emit( PPC_NEG( R3, R3 ) );
				store_opstack( R3 );
				break;

			case OP_ADD:
				load_opstack( R4 );    // R4 = top
				dec_opstack();
				load_opstack( R3 );    // R3 = second
				emit( PPC_ADD( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_SUB:
				load_opstack( R4 );    // R4 = top
				dec_opstack();
				load_opstack( R3 );    // R3 = second
				emit( PPC_SUB( R3, R3, R4 ) );  // R3 = second - top
				store_opstack( R3 );
				break;

			case OP_MULI:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_MULLW( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_MULU:
				// unsigned multiply - same instruction for low word
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_MULLW( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_DIVI:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_DIVW( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_DIVU:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_DIVWU( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_MODI:
				// mod = a - (a/b)*b
				load_opstack( R4 );        // R4 = b (top)
				dec_opstack();
				load_opstack( R3 );        // R3 = a (second)
				emit( PPC_DIVW( R5, R3, R4 ) );   // R5 = a / b (signed)
				emit( PPC_MULLW( R5, R5, R4 ) );  // R5 = (a/b) * b
				emit( PPC_SUB( R3, R3, R5 ) );    // R3 = a - (a/b)*b
				store_opstack( R3 );
				break;

			case OP_MODU:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_DIVWU( R5, R3, R4 ) );
				emit( PPC_MULLW( R5, R5, R4 ) );
				emit( PPC_SUB( R3, R3, R5 ) );
				store_opstack( R3 );
				break;

			// ---- Bitwise operations ----
			case OP_BAND:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_AND( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_BOR:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_OR( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_BXOR:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_XOR( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_BCOM:
				load_opstack( R3 );
				emit( PPC_NOT( R3, R3 ) );
				store_opstack( R3 );
				break;

			// ---- Shifts ----
			case OP_LSH:
				load_opstack( R4 );    // shift amount
				dec_opstack();
				load_opstack( R3 );    // value
				emit( PPC_SLW( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_RSHI:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_SRAW( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			case OP_RSHU:
				load_opstack( R4 );
				dec_opstack();
				load_opstack( R3 );
				emit( PPC_SRW( R3, R3, R4 ) );
				store_opstack( R3 );
				break;

			// ---- Float arithmetic ----
			case OP_NEGF:
				emit( PPC_LFS( F0, opstack, rOPSTACK ) );
				emit( PPC_FNEG( F0, F0 ) );
				emit( PPC_STFS( F0, opstack, rOPSTACK ) );
				break;

			case OP_ADDF:
				emit( PPC_LFS( F1, opstack, rOPSTACK ) );  // F1 = top
				dec_opstack();
				emit( PPC_LFS( F0, opstack, rOPSTACK ) );  // F0 = second
				emit( PPC_FADDS( F0, F0, F1 ) );
				emit( PPC_STFS( F0, opstack, rOPSTACK ) );
				break;

			case OP_SUBF:
				emit( PPC_LFS( F1, opstack, rOPSTACK ) );
				dec_opstack();
				emit( PPC_LFS( F0, opstack, rOPSTACK ) );
				emit( PPC_FSUBS( F0, F0, F1 ) );  // second - top
				emit( PPC_STFS( F0, opstack, rOPSTACK ) );
				break;

			case OP_MULF:
				emit( PPC_LFS( F1, opstack, rOPSTACK ) );
				dec_opstack();
				emit( PPC_LFS( F0, opstack, rOPSTACK ) );
				emit( PPC_FMULS( F0, F0, F1 ) );
				emit( PPC_STFS( F0, opstack, rOPSTACK ) );
				break;

			case OP_DIVF:
				emit( PPC_LFS( F1, opstack, rOPSTACK ) );
				dec_opstack();
				emit( PPC_LFS( F0, opstack, rOPSTACK ) );
				emit( PPC_FDIVS( F0, F0, F1 ) );
				emit( PPC_STFS( F0, opstack, rOPSTACK ) );
				break;

			// ---- Type conversion ----
			case OP_CVIF:
				// Convert integer to float
				// Load 32-bit int from opstack, convert to float, store back
				// Method: use the stack as scratch space
				// 1. Load int32 from opstack
				// 2. Sign-extend to 64-bit
				// 3. Store as int64 to scratch, load as double (fcfid), round to single (frsp), store
				// On Power ISA 2.06+ (Power7+), we can use fcfids (convert from int64 to single)
				//
				// scratch area: use R1+120 (within our frame)
				load_opstack( R3 );                     // R3 = int32 value
				emit( PPC_EXTSW( R3, R3 ) );           // sign-extend to 64-bit
				emit( PPC_STD( R3, 120, R1 ) );        // store int64 to stack scratch
				emit( PPC_LFD( F0, 120, R1 ) );       // load as double (reinterpret int64 bits)
				emit( PPC_FCFIDS( F0, F0 ) );          // convert int64 -> float single
				emit( PPC_STFS( F0, opstack, rOPSTACK ) ); // store float to opstack
				break;

			case OP_CVFI:
				// Convert float to integer (truncate toward zero)
				// 1. Load float from opstack
				// 2. fctiwz -> convert to int32 in FPR (stored in low 32 bits of doubleword)
				// 3. stfd to scratch, load low 32 bits
				emit( PPC_LFS( F0, opstack, rOPSTACK ) );   // load float
				emit( PPC_FCTIWZ( F0, F0 ) );               // convert to int32 (in FPR)
				emit( PPC_STFD( F0, 120, R1 ) );            // store doubleword to scratch
				// On little-endian, the int32 result is in the first 4 bytes (offset 0)
				// On big-endian, it would be at offset 4
				// For ppc64le: the integer result is at the low address
				emit( PPC_LWZ( R3, 120, R1 ) );             // load int32 from scratch (LE: offset 0)
				store_opstack( R3 );
				break;

			default:
				Com_Error( ERR_DROP, "VM: bad opcode %02x at instruction %i", ci->op, ip - 1 );
				break;

		} // switch op
	} // while ip

#ifdef FUNC_ALIGN
	emitAlign( FUNC_ALIGN );
#endif

	// Emit helper functions
	emitCallFunc( vm );

#ifdef FUNC_ALIGN
	emitAlign( FUNC_ALIGN );
#endif

	savedOffset[ FUNC_BCPY ] = compiledOfs;
	emitBlockCopyFunc( vm );

	savedOffset[ FUNC_BADJ ] = compiledOfs;
	emit_MOVi64( R12, (intptr_t)BadJump );
	emit( PPC_MTCTR( R12 ) );
	emit( PPC_BCTRL() );

	savedOffset[ FUNC_OUTJ ] = compiledOfs;
	emit_MOVi64( R12, (intptr_t)OutJump );
	emit( PPC_MTCTR( R12 ) );
	emit( PPC_BCTRL() );

	savedOffset[ FUNC_OSOF ] = compiledOfs;
	emit_MOVi64( R12, (intptr_t)ErrBadOpStack );
	emit( PPC_MTCTR( R12 ) );
	emit( PPC_BCTRL() );

	savedOffset[ FUNC_PSOF ] = compiledOfs;
	emit_MOVi64( R12, (intptr_t)ErrBadProgramStack );
	emit( PPC_MTCTR( R12 ) );
	emit( PPC_BCTRL() );

	savedOffset[ FUNC_BADR ] = compiledOfs;
	emit_MOVi64( R12, (intptr_t)ErrBadDataRead );
	emit( PPC_MTCTR( R12 ) );
	emit( PPC_BCTRL() );

	savedOffset[ FUNC_BADW ] = compiledOfs;
	emit_MOVi64( R12, (intptr_t)ErrBadDataWrite );
	emit( PPC_MTCTR( R12 ) );
	emit( PPC_BCTRL() );

	} // pass

	if ( vm->codeBase.ptr == NULL ) {
		uint32_t allocSize = compiledOfs;

		vm->codeBase.ptr = mmap( NULL, allocSize, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
		if ( vm->codeBase.ptr == MAP_FAILED ) {
			VM_FreeBuffers();
			Com_Printf( S_COLOR_YELLOW "%s(%s): mmap failed\n", __func__, vm->name );
			return qfalse;
		}

		vm->codeLength = allocSize;
		vm->codeSize = compiledOfs;
		code = (uint32_t*)vm->codeBase.ptr;
		goto __recompile;
	}

	// Offset all instruction pointers for the final code location
	for ( i = 0; i < header->instructionCount; i++ ) {
		if ( !inst[i].jused ) {
			vm->instructionPointers[ i ] = (intptr_t)BadJump;
			continue;
		}
		vm->instructionPointers[ i ] += (intptr_t)vm->codeBase.ptr;
	}

	VM_FreeBuffers();

	if ( mprotect( vm->codeBase.ptr, vm->codeLength, PROT_READ | PROT_EXEC ) ) {
		VM_Destroy_Compiled( vm );
		Com_Printf( S_COLOR_YELLOW "%s(%s): mprotect failed\n", __func__, vm->name );
		return qfalse;
	}

	// Synchronize instruction cache
	__builtin___clear_cache( vm->codeBase.ptr, vm->codeBase.ptr + vm->codeLength );

	vm->destroy = VM_Destroy_Compiled;

	Com_Printf( "VM file %s compiled to %i bytes of code (dataMask=0x%x stackBottom=%d programStack=%d)\n",
		vm->name, vm->codeLength, vm->dataMask, vm->stackBottom, vm->programStack );

	return qtrue;
}


// =========================================================================
// VM_CallCompiled
// =========================================================================

int32_t VM_CallCompiled( vm_t *vm, int nargs, int32_t *args )
{
	int32_t		opStack[ MAX_OPSTACK_SIZE ];
	int32_t		stackOnEntry;
	int32_t		*image;
	int			i;

	// we might be called recursively, so this might not be the very top
	stackOnEntry = vm->programStack;

	vm->programStack -= ( MAX_VMMAIN_CALL_ARGS + 2 ) * sizeof( int32_t );

	// set up the stack frame
	image = (int32_t*)( vm->dataBase + vm->programStack );
	for ( i = 0; i < nargs; i++ ) {
		image[ i + 2 ] = args[ i ];
	}

#ifdef DEBUG_VM
	opStack[0] = 0xDEADC0DE;
#endif
	opStack[1] = 0;

	vm->opStack = opStack;
	vm->opStackTop = opStack + ARRAY_LEN( opStack ) - 1;

	vm->codeBase.func(); // go into generated code

#ifdef DEBUG_VM
	if ( opStack[0] != 0xDEADC0DE ) {
		Com_Error( ERR_DROP, "%s(%s): opStack corrupted in compiled code", __func__, vm->name );
	}

	if ( vm->programStack != (int32_t)( stackOnEntry - ( MAX_VMMAIN_CALL_ARGS + 2 ) * sizeof( int32_t ) ) ) {
		Com_Error( ERR_DROP, "%s(%s): programStack corrupted in compiled code", __func__, vm->name );
	}
#endif

	vm->programStack = stackOnEntry;

	return opStack[1];
}
