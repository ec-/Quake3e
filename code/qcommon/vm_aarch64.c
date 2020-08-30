/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

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

// load time compiler and execution environment for ARM aarch64

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

#define CONST_OPTIMIZE
#define REGS_OPTIMIZE
#define S0_OPTIMIZE
#define MISC_OPTIMIZE
#define MACRO_OPTIMIZE

// registers map

#define R0	0 // scratch
#define R1	1 // scratch
#define R2	2 // scratch
#define R3	3 // scratch
#define R4	4 // scratch
#define R5	5 // scratch
#define R6	6 // scratch
#define R7	7 // scratch
#define R8	8 // indirect return value
#define R9	9  // caller-saved
#define R10	10 // caller-saved
#define R11	11 // caller-saved
#define R12	12 // caller-saved
#define R13	13 // caller-saved
#define R14	14 // caller-saved
#define R15	15 // caller-saved
#define R16	16 // intra-procedure-call scratch
#define R17	17 // intra-procedure-call scratch
#define R18	18 // *
#define R19	19 // *
#define R20	20 // * litBase?
#define R21	21 // * vmBase
#define R22	22 // * opStack
#define R23	23 // * opStackTop
#define R24	24 // * instructionPointers
#define R25	25 // * programStack
#define R26	26 // * programStackBottom
#define R27	27 // * dataBase
#define R28	28 // * dataMask
#define R29	29 // * frame pointer, procBase
#define R30	30 // link register
#define R31	31 // stack or zero

#define FP	R29
#define LR	R30
#define SP	R31

#define rVMBASE		R21
#define rOPSTACK	R22
#define rOPSTACKTOP	R23
#define rINSPOINTERS	R24
#define rPSTACK		R25
#define rPSTACKBOTTOM	R26
#define rDATABASE	R27
#define rDATAMASK	R28
#define rPROCBASE	FP

#define S0      0
#define S1      1

typedef enum
{
	LAST_COMMAND_NONE = 0,
	LAST_COMMAND_STORE_OPSTACK_R0,
	LAST_COMMAND_STORE_OPSTACK_R0_SYSCALL,
	LAST_COMMAND_STORE_OPSTACK_P4_R0,
	LAST_COMMAND_STORE_OPSTACK_S0,
} ELastCommand;


typedef enum
{
	LIT_VMBASE,
	LIT_DATABASE,
	LIT_INSPOINTERS,
	LIT_DATAMASK,
	LIT_PSTACKBOTTOM,

	FUNC_ENTR,
	FUNC_CALL,
	FUNC_SYSC,
	FUNC_SYSF,
	FUNC_PSOF,
	FUNC_OSOF,
	FUNC_BADJ,
	FUNC_OUTJ,
	FUNC_BADD,
	OFFSET_T_LAST
} offset_t;


// macro opcode sequences
typedef enum {
	MOP_UNDEF = OP_MAX,
	MOP_ADD4,
	MOP_SUB4,
	MOP_MUL4,
	MOP_DIVI4,
	MOP_DIVU4,
	MOP_MODI4,
	MOP_MODU4,
	MOP_BAND4,
	MOP_BOR4,
} macro_op_t;


static uint32_t *code;
static uint32_t compiledOfs;

static  instruction_t *inst = NULL;
static  instruction_t *ci;
static  instruction_t *ni;

static	uint32_t	ip;
static	uint32_t	pass;
static	ELastCommand	LastCommand;
uint32_t savedOffset[ OFFSET_T_LAST ];


static void VM_FreeBuffers( void )
{
	// should be freed in reversed allocation order
	//if ( instructionOffsets ) {
	//	Z_Free( instructionOffsets );
	//	instructionOffsets = NULL;
	//}

	if ( inst ) {
		Z_Free( inst );
		inst = NULL;
	}
}


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


static void __attribute__((__noreturn__)) ErrBadData( void )
{
	Com_Error( ERR_DROP, "program tried to read/write out of data segment" );
}


static void emit( uint32_t isn )
{
	if ( code )
	{
		code[ compiledOfs >> 2 ] = isn;
	}

	compiledOfs += 4;

	LastCommand = LAST_COMMAND_NONE;
}


static void emit8( uint64_t imm )
{
	if ( code )
	{
		code[ (compiledOfs + 0) >> 2 ] = (imm >> 0 ) & 0xFFFFFFFF;
		code[ (compiledOfs + 4) >> 2 ] = (imm >> 32) & 0xFFFFFFFF;
	}

	compiledOfs += 8;
}


// conditions
#define EQ (0b0000) // equal/equals zero
#define NE (0b0001) // not equal
#define CS (0b0010) // unsigned higher or same
#define HS CS       // unsigned higher or same
#define CC (0b0011) // unsigned lower
#define LO CC       // unsigned lower
#define MI (0b0100) // minus/negative
#define PL (0b0101) // plus/positive or zero
#define VS (0b0110) // overflow
#define VC (0b0111) // no overflow
#define HI (0b1000) // unsigned higher
#define LS (0b1001) // unsigned lower or same
#define GE (0b1010) // signed greater or equal
#define LT (0b1011) // signed less than
#define GT (0b1100) // signed greater than
#define LE (0b1101) // signed less than or equal
#define AL (0b1110) // always
#define NV (0b1111) // never

#define WZR 0b11111
#define XZR 0b11111

#define BRK(imm16)              ( (0b11010100001<<21) | (imm16<<5) )
#define RET(Rn)                 ( (0b1101011<<25) | (0b0010<<21) | (0b11111<<16) | (0b000000<<10) | (Rn<<5) | 0b00000 /*Rm*/ )

#define MOVZ32(Rd,imm16)        ( (0<<31) /*sf*/ | (0b10100101<<23) | (0b00<<21) | (((imm16)&0xFFFF)<<5) | Rd )
#define MOVZ32_16(Rd,imm16)     ( (0<<31) /*sf*/ | (0b10100101<<23) | (0b01<<21) | (((imm16)&0xFFFF)<<5) | Rd )
#define MOVZ64(Rd,imm16)        ( (1<<31) /*sf*/ | (0b10100101<<23) | (0b00<<21) | (((imm16)&0xFFFF)<<5) | Rd )

#define MOVK32_16(Rd,imm16)     ( (0<<31) /*sf*/ | (0b11100101<<23) | (0b01<<21) | (((imm16)&0xFFFF)<<5) | Rd )
#define MOVK64_16(Rd,imm16)     ( (1<<31) /*sf*/ | (0b11100101<<23) | (0b01<<21) | (((imm16)&0xFFFF)<<5) | Rd )
#define MOVK64_32(Rd,imm16)     ( (1<<31) /*sf*/ | (0b11100101<<23) | (0b10<<21) | (((imm16)&0xFFFF)<<5) | Rd )
#define MOVK64_48(Rd,imm16)     ( (1<<31) /*sf*/ | (0b11100101<<23) | (0b11<<21) | (((imm16)&0xFFFF)<<5) | Rd )

#define MOVN32(Rd,imm16)        ( (0<<31) /*sf*/ | (0b00100101<<23) | (0b00<<21) | ((imm16&0xFFFF)<<5) | Rd )
#define MOVN32_16(Rd,imm16)     ( (0<<31) /*sf*/ | (0b00100101<<23) | (0b01<<21) | ((imm16&0xFFFF)<<5) | Rd )
#define MOVN64(Rd,imm16)        ( (1<<31) /*sf*/ | (0b00100101<<23) | (0b00<<21) | ((imm16&0xFFFF)<<5) | Rd )
#define MOVN64_16(Rd,imm16)     ( (1<<31) /*sf*/ | (0b00100101<<23) | (0b01<<21) | ((imm16&0xFFFF)<<5) | Rd )
#define MOVN64_32(Rd,imm16)     ( (1<<31) /*sf*/ | (0b00100101<<23) | (0b10<<21) | ((imm16&0xFFFF)<<5) | Rd )
#define MOVN64_48(Rd,imm16)     ( (1<<31) /*sf*/ | (0b00100101<<23) | (0b11<<21) | ((imm16&0xFFFF)<<5) | Rd )

#define ORR32(Rd, Rn, Rm)       ( (0<<31) /*sf*/ | 0b0101010 << 24 | 0b00<<22 /*shift*/ | (0<<21) /*N*/ | (Rm<<16) | 0b000000<<10 /*imm6*/ | (Rn<<5) | Rd )
#define ORR64(Rd, Rn, Rm)       ( (1<<31) /*sf*/ | 0b0101010 << 24 | 0b00<<22 /*shift*/ | (0<<21) /*N*/ | (Rm<<16) | 0b000000<<10 /*imm6*/ | (Rn<<5) | Rd )

#define EOR32(Rd, Rn, Rm)       ( (0<<31) /*sf*/ | (0b1001010<<24) | 0b00<<22 /*shift*/ | (0<<21) /*N*/ | (Rm<<16) | 0b000000<<10 /*imm6*/ | (Rn<<5) | Rd )
#define EOR64(Rd, Rn, Rm)       ( (1<<31) /*sf*/ | (0b1001010<<24) | 0b00<<22 /*shift*/ | (0<<21) /*N*/ | (Rm<<16) | 0b000000<<10 /*imm6*/ | (Rn<<5) | Rd )

