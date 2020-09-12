/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2020 Quake3e project

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
// with dynamic register allocation and various optimizations

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

// various defintions to enable/disable particular optimization
#define DYN_ALLOC_RX
#define DYN_ALLOC_SX

#define REGS_OPTIMIZE
#define FPU_OPTIMIZE
#define CONST_OPTIMIZE
#define MISC_OPTIMIZE
#define MACRO_OPTIMIZE
#define USE_LITERAL_POOL

//#define DUMP_CODE

// registers map

// general purpose registers:
// R0..R17 can be used as a scratch registers
// R18 must not be used, especially on windows
// R19..R29 and R31 must be preserved

// FPU scalar registers:
// S0..S7 can be used as a scratch registers
// S8..S15 must be preserved
// S16..S31 can be used as a scratch registers

#define R0	0 // scratch, return value
#define R1	1 // scratch
#define R2	2 // scratch
#define R3	3 // scratch
#define R4	4 // scratch
#define R5	5 // scratch
#define R6	6 // scratch
#define R7	7 // scratch
#define R8	8 // scratch, indirect return value
#define R9	9  // scratch
#define R10	10 // scratch
#define R11	11 // scratch
#define R12	12 // scratch
#define R13	13 // scratch
#define R14	14 // scratch
#define R15	15 // scratch
#define R16	16 // intra-procedure-call scratch
#define R17	17 // intra-procedure-call scratch - opStack shift
#define R18	18 // ! platform-specific, do not use
#define R19	19 // * litBase
#define R20	20 // * vmBase
#define R21	21 // * opStack
#define R22	22 // * opStackTop
#define R23	23 // * instructionPointers
#define R24	24 // * programStack
#define R25	25 // * programStackBottom
#define R26	26 // * dataBase
#define R27	27 // * dataMask
#define R28	28 // * procBase
#define R29	29 // * FP
#define R30	30 // link register
#define R31	31 // stack or zero

#define FP	R29
#define LR	R30
#define SP	R31

#define rOPSTACKSHIFT   R17

#define rLITBASE		R19
#define rVMBASE			R20
#define rOPSTACK		R21
#define rOPSTACKTOP		R22
#define rINSPOINTERS	R23
#define rPSTACK			R24
#define rPSTACKBOTTOM	R25
#define rDATABASE		R26
#define rDATAMASK		R27
#define rPROCBASE		R28

#define S0      0
#define S1      1