#define AND32(Rd, Rn, Rm)       ( (0<<31) /*sf*/ | (0b0001010<<24) | 0b00<<22 /*shift*/ | (0<<21) /*N*/ | (Rm<<16) | 0b000000<<10 /*imm6*/ | (Rn<<5) | Rd )
#define AND64(Rd, Rn, Rm)       ( (1<<31) /*sf*/ | (0b0001010<<24) | 0b00<<22 /*shift*/ | (0<<21) /*N*/ | (Rm<<16) | 0b000000<<10 /*imm6*/ | (Rn<<5) | Rd )

#define MOV32(Rd, Rm)            ORR32(Rd, WZR, Rm)
#define MOV64(Rd, Rm)            ORR64(Rd, XZR, Rm)

// MUL, alias for MADD
#define MUL32(Rd, Rn, Rm)       ( (0<<31) | (0b00<<29) | (0b11011<<24) | (0b000<<21) | (Rm<<16) | (0<<15) | (WZR<<10) /*Ra*/ | (Rn<<5) | Rd )

// ADD (shifted register)
#define ADD32(Rd, Rn, Rm)       ( (0<<31) | (0b0001011000<<21) | (Rm<<16) | (0b000000<<10) /*imm6*/ | (Rn<<5) | Rd  )
#define ADD64(Rd, Rn, Rm)       ( (1<<31) | (0b0001011000<<21) | (Rm<<16) | (0b000000<<10) /*imm6*/ | (Rn<<5) | Rd  )

// ADD (immediate)
#define ADD32i(Rd, Rn, pimm12)  ( (0<<31) | (0b00100010<<23) | (0<<22) /*sh*/ | ((pimm12)<<10) | (Rn<<5) | Rd )
#define ADD64i(Rd, Rn, pimm12)  ( (1<<31) | (0b00100010<<23) | (0<<22) /*sh*/ | ((pimm12)<<10) | (Rn<<5) | Rd )

// SUB (shifted register)
#define SUB32(Rd, Rn, Rm)       ( (0<<31) | 0b1001011000<<21 | (Rm<<16) | 0b000000<<10 /*imm6*/ | (Rn<<5) | Rd  )
#define SUB64(Rd, Rn, Rm)       ( (1<<31) | 0b1001011000<<21 | (Rm<<16) | 0b000000<<10 /*imm6*/ | (Rn<<5) | Rd  )

// SUB (immediate)
#define SUB32i(Rd, Rn, pimm12)  ( (0<<31) | (0b10100010<<23) | (0<<22) /*sh*/ | ((pimm12)<<10) | (Rn<<5) | Rd )
#define SUB64i(Rd, Rn, pimm12)  ( (1<<31) | (0b10100010<<23) | (0<<22) /*sh*/ | ((pimm12)<<10) | (Rn<<5) | Rd )

#define SDIV32(Rd, Rn, Rm)      ( (0<<31) | (0b00<<29) | (0b11010110<<21) | (Rm<<16) | (0b00001<<11) | (1<<10) | (Rn<<5) | Rd )
#define UDIV32(Rd, Rn, Rm)      ( (0<<31) | (0b00<<29) | (0b11010110<<21) | (Rm<<16) | (0b00001<<11) | (0<<10) | (Rn<<5) | Rd )

#define MSUB32(Rd, Rn,Rm, Ra)   ( (0<<31) | (0b00<<29) | (0b11011<<24) | (0b000<<21) | (Rm<<16) | (1<<15) | (Ra<<10) | (Rn<<5) | Rd )

// MVN, alias for ORN (shifted register)
#define MVN32(Rd, Rm)           ( (0<<31) | (0b01<<29) | (0b01010<<24) | (0b001<<21) | (Rm<<16) | (0b000000<<10) | (0b11111<<5) | Rd  )

// NEG (shifted register), alias for SUB(shifted register)
#define NEG32(Rd, Rm)           SUB32(Rd, WZR, Rm)
//#define NEG64(Rd, Rm)           SUB64(Rd, XZR, Rm)

// LSL (register)
#define LSL32(Rd, Rn, Rm)       ( (0<<31) | (0b00<<29) | (0b11010110<<21) | (Rm<<16) | (0b0010<<12) | (0b00<<10) | (Rn<<5) | Rd )

// LSR (register)
#define LSR32(Rd, Rn, Rm)       ( (0<<31) | (0b00<<29) | (0b11010110<<21) | (Rm<<16) | (0b0010<<12) | (0b01<<10) | (Rn<<5) | Rd )

// ASR (register)
#define ASR32(Rd, Rn, Rm)       ( (0<<31) | (0b00<<29) | (0b11010110<<21) | (Rm<<16) | (0b0010<<12) | (0b10<<10) | (Rn<<5) | Rd )

// LDP - load pair of registers with signed offset
#define LDP32(Rt1,Rt2,Rn,simm7) ( 0b00<<30 | 0b101<<27 | 0<<26 | 0b010<<23 | 1<<22 /*L*/ | (((simm7)&0x7F)>>2)<<15 | Rt2<<10 | Rn<<5 | Rt1 )
#define LDP64(Rt1,Rt2,Rn,simm7) ( 0b10<<30 | 0b101<<27 | 0<<26 | 0b010<<23 | 1<<22 /*L*/ | (((simm7)&0x7F)>>3)<<15 | Rt2<<10 | Rn<<5 | Rt1 )

// STP - store pair of registers with signed offset
#define STP32(Rt1,Rt2,Rn,simm7) ( 0b00<<30 | 0b101<<27 | 0<<26 | 0b010<<23 | 0<<22 /*L*/ | (((simm7)&0x7F)>>2)<<15 | Rt2<<10 | Rn<<5 | Rt1 )
#define STP64(Rt1,Rt2,Rn,simm7) ( 0b10<<30 | 0b101<<27 | 0<<26 | 0b010<<23 | 0<<22 /*L*/ | (((simm7)&0x7F)>>3)<<15 | Rt2<<10 | Rn<<5 | Rt1 )

#define LDR32iwpost(Rt, Rn, simm9) ( (0b10<<30) | 0b111000010<<21 | ((simm9&511) << 12) | (0b01 << 10) | (Rn << 5) | Rt )
#define LDR32iwpre(Rt, Rn, simm9)  ( (0b10<<30) | 0b111000010<<21 | ((simm9&511) << 12) | (0b11 << 10) | (Rn << 5) | Rt )

//#define LDR32(Rt, Rn, Rm)       ( (0b10<<30) | (0b111000011<<21) | (Rm<<16) | (0b011<<13) /*LSL*/  | (0<<12) /*#0*/ | (0b10<<10) | (Rn << 5) | Rt )
#define LDR32(Rt, Rn, Rm)       ( (0b10<<30) | (0b111000011<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn << 5) | Rt )
#define LDRH32(Rt, Rn, Rm)      ( (0b01<<30) | (0b111000011<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn << 5) | Rt )
#define LDRB32(Rt, Rn, Rm)      ( (0b00<<30) | (0b111000011<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn << 5) | Rt )

#define LDR32i(Rt, Rn, imm12)   ( (0b10<<30) | 0b11100101<<22 |  (imm12_scale(imm12,2) << 10) | (Rn << 5) | Rt )
#define LDRH32i(Rt, Rn, imm12)  ( (0b01<<30) | 0b11100101<<22 |  (imm12_scale(imm12,1) << 10) | (Rn << 5) | Rt )
#define LDRB32i(Rt, Rn, imm12)  ( (0b00<<30) | 0b11100101<<22 |  (imm12_scale(imm12,0) << 10) | (Rn << 5) | Rt )

//#define LDR32_4(Rt, Rn, Rm)        ( (0b10<<30) | (0b111000011<<21) | (Rm<<16) | (0b011<<13) /*LSL*/ | (1<<12) /*#2*/ | (0b10<<10) | (Rn << 5) | Rt )
#define LDR64_8(Rt, Rn, Rm)        ( (0b11<<30) | (0b111000011<<21) | (Rm<<16) | (0b011<<13) /*LSL*/ | (1<<12) /*#3*/ | (0b10<<10) | (Rn << 5) | Rt )

#define LDR64iwpost(Rt, Rn, simm9) ( (0b11<<30) | 0b111000010<<21 | ((simm9&511) << 12) | (0b01 << 10) | (Rn << 5) | Rt )
#define LDR64iwpre(Rt, Rn, simm9)  ( (0b11<<30) | 0b111000010<<21 | ((simm9&511) << 12) | (0b11 << 10) | (Rn << 5) | Rt )
#define LDR64i(Rt, Rn, imm12)      ( (0b11<<30) | 0b11100101<<22 |  (imm12_scale(imm12,3) << 10) | (Rn << 5) | Rt )

#define STR32iwpost(Rt, Rn, simm9) ( (0b10<<30) | (0b111000000<<21) | ((simm9&511) << 12) | (0b01<<10) | (Rn<<5) | Rt )
#define STR32iwpre(Rt, Rn, simm9)  ( (0b10<<30) | (0b111000000<<21) | ((simm9&511) << 12) | (0b11<<10) | (Rn<<5) | Rt )
#define STR32i(Rt, Rn, imm12)      ( (0b10<<30) | (0b11100100<<22) |  (imm12_scale(imm12,2) << 10) | (Rn << 5) | Rt )

#define STR64iwpost(Rt, Rn, simm9) ( (0b11<<30) | (0b111000000<<21( | ((simm9&511) << 12) | (0b01<<10) | (Rn<<5) | Rt )
#define STR64iwpre(Rt, Rn, simm9)  ( (0b11<<30) | (0b111000000<<21) | ((simm9&511) << 12) | (0b11<<10) | (Rn<<5) | Rt )
#define STR64i(Rt, Rn, imm12)      ( (0b11<<30) | (0b11100100<<22) |  (imm12_scale(imm12,3) << 10) | (Rn << 5) | Rt )

//#define STR32(Rt, Rn, Rm)          ( (0b10<<30) | (0b111000001<<21) | (Rm<<16) | (0b011<<13) /*LSL*/  | (0<<12) /*#0*/ | (0b10<<10) | (Rn<<5) | Rt )
#define STR32(Rt, Rn, Rm)          ( (0b10<<30) | (0b111000001<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn<<5) | Rt )
#define STRH32(Rt, Rn, Rm)         ( (0b01<<30) | (0b111000001<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn<<5) | Rt )
#define STRB32(Rt, Rn, Rm)         ( (0b00<<30) | (0b111000001<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn<<5) | Rt )

// LDR (literal) - PC-related load
#define LDR32lit(Rt,simm19)        ( (0b00<<30) | (0b011<<27) | (0<<26) | (0b00<<24) | (encode_offset19(simm19)<<5) | Rt )
#define LDR64lit(Rt,simm19)        ( (0b01<<30) | (0b011<<27) | (0<<26) | (0b00<<24) | (encode_offset19(simm19)<<5) | Rt )

//#define STR32_4(Rt, Rn, Rm)       ( (0b10<<30) | 0b111000001<<21 | Rm << 16 | 0b011<<13 /*LSL*/ | 1<<12 /*#2*/ | 0b10 << 10 | (Rn << 5) | Rt )
//#define STR64_8(Rt, Rn, Rm)       ( (0b11<<30) | 0b111000001<<21 | Rm << 16 | 0b011<<13 /*LSL*/ | 1<<12 /*#3*/ | 0b10 << 10 | (Rn << 5) | Rt )

#define SXTB(Rd, Rn)               ( (0<<31) | (0b00<<29) | (0b100110<<23) | (0<<22) /*N*/ | (0b000000<<16) /*immr*/ | (0b000111<<10) /*imms*/ | (Rn<<5) | Rd )
#define SXTH(Rd, Rn)               ( (0<<31) | (0b00<<29) | (0b100110<<23) | (0<<22) /*N*/ | (0b000000<<16) /*immr*/ | (0b001111<<10) /*imms*/ | (Rn<<5) | Rd )

// CMP (immediate)
#define CMP32i(Rn, imm12)          ( (0<<31) | (0b11<<29) | (0b100010<<23) | (0<<22) /*sh*/ | (imm12) << 10 | (Rn<<5) | WZR /*Rd*/ )
#define CMP64i(Rn, imm12)          ( (1<<31) | (0b11<<29) | (0b100010<<23) | (0<<22) /*sh*/ | (imm12) << 10 | (Rn<<5) | XZR /*Rd*/ )

// CMP (shifted register)
#define CMP32(Rn, Rm)              ( (0<<31) | (0b11<<29) | (0b01011<<24) | (0b00<<22) /*sh*/ | (0<<21) | (Rm<<16) | (0b000000<<10) /*imm6*/ | (Rn<<5) | WZR /*Rd*/ )
#define CMP64(Rn, Rm)              ( (1<<31) | (0b11<<29) | (0b01011<<24) | (0b00<<22) /*sh*/ | (0<<21) | (Rm<<16) | (0b000000<<10) /*imm6*/ | (Rn<<5) | XZR /*Rd*/ )

// conditional branch within +/-1M
#define Bcond(cond, simm19)        ( (0b0101010<<25) | (0<<24) | (encode_offset19(simm19)<<5) | (0<<4) | cond )

// unconditional branch within +/-128M
#define B(simm26)                  ( (0<<31) | (0b00101<<26) | encode_offset26(simm26) )

// branch with link within +/-128M
#define BL(simm26)                 ( (1<<31) | (0b00101<<26) | encode_offset26(simm26) )

// branch to register
#define BR(Rn)                     ( (0b1101011<<25) | (0<<24) | (0<<23) | (0b00<<21) | (0b11111<<16) | (0b0000<<12) | (0<<11) /*A*/ | (0<<10) /*M*/ | (Rn<<5) | 0b00000 /*Rm*/ )

// branch with link to register
#define BLR(Rn)                    ( (0b1101011<<25) | (0<<24) | (0<<23) | (0b01<<21) | (0b11111<<16) | (0b0000<<12) | (0<<11) /*A*/ | (0<<10) /*M*/ | (Rn<<5) | 0b00000 /*Rm*/ )

#define FABS(Sd, Sn)             ( (0b00011110<<24) | (0b00<<22) | (0b100000110000<<10) | (Sn<<5) | Sd )
#define FNEG(Sd, Sn)             ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b0000<<17) | (0b10<<15) | (0b10000<<10) | (Sn<<5) | Sd )
#define FADD(Sd, Sn, Sm)         ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (Sm<<16) | (0b001<<13) | (0<<12) /*op*/ | (0b10<<10) | (Sn<<5) | Sd )
#define FSUB(Sd, Sn, Sm)         ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (Sm<<16) | (0b001<<13) | (1<<12) /*op*/ | (0b10<<10) | (Sn<<5) | Sd )
#define FMUL(Sd, Sn, Sm)         ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (Sm<<16) | (0<<15) /*op*/ | (0b000<<12) | (0b10<<10) | (Sn<<5) | Sd )
#define FDIV(Sd, Sn, Sm)         ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (Sm<<16) | (0b0001<<12) | (0b10<<10) | (Sn<<5) | Sd )

#define FCMP(Sn, Sm)             ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (Sm<<16) | (0b00<<14) | (0b1000<<10) | (Sn<<5) | (0b00<<3) /*opc*/ | 0b000 )
#define FCMP0(Sn)                ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0<<16)  | (0b00<<14) | (0b1000<<10) | (Sn<<5) | (0b01<<3) /*opc*/ | 0b000 )

// single precision to signed integer
#define FCVTZS(Rd, Sn)           ( (0<<31) | (0b00<<29) | (0b11110<<24) | (0b00<<22)  | (1<<21) | (0b11<<19) /*rmode*/ | (0b000<<16) /*opcode*/ | (0b000000<<10) | (Sn<<5) | Rd )
// signed integer to single precision
#define SCVTF(Sd, Rn)            ( (0<<31) | (0b00<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b00<<19) /*rmode*/ | (0b010<<16) /*opcode*/ | (0b000000<<10) | (Rn<<5) | Sd )

// move scalar to general
#define FMOVgs(Rd, Sn)           ( (0<<31) | (0b00<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b00<<19) /*rmode*/ | (0b110<<16) /*opcode*/ | (0b000000<<10) | (Sn<<5) | Rd )
// move general to scalar
#define FMOVsg(Sd, Rn)           ( (0<<31) | (0b00<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b00<<19) /*rmode*/ | (0b111<<16) /*opcode*/ | (0b000000<<10) | (Rn<<5) | Sd )

#define VLDRpost(St, Rn, simm9)  ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b00<<24) | (0b01<<22) /*opc*/ | (0<<21) | (((simm9)&0x1FF) << 12) | (0b01<<10) | (Rn<<5) | St )
#define VLDRpre(St, Rn, simm9)   ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b00<<24) | (0b01<<22) /*opc*/ | (0<<21) | (((simm9)&0x1FF) << 12) | (0b11<<10) | (Rn<<5) | St )
#define VLDR(St, Rn, pimm12)     ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b01<<24) | (0b01<<22) /*opc*/ | ((((pimm12)>>2)&0xFFF) << 10) | (Rn<<5) | St )

#define VSTRpost(St, Rn, simm9)  ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b00<<24) | (0b00<<22) /*opc*/ | (0<<21) | (((simm9)&0x1FF) << 12) | (0b01<<10) | (Rn<<5) | St )
#define VSTRpre(St, Rn, simm9)   ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b00<<24) | (0b00<<22) /*opc*/ | (0<<21) | (((simm9)&0x1FF) << 12) | (0b11<<10) | (Rn<<5) | St )

#define VSTR(St, Rn, pimm12)     ( (0b10<<30) | (0b111<<27) | (1<<26) /*0?*/ | (0b01<<24) | (0b00<<22) /*opc*/ | ((((pimm12)>>2)&0xFFF) << 10) | (Rn<<5) | St )