typedef enum
{
	LIT_LITBASE,
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
	//MOP_MODI4,
	//MOP_MODU4,
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
static	uint32_t	savedOffset[ OFFSET_T_LAST ];


// literal pool
#ifdef USE_LITERAL_POOL

#define MAX_LITERALS  4096
#define LIT_HASH_SIZE 512
#define LIT_HASH_FUNC(v) ((v*157)&(LIT_HASH_SIZE-1))

typedef struct literal_s {
	struct literal_s *next;
	uint32_t value;
} literal_t;

static uint32_t numLiterals;
static literal_t *litHash[ LIT_HASH_SIZE ];
static literal_t litList[ MAX_LITERALS ];

static void VM_InitLiterals( void )
{
	Com_Memset( litHash, 0, sizeof( litHash ) );
	Com_Memset( litList, 0, sizeof( litList ) );
	numLiterals = 0;
}

static int VM_SearchLiteral( const uint32_t value )
{
	uint32_t h = LIT_HASH_FUNC( value );
	literal_t *lt = litHash[ h ];

	while ( lt ) {
		if ( lt->value == value ) {
			return (lt - &litList[0]);
		}
		lt = lt->next;
	}

	if ( numLiterals >= ARRAY_LEN( litList ) ) {
		return -1;
	}

	lt = &litList[ numLiterals ];
	lt->next = litHash[ h ];
	lt->value = value;
	litHash[ h ] = lt;

	return numLiterals++;
}
#endif // USE_LITERAL_POOL


#define DROP( reason, args... ) \
	do { \
		VM_FreeBuffers(); \
		Com_Error( ERR_DROP, "%s: " reason, __func__, ##args ); \
	} while(0)


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

#define NOP                     YIELD
#define YIELD                   ( (0b1101010100<<22) | (0b000011<<16) | (0b00100000<<8) | 0b00111111 )
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

// LSL (immediate in range 1..31)
#define LSL32i(Rd, Rn, shift)   ( (0<<31) | (0b10<<29) | (0b100110<<23) | (0<<22) | (((-(shift))&31)<<16) | ((31-(shift))<<10) | ((Rn)<<5) | Rd )

// LSR (immediate in range 1..31)
#define LSR32i(Rd, Rn, shift)   ( (0<<31) | (0b10<<29) | (0b100110<<23) | (0<<22) | ((shift)<<16) | (31<<10) | ((Rn)<<5) | Rd )

// ASR (immediate in range 1..31)
#define ASR32i(Rd, Rn, shift)   ( (0<<31) | (0b00<<29) | (0b100110<<23) | (0<<22) | ((shift)<<16) | (31<<10) | ((Rn)<<5) | Rd )


// LDP - load pair of registers with signed offset
#define LDP32(Rt1,Rt2,Rn,simm7)     ( 0b00<<30 | 0b101<<27 | 0<<26 | 0b010<<23 | 1<<22 /*L*/ | ((((simm7)>>2)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )
#define LDP64(Rt1,Rt2,Rn,simm7)     ( 0b10<<30 | 0b101<<27 | 0<<26 | 0b010<<23 | 1<<22 /*L*/ | ((((simm7)>>3)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )

// LDP - load pair of registers with post-index
#define LDP32post(Rt1,Rt2,Rn,simm7) ( 0b00<<30 | 0b101<<27 | 0<<26 | 0b001<<23 | 1<<22 /*L*/ | ((((simm7)>>2)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )
#define LDP64post(Rt1,Rt2,Rn,simm7) ( 0b10<<30 | 0b101<<27 | 0<<26 | 0b001<<23 | 1<<22 /*L*/ | ((((simm7)>>3)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )

// LDP - load pair of registers with pre-index
#define LDP32pre(Rt1,Rt2,Rn,simm7)  ( 0b00<<30 | 0b101<<27 | 0<<26 | 0b011<<23 | 1<<22 /*L*/ | ((((simm7)>>2)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )
#define LDP64pre(Rt1,Rt2,Rn,simm7)  ( 0b10<<30 | 0b101<<27 | 0<<26 | 0b011<<23 | 1<<22 /*L*/ | ((((simm7)>>3)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )

// STP - store pair of registers with signed offset
#define STP32(Rt1,Rt2,Rn,simm7)     ( 0b00<<30 | 0b101<<27 | 0<<26 | 0b010<<23 | 0<<22 /*L*/ | ((((simm7)>>2)&0x7F)<<15) | ((Rt2)<<10) | ((Rn)<<5) | (Rt1) )
#define STP64(Rt1,Rt2,Rn,simm7)     ( 0b10<<30 | 0b101<<27 | 0<<26 | 0b010<<23 | 0<<22 /*L*/ | ((((simm7)>>3)&0x7F)<<15) | ((Rt2)<<10) | ((Rn)<<5) | (Rt1) )

// STP - load pair of registers with post-index
#define STP32post(Rt1,Rt2,Rn,simm7) ( 0b00<<30 | 0b101<<27 | 0<<26 | 0b001<<23 | 0<<22 /*L*/ | ((((simm7)>>2)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )
#define STP64post(Rt1,Rt2,Rn,simm7) ( 0b10<<30 | 0b101<<27 | 0<<26 | 0b001<<23 | 0<<22 /*L*/ | ((((simm7)>>3)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )

// LDP - load pair of registers with pre-index
#define STP32pre(Rt1,Rt2,Rn,simm7)  ( 0b00<<30 | 0b101<<27 | 0<<26 | 0b011<<23 | 0<<22 /*L*/ | ((((simm7)>>2)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )
#define STP64pre(Rt1,Rt2,Rn,simm7)  ( 0b10<<30 | 0b101<<27 | 0<<26 | 0b011<<23 | 0<<22 /*L*/ | ((((simm7)>>3)&0x7F)<<15) | Rt2<<10 | Rn<<5 | Rt1 )


#define LDR32iwpost(Rt, Rn, simm9)  ( (0b10<<30) | 0b111000010<<21 | ((simm9&511) << 12) | (0b01 << 10) | (Rn << 5) | Rt )
#define LDR32iwpre(Rt, Rn, simm9)   ( (0b10<<30) | 0b111000010<<21 | ((simm9&511) << 12) | (0b11 << 10) | (Rn << 5) | Rt )
#define LDR32ipre(Rt, Rn, simm9)    ( (0b10<<30) | 0b111000010<<21 | ((simm9&511) << 12) | (0b00 << 10) | (Rn << 5) | Rt )

//#define LDR32(Rt, Rn, Rm)       ( (0b10<<30) | (0b111000011<<21) | (Rm<<16) | (0b011<<13) /*LSL*/  | (0<<12) /*#0*/ | (0b10<<10) | (Rn << 5) | Rt )
#define LDR32(Rt, Rn, Rm)       ( (0b10<<30) | (0b111000011<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn << 5) | Rt )
#define LDRH32(Rt, Rn, Rm)      ( (0b01<<30) | (0b111000011<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn << 5) | Rt )
#define LDRB32(Rt, Rn, Rm)      ( (0b00<<30) | (0b111000011<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*#0*/ | (0b10<<10) | (Rn << 5) | Rt )

#define LDR32i(Rt, Rn, imm12)   ( (0b10<<30) | (0b11100101<<22) |  (imm12_scale((imm12),2) << 10) | (Rn << 5) | Rt )
#define LDRH32i(Rt, Rn, imm12)  ( (0b01<<30) | (0b11100101<<22) |  (imm12_scale((imm12),1) << 10) | (Rn << 5) | Rt )
#define LDRB32i(Rt, Rn, imm12)  ( (0b00<<30) | (0b11100101<<22) |  (imm12_scale((imm12),0) << 10) | (Rn << 5) | Rt )

#define LDRSB32(Rt, Rn, Rm)     ( (0b00<<30) | (0b111000<<24) | (0b11<<22) /*opc*/ | (1<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*S*/ | (0b10<<10)  | (Rn<<5) | Rt )
#define LDRSH32(Rt, Rn, Rm)     ( (0b01<<30) | (0b111000<<24) | (0b11<<22) /*opc*/ | (1<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*S*/ | (0b10<<10)  | (Rn<<5) | Rt )

#define LDRSB32i(Rt, Rn, imm12) ( (0b00<<30) | (0b111001<<24) | (0b11<<22) | (imm12_scale(imm12,0)<<10) | (Rn<<5) | Rt )
#define LDRSH32i(Rt, Rn, imm12) ( (0b01<<30) | (0b111001<<24) | (0b11<<22) | (imm12_scale(imm12,1)<<10) | (Rn<<5) | Rt )

#define LDRSWi(Rt, Rn, imm12)   ( (0b10<<30) | (0b111001<<24) | (0b10<<22) | (imm12_scale(imm12,2)<<10) | ((Rn)<<5) | (Rt) )

//#define LDR32_4(Rt, Rn, Rm)        ( (0b10<<30) | (0b111000011<<21) | (Rm<<16) | (0b011<<13) /*LSL*/ | (1<<12) /*#2*/ | (0b10<<10) | (Rn << 5) | Rt )
#define LDR64_8(Rt, Rn, Rm)        ( (0b11<<30) | (0b111000011<<21) | (Rm<<16) | (0b011<<13) /*LSL*/ | (1<<12) /*#3*/ | (0b10<<10) | (Rn << 5) | Rt )

#define LDR64iwpost(Rt, Rn, simm9) ( (0b11<<30) | (0b111000010<<21) | ((simm9&511) << 12) | (0b01 << 10) | (Rn << 5) | Rt )
#define LDR64iwpre(Rt, Rn, simm9)  ( (0b11<<30) | (0b111000010<<21) | ((simm9&511) << 12) | (0b11 << 10) | (Rn << 5) | Rt )
#define LDR64i(Rt, Rn, imm12)      ( (0b11<<30) | (0b11100101<<22) |  (imm12_scale(imm12,3) << 10) | (Rn << 5) | Rt )

#define STR32iwpost(Rt, Rn, simm9) ( (0b10<<30) | (0b111000000<<21) | ((simm9&511) << 12) | (0b01<<10) | (Rn<<5) | Rt )
#define STR32iwpre(Rt, Rn, simm9)  ( (0b10<<30) | (0b111000000<<21) | ((simm9&511) << 12) | (0b11<<10) | (Rn<<5) | Rt )
#define STR32ipre(Rt, Rn, simm9)   ( (0b10<<30) | (0b111000000<<21) | ((simm9&511) << 12) | (0b00<<10) | (Rn<<5) | Rt )

#define STR32i(Rt, Rn, imm12)      ( (0b10<<30) | (0b11100100<<22) |  (imm12_scale(imm12,2) << 10) | (Rn << 5) | Rt )

#define STR64iwpost(Rt, Rn, simm9) ( (0b11<<30) | (0b111000000<<21) | ((simm9&511) << 12) | (0b01<<10) | (Rn<<5) | Rt )
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

// CBZ - Compare and Branch on Zero
#define CBZ32(Rt, simm19)          ( (0<<31) | (0b011010<<25) | (0<<24) /*op*/ | (encode_offset19(simm19)<<5) | Rt )

// CBNZ - Compare and Branch on Nonzero
#define CBNZ32(Rt, simm19)         ( (0<<31) | (0b011010<<25) | (1<<24) /*op*/ | (encode_offset19(simm19)<<5) | Rt )

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

// Prefetch Memory (immediate)
#define PRFMi(Rt, Rn, imm12)       ( (0b11111<<27) | (0b00110<<22) | (((imm12>>3)&0xFFF)<<10) | ((Rn)<<5) | (Rt) )
// Rt register fields:
// policy
#define KEEP 0
#define STRM 1 // non-temporal hint
// target cache
#define L1 (0b00<<1)
#define L2 (0b01<<1)
#define L3 (0b10<<1)
// type
#define PLD (0b00<<3) // prefetch for load
#define PLI (0b01<<3) // preload instructions
#define PST (0b10<<3) // prefetch for store


#define FABS(Sd, Sn)             ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b00000110000<<10) | (Sn<<5) | Sd )
#define FSQRT(Sd, Sn)            ( (0b000<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b00001110000<<10) | (Sn<<5) | Sd )
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

// move scalar to scalar
#define FMOV(Sd, Sn)             ( (0<<31) | (0b00<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b00<<19) /*rmode*/ | (0b000<<16) /*opcode*/ | (0b010000<<10) | (Sn<<5) | Sd )
// move scalar to general
#define FMOVgs(Rd, Sn)           ( (0<<31) | (0b00<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b00<<19) /*rmode*/ | (0b110<<16) /*opcode*/ | (0b000000<<10) | (Sn<<5) | Rd )
// move general to scalar
#define FMOVsg(Sd, Rn)           ( (0<<31) | (0b00<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | (0b00<<19) /*rmode*/ | (0b111<<16) /*opcode*/ | (0b000000<<10) | (Rn<<5) | Sd )
// move immediate to scalar
#define FMOVi(Sd, imm8)          ( (0<<31) | (0b00<<29) | (0b11110<<24) | (0b00<<22) | (1<<21) | ((imm8)<<13) | (0b100<<10) | (0b00000<<5) | Sd )

#define VLDR(St, Rn, Rm)         ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b00<<24) | (0b01<<22) /*opc*/ | (1<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*S*/ | (0b10<<10) | (Rn<<5) | St )
#define VSTR(St, Rn, Rm)         ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b00<<24) | (0b00<<22) /*opc*/ | (1<<21) | (Rm<<16) | (0b010<<13) /*UXTW*/ | (0<<12) /*S*/ | (0b10<<10) | (Rn<<5) | St )

#define VLDRi(St, Rn, imm12)     ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b01<<24) | (0b01<<22) /*opc*/ | (imm12_scale(imm12,2) << 10) | (Rn<<5) | St )
#define VSTRi(St, Rn, imm12)     ( (0b10<<30) | (0b111<<27) | (1<<26) | (0b01<<24) | (0b00<<22) /*opc*/ | (imm12_scale(imm12,2) << 10) | (Rn<<5) | St )


static qboolean can_encode_imm12( const uint32_t imm12, const uint32_t scale )
{
	const uint32_t mask = (1<<scale) - 1;

	if ( imm12 & mask || imm12 >= 4096 * (1 << scale) )
		return qfalse;

	return qtrue;
}


static uint32_t imm12_scale( const uint32_t imm12, const uint32_t scale )
{
	const uint32_t mask = (1<<scale) - 1;

	if ( imm12 & mask || imm12 >= 4096 * (1 << scale) )
		DROP( "can't encode offset %i with scale %i", imm12, (1 << scale) );

	return imm12 >> scale;
}


// check if we can encode single-precision scalar immediate
static qboolean can_encode_f32_imm( const uint32_t v )
{
	uint32_t exp3 = (v >> 25) & ((1<<6)-1);

	if ( exp3 != 0x20 && exp3 != 0x1F )
		return qfalse;

	if ( v & ((1<<19)-1) )
		return qfalse;

	return qtrue;
}


static uint32_t encode_f32_imm( const uint32_t v )
{
	return  (((v >> 31) & 0x1) << 7) | (((v >> 23) & 0x7) << 4) | ((v >> 19) & 0xF);
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
#ifdef USE_LITERAL_POOL
	int litIndex;
#endif
	if ( imm <= 0xFFFF ) {
		emit( MOVZ32( reg, imm & 0xFFFF ) );
		return;
	}

	if ( ( imm & 0xFFFF ) == 0 ) {
		emit( MOVZ32_16( reg, (imm >> 16)&0xFFFF ) );
		return;
	}

#ifdef USE_LITERAL_POOL
	litIndex = VM_SearchLiteral( imm );
	if ( litIndex >= 0 ) {
		emit( LDR32i( reg, rLITBASE, (litIndex*4) ) );
		return;
	}
#endif

	emit( MOVZ32( reg, imm & 0xFFFF ) );
	emit( MOVK32_16( reg, (imm >> 16)&0xFFFF ) );
}


static uint32_t alloc_rx( uint32_t pref );
static uint32_t alloc_sx( uint32_t pref );

// ---------------- register allocation --------------------

// register allocation preferences

#define FORCED 0x20 // load function must return specified register
#define TEMP   0x40 // hint: temporary allocation, will not be pushed on stack

#define RMASK  0x1F

// general-purpose register list available for dynamic allocation
static const uint32_t rx_list[] = {
	R0, R1, R2, R3, // R0-R3 are required minimum
	R4, R5, R6, R7,
	R8, R9, R10, R11,
	R12, R13, R14, R15,
	R16, R17
};

// FPU scalar register list available for dynamic allocation
static const uint32_t sx_list[] = {
	S0, S1, 2, 3, 4, 5, 6, 7, // S0 and S1 are required minimum
	// 8..15 must be preserved
	16, 17, 18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 30, 31
};

// types of items on the stack
typedef enum {
	TYPE_RAW,        // stored value
	TYPE_CONST,      // constant
	TYPE_LOCAL,      // address
	TYPE_RX,         // volatile - general-purpose register
	TYPE_RX_SYSCALL, // volatile - R0, syscall return value
	TYPE_SX,         // volatile - FPU scalar register
} opstack_value_t;


typedef struct opstack_s {
	uint32_t value;
	uint32_t offset;
	opstack_value_t type;
	int safe_arg;
} opstack_t;


static int opstack;
static opstack_t opstackv[ (PROC_OPSTACK_SIZE + 1) * 4 ];

static uint32_t rx_mask;
static uint32_t sx_mask;

// masked register can't be allocated or flushed to opstack on register pressure

static qboolean is_masked_rx( const uint32_t reg )
{
	if ( rx_mask & (1 << reg) )
		return qtrue;
	else
		return qfalse;
}


static void mask_rx( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( rx_mask & (1 << reg) )
		DROP( "register #%i is already masked", reg );
#endif

	rx_mask |= (1 << reg);
}


static void mask_sx( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( sx_mask & (1 << reg) )
		DROP( "register #%i is already masked", reg );
#endif

	sx_mask |= (1 << reg);
}


static void unmask_rx( uint32_t reg )
{
	rx_mask &= ~(1 << reg);
}


static void unmask_sx( uint32_t reg )
{
	sx_mask &= ~(1 << reg);
}


static void emit_MOVSi( uint32_t reg, uint32_t imm )
{
	uint32_t tr;

#ifdef USE_LITERAL_POOL
	int litIndex;
#endif
	if ( imm == 0 ) {
		emit( FMOVsg( reg, WZR ) );
		return;
	}

	if ( can_encode_f32_imm( imm ) ) {
		emit( FMOVi( reg, encode_f32_imm( imm ) ) );
		return;
	}

#ifdef USE_LITERAL_POOL
	litIndex = VM_SearchLiteral( imm );
	if ( litIndex >= 0 ) {
		emit( VLDRi( reg, rLITBASE, (litIndex*4) ) );
		return;
	}
#endif

	tr = alloc_rx( R2 | TEMP );
	emit_MOVRi(tr, imm);    // tr = ci->value
	emit(FMOVsg(reg, tr));  // sX = tr
	unmask_rx( tr );
}


static void flush_item( opstack_t *it )
{
	uint32_t rx;

	switch ( it->type ) {

		case TYPE_RX:
			emit(STR32i(it->value, rOPSTACK, it->offset)); // *opstack = rX
			unmask_rx( it->value );
			break;

		case TYPE_SX:
			emit(VSTRi(it->value, rOPSTACK, it->offset));  // *opstack = sX
			unmask_sx( it->value );
			break;

		case TYPE_CONST:
			rx = alloc_rx( R2 | TEMP );
			emit_MOVRi(rx, it->value);              // r2 = const
			emit(STR32i(rx, rOPSTACK, it->offset)); // *opstack = r2
			unmask_rx( rx );
			break;

		case TYPE_LOCAL:
			rx = alloc_rx( R2 | TEMP );
			if ( it->value < 4096 ) {
				emit(ADD32i(rx, rPSTACK, it->value)); // r2 = pstack + arg
			} else {
				emit_MOVRi(rx, it->value);            // r2 = arg;
				emit(ADD32(rx, rPSTACK, rx));         // r2 = pstack + r2
			}
			emit(STR32i(rx, rOPSTACK, it->offset));   // *opstack = r2
			unmask_rx( rx );
			break;

		case TYPE_RX_SYSCALL:
			// discard R0
			break;

		default:
			break;
	}

	it->type = TYPE_RAW;
	it->safe_arg = 0;

}


static void init_opstack( void )
{
	opstack = 0;
	rx_mask = 0;
	sx_mask = 0;
	Com_Memset( &opstackv[0], 0, sizeof( opstackv ) );
}


static qboolean scalar_on_top( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE * 4 || opstack <= 0 )
		DROP( "bad opstack %i", opstack );
#endif
	if ( opstackv[ opstack ].type == TYPE_SX )
		return qtrue;
	else
		return qfalse;
}


static int is_safe_arg( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE * 4 || opstack <= 0 )
		DROP( "bad opstack %i", opstack );
#endif
	return opstackv[ opstack ].safe_arg;
}