static uint32_t imm12_scale( const uint32_t imm12, const uint32_t scale )
{
	const uint32_t mask = (1<<scale) - 1;

	if ( imm12 & mask || imm12 >= 4096 * (1 << scale) )
		Com_Error( ERR_DROP, "offset12_%i %i cannot be encoded\n", (1 << scale), imm12 );

	return imm12 >> scale;
}


static void emit_MOVXi( uint32_t reg, uint64_t imm )
{
	emit( MOVZ64( reg, imm & 0xFFFF ) );

	if ( imm <= 0xFFFF )
		return;

	emit( MOVK64_16( reg, (imm >> 16)&0xFFFF ) );

	if ( imm <= 0xFFFFFFFF )
		return;

	emit( MOVK64_32( reg, (imm >> 32)&0xFFFF ) );

	if ( imm <= 0xFFFFFFFFFFFF )
		return;

	emit( MOVK64_48( reg, (imm >> 48)&0xFFFF ) );
}


static void emit_MOVRi( uint32_t reg, uint32_t imm )
{
	if ( imm <= 0xFFFF ) {
		emit( MOVZ32( reg, imm & 0xFFFF ) );
		return;
	}

	if ( ( imm & 0xFFFF ) == 0 ) {
		emit( MOVZ32_16( reg, (imm >> 16)&0xFFFF ) );
		return;
	}

	emit( MOVZ32( reg, imm & 0xFFFF ) );
	emit( MOVK32_16( reg, (imm >> 16)&0xFFFF ) );
}


static void rewind4( vm_t *vm )
{
	compiledOfs -= 4;
	vm->instructionPointers[ ip-1 ] = compiledOfs;
	LastCommand = LAST_COMMAND_NONE;
}


static void emit_load_r0_opstack( vm_t *vm )
{
#ifdef REGS_OPTIMIZE
	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_R0 ) // *opstack = r0;
	{
		rewind4( vm );
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_P4_R0 ) // opstack +=4; *opstack = r0;
	{
		rewind4( vm );
		emit(ADD64i(rOPSTACK, rOPSTACK, 4)); // opstack += 4;
		return;
	}
#endif
	emit(LDR32i(R0, rOPSTACK, 0)); // r0 = *opstack
}


static void emit_load_r0_opstack_m4( vm_t *vm )
{
#ifdef REGS_OPTIMIZE
	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_R0 ) // *opstack = r0;
	{
		rewind4( vm );
		emit(SUB64i(rOPSTACK, rOPSTACK, 4)); // opstack -= 4;
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_P4_R0 ) // opstack +=4; *opstack = r0;
	{
		rewind4( vm );
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_R0_SYSCALL ) // [opstack -= 4; *opstack = r0;]
	{
		emit(SUB64i(rOPSTACK, rOPSTACK, 4)); // opstack -= 4;
		return;
	}
#endif
	emit(LDR32iwpost(R0, rOPSTACK, -4));  // r0 = *opstack; rOPSTACK -= 4
}


static void emit_store_opstack_r0( vm_t *vm )
{
	emit(STR32i(R0, rOPSTACK, 0));  // *opstack = r0
	LastCommand = LAST_COMMAND_STORE_OPSTACK_R0;
}


static void emit_store_opstack_p4_r0( vm_t *vm )
{
	emit(STR32iwpre(R0, rOPSTACK, 4)); // opstack+=4; *opstack = r0
	LastCommand = LAST_COMMAND_STORE_OPSTACK_P4_R0;
}


static void emit_load_opstack_s0( vm_t *vm )
{
#ifdef S0_OPTIMIZE
	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_S0 ) // *((float*)opstack) = s0
	{
		rewind4( vm );
		return;
	}
#endif
	emit(VLDR(S0, rOPSTACK, 0));   // s0 = *opstack
}


static void emit_load_opstack_s0_m4( vm_t *vm )
{
	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_S0 ) // *opstack = s0
	{
		rewind4( vm );
		emit(SUB64i(rOPSTACK, rOPSTACK, 4)); // opstack -= 4
		return;
	}

	emit(VLDRpost(S0, rOPSTACK, -4)); // s0 = *opstack; opstack -=4
}


static void emit_store_opstack_s0( vm_t *vm )
{
	emit(VSTR(S0, rOPSTACK, 0));   // *opstack = s0
	LastCommand = LAST_COMMAND_STORE_OPSTACK_S0;
}


static uint32_t get_comp( opcode_t op )
{
	switch ( op ) {
		case OP_EQ: return EQ;
		case OP_NE: return NE;
		case OP_LTI: return LT;
		case OP_LEI: return LE;
		case OP_GTI: return GT;
		case OP_GEI: return GE;
		case OP_LTU: return LO;
		case OP_LEU: return LS;
		case OP_GTU: return HI;
		case OP_GEU: return HS;
		case OP_EQF: return EQ;
		case OP_NEF: return NE;
		case OP_LTF: return LT;
		case OP_LEF: return LE;
		case OP_GTF: return GT;
		case OP_GEF: return GE;
		default: break;
	}

	Com_Error( ERR_DROP, "Unexpected op %i\n", op );
}


static uint32_t encode_offset26( uint32_t ofs )
{
	const uint32_t x = ofs >> 2;
	const uint32_t t = x >> 26;
	if ( ( ( t != 0x0F && t != 0x00 ) || ( ofs & 3 ) ) && pass != 0 )
		Com_Error( ERR_DROP, "can't encode offsset26 %i", ofs );
	return x & 0x03FFFFFF;
}


static uint32_t encode_offset19( uint32_t ofs )
{
	const uint32_t x = ofs >> 2;
	const uint32_t t = x >> 19;
	if ( ( ( t != 0x7FF && t != 0x00 ) || ( ofs & 3 ) ) && pass != 0 )
		Com_Error( ERR_DROP, "can't encode offsset19 %i", ofs );
	return x & 0x7FFFF;
}


static void emitFuncOffset( vm_t *vm, offset_t func )
{
	uint32_t offset = savedOffset[ func ] - compiledOfs;

	emit( BL( offset ) );
}


static int VM_SysCall( int call, int pstack, vm_t *vm )
{
	intptr_t args[ 16 ];
	int *argPtr;
	int i;

	vm->programStack = pstack - 8;

	args[0] = call;
	argPtr = (int *)((byte *)vm->dataBase + pstack + 4);

	for( i = 1; i < ARRAY_LEN( args ); i++ )
		args[ i ] = argPtr[ i ];

	return vm->systemCall( args );
}


static void emit_CheckReg( vm_t *vm, instruction_t *ins, uint32_t reg )
{
	if ( ins->safe )
		return;

	if ( vm->forceDataMask )
	{
		emit(AND32(reg, rDATAMASK, reg)); // rN = rN & rDATAMASK
		return;
	}

	if ( !( vm_rtChecks->integer & 8 ) )
		return;

	emit(CMP32(reg, rDATAMASK));
	emit(Bcond(LO, +8));
	emitFuncOffset(vm, FUNC_BADD);  // error function
}


static void emit_CheckJump( vm_t *vm )
{
	if ( ( vm_rtChecks->integer & 4 ) == 0 )
		return;

	// check if R0 >= header->instructionCount
	emit(LDR32i(R2, rVMBASE, offsetof(vm_t, instructionCount)));
	emit(CMP32(R0, R2));
	emit(Bcond(LO, +8));
	emitFuncOffset(vm, FUNC_OUTJ);  // error function
}


static void emit_CheckProc( vm_t *vm, instruction_t *inst )
{
	// programStack overflow check
	if ( vm_rtChecks->integer & 1 ) {
		emit(CMP32(rPSTACK, rPSTACKBOTTOM));  // check if pStack < vm->stackBottom
		emit(Bcond(HS, +8));                  // jump over if unsigned higher or equal
		emitFuncOffset( vm, FUNC_PSOF );      // error function
	}

	// opStack overflow check
	if ( vm_rtChecks->integer & 2 ) {
		uint32_t n = inst->opStack;        // proc->opStack carries max.used opStack value
		if ( n < 4096 ) {
			emit(ADD32i(R2, rOPSTACK, n)); // r2 = opstack + n;
		} else {
			emit_MOVRi(R2, n);             // r2 = n
			emit(ADD32(R2, rOPSTACK, R2)); // r2 = opstack + r2;
		}
		emit(CMP64(R2, rOPSTACKTOP));      // check if opStack > vm->opstackTop
		emit(Bcond(LS, +8));               // jump over if unsigned less or equal
		emitFuncOffset( vm, FUNC_OSOF );
	}
}


static void emitCallFunc( vm_t *vm )
{
savedOffset[ FUNC_CALL ] = compiledOfs; // to jump from OP_CALL

	emit(CMP32i(R0, 0)); // check if syscall
	emit(Bcond(LT, savedOffset[ FUNC_SYSC ] - compiledOfs));

	// check if R0 >= header->instructionCount
	emit_CheckJump( vm );

	// local function call
	emit(LDR64_8(R16, rINSPOINTERS, R0)); // r16 = instructionPointers[ r0 ]
	emit(BR(R16));
	emit(BRK(0));

	// syscall
savedOffset[ FUNC_SYSC ] = compiledOfs; // to jump from OP_CALL

	emit(MVN32(R0, R0));   // r0 = ~r0

savedOffset[ FUNC_SYSF ] = compiledOfs; // to jump from ConstOptimize()

	emit(SUB64i(SP, SP, 16));
	// save LR because it will be clobbered by BLR instruction
	emit(STP64(LR, rPROCBASE, SP, 0));

	// R0 - call, R1 - programStack, R2 - vmBASE
	emit(MOV32(R1,rPSTACK));
	emit(MOV64(R2,rVMBASE));
	emit_MOVXi(R16, (intptr_t)VM_SysCall);
	emit(BLR(16));

	// restore LR
	emit(LDP64(LR, rPROCBASE, SP, 0));
	emit(ADD64i(SP, SP, 16));

	// store return value
	emit_store_opstack_p4_r0( vm ); // opstack+=4; *opstack = r0;

	emit(RET(LR));
}


/*
=================
VM_BlockCopy
Executes a block copy operation within current VM data space
=================
*/
static void VM_BlockCopy( uint32_t src, uint32_t dest, const uint32_t n, const vm_t *vm )
{
	const uint32_t dataMask = vm->dataMask;

	if ((dest & dataMask) != dest
	|| (src & dataMask) != src
	|| ((dest + n) & dataMask) != dest + n
	|| ((src + n) & dataMask) != src + n)
	{
		Com_Error( ERR_DROP, "OP_BLOCK_COPY out of range for vm %s", vm->name );
	}

	Com_Memcpy( vm->dataBase + dest, vm->dataBase + src, n );
}


#if 0
static void dump_code( uint32_t *code, uint32_t code_len )
{
	int fd = open( "code.hex", O_TRUNC | O_WRONLY | O_CREAT, 0644 );
	if ( fd > 0 )
	{
		char buf[32];
		uint32_t i;
		int len;
		for ( i = 0; i < code_len; i++ )
		{
			len = sprintf( buf, "%02x %02x %02x %02x\n", (code[i]>>0)&0xFF, (code[i]>>8)&0xFF, (code[i]>>16)&0xFF, (code[i]>>24)&0xFF );
			write( fd, buf, len );
		}
		close( fd );
	}
}
#endif


#ifdef CONST_OPTIMIZE
qboolean ConstOptimize( vm_t *vm )
{
	uint32_t x;
	// we can safely perform optimizations only in case if
	// we are 100% sure that next instruction is not a jump label
	if ( ni->jused )
		return qfalse;

	switch ( ni->op ) {

	case OP_LOAD4:
		x = ci->value;
		if ( x < 4096*4 && (x & 3) == 0 ) {
			emit(LDR32i(R0, rDATABASE, x)); // r0 = [dataBase + v]
		} else {
			emit_MOVRi(R1, x);
			emit(LDR32(R0, rDATABASE, R1)); // r0 = [dataBase + r1]
		}
		emit_store_opstack_p4_r0( vm ); // opstack +=4 ; *opstack = r0;
		ip += 1; // OP_LOAD4
		return qtrue;

	case OP_LOAD2:
		x = ci->value;
		if ( x < 4096*2 && (x & 1) == 0 ) {
			emit(LDRH32i(R0, rDATABASE, x)); // r0 = [dataBase + v]
		} else {
			emit_MOVRi(R1, x);
			emit(LDRH32(R0, rDATABASE, R1)); // r0 = [dataBase + r1]
		}
		emit_store_opstack_p4_r0( vm ); // opstack +=4 ; *opstack = r0;
		ip += 1; // OP_LOAD2
		return qtrue;

	case OP_LOAD1:
		x = ci->value;
		if ( x < 4096 ) {
			emit(LDRB32i(R0, rDATABASE, x)); // r0 = [dataBase + v]
		} else {
			emit_MOVRi(R1, x);
			emit(LDRB32(R0, rDATABASE, R1)); // r0 = [dataBase + r1]
		}
		emit_store_opstack_p4_r0( vm ); // opstack +=4 ; *opstack = r0;
		ip += 1; // OP_LOAD1
		return qtrue;

	case OP_STORE4:
		x = ci->value;
		emit_load_r0_opstack_m4( vm ); // r0 = *opstack; opstack -=4;
		emit_CheckReg(vm, ni, R0);
		emit_MOVRi(R1, x);
		emit(STR32(R1, rDATABASE, R0)); // [dataBase + r0] = r1;
		ip += 1; // OP_STORE4
		return qtrue;

	case OP_STORE2:
		x = ci->value;
		emit_load_r0_opstack_m4( vm ); // r0 = *opstack; opstack -=4;
		emit_CheckReg(vm, ni, R0);
		emit_MOVRi(R1, x);
		emit(STRH32(R1, rDATABASE, R0)); // (short)[dataBase + r0] = r1;
		ip += 1; // OP_STORE2
		return qtrue;

	case OP_STORE1:
		x = ci->value;
		emit_load_r0_opstack_m4( vm ); // r0 = *opstack; opstack -=4;
		emit_CheckReg(vm, ni, R0);
		emit_MOVRi(R1, x);
		emit(STRB32(R1, rDATABASE, R0)); // (byte)[dataBase + r0] = r1;
		ip += 1; // OP_STORE1
		return qtrue;

	case OP_ADD:
		x = ci->value;
		emit_load_r0_opstack( vm ); // r0 = *opstack;
		if ( x < 4096 ) {
			emit(ADD32i(R0, R0, x));
		} else {
			emit_MOVRi(R1, x);
			emit(ADD32(R0, R1, R0));
		}
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_ADD
		return qtrue;

	case OP_SUB:
		x = ci->value;
		emit_load_r0_opstack( vm ); // r0 = *opstack;
		if ( x < 4096 ) {
			emit(SUB32i(R0, R0, x));
		} else {
			emit_MOVRi(R1, x);
			emit(SUB32(R0, R0, R1));
		}
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_SUB
		return qtrue;

	case OP_MULI:
	case OP_MULU:
		emit_load_r0_opstack( vm );     // r0 = *opstack;
		x = ci->value;
		emit_MOVRi(R1, x);
		emit(MUL32(R0, R1, R0));
		emit_store_opstack_r0( vm );    // *opstack = r0;
		ip += 1; // OP_MULI|OP_MULU
		return qtrue;

	case OP_DIVI:
	case OP_DIVU:
		emit_load_r0_opstack( vm );     // r0 = *opstack;
		x = ci->value;
		emit_MOVRi(R1, x);
		if ( ni->op == OP_DIVI ) {
			emit(SDIV32(R0, R0, R1));     // r0 = r0 / r1
		} else {
			emit(UDIV32(R0, R0, R1));
		}
		emit_store_opstack_r0( vm );    // *opstack = r0;
		ip += 1;
		return qtrue;

	case OP_MODI:
	case OP_MODU:
		emit_load_r0_opstack( vm );     // r0 = *opstack;
		x = ci->value;
		emit_MOVRi(R1, x);
		if ( ni->op == OP_MODI ) {
			emit(SDIV32(R2, R0, R1));     // r2 = r0 / r1
		} else {
			emit(UDIV32(R2, R0, R1));
		}
		emit(MSUB32(R0, R1, R2, R0));      // r0 = r0 - r1 * r2
		emit_store_opstack_r0( vm );    // *opstack = r0;
		ip += 1;
		return qtrue;

	case OP_JUMP:
		emit(B(vm->instructionPointers[ ci->value ] - compiledOfs));
		ip += 1; // OP_JUMP
		return qtrue;

	case OP_CALL:
		if ( ci->value < 0 ) // syscall
		{
			x = ~ci->value;
			emit_MOVRi(R0, x);
			emitFuncOffset( vm, FUNC_SYSF );
			ip += 1; // OP_CALL;
			LastCommand = LAST_COMMAND_STORE_OPSTACK_R0_SYSCALL; // we have result in r0
			return qtrue;
		}
		emit(BL(vm->instructionPointers[ ci->value ] - compiledOfs));
		ip += 1; // OP_CALL;
		return qtrue;

	case OP_EQ:
	case OP_NE:
	case OP_GEI:
	case OP_GTI:
	case OP_GTU:
	case OP_GEU:
	case OP_LTU:
	case OP_LEU:
	case OP_LEI:
	case OP_LTI: {
		uint32_t comp = get_comp( ni->op );
		emit_load_r0_opstack_m4( vm ); // r0 = *opstack; rOPSTACK -= 4
		x = ci->value;
		if ( x < 4096 ) {
			emit(CMP32i(R0, x));
		} else {
			emit_MOVRi(R1, x);
			emit(CMP32(R0, R1));
		}
		emit(Bcond(comp, vm->instructionPointers[ ni->value ] - compiledOfs));
		}
		ip += 1; // OP_cond
		return qtrue;

#if 0
	case OP_ADDF:
	case OP_SUBF:
	case OP_MULF:
	case OP_DIVF:
		emit_load_opstack_s0( vm );        // s0 = *opstack;
		x = ci->value;
		emit_MOVRi(R1, x);                 // r1 = ci->value
		emit(FMOVsg(S1, R1));              // s1 = r1
		switch ( ni->op ) {
			case OP_ADDF: emit(FADD(S0, S0, S1)); break; // s0 = s0 + s1
			case OP_SUBF: emit(FSUB(S0, S0, S1)); break; // s0 = s0 - s1
			case OP_MULF: emit(FMUL(S0, S0, S1)); break; // s0 = s0 * s1
			case OP_DIVF: emit(FDIV(S0, S0, S1)); break; // s0 = s0 / s1
			default: break;
		}
		emit_store_opstack_s0( vm );       // *opstack = s0
		ip += 1; // OP_XXXF
		return qtrue;
#endif

	default:
		break;
	};

	return qfalse;
}
#endif // CONST_OPTIMIZE