static void inc_opstack( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE * 4 )
		DROP( "opstack overflow - %i", opstack );
#endif

	opstack += 4;

#ifdef DEBUG_VM
	if ( opstackv[ opstack ].type != TYPE_RAW )
		DROP( "bad item type %i at opstack %i", opstackv[ opstack ].type, opstack );
#endif
}


static void dec_opstack( void )
{
#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "opstack underflow - %i", opstack );
#endif

	flush_item( opstackv + opstack ); // in case if it was not consumed by any load function

	opstack -= 4;
}


// returns bitmask of registers present on opstack
static uint32_t build_mask( uint32_t reg_type )
{
	uint32_t mask = 0;
	int i;
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == reg_type ) {
			mask |= (1 << it->value);
		}
	}
	return mask;
}


// integer register allocation
static uint32_t alloc_rx( uint32_t pref )
{
	uint32_t n, reg = pref & RMASK;
	int i;

#ifdef DYN_ALLOC_RX
	if ( (pref & FORCED) == 0 ) {
		uint32_t mask = rx_mask | build_mask( TYPE_RX );

		// pickup first free register from rx_list
		for ( i = 0; i < ARRAY_LEN( rx_list ) ; i++ ) {
			n = rx_list[ i ];
			if  ( mask & (1 << n) ) {
				continue;
			}
			mask_rx( n );
			return n;
		}

		// no free registers, flush bottom of the stack
		for ( i = 0; i <= opstack; i++ ) {
			opstack_t *it = opstackv + i;
			if ( it->type == TYPE_RX ) {
				n = it->value;
				// skip masked registers
				if ( rx_mask & (1 << n) ) {
					continue;
				}
				flush_item( it );
				mask_rx( n );
				return n;
			}
		}
		DROP( "no free registers, pref %x, opStack %i, mask %04x", pref, opstack, rx_mask );
	}
#endif

	// FORCED option: find and flush target register
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_RX && it->value == reg ) {
			flush_item( it );
			break;
		}
	}

#ifdef DEBUG_VM
	if ( rx_mask & ( 1 << reg ) )
		DROP( "forced register R%i is already masked!", reg );
#endif

	mask_rx( reg );
	return reg;
}


// scalar register allocation
static uint32_t alloc_sx( uint32_t pref )
{
	uint32_t n, reg = pref & RMASK;
	int i;

#ifdef DYN_ALLOC_SX
	if ( (pref & FORCED) == 0 ) {
		uint32_t mask = sx_mask | build_mask( TYPE_SX );

		// pickup first free register from sx_list
		for ( i = 0; i < ARRAY_LEN( sx_list ) ; i++ ) {
			n = sx_list[ i ];
			if  ( mask & (1 << n) ) {
				continue;
			}
			mask_sx( n );
			return n;
		}

		// no free registers, flush bottom of the stack
		for ( i = 0; i <= opstack; i++ ) {
			opstack_t *it = opstackv + i;
			if ( it->type == TYPE_SX ) {
				n = it->value;
				// skip masked registers
				if ( sx_mask & (1 << n) ) {
					continue;
				}
				flush_item( it );
				mask_sx( n );
				return n;
			}
		}
	}
	DROP( "no free registers, pref %x, opStack %i, mask %04x", pref, opstack, sx_mask );
#endif

	// FORCED option: find and flush target register
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_SX && it->value == reg ) {
			flush_item( it );
			break;
		}
	}

#ifdef DEBUG_VM
	if ( sx_mask & ( 1 << reg ) )
		DROP( "forced register S%i is already masked!", reg );
#endif

	mask_sx( reg );
	return reg;
}


static void flush_volatile( void )
{
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_RX || it->type == TYPE_SX || it->type == TYPE_RX_SYSCALL ) {
			flush_item( it );
		}
	}
}


static void store_rx_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack );
#endif

	it->type = TYPE_RX;
	it->offset = opstack;
	it->value = reg;
	it->safe_arg = 0;

	unmask_rx( reg ); // so it can be flushed on demand
}


static void store_syscall_opstack( void )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack );
#endif

	it->type = TYPE_RX_SYSCALL;
	it->offset = opstack;
	it->value = R0;
	it->safe_arg = 0;
}


static void store_sx_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack );
#endif

	it->type = TYPE_SX;
	it->offset = opstack;
	it->value = reg;
	it->safe_arg = 0;

	unmask_sx( reg ); // so it can be flushed on demand
}


static void store_item_opstack( instruction_t *inst )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack );
#endif
	switch ( inst->op ) {
		case OP_CONST: it->type = TYPE_CONST; break;
		case OP_LOCAL: it->type = TYPE_LOCAL; break;
		default: DROP( "incorrect opcode %i", inst->op );
	}

	it->offset = opstack;
	it->value = inst->value;
	it->safe_arg = inst->safe;
}


// we must unmask register manually after allocation/loading
static uint32_t load_rx_opstack( vm_t *vm, uint32_t pref )
{
	opstack_t *it = opstackv + opstack;
	uint32_t reg = pref & RMASK;
	uint32_t rx;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack );
#endif

	if ( it->type == TYPE_RX || it->type == TYPE_RX_SYSCALL ) {
#ifdef DYN_ALLOC_RX
		if ( !(pref & FORCED) ) {
			mask_rx( it->value );
			it->type = TYPE_RAW;
			return it->value; // return current register
		}
#endif
		if ( it->value == reg ) {
			mask_rx( it->value );
			it->type = TYPE_RAW;
			return reg;
		} else {
			// allocate target register
			reg = alloc_rx( pref );
			// copy source to target
			emit(MOV32(reg, it->value));
			// release source
			unmask_rx( it->value );
			it->type = TYPE_RAW;
			return reg;
		}
	}

	// scalar register on the stack
	if ( it->type == TYPE_SX ) {
		// move from scalar to general-purpose register
		reg = alloc_rx( pref );
		emit(FMOVgs(reg, it->value));
		// release source
		unmask_sx( it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	if ( it->type == TYPE_CONST ) {
		// move constant to general-purpose register
		reg = alloc_rx( pref );
		emit_MOVRi( reg, it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	if ( it->type == TYPE_LOCAL ) {
		reg = alloc_rx( pref );
		if ( it->value < 4096 ) {
			emit(ADD32i(reg, rPSTACK, it->value)); // rX = pstack + arg
		} else {
			rx = alloc_rx( R2 | TEMP );
			emit_MOVRi(rx, it->value);             // r2 = arg;
			emit(ADD32(reg, rPSTACK, rx));         // rX = pstack + r2
			unmask_rx( rx );
		}
		it->type = TYPE_RAW;
		return reg;
	}

	// default raw type, explicit load
	reg = alloc_rx( pref );
	emit(LDR32i(reg, rOPSTACK, opstack)); // rX = *opstack
	it->type = TYPE_RAW;
	return reg;
}


// we must unmask register manually after allocation/loading
static uint32_t load_sx_opstack( vm_t *vm, uint32_t pref )
{
	opstack_t *it = opstackv + opstack;
	uint32_t reg = pref & RMASK;
	uint32_t rx;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack );
#endif

	// scalar register on the stack
	if ( it->type == TYPE_SX ) {
#ifdef DYN_ALLOC_SX
		if ( !(pref & FORCED) ) {
			mask_sx( it->value );
			it->type = TYPE_RAW;
			return it->value; // return current register
		}
#endif
		if ( it->value == reg ) {
			mask_sx( it->value );
			it->type = TYPE_RAW;
			return reg;
		} else {
			// allocate target register
			reg = alloc_sx( pref );
			// copy source to target
			emit(FMOV(reg, it->value));
			// release source
			unmask_sx( it->value );
			it->type = TYPE_RAW;
			return reg;
		}
	}

	// integer register on the stack
	if ( it->type == TYPE_RX || it->type == TYPE_RX_SYSCALL ) {
		// move from general-purpose to scalar register
		// should never happen with FPU type promotion, except syscalls
		reg = alloc_sx( pref );
		emit( FMOVsg( reg, it->value ) );
		// release source
		unmask_rx( it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	if ( it->type == TYPE_CONST ) {
		// move constant to scalar register
		reg = alloc_sx( pref );
		emit_MOVSi( reg, it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	if ( it->type == TYPE_LOCAL ) {
		reg = alloc_sx( pref );
		rx = alloc_rx( R2 | TEMP );
		if ( it->value < 4096 ) {
			emit(ADD32i(rx, rPSTACK, it->value)); // r2 = pstack + arg
		} else {
			emit_MOVRi(rx, it->value);            // r2 = arg;
			emit(ADD32(rx, rPSTACK, rx));         // r2 = pstack + r2
		}
		emit( FMOVsg( reg, rx ) );
		unmask_rx( rx );
		it->type = TYPE_RAW;
		return reg;
	}

	// default raw type, explicit load
	reg = alloc_sx( pref );
	emit( VLDRi( reg, rOPSTACK, opstack ) ); // sX = *opstack
	it->type = TYPE_RAW;
	return reg;
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

	DROP( "unexpected op %i", op );
}


static uint32_t encode_offset26( uint32_t ofs )
{
	const uint32_t x = ofs >> 2;
	const uint32_t t = x >> 26;

	if ( ( ( t != 0x0F && t != 0x00 ) || ( ofs & 3 ) ) && pass != 0 )
		DROP( "can't encode %i", ofs );

	return x & 0x03FFFFFF;
}


static uint32_t encode_offset19( uint32_t ofs )
{
	const uint32_t x = ofs >> 2;
	const uint32_t t = x >> 19;

	if ( ( ( t != 0x7FF && t != 0x00 ) || ( ofs & 3 ) ) && pass != 0 )
		DROP( "can't encode %i", ofs );

	return x & 0x7FFFF;
}


static void emitAlign( const uint32_t align )
{
	while ( compiledOfs & (align-1) )
		emit(NOP);
}


static void emitFuncOffset( vm_t *vm, offset_t func )
{
	uint32_t offset = savedOffset[ func ] - compiledOfs;

	emit( BL( offset ) );
}


static void emit_CheckReg( vm_t *vm, instruction_t *ins, uint32_t reg )
{
	if ( ins->safe )
		return;
#ifdef DEBUG_VM
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
#else
	if ( vm_rtChecks->integer & 8 || vm->forceDataMask ) {
		emit(AND32(reg, rDATAMASK, reg)); // rN = rN & rDATAMASK
	}
#endif
}


static void emit_CheckJump( vm_t *vm, uint32_t reg, int proc_base, int proc_len )
{
	qboolean masked = is_masked_rx( reg );
	uint32_t rx[2];

	if ( ( vm_rtChecks->integer & 4 ) == 0 ) {
		return;
	}

	if ( !masked ) {
		mask_rx( reg ); // so allocator will not chose it
	}

	if ( proc_base != -1 ) {
		// allow jump within local function scope only
		rx[0] = alloc_rx( R2 | TEMP );
		if ( proc_base < 4096 )
			emit(SUB32i(rx[0], reg, proc_base)); // r2 = ip - procBase
		else {
			emit_MOVRi(rx[0], proc_base);        // r2 = procBase
			emit(SUB32(rx[0], reg, rx[0]));      // r2 = ip - R2
		}
		// (ip > proc_len) ?
		if ( proc_len < 4096 ) {
			emit(CMP32i(rx[0], proc_len));       // cmp r2, proclen  
		} else {
			rx[1] = alloc_rx( R1 | TEMP );
			emit_MOVRi(rx[1], proc_len);         // r1 = procLen
			emit(CMP32(rx[0], rx[1]));           // cmp r2, r1
			unmask_rx( rx[1] );
		}
		unmask_rx( rx[0] );
		emit(Bcond(LS, +8)); // jump over if unsigned less or same
		emitFuncOffset(vm, FUNC_OUTJ);
	} else {
		// check if reg >= header->instructionCount
		rx[0] = alloc_rx( R2 | TEMP );
		emit(LDR32i(rx[0], rVMBASE, offsetof(vm_t, instructionCount))); // r2 = vm->instructionCount
		emit(CMP32(reg, rx[0]));       // cmp reg, r2
		emit(Bcond(LO, +8));           // jump over if unsigned less
		emitFuncOffset(vm, FUNC_OUTJ); // error function
		unmask_rx( rx[0] );
	}

	if ( !masked ) {
		unmask_rx( reg );
	}
}


static void emit_CheckProc( vm_t *vm, instruction_t *inst )
{
	// programStack overflow check
	if ( vm_rtChecks->integer & 1 ) {
		emit(CMP32(rPSTACK, rPSTACKBOTTOM));  // check if pStack < vm->stackBottom
		emit(Bcond(GE, +8));                  // jump over if signed higher or equal
		emitFuncOffset( vm, FUNC_PSOF );      // error function
	}

	// opStack overflow check
	if ( vm_rtChecks->integer & 2 ) {
		uint32_t n = inst->opStack;        // proc->opStack carries max.used opStack value
		uint32_t rx = alloc_rx( R2 | TEMP );
		if ( n < 4096 ) {
			emit(ADD64i(rx, rOPSTACK, n)); // r2 = opstack + max.opStack
		} else {
			emit_MOVRi(rx, n);             // r2 = max.opStack
			emit(ADD64(rx, rOPSTACK, rx)); // r2 = opStack + r2
		}
		emit(CMP64(rx, rOPSTACKTOP));      // check if r2 > vm->opstackTop
		emit(Bcond(LS, +8));               // jump over if unsigned less or equal
		emitFuncOffset( vm, FUNC_OSOF );
		unmask_rx( rx );
	}
}


static void emitCallFunc( vm_t *vm )
{
	int i;
	init_opstack(); // to avoid any side-effects on emit_CheckJump()

savedOffset[ FUNC_CALL ] = compiledOfs; // to jump from OP_CALL

	emit(CMP32i(R0, 0)); // check if syscall
	emit(Bcond(LT, savedOffset[ FUNC_SYSC ] - compiledOfs));

	// check if R0 >= header->instructionCount
	emit_CheckJump( vm, R0, -1, 0 );

	// local function call
	emit(LDR64_8(R16, rINSPOINTERS, R0)); // r16 = instructionPointers[ r0 ]
	emit(BR(R16));
	emit(BRK(0));

	// syscall
savedOffset[ FUNC_SYSC ] = compiledOfs; // to jump from OP_CALL

	emit(MVN32(R0, R0));   // r0 = ~r0
	emit(ADD32i(rOPSTACKSHIFT, rOPSTACKSHIFT, 4)); // opStackShift += 4

savedOffset[ FUNC_SYSF ] = compiledOfs; // to jump from ConstOptimize()

	emit(SUB64i(SP, SP, 128+16)); // SP -= (128 + 16)

	// save LR, opStackShift because they will be clobbered by BLR instruction
	emit(STP64(LR, rOPSTACKSHIFT, SP, 128)); // SP[128] = { LR, opStackShift }

	// modify VM stack pointer for recursive VM entry

	//currentVM->programStack = pstack - 8;
	emit(SUB32i(R1, rPSTACK, 8)); // r1 = pstack - 8
	emit(STR32i(R1, rVMBASE, offsetof(vm_t, programStack))); // vm->programStack = r1

	// sign-extend agruments starting from [procBase+8]
	// R0 is already zero-extended
#if 1
	emit(LDRSWi(R1, rPROCBASE, 8));
	emit(STP64(R0, R1, SP, 0));
	for ( i = 2 ; i < 16; i += 2 ) {
		//emit(LDRSWi(R0+i+0, rPROCBASE, 4+(i+0)*4));
		//emit(LDRSWi(R0+i+1, rPROCBASE, 4+(i+1)*4));
		//emit(STP64(R0+i+0, R0+i+1, SP, (i/2)*16));
		emit(LDRSWi(R0, rPROCBASE, 4+(i+0)*4));
		emit(LDRSWi(R1, rPROCBASE, 4+(i+1)*4));
		emit(STP64(R0, R1, SP, (i/2)*16));
	}
#else
	// sign-extend 15 agruments to R1-R15
	for ( i = 0; i < 15; i++ ) {
		emit(LDRSWi(R1+i, rPROCBASE, 8+i*4));
	}
	// store all extended registers to [SP+0]
	for ( i = 0; i < 16; i += 2 ) {
		emit(STP64(R0+i+0, R0+i+1, SP, (i/2)*16));
	}
#endif
	emit(ADD64i(R0, SP, 0)); // r0 = sp

	//ret = currentVM->systemCall( args );
	emit(LDR64i(R16, rVMBASE, offsetof(vm_t,systemCall))); // r16 = vm->systemCall
	emit(BLR(R16)); // call [r16]( r0 )

	// restore LR, opStackShift
	emit(LDP64(LR, rOPSTACKSHIFT, SP, 128)); // { LR, opStackShift } = SP[ 128 ]
	emit(ADD64i(SP, SP, 128+16)); // SP += 128 + 16

	// store return value
	emit(STR32(R0, rOPSTACK, rOPSTACKSHIFT)); // *(opstack+shift) = r0;

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


static void emitBlockCopy( vm_t *vm, const uint32_t count )
{
	if ( count == 12 ) // most common case - 3d vector copy
	{
		uint32_t rx[3];
		int safe_arg[2];

		rx[0] = load_rx_opstack( vm, R0 ); // src: r0 = *opstack;
		safe_arg[0] = is_safe_arg();
		dec_opstack(); // opstack -= 4

		rx[1] = load_rx_opstack( vm, R1 ); // dst: r1 = *opstack
		safe_arg[1] = is_safe_arg();
		dec_opstack(); // opstack -= 4

		if ( !safe_arg[0] )
			emit(AND32(rx[0], rx[0], rDATAMASK)); // r0 &= dataMask

		if ( !safe_arg[1] )
			emit(AND32(rx[1], rx[1], rDATAMASK)); // r1 &= dataMask

		emit(ADD64(rx[0], rx[0], rDATABASE));
		emit(ADD64(rx[1], rx[1], rDATABASE));

		rx[2] = alloc_rx( R2 | TEMP );

		// load/store double word
		//emit(LDR64iwpost(rx[2], rx[0], 8));
		//emit(STR64iwpost(rx[2], rx[1], 8));

		// load/store word
		//emit(LDR32iwpost(rx[2], rx[0], 4));
		//emit(STR32iwpost(rx[2], rx[1], 4));

		// load/store double word
		emit(LDR64i(rx[2], rx[0], 0));
		emit(STR64i(rx[2], rx[1], 0));

		// load/store word
		emit(LDR32i(rx[2], rx[0], 8));
		emit(STR32i(rx[2], rx[1], 8));

		unmask_rx( rx[2] );

		unmask_rx( rx[1] );
		unmask_rx( rx[0] );
		return;
	}

	// src: opStack[0]
	// dst: opstack[-4]
	load_rx_opstack( vm, R0 | FORCED ); dec_opstack(); // src: r0 = *opstack; opstack -= 4
	load_rx_opstack( vm, R1 | FORCED ); dec_opstack(); // dst: r1 = *opstack; opstack -= 4
	unmask_rx( R0 );
	unmask_rx( R1 );
	flush_volatile();
	emit_MOVRi(R2, count);    // r2 - count
	emit(MOV64(R3, rVMBASE)); // r3 - vmBase
	emit_MOVXi(R16, (intptr_t)VM_BlockCopy);
	emit(BLR(R16));
}


#ifdef DUMP_CODE
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
	uint32_t rx[3];
	uint32_t sx[2];
	uint32_t x;

	switch ( ni->op ) {

	case OP_LOAD4:
		x = ci->value;
#ifdef FPU_OPTIMIZE
		if ( ni->fpu ) {
			sx[0] = alloc_sx( S0 );
			if ( can_encode_imm12( x, 2 ) ) {
				emit(VLDRi(sx[0], rDATABASE, x));    // s0 = [dataBase + v]
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				emit_MOVRi(rx[1], x);                // r2 = 0x12345678
				emit(VLDR(sx[0], rDATABASE, rx[1])); // s0 = [dataBase + r2]
				unmask_rx( rx[1] );
			}
			inc_opstack(); store_sx_opstack(sx[0]); // opstack += 4; *opstack = s0
			ip += 1; // OP_LOAD4
			return qtrue;
		}
#endif
		rx[0] = alloc_rx( R0 );
		if ( can_encode_imm12( x, 2 ) ) {
			emit(LDR32i(rx[0], rDATABASE, x)); // r0 = [dataBase + v]
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRi(rx[1], x);                 // r2 = 0x12345678
			emit(LDR32(rx[0], rDATABASE, rx[1])); // r0 = [dataBase + r2]
			unmask_rx( rx[1] );
		}
		inc_opstack(); store_rx_opstack( rx[0] ); // opstack +=4 ; *opstack = r0;
		ip += 1; // OP_LOAD4
		return qtrue;

	case OP_LOAD2:
		x = ci->value;
		rx[0] = alloc_rx( R0 );
		if ( (ni+1)->op == OP_SEX16 ) {
			if ( can_encode_imm12( x, 1 ) ) {
				emit(LDRSH32i(rx[0], rDATABASE, x));    // r0 = (signed short*)[dataBase + v]
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				emit_MOVRi(rx[1], x);                   // r2 = 0x12345678
				emit(LDRSH32(rx[0], rDATABASE, rx[1])); // r0 = (signed short*)[dataBase + r2]
				unmask_rx( rx[1] );
			}
			ip += 1; // OP_SEX16
		} else {
			if ( can_encode_imm12( x, 1 ) ) {
				emit(LDRH32i(rx[0], rDATABASE, x));    // r0 = (unsigned short*)[dataBase + v]
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				emit_MOVRi(rx[1], x);
				emit(LDRH32(rx[0], rDATABASE, rx[1])); // r0 = (unsigned short*)[dataBase + tr]
				unmask_rx( rx[1] );
			}
		}
		inc_opstack(); store_rx_opstack( rx[0] ); // opstack +=4 ; *opstack = r0;
		ip += 1; // OP_LOAD2
		return qtrue;

	case OP_LOAD1:
		x = ci->value;
		rx[0] = alloc_rx( R0 );
		if ( (ni+1)->op == OP_SEX8 ) {
			if ( x < 4096 ) {
				emit(LDRSB32i(rx[0], rDATABASE, x)); // r0 = (signed byte*)[dataBase + v]
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				emit_MOVRi(rx[1], x);
				emit(LDRSB32(rx[0], rDATABASE, rx[1])); // r0 = (signed byte*)[dataBase + tr]
				unmask_rx( rx[1] );
			}
			ip += 1; // OP_SEX8
		} else {
			if ( x < 4096 ) {
				emit(LDRB32i(rx[0], rDATABASE, x));    // r0 = (byte*)[dataBase + v]
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				emit_MOVRi(rx[1], x);
				emit(LDRB32(rx[0], rDATABASE, rx[1])); // r0 = (byte*)[dataBase + tr]
				unmask_rx( rx[1] );
			}
		}
		inc_opstack(); store_rx_opstack( rx[0] ); // opstack +=4 ; *opstack = r0; 
		ip += 1; // OP_LOAD1
		return qtrue;

	case OP_STORE4:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
		emit_CheckReg(vm, ni, rx[0]);
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRi(rx[1], x);                 // r2 = 0x12345678
		emit(STR32(rx[1], rDATABASE, rx[0])); // [dataBase + r0] = r2;
		unmask_rx( rx[0] );
		unmask_rx( rx[1] );
		ip += 1; // OP_STORE4
		return qtrue;

	case OP_STORE2:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
		emit_CheckReg(vm, ni, rx[0]);
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRi(rx[1], x);                  // r2 = 0x12345678
		emit(STRH32(rx[1], rDATABASE, rx[0])); // (short)[dataBase + r0] = r2;
		unmask_rx( rx[0] );
		unmask_rx( rx[1] );
		ip += 1; // OP_STORE2
		return qtrue;

	case OP_STORE1:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
		emit_CheckReg(vm, ni, rx[0]);
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRi(rx[1], x);                  // r2 = 0x12345678
		emit(STRB32(rx[1], rDATABASE, rx[0])); // (byte)[dataBase + r0] = r2;
		unmask_rx( rx[0] );
		unmask_rx( rx[1] );
		ip += 1; // OP_STORE1
		return qtrue;

	case OP_ADD:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		if ( x < 4096 ) {
			emit(ADD32i(rx[0], rx[0], x));
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRi(rx[1], x);             // r2 = 0x12345678
			emit(ADD32(rx[0], rx[0], rx[1])); // r0 = r0 + r2
			unmask_rx( rx[1] );
		}
		store_rx_opstack( rx[0] ); // *opstack = r0
		ip += 1; // OP_ADD
		return qtrue;

	case OP_SUB:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		if ( x < 4096 ) {
			emit(SUB32i(rx[0], rx[0], x));
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRi(rx[1], x);             // r2 = 0x12345678  
			emit(SUB32(rx[0], rx[0], rx[1])); // r0 = r0 - r2
			unmask_rx( rx[1] );
		}
		store_rx_opstack( rx[0] ); // *opstack = r0
		ip += 1; // OP_SUB
		return qtrue;

	case OP_MULI:
	case OP_MULU:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRi(rx[1], x);              // r2 = 0x12345678
		emit(MUL32(rx[0], rx[0], rx[1]));  // r0 = r0 * r2
		unmask_rx( rx[1] );
		store_rx_opstack( rx[0] );         // *opstack = r0
		ip += 1; // OP_MULI|OP_MULU
		return qtrue;

	case OP_DIVI:
	case OP_DIVU:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRi(rx[1], x);
		if ( ni->op == OP_DIVI ) {
			emit(SDIV32(rx[0], rx[0], rx[1])); // r0 = r0 / r1
		} else {
			emit(UDIV32(rx[0], rx[0], rx[1])); // r0 = (unsigned)r0 / r1
		}
		unmask_rx( rx[1] );
		store_rx_opstack( rx[0] ); // *opstack = r0
		ip += 1;
		return qtrue;

	case OP_MODI:
	case OP_MODU:
		rx[0] = load_rx_opstack( vm, R0 );    // r0 = *opstack
		rx[1] = alloc_rx( R1 | TEMP );
		rx[2] = alloc_rx( R2 | TEMP );
		emit_MOVRi(rx[1], ci->value);
		if ( ni->op == OP_MODI ) {
			emit(SDIV32(rx[2], rx[0], rx[1])); // r2 = r0 / r1
		} else {
			emit(UDIV32(rx[2], rx[0], rx[1])); // r2 = (unsigned)r0 / r1
		}
		emit(MSUB32(rx[0], rx[1], rx[2], rx[0])); // r0 = r0 - r1 * r2
		store_rx_opstack( rx[0] ); // *opstack = r0
		unmask_rx( rx[1] );
		unmask_rx( rx[2] );
		ip += 1;
		return qtrue;

	case OP_LSH:
		x = ci->value;
		if ( x < 0 || x > 31 )
			break;
		if ( x ) {
			rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
			emit(LSL32i(rx[0], rx[0], x));     // r0 = r0 << x
			store_rx_opstack( rx[0] );         // *opstack = r0
		}
		ip += 1; // OP_LSH
		return qtrue;

	case OP_RSHI:
		x = ci->value;
		if ( x < 0 || x > 31 )
			break;
		if ( x ) {
			rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
			emit(ASR32i(rx[0], rx[0], x));     // r0 = r0 >> x
			store_rx_opstack( rx[0] );         // *opstack = r0
		}
		ip += 1; // OP_RSHI
		return qtrue;

	case OP_RSHU:
		x = ci->value;
		if ( x < 0 || x > 31 )
			break;
		if ( x ) {
			rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
			emit(LSR32i(rx[0], rx[0], x));     // r0 = (unsigned)r0 >> x
			store_rx_opstack( rx[0] );         // *opstack = r0
		}
		ip += 1; // OP_RSHU
		return qtrue;

	case OP_JUMP:
		emit(B(vm->instructionPointers[ ci->value ] - compiledOfs));
		ip += 1; // OP_JUMP
		return qtrue;

	case OP_CALL:
		inc_opstack(); // opstack += 4
		if ( ci->value == ~TRAP_SQRT ) {
			sx[0] = alloc_sx( S0 );
			emit(VLDRi(sx[0], rPROCBASE, 8)); // s0 = [procBase + 8]
			emit(FSQRT(sx[0], sx[0]));        // s0 = sqrtf( s0 )
			store_sx_opstack( sx[0] );        // *opstack = s0
			ip += 1; // OP_CALL
			return qtrue;
		}
		flush_volatile();
		if ( ci->value == ~TRAP_SIN || ci->value == ~TRAP_COS ) {
			sx[0] = alloc_sx( S0 );
			emit(VLDRi(sx[0], rPROCBASE, 8)); // s0 = [procBase + 8]
			if ( ci->value == ~TRAP_SIN )
				emit_MOVXi(R17, (intptr_t)sinf);
			else
				emit_MOVXi(R17, (intptr_t)cosf);
			emit(BLR(R17));
			store_sx_opstack( sx[0] );        // *opstack = s0
			ip += 1; // OP_CALL
			return qtrue;
		}
		if ( ci->value < 0 ) // syscall
		{
			x = ~ci->value;
			emit_MOVRi(R0, x); // r0 = syscall number
			emit_MOVRi(rOPSTACKSHIFT, opstack); // opStack shift
			emitFuncOffset( vm, FUNC_SYSF );
			ip += 1; // OP_CALL;
			store_syscall_opstack();
			return qtrue;
		}
		emit_MOVRi(rOPSTACKSHIFT, opstack-4); // opStack shift
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
		rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
		x = ci->value;
		if ( x == 0 && ( ni->op == OP_EQ || ni->op == OP_NE ) ) {
			if ( ni->op == OP_EQ )
				emit(CBZ32(rx[0], vm->instructionPointers[ ni->value ] - compiledOfs));
			else
				emit(CBNZ32(rx[0], vm->instructionPointers[ ni->value ] - compiledOfs));
		} else {
			if ( x < 4096 ) {
				emit(CMP32i(rx[0], x));
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				emit_MOVRi(rx[1], x);
				emit(CMP32(rx[0], rx[1]));
				unmask_rx( rx[1] );
			}
			emit(Bcond(comp, vm->instructionPointers[ ni->value ] - compiledOfs));
		}
		}
		unmask_rx( rx[0] );
		ip += 1; // OP_cond
		return qtrue;

	case OP_EQF:
	case OP_NEF:
	case OP_LTF:
	case OP_LEF:
	case OP_GTF:
	case OP_GEF: {
		uint32_t comp = get_comp( ni->op );
		x = ci->value;
		sx[0] = load_sx_opstack( vm, S0 ); dec_opstack(); // s0 = *opstack; opstack -= 4
		if ( x == 0 ) {
			emit(FCMP0(sx[0]));
		} else {
			sx[1] = alloc_sx( S1 | TEMP );
			emit_MOVSi(sx[1], x);
			emit(FCMP(sx[0], sx[1]));
			unmask_sx( sx[1] );
		}
		emit(Bcond(comp, vm->instructionPointers[ni->value] - compiledOfs));
		unmask_sx( sx[0] );
		ip += 1; // OP_cond
		return qtrue;
		}

	case OP_ADDF:
	case OP_SUBF:
	case OP_MULF:
	case OP_DIVF:
		x = ci->value;
		sx[0] = load_sx_opstack( vm, S0 ); // s0 = *opstack
		sx[1] = alloc_sx( S1 | TEMP );
		emit_MOVSi( sx[1], x );
		switch ( ni->op ) {
			case OP_ADDF: emit(FADD(sx[0], sx[0], sx[1])); break; // s0 = s0 + s1
			case OP_SUBF: emit(FSUB(sx[0], sx[0], sx[1])); break; // s0 = s0 - s1
			case OP_MULF: emit(FMUL(sx[0], sx[0], sx[1])); break; // s0 = s0 * s1
			case OP_DIVF: emit(FDIV(sx[0], sx[0], sx[1])); break; // s0 = s0 / s1
			default: break;
		}
		unmask_sx( sx[1] );
		store_sx_opstack( sx[0] );    // *opstack = s0
		ip += 1; // OP_XXXF
		return qtrue;

	default:
		break;
	};

	return qfalse;
}
#endif // CONST_OPTIMIZE


#ifdef MISC_OPTIMIZE
qboolean LocalOptimize( vm_t *vm )
{
	uint32_t v = ci->value;
	uint32_t rx[2];

	if ( ni->op == OP_LOAD4 ) // merge OP_LOCAL + OP_LOAD4
	{
#ifdef FPU_OPTIMIZE
		if ( ni->fpu ) {
			uint32_t sx[1];
			sx[0] = alloc_sx( S0 );
			if ( can_encode_imm12( v, 2 ) ) {
				emit(VLDRi(sx[0], rPROCBASE, v));    // s0 = [procBase + v]
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				emit_MOVRi(rx[1], v);
				emit(VLDR(sx[0], rPROCBASE, rx[1])); // s0 = [procBase + r2]
				unmask_rx( rx[1] );
			}
			inc_opstack(); store_sx_opstack( sx[0] );  // opstack+=4; *opstack = s0
			ip++; // OP_LOAD4
			return qtrue;
		}
#endif
		rx[0] = alloc_rx( R0 );
		if ( can_encode_imm12( v, 2 ) ) {
			emit(LDR32i(rx[0], rPROCBASE, v));    // r0 = [procBase + v]
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRi(rx[1], v);
			emit(LDR32(rx[0], rPROCBASE, rx[1])); // r0 = [procBase + r2]
			unmask_rx( rx[1] );
		}
		inc_opstack(); store_rx_opstack( rx[0] );   // opstack+=4; *opstack = r0
		ip++; // OP_LOAD4
		return qtrue;
	}

	if ( ni->op == OP_LOAD2 ) // merge OP_LOCAL + OP_LOAD2
	{
		rx[0] = alloc_rx( R0 );
		if ( can_encode_imm12( v, 1 ) ) {
			emit(LDRH32i(rx[0], rPROCBASE, v)); // r0 = (short*)[procBase + v]
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRi(rx[1], v);
			emit(LDRH32(rx[0], rPROCBASE, rx[1])); // r0 = (short*)[procBase+r1]
			unmask_rx( rx[1] );
		}
		inc_opstack(); store_rx_opstack( rx[0] ); // opstack+=4; *opstack = r0
		ip++; // OP_LOAD2
		return qtrue;
	}

	if ( ni->op == OP_LOAD1 ) // merge OP_LOCAL + OP_LOAD1
	{
		rx[0] = alloc_rx( R0 );
		if ( v < 4096 ) {
			emit(LDRB32i(rx[0], rPROCBASE, v)); // r0 = (byte*)[procBase + v]
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRi(rx[1], v);
			emit(LDRB32(rx[0], rPROCBASE, rx[1])); // r0 = (byte*)[procBase+r1]
			unmask_rx( rx[1] );
		}
		inc_opstack(); store_rx_opstack( rx[0] ); // opstack+=4; *opstack = r0
		ip++; // OP_LOAD1
		return qtrue;
	}

	return qfalse;
}
#endif // MISC_OPTIMIZE


#ifdef MACRO_OPTIMIZE
/*
=================
VM_FindMOps

Search for known macro-op sequences
=================
*/
static void VM_FindMOps( instruction_t *buf, const int instructionCount )
{
	int n, v;
	instruction_t *i;

	i = buf;
	n = 0;

	while ( n < instructionCount )
	{
		if ( i->op == OP_LOCAL && i->opStack == 0 ) {
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
#if 0
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
	uint32_t rx[3];
	int short_addr32;

	switch ( op )
	{
		//[local] += CONST
		case MOP_ADD4:
		case MOP_SUB4:
		case MOP_MUL4:
		case MOP_DIVI4:
		case MOP_DIVU4:
		//case MOP_MODI4:
		//case MOP_MODU4:
		case MOP_BAND4:
		case MOP_BOR4:
			value = inst[ip+2].value;
			addr = ci->value;
			short_addr32 = can_encode_imm12( addr, 2 );

			rx[0] = alloc_rx( R0 | TEMP );
			rx[1] = alloc_rx( R1 | TEMP );

			// load
			if ( short_addr32 ) {
				emit(LDR32i(rx[0], rPROCBASE, addr)); // r0 = [procBase + addr]
			} else {
				rx[2] = alloc_rx( R2 | TEMP );
				emit_MOVRi(rx[2], addr);              // r2 = addr
				emit(LDR32(rx[0], rPROCBASE, rx[2])); // r0 = [procBase + r2]
			}

			// modify
			switch ( op ) {
				case MOP_ADD4:
					if ( value < 4096 ) {
						emit(ADD32i(rx[0], rx[0], value)); // r0 += value;
					} else {
						emit_MOVRi(rx[1], value);          // r1 = value
						emit(ADD32(rx[0], rx[0], rx[1]));  // r0 += r1
					}
					break;
				case MOP_SUB4:
					if ( value < 4096 ) {
						emit(SUB32i(rx[0], rx[0], value)); // r0 -= value;
					} else {
						emit_MOVRi(rx[1], value);          // r1 = value
						emit(SUB32(rx[0], rx[0], rx[1]));  // r0 -= r1
					}
					break;

				case MOP_MUL4:
					emit_MOVRi(rx[1], value);          // r1 = value
					emit(MUL32(rx[0], rx[0], rx[1]));  // r0 *= r1
					break;

				case MOP_DIVI4:
					emit_MOVRi(rx[1], value);          // r1 = value
					emit(SDIV32(rx[0], rx[0], rx[1])); // r0 /= r1
					break;

				case MOP_DIVU4:
					emit_MOVRi(rx[1], value);          // r1 = value
					emit(UDIV32(rx[0], rx[0], rx[1])); // r0 /= r1
					break;
#if 0 // this overwrites R2. will be fixed later
				case MOP_MODI4:
					emit_MOVRi(rx[1], value);                 // r1 = value
					emit(SDIV32(rx[2], rx[0], rx[1]));        // r2 = r0 / r1
					emit(MSUB32(rx[0], rx[1], rx[2], rx[0])); // r0 = r0 - r1 * r2
					break;

				case MOP_MODU4:
					emit_MOVRi(rx[1], value);                 // r1 = value
					emit(UDIV32(rx[2], rx[0], rx[1]));        // r2 = r0 / r1
					emit(MSUB32(rx[0], rx[1], rx[2], rx[0])); // r0 = r0 - r1 * r2
					break;
#endif
				case MOP_BAND4:
					emit_MOVRi(rx[1], value);         // r1 = value
					emit(AND32(rx[0], rx[0], rx[1])); // r0 &= r1
					break;

				case MOP_BOR4:
					emit_MOVRi(rx[1], value);         // r1 = value
					emit(ORR32(rx[0], rx[0], rx[1])); // r0 |= r1
					break;
				default:
					break;
			}

			// store
			if ( short_addr32 ) {
				emit(STR32i(rx[0], rPROCBASE, addr)); // [procBase + addr] = r0
			} else {
				emit(STR32(rx[0], rPROCBASE, rx[2])); // [procBase + r2] = r0
				unmask_rx( rx[2] );
			}

			unmask_rx( rx[1] );
			unmask_rx( rx[0] );

			ip += 5;
			return qtrue;

		default: Com_Error( ERR_FATAL, "Unknown macro opcode %i", op );
			break;
	};

	return qfalse;
}
#endif // MACRO_OPTIMIZE


qboolean VM_Compile( vm_t *vm, vmHeader_t *header )
{
	const char *errMsg;
	uint32_t *litBase;
	uint32_t rx[3];
	uint32_t sx[2];
	qboolean scalar;
	int proc_base;
	int proc_len;
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

	litBase = NULL;
#ifdef USE_LITERAL_POOL
	VM_InitLiterals();
#endif

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

	init_opstack();

	emit(SUB64i(SP, SP, 96)); // SP -= 80

	emit(STP64(R20, R21, SP, 0));
	emit(STP64(R22, R23, SP, 16));
	emit(STP64(R24, R25, SP, 32));
	emit(STP64(R26, R27, SP, 48));
	emit(STP64(R28, R29, SP, 64));
	emit(STP64(R19, LR,  SP, 80));

	emit(LDR64lit(rLITBASE, savedOffset[LIT_LITBASE] - compiledOfs));
	emit(LDR64lit(rVMBASE, savedOffset[LIT_VMBASE] - compiledOfs));
	emit(LDR64lit(rINSPOINTERS, savedOffset[LIT_INSPOINTERS] - compiledOfs));
	emit(LDR64lit(rDATABASE, savedOffset[LIT_DATABASE] - compiledOfs));

	emit(LDR32lit(rDATAMASK, savedOffset[LIT_DATAMASK] - compiledOfs));
	emit(LDR32lit(rPSTACKBOTTOM, savedOffset[LIT_PSTACKBOTTOM] - compiledOfs));

	// these are volatile variables
	emit(LDR64i(rOPSTACK, rVMBASE, offsetof(vm_t, opStack)));
	emit(LDR64i(rOPSTACKTOP, rVMBASE, offsetof(vm_t, opStackTop)));
	emit(LDR32i(rPSTACK, rVMBASE, offsetof(vm_t, programStack)));

	emit_MOVRi(rOPSTACKSHIFT, 0); // opStack shift

	emitFuncOffset( vm, FUNC_ENTR );  // call vmMain()

#ifdef DEBUG_VM
	emit(STR32i(rPSTACK, rVMBASE, offsetof(vm_t, programStack))); // vm->programStack = rPSTACK;
#endif

	emit(LDP64(R20, R21, SP, 0));
	emit(LDP64(R22, R23, SP, 16));
	emit(LDP64(R24, R25, SP, 32));
	emit(LDP64(R26, R27, SP, 48));
	emit(LDP64(R28, R29, SP, 64));
	emit(LDP64(R19, LR,  SP, 80));

	emit(ADD64i(SP, SP, 96)); // SP += 96

	emit(RET(LR));

	// begin literals

savedOffset[ LIT_LITBASE ] = compiledOfs;
	emit8( (intptr_t)litBase );

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

	emitAlign( 16 ); // align to quadword boundary

savedOffset[ FUNC_ENTR ] = compiledOfs; // offset to vmMain() entry point

	while ( ip < header->instructionCount ) {

		uint32_t v;

		ci = &inst[ ip + 0 ];
		ni = &inst[ ip + 1 ];

#ifdef REGS_OPTIMIZE
		if ( ci->jused )
#endif
		{
			// we can safely perform register optimizations only in case if
			// we are 100% sure that current instruction is not a jump label
			flush_volatile();
		}

		vm->instructionPointers[ ip++ ] = compiledOfs;
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
				emitAlign( 16 ); // align to quadword boundary
				vm->instructionPointers[ ip - 1 ] = compiledOfs;

				proc_base = ip; // this points on next instruction after OP_ENTER
				// locate endproc
				for ( proc_len = -1, i = ip; i < header->instructionCount; i++ ) {
					if ( inst[ i ].op == OP_PUSH && inst[ i + 1 ].op == OP_LEAVE ) {
						proc_len = i - proc_base;
						break;
					}
				}

				// save opStack, LR
				emit(STP64pre(LR, rOPSTACK, SP, -16));

				// save programStack, procBase
				emit(STP64pre(rPSTACK, rPROCBASE, SP, -16));

				if ( ip != 1 ) {
					// opStack shift
					emit(ADD64(rOPSTACK, rOPSTACK, rOPSTACKSHIFT));
				}

				if ( v < 4096 ) {
					emit(SUB32i(rPSTACK, rPSTACK, v));    // pstack -= arg
				} else {
					rx[0] = alloc_rx( R2 | TEMP );
					emit_MOVRi(rx[0], v);                 // r2 = arg
					emit(SUB32(rPSTACK, rPSTACK, rx[0])); // pstack -= r2
					unmask_rx( rx[0] );
				}

				emit_CheckProc( vm, ci );
				emit(ADD64(rPROCBASE, rPSTACK, rDATABASE)); // procBase = programStack + dataBase
				break;

			case OP_LEAVE:
				dec_opstack(); // opstack -= 4
				flush_volatile(); // dump all constants etc.

#ifdef DEBUG_VM
				if ( opstack != 0 )
					DROP( "opStack corrupted on OP_LEAVE" );
#endif

#ifdef MISC_OPTIMIZE
				if ( !ci->endp && proc_base >= 0 ) {
					// jump to last OP_LEAVE instruction in this function
					v = proc_base + proc_len + 1;
					emit(B(vm->instructionPointers[ v ] - compiledOfs));
					break;
				}
#endif
				// restore programStack, procBase
				emit(LDP64post(rPSTACK, rPROCBASE, SP, 16));

				// restore LR, opStack
				emit(LDP64post(LR, rOPSTACK, SP, 16));

				// return to caller
				emit(RET(LR));
				break;

			case OP_CALL:
				rx[0] = load_rx_opstack( vm, R0 | FORCED ); // r0 = *opstack
				flush_volatile();
				emit_MOVRi(rOPSTACKSHIFT, opstack-4);
				emitFuncOffset(vm, FUNC_CALL);
				unmask_rx( rx[0] );
				break;

			case OP_PUSH:
				inc_opstack(); // opstack += 4
				if ( ni->op == OP_LEAVE ) {
					proc_base = -1;
				}
				break;

			case OP_POP:
				dec_opstack(); // opstack -= 4
				break;

			case OP_CONST:
#ifdef CONST_OPTIMIZE
				if ( ConstOptimize( vm ) )
					break;
#endif
				inc_opstack(); // opstack += 4
				store_item_opstack( ci );
				break;

			case OP_LOCAL:
#ifdef MISC_OPTIMIZE
				if ( LocalOptimize( vm ) )
					break;
#endif
				inc_opstack(); // opstack += 4
				store_item_opstack( ci );
				break;

			case OP_JUMP:
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
				flush_volatile();
				emit_CheckJump( vm, rx[0], proc_base, proc_len ); // check if r0 is within current proc
				emit(LDR64_8(R16, rINSPOINTERS, rx[0]));          // r16 = instructionPointers[ r0 ]
				emit(BR(R16));
				unmask_rx( rx[0] );
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
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
				rx[1] = load_rx_opstack( vm, R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
				unmask_rx( rx[0] );
				unmask_rx( rx[1] );
				emit(CMP32(rx[1], rx[0]));
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
				sx[0] = load_sx_opstack( vm, S0 ); dec_opstack(); // s0 = *opstack; opstack -= 4
				sx[1] = load_sx_opstack( vm, S1 ); dec_opstack(); // s1 = *opstack; opstack -= 4
				unmask_sx( sx[0] );
				unmask_sx( sx[1] );
				emit(FCMP(sx[1], sx[0]));
				emit(Bcond(comp, vm->instructionPointers[v] - compiledOfs));
				}
				break;

			case OP_LOAD1:
				rx[0] = load_rx_opstack( vm, R0 );     // r0 = *opstack;
				emit_CheckReg(vm, ci, rx[0]);
				emit(LDRB32(rx[0], rDATABASE, rx[0])); // r0 = (unsigned char)dataBase[r0]
				store_rx_opstack( rx[0] );             // *opstack = r0;
				break;

			case OP_LOAD2:
				rx[0] = load_rx_opstack( vm, R0 );     // r0 = *opstack;
				emit_CheckReg(vm, ci, rx[0]);
				emit(LDRH32(rx[0], rDATABASE, rx[0])); // r0 = (unsigned short)dataBase[r0]
				store_rx_opstack( rx[0] );             // *opstack = r0;
				break;

			case OP_LOAD4:
				rx[0] = load_rx_opstack( vm, R0 );     // r0 = *opstack;
				emit_CheckReg(vm, ci, rx[0]);
#ifdef FPU_OPTIMIZE
				if ( ci->fpu ) {
					sx[0] = alloc_sx( S0 );
					emit(VLDR(sx[0], rDATABASE, rx[0])); // s0 = dataBase[r0]
					store_sx_opstack( sx[0] );           // *opstack = s0;
					unmask_rx( rx[0] );
					break;
				}
#endif
				emit(LDR32(rx[0], rDATABASE, rx[0])); // r0 = dataBase[r0]
				store_rx_opstack( rx[0] );            // *opstack = r0;
				break;

			case OP_STORE1:
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
				rx[1] = load_rx_opstack( vm, R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
				emit_CheckReg(vm, ci, rx[1]);
				emit(STRB32(rx[0], rDATABASE, rx[1]));     // (byte*)database[r1] = r0
				unmask_rx( rx[0] );
				unmask_rx( rx[1] );
				break;

			case OP_STORE2:
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
				rx[1] = load_rx_opstack( vm, R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
				emit_CheckReg(vm, ci, rx[1]);
				emit(STRH32(rx[0], rDATABASE, rx[1]));     // (short*)database[r1] = r0
				unmask_rx( rx[0] );
				unmask_rx( rx[1] );
				break;

			case OP_STORE4:
				if ((scalar = scalar_on_top())) {
					sx[0] = load_sx_opstack( vm, S0 ); // s0 = *opstack;
				} else {
					rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack;
				}
				dec_opstack(); // opstack -= 4
				rx[1] = load_rx_opstack( vm, R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
				emit_CheckReg(vm, ci, rx[1]);
				if ( scalar ) {
					emit(VSTR(sx[0], rDATABASE, rx[1]));  // database[r1] = r0
					unmask_sx( sx[0] );
				} else {
					emit(STR32(rx[0], rDATABASE, rx[1])); // database[r1] = r0
					unmask_rx( rx[0] );
				}
				unmask_rx( rx[1] );
				break;

			case OP_ARG:
				if ( scalar_on_top() ) {
					sx[0] = load_sx_opstack( vm, S0 ); dec_opstack(); // s0 = *opstack; opstack -=4
					emit(VSTRi(sx[0], rPROCBASE, v)); // [procBase + v] = s0
					unmask_sx( sx[0] );
				} else {
					rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -=4
					emit(STR32i(rx[0], rPROCBASE, v)); // [procBase + v] = r0
					unmask_rx( rx[0] );
				}
				break;

			case OP_BLOCK_COPY:
				emitBlockCopy( vm, ci->value );
				break;

			case OP_SEX8:
			case OP_SEX16:
			case OP_NEGI:
			case OP_BCOM:
				rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
				switch ( ci->op ) {
					case OP_SEX8:  emit(SXTB(rx[0], rx[0]));  break; // r0 = sign extend byte r0
					case OP_SEX16: emit(SXTH(rx[0], rx[0]));  break; // r0 = sign extend short r0
					case OP_NEGI:  emit(NEG32(rx[0], rx[0])); break; // r0 = -r0
					case OP_BCOM:  emit(MVN32(rx[0], rx[0])); break; // r0 = ~r0
				}
				store_rx_opstack( rx[0] ); // *opstack = r0
				break;

			case OP_ADD:
			case OP_SUB:
			case OP_MULI:
			case OP_MULU:
			case OP_DIVI:
			case OP_DIVU:
			case OP_MODI:
			case OP_MODU:
			case OP_BAND:
			case OP_BOR:
			case OP_BXOR:
			case OP_LSH:
			case OP_RSHI:
			case OP_RSHU:
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack
				rx[1] = load_rx_opstack( vm, R1 ); // opstack-=4; r1 = *opstack
				switch ( ci->op ) {
					case OP_ADD:  emit(ADD32(rx[0], rx[1], rx[0])); break;  // r0 = r1 + r0
					case OP_SUB:  emit(SUB32(rx[0], rx[1], rx[0])); break;  // r0 = r1 - r0
					case OP_MULI:
					case OP_MULU: emit(MUL32(rx[0], rx[1], rx[0])); break;  // r0 = r1 * r0
					case OP_DIVI: emit(SDIV32(rx[0], rx[1], rx[0])); break; // r0 = r1 / r0
					case OP_DIVU: emit(UDIV32(rx[0], rx[1], rx[0])); break; // r0 = (unsigned)r1 / r0
					case OP_BAND: emit(AND32(rx[0], rx[1], rx[0])); break;  // r0 = r1 & r0
					case OP_BOR:  emit(ORR32(rx[0], rx[1], rx[0])); break;  // r0 = r1 | r0
					case OP_BXOR: emit(EOR32(rx[0], rx[1], rx[0])); break;  // r0 = r1 ^ r0
					case OP_LSH:  emit(LSL32(rx[0], rx[1], rx[0])); break;  // r0 = r1 << r0
					case OP_RSHI: emit(ASR32(rx[0], rx[1], rx[0])); break;  // r0 = r1 >> r0
					case OP_RSHU: emit(LSR32(rx[0], rx[1], rx[0])); break;  // r0 = (unsigned)r1 >> r0
					case OP_MODI:
					case OP_MODU:
						rx[2] = alloc_rx( R2 | TEMP );
						if ( ci->op == OP_MODI )
							emit(SDIV32(rx[2], rx[1], rx[0]));      // r2 = r1 / r0
						else
							emit(UDIV32(rx[2], rx[1], rx[0]));      // r2 = (unsigned)r1 / r0
						emit(MSUB32(rx[0], rx[0], rx[2], rx[1]));       // r0 = r1 - r0 * r2
						unmask_rx( rx[2] );
						break;
				}
				store_rx_opstack( rx[0] ); // *opstack = r0
				unmask_rx( rx[1] );
				break;

			case OP_ADDF:
			case OP_SUBF:
			case OP_MULF:
			case OP_DIVF:
				sx[0] = load_sx_opstack( vm, S0 ); dec_opstack(); // s0 = *opstack
				sx[1] = load_sx_opstack( vm, S1 ); // opstack -= 4; s1 = *opstack
				switch ( ci->op ) {
					case OP_ADDF: emit(FADD(sx[0], sx[1], sx[0])); break; // s0 = s1 + s0
					case OP_SUBF: emit(FSUB(sx[0], sx[1], sx[0])); break; // s0 = s1 - s0
					case OP_MULF: emit(FMUL(sx[0], sx[1], sx[0])); break; // s0 = s1 * s0
					case OP_DIVF: emit(FDIV(sx[0], sx[1], sx[0])); break; // s0 = s1 / s0
				}
				store_sx_opstack( sx[0] ); // *opstack = s0;
				unmask_sx( sx[1] );
				break;

			case OP_NEGF:
				sx[0] = load_sx_opstack( vm, S0 ); // s0 = *opstack
				emit(FNEG(sx[0], sx[0]));          // s0 = -s0
				store_sx_opstack( sx[0] );         // *opstack = s0
				break;

			case OP_CVIF:
				sx[0] = alloc_sx( S0 );
				rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
				emit(SCVTF(sx[0], rx[0]));         // s0 = (float)r0
				store_sx_opstack( sx[0] );         // *opstack = s0
				unmask_rx( rx[0] );
				break;

			case OP_CVFI:
				rx[0] = alloc_rx( R0 );
				sx[0] = load_sx_opstack( vm, S0 ); // s0 = *opstack
				emit(FCVTZS(rx[0], sx[0]));        // r0 = (int)s0
				store_rx_opstack( rx[0] );         // *opstack = r0;
				unmask_sx( sx[0] );
				break;

#ifdef MACRO_OPTIMIZE
			case MOP_ADD4:
			case MOP_SUB4:
			case MOP_MUL4:
			case MOP_DIVI4:
			case MOP_DIVU4:
			//case MOP_MODI4:
			//case MOP_MODU4:
			case MOP_BAND4:
			case MOP_BOR4:
				EmitMOPs( vm, ci->op );
				break;
#endif
		} // switch op
	} // ip

		emitAlign( 16 ); // align to quadword boundary
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
#ifdef USE_LITERAL_POOL
		uint32_t allocSize = compiledOfs + numLiterals * sizeof( uint32_t );
#else
		uint32_t allocSize = compiledOfs;
#endif
		vm->codeBase.ptr = mmap( NULL, allocSize, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
		if ( vm->codeBase.ptr == MAP_FAILED ) {
			VM_FreeBuffers();
			Com_Printf( S_COLOR_YELLOW "%s(%s): mmap failed\n", __func__, vm->name );
			return qfalse;
		}
		vm->codeLength = allocSize; // code + literals
		vm->codeSize = compiledOfs;
		code = (uint32_t*)vm->codeBase.ptr;
		litBase = (uint32_t*)(vm->codeBase.ptr + compiledOfs);
		goto __recompile;
	}

#ifdef USE_LITERAL_POOL
	// append literals to the code
	if ( numLiterals ) {
		uint32_t i, *lp = litBase;
		for ( i = 0; i < numLiterals; i++, lp++ ) {
			*lp = litList[ i ].value;
		}
	}
#endif

#ifdef DUMP_CODE
	dump_code( code, compiledOfs / 4 );
#endif

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
	if ( opStack[0] != 0xDEADC0DE ) {
		Com_Error( ERR_DROP, "%s(%s): opStack corrupted in compiled code", __func__, vm->name );
	}

	if ( vm->programStack != stackOnEntry - (MAX_VMMAIN_CALL_ARGS+2)*4 ) {
		Com_Error( ERR_DROP, "%s(%s): programStack corrupted in compiled code", __func__, vm->name );
	}
#endif

	vm->programStack = stackOnEntry;

	return opStack[1];
}