#ifdef MACRO_OPTIMIZE
/*
=================
VM_FindMOps

Search for known macro-op sequences
=================
*/
static void VM_FindMOps( instruction_t *buf, const int instructionCount )
{
	int n, v, op0;
	instruction_t *i;

	i = buf;
	n = 0;

	while ( n < instructionCount )
	{
		op0 = i->op;
		if ( op0 == OP_LOCAL ) {
			// OP_LOCAL + OP_LOCAL + OP_LOAD4 + OP_CONST + OP_XXX + OP_STORE4
			if ( (i+1)->op == OP_LOCAL && i->value == (i+1)->value && (i+2)->op == OP_LOAD4 && (i+3)->op == OP_CONST && (i+4)->op != OP_UNDEF && (i+5)->op == OP_STORE4 ) {
				v = (i+4)->op;
				if ( v == OP_ADD ) {
					i->op = MOP_ADD4;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_SUB ) {
					i->op = MOP_SUB4;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_MULU || v == OP_MULI ) {
					i->op = MOP_MUL4;
					i += 6; n += 6;
					continue;
				}
#if 0
				if ( v == OP_DIVI ) {
					i->op = MOP_DIVI4;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_DIVU ) {
					i->op = MOP_DIVU4;
					i += 6; n += 6;
					continue;
				}

				if ( v == OP_MODI ) {
					i->op = MOP_MODI4;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_MODU ) {
					i->op = MOP_MODU4;
					i += 6; n += 6;
					continue;
				}
#endif
				if ( v == OP_BAND ) {
					i->op = MOP_BAND4;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_BOR ) {
					i->op = MOP_BOR4;
					i += 6; n += 6;
					continue;
				}
			}
		}

		i++;
		n++;
	}
}


/*
=================
EmitMOPs
=================
*/
static qboolean EmitMOPs( vm_t *vm, int op )
{
	uint32_t addr, value;
	int short_addr32;

	switch ( op )
	{
		//[local] += CONST
		case MOP_ADD4:
		case MOP_SUB4:
		case MOP_MUL4:
		case MOP_DIVI4:
		case MOP_DIVU4:
		case MOP_MODI4:
		case MOP_MODU4:
		case MOP_BAND4:
		case MOP_BOR4:
			value = inst[ip+2].value;
			addr = ci->value;
			short_addr32 = (addr < 4096*4 && (addr & 3) == 0);

			// load
			if ( short_addr32 ) {
				emit(LDR32i(R0, rPROCBASE, addr)); // r0 = [procBase + addr]
			} else {
				emit_MOVRi(R2, addr);              // r2 = addr
				emit(LDR32(R0, rPROCBASE, R2));    // r0 = [procBase + r2]
			}

			// modify
			switch ( op ) {
				case MOP_ADD4:
					if ( value < 4096 ) {
						emit(ADD32i(R0, R0, value));     // r0 += value;
					} else {
						emit_MOVRi(R1, value);           // r1 = value
						emit(ADD32(R0, R0, R1));         // r0 += r1
					}
					break;
				case MOP_SUB4:
					if ( value < 4096 ) {
						emit(SUB32i(R0, R0, value));     // r0 -= value;
					} else {
						emit_MOVRi(R1, value);           // r1 = value
						emit(SUB32(R0, R0, R1));         // r0 -= r1
					}
					break;

				case MOP_MUL4:
					emit_MOVRi(R1, value);           // r1 = value
					emit(MUL32(R0, R0, R1));         // r0 *= r1
					break;

				case MOP_DIVI4:
					emit_MOVRi(R1, value);           // r1 = value
					emit(SDIV32(R0, R0, R1));        // r0 /= r1
					break;

				case MOP_DIVU4:
					emit_MOVRi(R1, value);           // r1 = value
					emit(UDIV32(R0, R0, R1));        // r0 /= r1
					break;

				case MOP_MODI4:
					emit_MOVRi(R1, value);           // r1 = value
					emit(SDIV32(R2, R0, R1));        // r2 = r0 / r1
					emit(MSUB32(R0, R1, R2, R0));    // r0 = r0 - r1 * r2
					break;

				case MOP_MODU4:
					emit_MOVRi(R1, value);           // r1 = value
					emit(UDIV32(R2, R0, R1));        // r2 = r0 / r1
					emit(MSUB32(R0, R1, R2, R0));    // r0 = r0 - r1 * r2
					break;

				case MOP_BAND4:
					emit_MOVRi(R1, value);           // r1 = value
					emit(AND32(R0, R0, R1));         // r0 &= r1
					break;

				case MOP_BOR4:
					emit_MOVRi(R1, value);           // r1 = value
					emit(ORR32(R0, R0, R1));         // r0 |= r1
					break;
				default:
					break;
			}

			// store
			if ( short_addr32 ) {
				emit(STR32i(R0, rPROCBASE, addr)); // [procBase + addr] = r0
			} else {
				emit(STR32(R0, rPROCBASE, R2));    // [procBase + r2] = r0
			}

			ip += 5;
			return qtrue;

		default: Com_Error( ERR_DROP, "Unknown macro opcode %i", op );
			break;
	};

	return qfalse;
}
#endif // MACRO_OPTIMIZE


qboolean VM_Compile( vm_t *vm, vmHeader_t *header )
{
	const char *errMsg;
	int i;

	inst = (instruction_t*)Z_Malloc( (header->instructionCount + 8 ) * sizeof( instruction_t ) );
	//instructionOffsets = (uint32_t*)Z_Malloc( header->instructionCount * sizeof( uint32_t ) );

	errMsg = VM_LoadInstructions( (byte *) header + header->codeOffset, header->codeLength, header->instructionCount, inst );
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

#ifdef MACRO_OPTIMIZE
	VM_FindMOps( inst, vm->instructionCount );
#endif

	memset( savedOffset, 0, sizeof( savedOffset ) );

	code = NULL;
	vm->codeBase.ptr = NULL;

	for ( pass = 0; pass < NUM_PASSES; pass++ ) {
__recompile:

	// translate all instructions
	ip = 0;
	compiledOfs = 0;
	LastCommand = LAST_COMMAND_NONE;

	emit(SUB64i(SP, SP, 80)); // SP -= 80
	emit(STP64(LR, R21, SP, 0));
	emit(STP64(R22, R23, SP, 16));
	emit(STP64(R24, R25, SP, 32));
	emit(STP64(R26, R27, SP, 48));
	emit(STP64(R28, R29, SP, 64));

	emit(LDR64lit(rVMBASE, savedOffset[LIT_VMBASE] - compiledOfs));
	emit(LDR64lit(rINSPOINTERS, savedOffset[LIT_INSPOINTERS] - compiledOfs));
	emit(LDR64lit(rDATABASE, savedOffset[LIT_DATABASE] - compiledOfs));

	emit(LDR32lit(rDATAMASK, savedOffset[LIT_DATAMASK] - compiledOfs));
	emit(LDR32lit(rPSTACKBOTTOM, savedOffset[LIT_PSTACKBOTTOM] - compiledOfs));

	// these are volatile variables
	emit(LDR64i(rOPSTACK, rVMBASE, offsetof(vm_t, opStack)));
	emit(LDR64i(rOPSTACKTOP, rVMBASE, offsetof(vm_t, opStackTop)));
	emit(LDR32i(rPSTACK, rVMBASE, offsetof(vm_t, programStack)));

	emitFuncOffset( vm, FUNC_ENTR );  // call vmMain()

#ifdef DEBUG_VM
	emit(STR32i(rPSTACK, rVMBASE, offsetof(vm_t, programStack))); // vm->programStack = rPSTACK;
	emit(STR64i(rOPSTACK, rVMBASE, offsetof(vm_t, opStack)));     // vm->opStack = rOPSTACK;
#endif

	emit(LDP64(LR, R21, SP, 0));
	emit(LDP64(R22, R23, SP, 16));
	emit(LDP64(R24, R25, SP, 32));
	emit(LDP64(R26, R27, SP, 48));
	emit(LDP64(R28, R29, SP, 64));
	emit(ADD64i(SP, SP, 80)); // SP += 80
	emit(RET(LR));

	// begin literals

savedOffset[ LIT_VMBASE ] = compiledOfs;
	emit8( (intptr_t) vm );

savedOffset[ LIT_DATABASE ] = compiledOfs;
	emit8( (intptr_t) vm->dataBase );

savedOffset[ LIT_INSPOINTERS ] = compiledOfs;
	emit8( (intptr_t) vm->instructionPointers );

savedOffset[ LIT_DATAMASK ] = compiledOfs;
	emit( vm->dataMask );

savedOffset[ LIT_PSTACKBOTTOM ] = compiledOfs;
	emit( vm->stackBottom );

	// end literals

savedOffset[ FUNC_ENTR ] = compiledOfs; // offset to vmMain() entry point

	while ( ip < header->instructionCount ) {

		uint32_t v;
		vm->instructionPointers[ ip ] = compiledOfs;

		ci = &inst[ ip + 0 ];
		ni = &inst[ ip + 1 ];

		if ( ci->jused )
		{
			LastCommand = LAST_COMMAND_NONE;
		}

		ip++;
		v = ci->value;

		switch ( ci->op )
		{
			case OP_UNDEF:
				emit(BRK(0));
				break;

			case OP_IGNORE:
				break;

			case OP_BREAK:
				emit(BRK(3));
				break;

			case OP_ENTER:
				emit(SUB64i(SP, SP, 32));
				emit(STP64(LR, rPROCBASE, SP, 0)); // save LR, rPROCBASE
				emit(STP32(R16, rPSTACK, SP, 16)); // save R16, rPSTACK
				if ( v < 4096 ) {
					emit(SUB32i(rPSTACK, rPSTACK, v));
				} else {
					emit_MOVRi(R1, v);                 // r1 = arg
					emit(SUB32(rPSTACK, rPSTACK, R1)); // pstack -= r1
				}
				emit_CheckProc( vm, ci );
				emit(ADD64(rPROCBASE, rPSTACK, rDATABASE));
				break;

			case OP_LEAVE:
				emit(LDP64(LR, rPROCBASE, SP, 0)); // restore LR, rPROCBASE
				emit(LDP32(R16, rPSTACK, SP, 16)); // restore R16, rPSTACK
				emit(ADD64i(SP, SP, 32));
				emit(RET(LR));
				break;

			case OP_CALL:
				emit_load_r0_opstack_m4( vm );     // r0 = *opstack; rOPSTACK -= 4
				emitFuncOffset(vm, FUNC_CALL);
				break;

			case OP_PUSH:
				emit(ADD64i(rOPSTACK, rOPSTACK, 4)); // opstack += 4
				break;

			case OP_POP:
				emit(SUB64i(rOPSTACK, rOPSTACK, 4)); // opstack -= 4
				break;

			case OP_CONST:
#ifdef CONST_OPTIMIZE
				if ( ConstOptimize( vm ) )
					break;
#endif
				emit_MOVRi(R0, v);                    // mov r0, 0x12345678
				emit_store_opstack_p4_r0( vm );       // opstack+=4; *opstack = r0
				break;

			case OP_LOCAL:
#ifdef MISC_OPTIMIZE
				if ( ni->op == OP_LOAD4 ) // merge OP_LOCAL + OP_LOAD4
				{
					if ( v < 4096*4 && (v & 3) == 0 ) {
						emit(LDR32i(R0, rPROCBASE, v)); // r0 = [procBase + v]
					} else {
						emit_MOVRi(R1, v);
						emit(LDR32(R0, rPROCBASE, R1)); // r0 = [procBase+r1]
					}
					emit_store_opstack_p4_r0( vm ); // opstack+=4; *opstack = r0
					ip++; // OP_LOAD4
					break;
				}

				if ( ni->op == OP_LOAD2 ) // merge OP_LOCAL + OP_LOAD2
				{
					if ( v < 4096*2 && (v & 1) == 0 ) {
						emit(LDRH32i(R0, rPROCBASE, v)); // r0 = (short*)[procBase + v]
					} else {
						emit_MOVRi(R1, v);
						emit(LDRH32(R0, rPROCBASE, R1)); // r0 = (short*)[procBase+r1]
					}
					emit_store_opstack_p4_r0( vm ); // opstack+=4; *opstack = r0
					ip++; // OP_LOAD2
					break;
				}

				if ( ni->op == OP_LOAD1 ) // merge OP_LOCAL + OP_LOAD1
				{
					if ( v < 4096 ) {
						emit(LDRB32i(R0, rPROCBASE, v)); // r0 = (byte*)[procBase + v]
					} else {
						emit_MOVRi(R1, v);
						emit(LDRB32(R0, rPROCBASE, R1)); // r0 = (byte*)[procBase+r1]
					}
					emit_store_opstack_p4_r0( vm ); // opstack+=4; *opstack = r0
					ip++; // OP_LOAD1
					break;
				}
#endif
				if ( v < 4096 ) {
					emit(ADD32i(R0, rPSTACK, v)); // r0 = pstack + arg
				} else {
					emit_MOVRi(R1, v);            // r1 = arg;
					emit(ADD32(R0, rPSTACK, R1)); // r0 = pstack + r1
				}
				emit_store_opstack_p4_r0( vm );       // opstack+=4; *opstack = r0
				break;

			case OP_JUMP:
				emit_load_r0_opstack_m4( vm );        // r0 = *opstack; rOPSTACK -= 4
				emit_CheckJump( vm );
				emit(LDR64_8(R16, rINSPOINTERS, R0)); // r16 = instructionPointers[ r0 ]
				emit(BR(R16));
				break;

			case OP_EQ:
			case OP_NE:
			case OP_LTI:
			case OP_LEI:
			case OP_GTI:
			case OP_GEI:
			case OP_LTU:
			case OP_LEU:
			case OP_GTU:
			case OP_GEU: {
				uint32_t comp = get_comp( ci->op );
				emit_load_r0_opstack_m4( vm );       // r0 = *opstack; rOPSTACK -= 4
				emit(LDR32iwpost(R1, rOPSTACK, -4)); // r1 = *opstack; rOPSTACK -= 4
				emit(CMP32(R1, R0));
				emit(Bcond(comp, vm->instructionPointers[v] - compiledOfs));
				}
				break;

			case OP_EQF:
			case OP_NEF:
			case OP_LTF:
			case OP_LEF:
			case OP_GTF:
			case OP_GEF: {
				uint32_t comp = get_comp( ci->op );
				emit_load_opstack_s0_m4( vm );      // *s0 = opstack; opstack -=4
				emit(VLDRpost(S1, rOPSTACK, -4));   // s1 = *opstack; opstack -=4
				emit(FCMP(S1, S0));
				emit(Bcond(comp, vm->instructionPointers[v] - compiledOfs));
				}
				break;

			case OP_LOAD1:
				emit_load_r0_opstack( vm );          // r0 = *opstack
				emit_CheckReg(vm, ci, R0);
				emit(LDRB32(R0, rDATABASE, R0));     // r0 = (unsigned char)dataBase[r0]
				emit_store_opstack_r0( vm );         // *opstack = r0
				break;

			case OP_LOAD2:
				emit_load_r0_opstack( vm );          // r0 = *opstack
				emit_CheckReg(vm, ci, R0);
				emit(LDRH32(R0, rDATABASE, R0));     // r0 = (unsigned short)dataBase[r0]
				emit_store_opstack_r0( vm );         // *opstack = r0
				break;

			case OP_LOAD4:
				emit_load_r0_opstack( vm );          // r0 = *opstack
				emit_CheckReg(vm, ci, R0);
				emit(LDR32(R0, rDATABASE, R0));      // r0 = dataBase[r0]
				emit_store_opstack_r0( vm );         // *opstack = r0
				break;

			case OP_STORE1:
				emit_load_r0_opstack_m4( vm );       // r0 = *opstack; rOPSTACK -= 4
				emit(LDR32iwpost(R1, rOPSTACK, -4)); // r1 = *opstack; rOPSTACK -= 4
				emit_CheckReg(vm, ci, R1);
				emit(STRB32(R0, rDATABASE, R1));     // (byte*)database[r1] = r0
				break;

			case OP_STORE2:
				emit_load_r0_opstack_m4( vm );       // r0 = *opstack; rOPSTACK -= 4
				emit(LDR32iwpost(R1, rOPSTACK, -4)); // r1 = *opstack; rOPSTACK -= 4
				emit_CheckReg(vm, ci, R1);
				emit(STRH32(R0, rDATABASE, R1));     // (short*)database[r1] = r0
				break;

			case OP_STORE4:
				emit_load_r0_opstack_m4( vm );       // r0 = *opstack; rOPSTACK -= 4
				emit(LDR32iwpost(R1, rOPSTACK, -4)); // r1 = *opstack; rOPSTACK -= 4
				emit_CheckReg(vm, ci, R1);
				emit(STR32(R0, rDATABASE, R1));      // database[r1] = r0
				break;

			case OP_ARG:
				emit_load_r0_opstack_m4( vm );       // r0 = *opstack; rOPSTACK -= 4
#ifdef MISC_OPTIMIZE
				emit(STR32i(R0, rPROCBASE, v));      // [procBase + v] = r0;
#else
				emit(ADD32i(R1, rPSTACK, v));        // r1 = programStack+arg
				emit(STR32(R0, rDATABASE, R1));      // dataBase[r1] = r0
#endif
				break;

			case OP_BLOCK_COPY:
				// src: opStack[0]
				// dst: opstack[-4]
				emit_load_r0_opstack_m4( vm );       // src: r0 = *opstack; rOPSTACK -= 4
				emit(LDR32iwpost(R1, rOPSTACK, -4)); // dst: r1 = *opstack; rOPSTACK -= 4
				emit_MOVRi(R2, v);
				emit(MOV64(R3, rVMBASE));
				emit_MOVXi(R16, (intptr_t)VM_BlockCopy);
				emit(BLR(R16));
				break;

			case OP_SEX8:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(SXTB(R0, R0));            // r0 = sign extend byte r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_SEX16:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(SXTH(R0, R0));            // r0 = sign extend short r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_NEGI:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(NEG32(R0, R0));           // r0 = -r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_ADD:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(ADD32(R0, R1, R0));            // r0 = r1 + r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_SUB:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(SUB32(R0, R1, R0));            // r0 = r1 + r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_DIVI:
			case OP_DIVU:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				if ( ci->op == OP_DIVI ) {
					emit(SDIV32(R0, R1, R0));
				} else {
					emit(UDIV32(R0, R1, R0));
				}
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_MODI:
			case OP_MODU:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				if ( ci->op == OP_DIVI ) {
					emit(SDIV32(R2, R1, R0));
				} else {
					emit(UDIV32(R2, R1, R0));
				}
				emit(MSUB32(R0, R0, R2, R1));       // r0 = r1 - r0 * r2
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_MULI:
			case OP_MULU:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(MUL32(R0, R1, R0));            // r0 = r1 + r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_BAND:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(AND32(R0, R1, R0));            // r0 = r1 & r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_BOR:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(ORR32(R0, R1, R0));            // r0 = r1 | r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_BXOR:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(EOR32(R0, R1, R0));            // r0 = r1 ^ r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_BCOM:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(MVN32(R0, R0));                // r0 = ~r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_LSH:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(LSL32(R0, R1, R0));            // r0 = r1 << r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_RSHI:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(ASR32(R0, R1, R0));            // r0 = r1 >> r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_RSHU:
				emit_load_r0_opstack( vm );         // r0 = *opstack
				emit(LDR32iwpre(R1, rOPSTACK, -4)); // opstack-=4; r1 = *opstack
				emit(LSR32(R0, R1, R0));            // r0 = (unsigned)r1 >> r0
				emit_store_opstack_r0( vm );        // *opstack = r0
				break;

			case OP_NEGF:
				emit_load_opstack_s0( vm );    // s0 = *((float*)opstack)
				emit(FNEG(S0, S0));            // s0 = -s0
				emit_store_opstack_s0( vm );   // *((float*)opstack) = s0
				break;

			case OP_ADDF:
				emit_load_opstack_s0( vm );      // s0 = *((float*)opstack)
				emit(VLDRpre(S1, rOPSTACK, -4)); // opstack -=4; s1 = *opstack;
				emit(FADD(S0, S1, S0));          // s0 = s1 + s0
				emit_store_opstack_s0( vm );     // *((float*)opstack) = s0
				break;

			case OP_SUBF:
				emit_load_opstack_s0( vm );      // s0 = *((float*)opstack)
				emit(VLDRpre(S1, rOPSTACK, -4)); // opstack -=4; s1 = *opstack;
				emit(FSUB(S0, S1, S0));          // s0 = s1 - s0
				emit_store_opstack_s0( vm );     // *((float*)opstack) = s0
				break;

			case OP_DIVF:
				emit_load_opstack_s0( vm );      // s0 = *((float*)opstack)
				emit(VLDRpre(S1, rOPSTACK, -4)); // opstack -=4; s1 = *opstack;
				emit(FDIV(S0, S1, S0));          // s0 = s1 / s0
				emit_store_opstack_s0( vm );     // *((float*)opstack) = s0
				break;

			case OP_MULF:
				emit_load_opstack_s0( vm );      // s0 = *((float*)opstack)
				emit(VLDRpre(S1, rOPSTACK, -4)); // opstack -=4; s1 = *opstack;
				emit(FMUL(S0, S1, S0));          // s0 = s1 * s0
				emit_store_opstack_s0( vm );     // *((float*)opstack) = s0
				break;

			case OP_CVIF:
				emit_load_r0_opstack( vm );      // r0 = *opstack
				emit(SCVTF(S0, R0));             // s0 = (float)r0
				emit_store_opstack_s0( vm );     // *opstack = s0
				break;

			case OP_CVFI:
				emit_load_opstack_s0( vm );   // s0 = *((float*)opstack)
				emit(FCVTZS(R0, S0));
				emit_store_opstack_r0( vm );  // *opstack = r0
				break;
#ifdef MACRO_OPTIMIZE
			case MOP_ADD4:
			case MOP_SUB4:
			case MOP_MUL4:
			case MOP_DIVI4:
			case MOP_DIVU4:
			case MOP_MODI4:
			case MOP_MODU4:
			case MOP_BAND4:
			case MOP_BOR4:
				EmitMOPs( vm, ci->op );
				break;
#endif
		} // switch op
	} // ip

		// it will set multiple offsets
		emitCallFunc( vm );

		savedOffset[ FUNC_BADJ ] = compiledOfs;
		emit_MOVXi(R16, (intptr_t)BadJump);
		emit(BLR(R16));

		savedOffset[ FUNC_OUTJ ] = compiledOfs;
		emit_MOVXi(R16, (intptr_t)OutJump);
		emit(BLR(R16));

		savedOffset[ FUNC_OSOF ] = compiledOfs;
		emit_MOVXi(R16, (intptr_t)ErrBadOpStack);
		emit(BLR(R16));

		savedOffset[ FUNC_PSOF ] = compiledOfs;
		emit_MOVXi(R16, (intptr_t)ErrBadProgramStack);
		emit(BLR(R16));

		savedOffset[ FUNC_BADD ] = compiledOfs;
		emit_MOVXi(R16, (intptr_t)ErrBadData);
		emit(BLR(R16));

	} // pass

	if ( vm->codeBase.ptr == NULL ) {
		vm->codeBase.ptr = mmap( NULL, compiledOfs, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
		if ( vm->codeBase.ptr == MAP_FAILED ) {
			VM_FreeBuffers();
			Com_Printf( S_COLOR_YELLOW "%s(%s): mmap failed\n", __func__, vm->name );
			return qfalse;
		}
		vm->codeLength = compiledOfs;
		vm->codeSize = compiledOfs;
		code = (uint32_t*)vm->codeBase.ptr;
		goto __recompile;
	}

	//dump_code( code, compiledOfs / 4 );

	// offset all the instruction pointers for the new location
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

	// clear icache, http://blogs.arm.com/software-enablement/141-caches-and-self-modifying-code/
	 __clear_cache( vm->codeBase.ptr, vm->codeBase.ptr + vm->codeLength );

	vm->destroy = VM_Destroy_Compiled;

	return qtrue;
}


int VM_CallCompiled( vm_t *vm, int nargs, int *args )
{
	int		opStack[MAX_OPSTACK_SIZE];
	unsigned int stackOnEntry;
	int		*image;
	int		i;

	// we might be called recursively, so this might not be the very top
	stackOnEntry = vm->programStack;

	vm->programStack -= (MAX_VMMAIN_CALL_ARGS+2)*4;

	// set up the stack frame
	image = (int*)( vm->dataBase + vm->programStack );
	for ( i = 0; i < nargs; i++ ) {
		image[ i + 2 ] = args[ i ];
	}

	// these only needed for interpreter:
	// image[1] =  0; // return stack
	// image[0] = -1; // will terminate loop on return

#ifdef DEBUG_VM
	opStack[0] = 0xDEADC0DE;
#endif
	opStack[1] = 0;

	vm->opStack = opStack;
	vm->opStackTop = opStack + ARRAY_LEN( opStack ) - 1;

	vm->codeBase.func(); // go into generated code

#ifdef DEBUG_VM
	if ( vm->opStack != &opStack[1] || opStack[0] != 0xDEADC0DE ) {
		Com_Error( ERR_DROP, "%s(%s): opStack corrupted in compiled code", __func__, vm->name );
	}

	if ( vm->programStack != stackOnEntry - (MAX_VMMAIN_CALL_ARGS+2)*4 ) {
		Com_Error( ERR_DROP, "%s(%s): programStack corrupted in compiled code", __func__, vm->name );
	}
#endif

	vm->programStack = stackOnEntry;

	return opStack[1];
}
