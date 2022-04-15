/*
===========================================================================
Copyright (C) 2009 David S. Miller <davem@davemloft.net>
Copyright (C) 2013,2014 SUSE Linux Products GmbH
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

ARMv7l VM by Ludwig Nussel <ludwig.nussel@suse.de>

TODO: optimization

Docu:
http://www.coranac.com/tonc/text/asm.htm
http://www.heyrick.co.uk/armwiki/Category:Opcodes
ARMv7-A_ARMv7-R_DDI0406_2007.pdf
*/

#ifdef _WIN32
#include <windows.h>
#pragma warning( disable : 4245 ) // conversion from int to XXX, signed/unsigned mismatch
#pragma warning( disable : 4146 ) // unary minus operator applied to unsigned type, result still unsigned
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#endif

#include "vm_local.h"

#define NUM_PASSES 1

// additional integrity checks
#define DEBUG_VM

// various defintions to enable/disable particular optimization

// use dynamic allocation of integer/scalar registers
#define DYN_ALLOC_RX
#define DYN_ALLOC_SX

// re-use constants previously stored in scratch registers
#define CONST_CACHE_RX
#define CONST_CACHE_SX

#define REGS_OPTIMIZE
#define FPU_OPTIMIZE
#define CONST_OPTIMIZE
#define ADDR_OPTIMIZE
#define LOAD_OPTIMIZE

// allow sharing both variables and constants in registers
#define REG_TYPE_MASK
// number of variables/memory mappings per register
#define REG_MAP_COUNT 4

#define FUNC_ALIGN 16

//#define DUMP_CODE

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
//static	uint32_t *instructionOffsets;
//static	intptr_t *instructionPointers;

static  instruction_t *inst = NULL;
static  instruction_t *ci;

static	uint32_t	ip;
static	uint32_t	pass;
static	uint32_t	savedOffset[ OFFSET_T_LAST ];



#define R0	0  // scratch
#define R1	1  // scratch
#define R2	2  // scratch
#define R3	3  // scratch
#define R4	4  // * vmBase
#define R5	5  // * opStack
#define R6	6  // * opStackTop
#define R7	7  // * instructionPointers
#define R8	8  // * programStack
#define R9	9  // * dataBase
#define R10	10 // * dataMask
#define R11	11 // * procBase, frame pointer
#define R12	12 // scratch, inter-procedure call
#define SP	13 // * stack pointer
#define LR	14 // link register
#define PC	15 // program counter

#define FP	R11

#define APSR_nzcv	15

#define S0      0
#define S1      1
#define S2      2

#define rVMBASE		R4
#define rOPSTACK	R5
#define rOPSTACKTOP	R6
#define rINSPOINTERS	R7
#define rPSTACK		R8
#define rDATABASE	R9
#define rDATAMASK	R10
#define rPROCBASE	FP

#define R4_R11 (1<<R4)|(1<<R5)|(1<<R6)|(1<<R7)|(1<<R8)|(1<<R9)|(1<<R10)|(1<<R11)

/* arm eabi, builtin gcc functions */
int __aeabi_idiv(int, int);
unsigned __aeabi_uidiv(unsigned, unsigned);
void __aeabi_idivmod(void);
void __aeabi_uidivmod(void);

#ifdef _MSC_VER
#define DROP( reason, ... ) \
	do { \
		VM_FreeBuffers(); \
		Com_Error( ERR_DROP, "%s: " reason, __func__, __VA_ARGS__ ); \
	} while(0)
#else
#define DROP( reason, args... ) \
	do { \
		VM_FreeBuffers(); \
		Com_Error( ERR_DROP, "%s: " reason, __func__, ##args ); \
	} while(0)
#endif


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
#ifdef _WIN32
		VirtualFree( vm->codeBase.ptr, 0, MEM_RELEASE );
#else
		if ( munmap( vm->codeBase.ptr, vm->codeLength ) )
			Com_Printf( S_COLOR_RED "%s(): memory unmap failed, possible memory leak!\n", __func__ );
#endif
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


static void __attribute__( ( __noreturn__ ) ) ErrBadDataRead( void )
{
	Com_Error( ERR_DROP, "program tried to read out of data segment" );
}


static void __attribute__( ( __noreturn__ ) ) ErrBadDataWrite( void )
{
	Com_Error( ERR_DROP, "program tried to write out of data segment" );
}


static void emit( uint32_t isn )
{
	if ( code )
	{
		code[ compiledOfs >> 2 ] = isn;
	}

	compiledOfs += 4;
}


static unsigned char off10_2( unsigned val )
{
	if ( val & 3 )
		DROP( "offset must be multiple of four" );

	if ( val > 1020 )
		DROP( "offset %i is too large", val );

	return val >> 2;
}


// ARM is really crazy ...
static unsigned short rimm( uint32_t val )
{
	unsigned shift = 0;
	if (val < 256)
		return val;
	// rotate the value until it fits
	while (shift < 16 && (val>255 || !(val&3))) {
		val = (val&3)<<30 | val>>2;
		++shift;
	}
	if (shift > 15 || val > 255) {
		DROP( "immediate cannot be encoded (%d, %d)\n", shift, val );
	}
	return (16-shift)<<8 | val;
}


// same as rimm but doesn't die, returns 0 if not encodable
static unsigned short can_encode( uint32_t val )
{
	unsigned shift = 0;
	if (val == 0)
		return 1; // to avoid redundand checks
		//DIE("can_encode: invalid argument");
	if (val < 256)
		return val;
	// rotate the value until it fits
	while (shift < 16 && (val>255 || !(val&3))) {
		val =  (val&3)<<30 | val>>2;
		++shift;
	}
	if (shift > 15 || val > 255) {
		return 0;
	}
	return (16-shift)<<8 | val;
}

#define rASR(i, reg) (0b10<<5 | ((i&31)<<7) | reg)
#define rLSL(i, reg) (0b00<<5 | ((i&31)<<7) | reg)
#define rLSR(i, reg) (0b01<<5 | ((i&31)<<7) | reg)
#define rROR(i, reg) (0b11<<5 | ((i&31)<<7) | reg)

// conditions
#define EQ (0b0000<<28)
#define NE (0b0001<<28)
#define CS (0b0010<<28)
#define HS CS
#define CC (0b0011<<28)
#define LO CC
#define MI (0b0100<<28)
#define PL (0b0101<<28)
#define VS (0b0110<<28)
#define VC (0b0111<<28)
#define HI (0b1000<<28)
#define LS (0b1001<<28)
#define GE (0b1010<<28)
#define LT (0b1011<<28)
#define GT (0b1100<<28)
#define LE (0b1101<<28)
#define AL (0b1110<<28)
#define cond(what, op) (what | (op&~AL))

#define BKPT(v) (AL | 0b10010<<20 | ((v&0xFFF0)<<8) | 0b0111<<4 | (v&0xF))

#define NOP (AL | 0b110010<<20 | 0b1111<<12)

#define ANDi(dst, src, i) (AL | (0b001<<25) | (0b00000<<20) | (src<<16) | (dst<<12) | rimm(i))
#define EORi(dst, src, i) (AL | (0b001<<25) | (0b00010<<20) | (src<<16) | (dst<<12) | rimm(i))
#define SUBi(dst, src, i) (AL | (0b001<<25) | (0b00100<<20) | (src<<16) | (dst<<12) | rimm(i))
#define RSBi(dst, src, i) (AL | (0b001<<25) | (0b00110<<20) | (src<<16) | (dst<<12) | rimm(i))
#define ADDi(dst, src, i) (AL | (0b001<<25) | (0b01000<<20) | (src<<16) | (dst<<12) | rimm(i))
#define ADCi(dst, src, i) (AL | (0b001<<25) | (0b01010<<20) | (src<<16) | (dst<<12) | rimm(i))
#define SBCi(dst, src, i) (AL | (0b001<<25) | (0b01100<<20) | (src<<16) | (dst<<12) | rimm(i))
#define RSCi(dst, src, i) (AL | (0b001<<25) | (0b01110<<20) | (src<<16) | (dst<<12) | rimm(i))

#define ORRi(dst, src, i) (AL | (0b001<<25) | (0b11000<<20) | (src<<16) | (dst<<12) | rimm(i))
#define MOVi(dst,      i) (AL | (0b001<<25) | (0b11010<<20) |             (dst<<12) | rimm(i))
#define BICi(dst, src, i) (AL | (0b001<<25) | (0b11100<<20) | (src<<16) | (dst<<12) | rimm(i))
#define MVNi(dst,      i) (AL | (0b001<<25) | (0b11110<<20) |             (dst<<12) | rimm(i))

#define MOVW(dst,      i) (AL |  (0b11<<24)                 | ((((i)>>12)&0xF)<<16) | (dst<<12) | ((i)&((1<<12)-1)))
#define MOVT(dst,      i) (AL |  (0b11<<24) |  (0b0100<<20) | ((((i)>>12)&0xF)<<16) | (dst<<12) | ((i)&((1<<12)-1)))

#define TSTi(     src, i) (AL | (0b001<<25) | (0b10001<<20) | (src<<16) |             rimm(i))
#define TEQi(     src, i) (AL | (0b001<<25) | (0b10011<<20) | (src<<16) |             rimm(i))
#define CMPi(     src, i) (AL | (0b001<<25) | (0b10101<<20) | (src<<16) |             rimm(i))
#define CMNi(     src, i) (AL | (0b001<<25) | (0b10111<<20) | (src<<16) |             rimm(i))

#define ANDSi(dst, src, i) (ANDi(dst, src, i) | (1<<20))
#define EORSi(dst, src, i) (EORi(dst, src, i) | (1<<20))
#define SUBSi(dst, src, i) (SUBi(dst, src, i) | (1<<20))
#define RSBSi(dst, src, i) (RSBi(dst, src, i) | (1<<20))
#define ADDSi(dst, src, i) (ADDi(dst, src, i) | (1<<20))
#define ADCSi(dst, src, i) (ADCi(dst, src, i) | (1<<20))
#define SBCSi(dst, src, i) (SBCi(dst, src, i) | (1<<20))
#define RSCSi(dst, src, i) (RSCi(dst, src, i) | (1<<20))

#define ORRSi(dst, src, i) (ORRi(dst, src, i) | (1<<20))
#define MOVSi(dst,      i) (MOVi(dst,      i) | (1<<20))
#define BICSi(dst, src, i) (BICi(dst, src, i) | (1<<20))
#define MVNSi(dst,      i) (MVNi(dst, src, i) | (1<<20))

#define AND(dst, src, reg) (AL | (0b000<<25) | (0b00000<<20) | (src<<16) | (dst<<12) | reg)
#define EOR(dst, src, reg) (AL | (0b000<<25) | (0b00010<<20) | (src<<16) | (dst<<12) | reg)
#define SUB(dst, src, reg) (AL | (0b000<<25) | (0b00100<<20) | (src<<16) | (dst<<12) | reg)
#define RSB(dst, src, reg) (AL | (0b000<<25) | (0b00110<<20) | (src<<16) | (dst<<12) | reg)
#define ADD(dst, src, reg) (AL | (0b000<<25) | (0b01000<<20) | (src<<16) | (dst<<12) | reg)
#define ADC(dst, src, reg) (AL | (0b000<<25) | (0b01010<<20) | (src<<16) | (dst<<12) | reg)
#define SBC(dst, src, reg) (AL | (0b000<<25) | (0b01100<<20) | (src<<16) | (dst<<12) | reg)
#define RSC(dst, src, reg) (AL | (0b000<<25) | (0b01110<<20) | (src<<16) | (dst<<12) | reg)

#define ORR(dst, src, reg) (AL | (0b000<<25) | (0b11000<<20) | (src<<16) | (dst<<12) | reg)
#define MOV(dst,      src) (AL | (0b000<<25) | (0b11010<<20) |             (dst<<12) | src)

#define LSL(dst, src, reg) (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) | (reg<<8)     | (0b0001<<4) | src)
#define LSR(dst, src, reg) (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) | (reg<<8)     | (0b0011<<4) | src)
#define ASR(dst, src, reg) (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) | (reg<<8)     | (0b0101<<4) | src)
#define ROR(dst, src, reg) (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) | (reg<<8)     | (0b0111<<4) | src)

#define LSLi(dst, src, i)  (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) | ((i&0x1F)<<7) | (0b000<<4) | src)
#define LSRi(dst, src, i)  (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) | ((i&0x1F)<<7) | (0b010<<4) | src)
#define ASRi(dst, src, i)  (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) | ((i&0x1F)<<7) | (0b100<<4) | src)
#define RORi(dst, src, i)  (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) | ((i&0x1F)<<7) | (0b110<<4) | src)
#define RRX(dst, src)      (AL | (0b000<<25) | (0b1101<<21) | (0<<20) | (dst<<12) |                 (0b110<<4) | src)

#define BIC(dst, src, reg) (AL | (0b000<<25) | (0b11100<<20) | (src<<16) | (dst<<12) | reg)
#define MVN(dst,      reg) (AL | (0b000<<25) | (0b11110<<20) |             (dst<<12) | reg)

#define TST(     src, reg) (AL | (0b000<<25) | (0b10001<<20) | (src<<16) |             reg)
#define TEQ(     src, reg) (AL | (0b000<<25) | (0b10011<<20) | (src<<16) |             reg)
#define CMP(     src, reg) (AL | (0b000<<25) | (0b10101<<20) | (src<<16) |             reg)
#define CMN(     src, reg) (AL | (0b000<<25) | (0b10111<<20) | (src<<16) |             reg)

// load word/byte with pre-increment
#define LDRa(dst, base, off)   (AL | (0b011<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRai(dst, base, off12)(AL | (0b010<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | off12)

// load byte with 12-bit offset
#define LDRBai(dst, base, off12) (AL | (0b010<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | off12)

// load word with post-increment
#define LDRTaiw(dst, base, off) (AL | (0b010<<25) | (0b0101<<21) | (1<<20) | base<<16 | dst<<12 | off)

// load/store byte with post-increment
#define LDRBTaiw(dst, base, off)(AL | (0b010<<25) | (0b0111<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define STRBTaiw(dst, base, off)(AL | (0b010<<25) | (0b0111<<21) | (0<<20) | base<<16 | dst<<12 | off)

// load byte with pre-increment
#define LDRBa(dst, base, off)   (AL | (0b011<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRSBa(dst, base, off)  (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | 0b1101<<4 | off)

// load signed byte with 8-bit offset
#define LDRSBai(dst, base, off8) (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | ((((off8)>>4)&0xF)<<8) | 0b1101<<4 | ((off8)&0xF))

// load half-word with pre-increment
#define LDRHa(dst, base, off)   (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | (0b1011<<4) | off)
#define LDRSHa(dst, base, off)  (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | (0b1111<<4) | off)

// load unsigned/signed half-word with 8-bit offset
#define LDRHai(dst, base, off8)  (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | ((((off8)>>4)&0xF)<<8) | (0b1011)<<4 | ((off8)&0xF) )
#define LDRSHai(dst, base, off8) (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | ((((off8)>>4)&0xF)<<8) | (0b1111)<<4 | ((off8)&0xF) )

// store byte/half-word with pre-increment
#define STRBa(dst, base, off)   (AL | (0b011<<25) | (0b1110<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRHa(dst, base, off)   (AL | (0b000<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | (0b1011<<4) | off)

// store word with pre-increment
#define STRa(dst, base, off)   (AL | (0b011<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRai(dst, base, off)  (AL | (0b010<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | off)

// store byte with 12-bit offset
#define STRBai(dst, base, off)  (AL | (0b010<<25) | (0b1110<<21) | (0<<20) | base<<16 | dst<<12 | off)

// store short with 8-bit offset
#define STRHai(dst, base, off8)  (AL | (0b000<<25) | (0b1110<<21) | (0<<20) | base<<16 | dst<<12 | ((((off8)>>4)&0xF)<<8) | (0b1011)<<4 | ((off8)&0xF) )

// store word with post-increment
#define STRTaiw(dst, base, off) (AL | (0b010<<25) | (0b0101<<21) | (0<<20) | base<<16 | dst<<12 | off)

// sign-extend byte to word
#define SXTB(Rd, Rm)       (AL | (0b01101<<23) | (0b010<<20) | (0b1111<<16) | (Rd<<12) | (0b00000111<<4) | Rm)
// zero-extend byte to word
#define UXTB(Rd, Rm)       (AL | (0b01101<<23) | (0b110<<20) | (0b1111<<16) | (Rd<<12) | (0b00000111<<4) | Rm)

// sign-extend short to word
#define SXTH(Rd, Rm)       (AL | (0b01101<<23) | (0b011<<20) | (0b1111<<16) | (Rd<<12) | (0b00000111<<4) | Rm)
// zero-extend short to word
#define UXTH(Rd, Rm)       (AL | (0b01101<<23) | (0b111<<20) | (0b1111<<16) | (Rd<<12) | (0b00000111<<4) | Rm)


// branch to target address (for small jumps within +/-32M)
#define Bi(imm24) \
	(AL | (0b101)<<25 | (0<<24) /*L*/ | (imm24))

// call subroutine
#define BLi(imm24) \
	(AL | (0b101)<<25 | (1<<24) /*L*/ | (imm24))

// branch and exchange (register)
#define BX(reg) \
	(AL | 0b00010010<<20 | 0b1111<<16 | 0b1111<<12 | 0b1111<<8| 0b0001<<4 | reg)

// call subroutine (register)
#define BLX(reg) \
	(AL | 0b00010010<<20 | 0b1111<<16 | 0b1111<<12 | 0b1111<<8| 0b0011<<4 | reg)

#define PUSH(mask)    (AL | (0b100100<<22) | (0b10<<20) | (0b1101<<16) |  mask)

#define POP(mask)     (0xe8bd0000|mask)

// note: Rd and Rm must not be the same
#define MUL(Rd, Rm, Rs) \
	(AL | 0b0000000<<21 | (0<<20) /*S*/ | (Rd<<16) | (Rs<<8) | 0b1001<<4 | Rm)

// Rd = Rn / Rm
#define SDIV(Rd, Rn, Rm) (AL | (0b01110<<23) | (0b001<<20) | (Rd<<16) | (0b1111<<12) | (Rm<<8) | (0b0001 << 4) | Rn)
#define UDIV(Rd, Rn, Rm) (AL | (0b01110<<23) | (0b011<<20) | (Rd<<16) | (0b1111<<12) | (Rm<<8) | (0b0001 << 4) | Rn)

// Rd = Ra - Rn * Rm
#define MLS(Rd, Rn, Rm, Ra) (AL | (0b0110<<20) | (Rd<<16) | (Ra<<12) | (Rm<<8) | (0b1001<<4) | Rn)

// immediate -> singe precision register
#define VMOVi(Vd, imm) (AL | (0b11101<<23)| ((Vd&1)<<22) | (0b11<<20)| (((imm&0xF0)>>4)<<16) | ((Vd>>1)<<12) | (0b10100000<<4) | (imm&0xF) )

// single precision register -> singe precision register
#define VMOV(Vd, Vm) (AL | (0b11101<<23)| ((Vd&1)<<22) | (0b110000<<16) | ((Vd>>1)<<12) | (0b101001<<6) | ((Vm&1)<<5) | (0<<4) | (Vm>>1) )

// arm core register -> singe precision register
#define VMOVass(Vn, Rt) (AL|(0b1110<<24)|(0b000<<21)|(0<<20)| ((Vn>>1)<<16) | (Rt<<12) | (0b1010<<8) | ((Vn&1)<<7) | (1<<4))

// singe precision register -> arm core register
#define VMOVssa(Rt, Vn) (AL|(0b1110<<24)|(0b000<<21)|(1<<20)| ((Vn>>1)<<16) | (Rt<<12) | (0b1010<<8) | ((Vn&1)<<7) | (1<<4))

#define _VCVT_F(Vd, Vm, opc2, op) \
	(AL|(0b11101<<23)|((Vd&1)<<22)|(0b111<<19)|(opc2<<16)|((Vd>>1)<<12)|(0b101<<9)|(0<<8)|(op<<7)|(1<<6)|((Vm&1)<<5)|(Vm>>1))
#define VCVT_F32_U32(Sd, Sm) _VCVT_F(Sd, Sm, 0b000, 0 /* unsigned */)
#define VCVT_U32_F32(Sd, Sm) _VCVT_F(Sd, Sm, 0b100, 1 /* round zero */)
#define VCVT_F32_S32(Sd, Sm) _VCVT_F(Sd, Sm, 0b000, 1 /* unsigned */)
#define VCVT_S32_F32(Sd, Sm) _VCVT_F(Sd, Sm, 0b101, 1 /* round zero */)

#define VLDRai(Vd, Rn, i) (AL|(0b1101<<24)|1<<23|((Vd&1)<<22)|1<<20|(Rn<<16)|((Vd>>1)<<12)|(0b1010<<8)|off10_2(i))
#define VSTRai(Vd, Rn, i) (AL|(0b1101<<24)|1<<23|((Vd&1)<<22)|0<<20|(Rn<<16)|((Vd>>1)<<12)|(0b1010<<8)|off10_2(i))

#define VNEG_F32(Vd, Vm) \
	(AL|(0b11101<<23)|((Vd&1)<<22)|(0b11<<20)|(1<<16)|((Vd>>1)<<12)|(0b101<<9)|(0<<8)|(1<<6)|((Vm&1)<<5)|(Vm>>1))

#define VADD_F32(Vd, Vn, Vm) \
	(AL|(0b11100<<23)|((Vd&1)<<22)|(0b11<<20)|((Vn>>1)<<16)|((Vd>>1)<<12)|(0b101<<9)|(0<<8)|((Vn&1)<<7)|(0<<6)|((Vm&1)<<5)|(Vm>>1))
#define VSUB_F32(Vd, Vn, Vm) \
	(AL|(0b11100<<23)|((Vd&1)<<22)|(0b11<<20)|((Vn>>1)<<16)|((Vd>>1)<<12)|(0b101<<9)|(0<<8)|((Vn&1)<<7)|(1<<6)|((Vm&1)<<5)|(Vm>>1))
#define VMUL_F32(Vd, Vn, Vm) \
	(AL|(0b11100<<23)|((Vd&1)<<22)|(0b10<<20)|((Vn>>1)<<16)|((Vd>>1)<<12)|(0b101)<<9|(0<<8)|((Vn&1)<<7)|(0<<6)|((Vm&1)<<5)|(Vm>>1))
#define VDIV_F32(Vd, Vn, Vm) \
	(AL|(0b11101<<23)|((Vd&1)<<22)|(0b00<<20)|((Vn>>1)<<16)|((Vd>>1)<<12)|(0b101<<9)|(0<<8)|((Vn&1)<<7)|(0<<6)|((Vm&1)<<5)|(Vm>>1))

#define VSQRT_F32(Vd, Vm) \
	(AL|(0b11101<<23)|((Vd&1)<<22)|(0b11<<20)|(0b0001<<16)|((Vd>>1)<<12)|(0b101<<9)|(0b011<<6)|((Vm&1)<<5)|(0<<4)|(Vm>>1))

#define _VCMP_F32(Vd, Vm, E) \
	(AL|(0b11101<<23)|((Vd&1)<<22)|(0b11<<20)|((0b0100)<<16)|((Vd>>1)<<12)|(0b101<<9)|(0<<8)|(E<<7)|(1<<6)|((Vm&1)<<5)|(Vm>>1))
#define VCMP_F32(Vd, Vm) _VCMP_F32(Vd, Vm, 0)

#define VMRS(Rt) \
	(AL|(0b11101111<<20)|(0b0001<<16)|(Rt<<12)|(0b1010<<8)|(1<<4))


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


static void emit_MOVRxi( uint32_t reg, uint32_t imm )
{
	if ( imm <= 0xFFFF ) {
		emit( MOVW( reg, imm ) );
	} else if ( can_encode( imm ) ) {
		emit( MOVi( reg, imm ) );
	} else if ( can_encode( ~imm ) ) {
		emit( MVNi( reg, ~imm ) );
	} else {
		emit( MOVW( reg, (imm&0xFFFF) ) );
		emit( MOVT( reg, (((imm>>16)&0xFFFF)) ) );
	}
}


static uint32_t alloc_rx( uint32_t pref );

static qboolean find_rx_const( uint32_t imm );
static uint32_t alloc_rx_const( uint32_t pref, uint32_t imm );
static uint32_t alloc_rx_local( uint32_t pref, uint32_t imm );

static uint32_t alloc_sx( uint32_t pref );


// ---------------- register allocation --------------------

// register allocation preferences

#define FORCED 0x20 // load function must return specified register
#define TEMP   0x40 // hint: temporary allocation, will not be stored on opStack
#define RCONST 0x80 // hint: register value will be not modified
#define XMASK  0x100 // exclude masked registers

#define RMASK  0x0F

// array sizes for cached/meta registers
#define NUM_RX_REGS 13 // [R0..R12]
#define NUM_SX_REGS 8 // [S0..S7]

// general-purpose register list available for dynamic allocation
static const uint32_t rx_list_alloc[] = {
	R0, R1, R2, R3, // R0-R3 are required minimum
	R12
};

// FPU scalar register list available for dynamic allocation
static const uint32_t sx_list_alloc[] = {
	S0, S1, 2, 3, 4, 5, 6, 7 // S0 and S1 are required minimum
};

#ifdef CONST_CACHE_RX
static const uint32_t rx_list_cache[] = {
	R12, R3, R2, R1
};
#endif

#ifdef CONST_CACHE_SX
static const uint32_t sx_list_cache[] = {
	S0, S1, 2, 3, 4, 5, 6, 7,
};
#endif

// types of items on the opStack
typedef enum {
	TYPE_RAW,        // stored value
	TYPE_CONST,      // constant
	TYPE_LOCAL,      // address of local variable
	TYPE_RX,         // volatile - general-purpose register
	TYPE_SX,         // volatile - FPU scalar register
} opstack_value_t;

typedef enum {
	RTYPE_UNUSED = 0x0,
	RTYPE_CONST =  0x1,
	RTYPE_VAR =    0x2
} reg_value_t;

typedef struct opstack_s {
	uint32_t value;
	int offset;
	opstack_value_t type;
	int safe_arg;
} opstack_t;

typedef struct var_addr_s {
	int32_t addr; // variable address/offset
	uint8_t base; // procBase or dataBase register, ranges should NOT overlap
	uint8_t size; // 1,2,4
} var_addr_t;

typedef enum {
	Z_NONE,
	Z_EXT8,
	S_EXT8,
	Z_EXT16,
	S_EXT16,
} ext_t;

typedef struct reg_s {
	int type_mask;
	struct {
		uint32_t value;
	} cnst;
	// register value can be mapped to many memory regions
	struct {
		var_addr_t map[REG_MAP_COUNT];
		unsigned idx; // next allocation slot
	} vars;
	uint32_t ip; // ip of last reference
	int refcnt;  // reference counter
	ext_t ext;	 // zero/sign-extension flags
} reg_t;

static int opstack;
static opstack_t opstackv[PROC_OPSTACK_SIZE + 1];

// cached register values

static reg_t rx_regs[NUM_RX_REGS];
static reg_t sx_regs[NUM_SX_REGS];

// masked register can't be allocated or flushed to opStack on register pressure

static int32_t rx_mask[NUM_RX_REGS];
static int32_t sx_mask[NUM_SX_REGS];


static qboolean find_free_rx( void ) {
	uint32_t i, n;

	for ( i = 0; i < ARRAY_LEN( rx_list_alloc ); i++ ) {
		n = rx_list_alloc[i];
		if ( rx_regs[n].type_mask == RTYPE_UNUSED ) {
			return qtrue;
		}
	}

	return qfalse;
}


static qboolean find_free_sx( void ) {
	uint32_t i, n;

	for ( i = 0; i < ARRAY_LEN( sx_list_alloc ); i++ ) {
		n = sx_list_alloc[i];
		if ( sx_regs[n].type_mask == RTYPE_UNUSED ) {
			return qtrue;
		}
	}

	return qfalse;
}


static void wipe_reg_range( reg_t *reg, const var_addr_t *v ) {
	if ( reg->type_mask & RTYPE_VAR ) {
		uint32_t c, n;
		for ( c = 0, n = 0; n < ARRAY_LEN( reg->vars.map ); n++ ) {
			var_addr_t *var = &reg->vars.map[n];
			if ( var->size != 0 ) {
				c++;
				if ( var->base == v->base ) {
					if ( v->addr < var->addr + var->size && v->addr + v->size > var->addr ) {
						memset( var, 0, sizeof( *var ) );
						//var->size = 0;
						c--; continue;
					}
				}
			}
		}
		if ( c == 0 ) {
			reg->type_mask &= ~RTYPE_VAR;
			reg->ext = Z_NONE;
		} else {
			//reg->type_mask |= RTYPE_VAR;
		}
	}
}


static void wipe_var_range( const var_addr_t *v )
{
#ifdef LOAD_OPTIMIZE
	uint32_t i;
#ifdef DEBUG_VM
	if ( v->size == 0 || v->base == 0 )
		DROP( "incorrect variable setup" );
#endif
	// wipe all types of overlapping variables
	for ( i = 0; i < ARRAY_LEN( rx_regs ); i++ ) {
		wipe_reg_range( &rx_regs[i], v );
	}
	for ( i = 0; i < ARRAY_LEN( sx_regs ); i++ ) {
		wipe_reg_range( &sx_regs[i], v );
	}
#endif
}


static void set_var_map( reg_t *r, const var_addr_t *v ) {
	uint32_t n;
	for ( n = 0; n < ARRAY_LEN( r->vars.map ); n++ ) {
		if ( r->vars.map[n].size == 0 ) {
			r->vars.map[n] = *v;
			r->vars.idx = ( n + 1 ) % ARRAY_LEN( r->vars.map );
			return;
		}
	}
	r->vars.map[r->vars.idx] = *v;
	r->vars.idx = ( r->vars.idx + 1 ) % ARRAY_LEN( r->vars.map );
}


static void set_rx_var( uint32_t reg, const var_addr_t *v ) {
#ifdef LOAD_OPTIMIZE
	if ( reg < ARRAY_LEN( rx_regs ) ) {
		reg_t *r = rx_regs + reg;
#ifdef REG_TYPE_MASK
		r->type_mask |= RTYPE_VAR;
#else
		r->type_mask = RTYPE_VAR;
#endif
		set_var_map( r, v );
		r->refcnt++; // = 1;
		r->ip = ip;
	}
#endif
}

static void set_rx_ext( uint32_t reg, ext_t ext ) {
#ifdef LOAD_OPTIMIZE
	if ( reg >= ARRAY_LEN( rx_regs ) )
		DROP( "register value %i s out of range", reg );
	rx_regs[reg].ext = ext;
#endif
}



static void set_sx_var( uint32_t reg, const var_addr_t *v ) {
#ifdef LOAD_OPTIMIZE
	if ( reg < ARRAY_LEN( sx_regs ) ) {
		reg_t *r = sx_regs + reg;
#ifdef REG_TYPE_MASK
		r->type_mask |= RTYPE_VAR;
#else
		r->type_mask = RTYPE_VAR;
#endif
		set_var_map( r, v );
		r->refcnt++; // = 1;
		r->ip = ip;
	}
#endif
}


static reg_t *find_rx_var( uint32_t *reg, const var_addr_t *v ) {
#ifdef LOAD_OPTIMIZE
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( rx_regs ); i++ ) {
		reg_t *r = &rx_regs[i];
		if ( r->type_mask & RTYPE_VAR ) {
			uint32_t n;
			for ( n = 0; n < ARRAY_LEN( r->vars.map ); n++ ) {
				if ( r->vars.map[n].size && r->vars.map[n].addr == v->addr && r->vars.map[n].size == v->size && r->vars.map[n].base == v->base ) {
					r->refcnt++;
					r->ip = ip;
					*reg = i;
					return r;
				}
			}
		}
	}
#endif
	return NULL;
}


static qboolean find_sx_var( uint32_t *reg, const var_addr_t *v ) {
#ifdef LOAD_OPTIMIZE
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( sx_regs ); i++ ) {
		reg_t *r = &sx_regs[i];
		if ( r->type_mask & RTYPE_VAR ) {
			uint32_t n;
			for ( n = 0; n < ARRAY_LEN( r->vars.map ); n++ ) {
				if ( r->vars.map[n].size && r->vars.map[n].addr == v->addr && r->vars.map[n].size == v->size && r->vars.map[n].base == v->base ) {
					r->refcnt++;
					r->ip = ip;
					*reg = i;
					return qtrue;
				}
			}
		}
	}
#endif // LOAD_OPTIMIZE
	return qfalse;
}


static void reduce_map_size( reg_t *reg, uint32_t size ) {
	int i;
	for ( i = 0; i < ARRAY_LEN( reg->vars.map ); i++ ) {
		if ( reg->vars.map[i].size > size ) {
			reg->vars.map[i].size = size;
		}
	}
}


static reg_t *rx_on_top( void ) {
	opstack_t *it = &opstackv[ opstack ];
	if ( it->type == TYPE_RX ) {
		return &rx_regs[ it->value ];
	} else {
		return NULL;
	}
}


static void wipe_vars( void )
{
#ifdef LOAD_OPTIMIZE
	uint32_t i;
	reg_t *r;

	for ( i = 0; i < ARRAY_LEN( rx_regs ); i++ ) {
		r = &rx_regs[i];
		memset( &r->vars, 0, sizeof( r->vars ) );
		r->type_mask &= ~RTYPE_VAR;
		r->ext = Z_NONE;
	}
	for ( i = 0; i < ARRAY_LEN( sx_regs ); i++ ) {
		r = &sx_regs[i];
		memset( &r->vars, 0, sizeof( r->vars ) );
		r->type_mask &= ~RTYPE_VAR;
		r->ext = Z_NONE;
	}
#endif
}


static qboolean search_opstack( opstack_value_t type, uint32_t value ) {
	int i;
	for ( i = 1; i <= opstack; i++ ) {
		if ( opstackv[i].type == type && opstackv[i].value == value ) {
			return qtrue;
		}
	}
	return qfalse;
}


static void wipe_rx_meta( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( reg >= ARRAY_LEN( rx_regs ) )
		DROP( "incorrect register index %i", reg );
#endif
	memset( &rx_regs[reg], 0, sizeof( rx_regs[0] ) );
	//rx_regs[reg].type_mask = RTYPE_UNUSED;
}


static void wipe_sx_meta( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( reg >= ARRAY_LEN( sx_regs ) )
		DROP( "incorrect register index %i", reg );
#endif
	memset( &sx_regs[reg], 0, sizeof( sx_regs[0] ) );
	//sx_regs[reg].type_mask = RTYPE_UNUSED;
}


static void mask_rx( uint32_t reg )
{
	rx_mask[reg]++;
}


static void mask_sx( uint32_t reg )
{
	sx_mask[reg]++;
}


static void unmask_rx( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( rx_mask[reg] <= 0 ) {
		DROP( "register R%i is already unmasked", reg );
	}
#endif
	rx_mask[reg]--;
}


static void unmask_sx( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( sx_mask[reg] <= 0 ) {
		DROP( "register S%i is already unmasked", reg );
	}
#endif
	sx_mask[reg]--;
}


static void emit_MOVSxi( uint32_t reg, uint32_t imm )
{
	uint32_t rx;

	//if ( imm == 0 ) {
	//	fmov.f32 d0, #0.0? it will wipe s1 too so we need to use s0, s2, s4, s6 etc.
	//	return;
	//}

	if ( can_encode_f32_imm( imm ) ) {
		emit( VMOVi( reg, encode_f32_imm( imm ) ) );
		return;
	}

	rx = alloc_rx_const( R2, imm ); // rx = imm
	emit(VMOVass(reg, rx)); // arm core register -> singe precision register
	unmask_rx( rx );
}


static void set_local_address( uint32_t reg, uint32_t addr )
{
	if ( can_encode( addr ) ) {
		emit(ADDi(reg, rPSTACK, addr));      // r2 = pstack + addr
	} else {
		if ( find_rx_const( addr ) ) {
			uint32_t rx = alloc_rx_const( R3, addr ); // rx = const
			emit(ADD(reg, rPSTACK, rx));         // reg = pstack + rx
			unmask_rx( rx );
		} else {
			emit_MOVRxi(reg, addr);              // r2 = arg
			emit(ADD(reg, rPSTACK, reg));        // ref = pstack + reg
		}
	}
}


static void flush_item( opstack_t *it )
{
	uint32_t rx;

	switch ( it->type ) {

		case TYPE_RX:
			if ( it->offset >= 0 )
				emit(STRai(it->value, rOPSTACK, it->offset)); // *opstack = rX
			break;

		case TYPE_SX:
			emit(VSTRai(it->value, rOPSTACK, it->offset)); // *opstack = sX
			break;

		case TYPE_CONST:
			rx = alloc_rx_const( R2, it->value );
			emit(STRai(rx, rOPSTACK, it->offset)); // *opstack = r2
			unmask_rx( rx );
			break;

		case TYPE_LOCAL:
			rx = alloc_rx_local( R2 | TEMP, it->value );
			emit(STRai(rx, rOPSTACK, it->offset));      // *opstack = r2
			unmask_rx( rx );
			break;

		default:
			break;
	}

	it->type = TYPE_RAW;
	it->safe_arg = 0;
}


static void flush_items( opstack_value_t type, uint32_t value ) {
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == type && it->value == value ) {
			flush_item( it );
		}
	}
}


static void init_opstack( void )
{
	opstack = 0;

	Com_Memset( &rx_mask[0], 0, sizeof( rx_mask ) );
	Com_Memset( &sx_mask[0], 0, sizeof( sx_mask ) );

	Com_Memset( &opstackv[0], 0, sizeof( opstackv ) );

	Com_Memset( &rx_regs[0], 0, sizeof( rx_regs ) );
	Com_Memset( &sx_regs[0], 0, sizeof( sx_regs ) );
}


static qboolean scalar_on_top( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
#ifdef FPU_OPTIMIZE
	if ( opstackv[ opstack ].type == TYPE_SX )
		return qtrue;
#endif
	return qfalse;
}


static qboolean addr_on_top( var_addr_t *addr )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
#ifdef ADDR_OPTIMIZE
	if ( opstackv[ opstack ].type == TYPE_CONST ) {
		addr->addr = opstackv[opstack].value;
		addr->base = rDATABASE;
		addr->size = 0;
		return qtrue;
	}
	if ( opstackv[ opstack ].type == TYPE_LOCAL ) {
		addr->addr = opstackv[opstack].value;
		addr->base = rPROCBASE;
		addr->size = 0;
		return qtrue;
	}
#endif
	return qfalse;
}


static void discard_top( void )
{
	opstack_t *it = &opstackv[ opstack ];
	it->type = TYPE_RAW;
	it->safe_arg = 0;
}

#if 1
static int is_safe_arg( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
	return opstackv[ opstack ].safe_arg;
}
#endif

static void inc_opstack( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE )
		DROP( "opstack overflow - %i", opstack * 4 );
#endif

	opstack += 1;

#ifdef DEBUG_VM
	if ( opstackv[ opstack ].type != TYPE_RAW )
		DROP( "bad item type %i at opstack %i", opstackv[ opstack ].type, opstack * 4 );
#endif
}


static void dec_opstack( void )
{
#ifdef DEBUG_VM
	opstack_t *it;

	if ( opstack <= 0 )
		DROP( "opstack underflow - %i", opstack * 4 );

	it = &opstackv[ opstack ];
	if ( it->type != TYPE_RAW )
		DROP( "opstack[%i]: item type %i is not consumed", opstack * 4, it->type );
#endif
	opstack -= 1;
}


static void dec_opstack_discard( void )
{
	opstack_t *it;

	it = &opstackv[ opstack ];
#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "opstack underflow - %i", opstack * 4 );

	if ( it->type != TYPE_RAW && ( it->type != TYPE_RX || it->offset >= 0 ) )
		DROP( "opstack[%i]: item type %i is not consumed", opstack * 4, it->type );
#endif

	it->type = TYPE_RAW; // discard value
	it->safe_arg = 0;

	opstack -= 1;
}


// returns bitmask of registers present on opstack
static uint32_t build_opstack_mask( opstack_value_t reg_type )
{
	uint32_t mask = 0;
	int i;
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == reg_type ) {
			mask |= ( 1 << it->value );
		}
	}
	return mask;
}


static uint32_t build_rx_mask( void )
{
	uint32_t i, mask = 0;
	for ( i = 0; i < ARRAY_LEN( rx_mask ); i++ ) {
		if ( rx_mask[i] ) {
			mask |= 1 << i;
		}
	}
	return mask;
}


static uint32_t build_sx_mask( void )
{
	uint32_t i, mask = 0;
	for ( i = 0; i < ARRAY_LEN( sx_mask ); i++ ) {
		if ( sx_mask[i] ) {
			mask |= 1 << i;
		}
	}
	return mask;
}


// allocate register with local address value
static uint32_t alloc_rx_local( uint32_t pref, uint32_t imm )
{
	uint32_t rx = alloc_rx( pref );
	set_local_address( rx, imm );

	return rx;
}


// returns qtrue if specified constant is found or there is a free register to store it
static qboolean find_rx_const( uint32_t imm )
{
#ifdef CONST_CACHE_RX
	uint32_t mask = build_rx_mask() | build_opstack_mask( TYPE_RX );
	int i;

	for ( i = 0; i < ARRAY_LEN( rx_list_cache ); i++ ) {
		reg_t *r;
		uint32_t n = rx_list_cache[ i ];
		if ( mask & ( 1 << n ) ) {
			// target register must be unmasked
			continue;
		}
		r = &rx_regs[ n ];
		if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
			return qtrue;
		}
		if ( r->type_mask == RTYPE_UNUSED ) {
			return qtrue;
		}
	}
#endif
	return qfalse;
}


// allocate integer register with constant value
static uint32_t alloc_rx_const( uint32_t pref, uint32_t imm )
{
#ifdef CONST_CACHE_RX
	reg_t *r;
#endif
	uint32_t rx;

#ifdef CONST_CACHE_RX
#ifdef DYN_ALLOC_RX
	if ( ( pref & FORCED ) == 0 ) {
		// support only dynamic allocation mode
		const uint32_t mask = build_rx_mask() | build_opstack_mask( TYPE_RX );
		int min_ref = MAX_QINT;
		int min_ip = MAX_QINT;
		int idx = -1;
		int i, n;

		if ( ( pref & XMASK ) == 0 ) {
			// we can select from already masked registers
			for ( n = 0; n < ARRAY_LEN( rx_regs ); n++ ) {
				r = &rx_regs[n];
				if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
					r->refcnt++;
					r->ip = ip;
					mask_rx( n );
					return n;
				}
			}
		}

		for ( i = 0; i < ARRAY_LEN( rx_list_cache ); i++ ) {
			n = rx_list_cache[i];
			if ( mask & ( 1 << n ) ) {
				// target register must be unmasked and not present on the opStack
				continue;
			}
			r = &rx_regs[n];
			if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
				// exact match, re-use this register
				r->refcnt++; // increase reference count
				r->ip = ip;  // update address too
				mask_rx( n );
				return n;
			}
			if ( r->type_mask == RTYPE_UNUSED ) {
				idx = n;
				break;
			}
			if ( ( r->refcnt < min_ref ) || ( r->refcnt == min_ref && r->ip < min_ip ) ) {
				// update least referenced item index
				min_ref = r->refcnt;
				min_ip = r->ip;
				idx = n;
				continue;
			}
		}
		if ( idx != -1 ) {
			r = &rx_regs[ idx ];
			memset( &r->vars, 0, sizeof( r->vars ) );
			r->type_mask = RTYPE_CONST;
			r->cnst.value = imm;
			r->refcnt = 1;
			r->ip = ip;
			r->ext = Z_NONE;
			emit_MOVRxi( idx, imm );
			mask_rx( idx );
			return idx;
		}
		// else go to usual allocation to handle register spilling
	}
#endif // DYN_ALLOC_RX
#endif // CONST_CACHE_RX

	rx = alloc_rx( pref );
	emit_MOVRxi( rx, imm );

#ifdef CONST_CACHE_RX
	r = &rx_regs[ rx ];
	//memset( &r->vars, 0, sizeof( r->vars ) );
	r->type_mask = RTYPE_CONST;
	r->cnst.value = imm;
	r->refcnt = 1;
	r->ip = ip;
	//r->ext = Z_NONE;
#endif

	return rx;
}


// allocate scalar register with constant value
static uint32_t alloc_sx_const( uint32_t pref, uint32_t imm )
{
#ifdef CONST_CACHE_SX
	reg_t *r;
#endif
	uint32_t sx;

#ifdef CONST_CACHE_SX
#ifdef DYN_ALLOC_SX
	if ( ( pref & FORCED ) == 0 ) {
		// support only dynamic allocation mode
		const uint32_t mask = build_sx_mask() | build_opstack_mask( TYPE_SX );
		int min_ref = MAX_QINT;
		int min_ip = MAX_QINT;
		int idx = -1;
		int i, n;

		if ( ( pref & XMASK ) == 0 ) {
			// we can select from already masked registers
			for ( n = 0; n < ARRAY_LEN( sx_regs ); n++ ) {
				r = &sx_regs[n];
				if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
					r->refcnt++;
					r->ip = ip;
					mask_sx( n );
					return n;
				}
			}
		}

		for ( i = 0; i < ARRAY_LEN( sx_list_cache ); i++ ) {
			n = sx_list_cache[i];
			if ( mask & ( 1 << n ) ) {
				// target register must be unmasked and not present on the opStack
				continue;
			}
			r = &sx_regs[n];
			if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
				// exact match, re-use this register
				r->refcnt++; // increase reference count
				r->ip = ip;  // update address too
				mask_sx( n );
				return n;
			}
			if ( r->type_mask == RTYPE_UNUSED ) {
				idx = n;
				break;
			}
			if ( ( r->refcnt < min_ref ) || ( r->refcnt == min_ref && r->ip < min_ip ) ) {
				// update least referenced item index
				min_ref = r->refcnt;
				min_ip = r->ip;
				idx = n;
				continue;
			}
		}
		if ( idx != -1 ) {
			r = &sx_regs[ idx ];
			memset( &r->vars, 0, sizeof( r->vars ) );
			r->type_mask = RTYPE_CONST;
			r->cnst.value = imm;
			r->refcnt = 1;
			r->ip = ip;
			r->ext = Z_NONE;
			emit_MOVSxi( idx, imm );
			mask_sx( idx );
			return idx;
		}
		// else go to usual allocation to handle register spilling
	}
#endif // DYN_ALLOC_SX
#endif // CONST_CACHE_SX

	sx = alloc_sx( pref );
	emit_MOVSxi( sx, imm );

#ifdef CONST_CACHE_SX
	r = &sx_regs[sx];
	//memset( &r->vars, 0, sizeof( r->vars ) );
	r->type_mask = RTYPE_CONST;
	r->cnst.value = imm;
	r->refcnt = 1;
	r->ip = ip;
	r->ext = Z_NONE;
#endif

	return sx;
}


static uint32_t dyn_alloc_rx( uint32_t pref )
{
	const uint32_t _rx_mask = build_rx_mask();
	const uint32_t mask = _rx_mask | build_opstack_mask( TYPE_RX );
	const reg_t *reg, *used = NULL;
	uint32_t i, n;

	// try to bypass registers with metadata
	for ( i = 0; i < ARRAY_LEN( rx_list_alloc ); i++ ) {
		n = rx_list_alloc[i];
		if ( mask & ( 1 << n ) ) {
			continue;
		}
		reg = &rx_regs[n];
		if ( reg->type_mask != RTYPE_UNUSED ) {
			// mark least used item
			if ( !used || reg->refcnt < used->refcnt || ( reg->refcnt == used->refcnt && reg->ip < used->ip ) ) {
				used = reg;
			}
			continue;
		}
		wipe_rx_meta( n );
		mask_rx( n );
		return n;
	}

	if ( used ) {
		// no free slots but something occupied by metadata
		uint32_t idx = used - rx_regs;
		wipe_rx_meta( idx );
		mask_rx( idx );
		return idx;
	}

	// no free registers, flush bottom of the opStack
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_RX ) {
			n = it->value;
			// skip masked registers
			if ( _rx_mask & ( 1 << n ) ) {
				continue;
			}
			flush_item( it );
			flush_items( TYPE_RX, n ); // flush cloned registers too
			wipe_rx_meta( n );
			mask_rx( n );
			return n;
		}
	}

	return ~0U;
}


// integer register allocation
static uint32_t alloc_rx( uint32_t pref )
{
	uint32_t reg;

#ifdef DYN_ALLOC_RX
	if ( ( pref & FORCED ) == 0 ) {
		uint32_t v = dyn_alloc_rx( pref );
		if ( v == ~0U ) {
			DROP( "no free registers at ip %i, pref %x, opStack %i, mask %04x", ip, pref, opstack * 4, build_rx_mask() );
		}
		return v;
	}
#endif

	reg = pref & RMASK;

#ifdef DEBUG_VM
	if ( rx_mask[reg] )
		DROP( "forced register R%i is already masked!", reg );
#endif

	// FORCED option: find and flush target register
	flush_items( TYPE_RX, reg );

	wipe_rx_meta( reg );
	mask_rx( reg );
	return reg;
}


static uint32_t dyn_alloc_sx( uint32_t pref )
{
	const uint32_t _sx_mask = build_sx_mask();
	const uint32_t mask = _sx_mask | build_opstack_mask( TYPE_SX );
	const reg_t *reg, *used = NULL;
	uint32_t i, n;

	// try to bypass registers with metadata
	for ( i = 0; i < ARRAY_LEN( sx_list_alloc ); i++ ) {
		n = sx_list_alloc[i];
		if ( mask & ( 1 << n ) ) {
			continue;
		}
		reg = &sx_regs[n];
		if ( reg->type_mask != RTYPE_UNUSED ) {
			// mark least used item
			if ( !used || reg->refcnt < used->refcnt || ( reg->refcnt == used->refcnt && reg->ip < used->ip ) ) {
				used = reg;
			}
			continue;
		}
		wipe_sx_meta( n );
		mask_sx( n );
		return n;
	}

	if ( used ) {
		// no free slots but something occupied by metadata
		uint32_t idx = used - sx_regs;
		wipe_sx_meta( idx );
		mask_sx( idx );
		return idx;
	}

	// no free registers, flush bottom of the opStack
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_SX ) {
			n = it->value;
			// skip masked registers
			if ( _sx_mask & ( 1 << n ) ) {
				continue;
			}
			flush_item( it );
			flush_items( TYPE_SX, n ); // flush cloned registers too
			wipe_sx_meta( n );
			mask_sx( n );
			return n;
		}
	}

	return ~0U;
}


// scalar register allocation
static uint32_t alloc_sx( uint32_t pref )
{
	uint32_t reg;

#ifdef DYN_ALLOC_SX
	if ( ( pref & FORCED ) == 0 ) {
		uint32_t v = dyn_alloc_sx( pref );
		if ( v == ~0U ) {
			DROP( "no free registers at ip %i, pref %x, opStack %i, mask %04x", ip, pref, opstack * 4, build_sx_mask() );
		}
		return v;
	}
#endif

	reg = pref & RMASK;

#ifdef DEBUG_VM
	if ( sx_mask[reg] )
		DROP( "forced register S%i is already masked!", reg );
#endif

	// FORCED option: find and flush target register
	flush_items( TYPE_SX, reg );

	wipe_sx_meta( reg );
	mask_sx( reg );
	return reg;
}


/*
==============
flush_volatile

flush any cached register/address/constant to opstack and reset meta (constants mapping)
this MUST be called before any unconditional jump, return or function call
==============
*/
static void flush_volatile( void )
{
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_RX || it->type == TYPE_SX ) {
			flush_item( it );
		}
	}

	// wipe all constants metadata
	Com_Memset( &rx_regs[0], 0, sizeof( rx_regs ) );
	Com_Memset( &sx_regs[0], 0, sizeof( sx_regs ) );
}


static void flush_opstack( void )
{
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		flush_item( it );
	}

	// wipe all constants metadata
	Com_Memset( &rx_regs[0], 0, sizeof( rx_regs ) );
	Com_Memset( &sx_regs[0], 0, sizeof( sx_regs ) );
}


static void store_rx_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack * 4 );
#endif

	it->type = TYPE_RX;
	it->offset = opstack * sizeof( int32_t );
	it->value = reg;
	it->safe_arg = 0;

	unmask_rx( reg ); // so it can be flushed on demand
}


static void store_syscall_opstack( void )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack * 4 );
#endif

	it->type = TYPE_RX;
	it->offset = -1; // opstack * sizeof( int32_t )
	it->value = R0;
	it->safe_arg = 0;

	wipe_rx_meta( it->value );

	unmask_rx( it->value ); // so it can be flushed on demand
}


static void store_sx_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack * 4 );
#endif

	it->type = TYPE_SX;
	it->offset = opstack * sizeof( int32_t );
	it->value = reg;
	it->safe_arg = 0;

	unmask_sx( reg ); // so it can be flushed on demand
}


static void store_item_opstack( instruction_t *ins )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack * 4 );
#endif
	switch ( ins->op ) {
		case OP_CONST: it->type = TYPE_CONST; break;
		case OP_LOCAL: it->type = TYPE_LOCAL; break;
		default: DROP( "incorrect opcode %i", ins->op );
	}

	it->offset = opstack * sizeof( int32_t );
	it->value = ins->value;
	it->safe_arg = ins->safe;
}


static uint32_t finish_rx( uint32_t pref, uint32_t reg ) {

	if ( pref & RCONST ) {
		// non-destructive operation
		return reg;
	}

	if ( search_opstack( TYPE_RX, reg ) ) {
		// another instance is present on opStack
		if ( pref & FORCED ) {
			// nothing should left for a FORCED register
			flush_items( TYPE_RX, reg );
		} else {
			// copy it
			int rx = alloc_rx( R2 );
			emit(MOV(rx, reg));
			unmask_rx( reg );
			return rx;
		}
	}

	wipe_rx_meta( reg );
	return reg;
}


/*
===========
load_rx_opstack

loads current opstack value into specified register
returns masked register number, must be unmasked manually if not stored on the opstack
output register is very likely to be modified unless CONST preference is specified
===========
*/
static uint32_t load_rx_opstack( uint32_t pref )
{
	opstack_t *it = opstackv + opstack;
	uint32_t reg = pref & RMASK;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack*4 );
#endif

	if ( it->type == TYPE_RX ) {
#ifdef DYN_ALLOC_RX
		if ( !( pref & FORCED ) ) {
			mask_rx( it->value );
			it->type = TYPE_RAW;
			return finish_rx( pref, it->value ); // return current register
		}
#endif
		// FORCED flag: return exact target register
		if ( it->value == reg ) {
			mask_rx( it->value );
			it->type = TYPE_RAW;
			return finish_rx( pref, reg );
		} else {
			// allocate target register
			reg = alloc_rx( pref );

			// copy source to target
			emit(MOV(reg, it->value));

			it->type = TYPE_RAW;
			return reg;
		}
	} // it->type == TYPE_RX

	// scalar register on the stack
	if ( it->type == TYPE_SX ) {
		// move from scalar to general-purpose register
		reg = alloc_rx( pref );

		emit(VMOVssa(reg, it->value)); // singe precision register -> arm core register

		it->type = TYPE_RAW;
		return reg;
	}

	if ( ( pref & RCONST ) == 0 ) {
		pref |= XMASK;
	} // else we can search for constants in masked registers

	if ( it->type == TYPE_CONST ) {
		// move constant to general-purpose register
		reg = alloc_rx_const( pref, it->value );
		it->type = TYPE_RAW;
		return finish_rx( pref, reg );
	}

	if ( it->type == TYPE_LOCAL ) {
		reg = alloc_rx_local( pref, it->value );
		it->type = TYPE_RAW;
		return finish_rx( pref, reg );
	}

	// default raw type, explicit load from opStack
	reg = alloc_rx( pref );

	emit(LDRai(reg, rOPSTACK, opstack * sizeof(int32_t))); // rX = *opstack
	it->type = TYPE_RAW;
	return reg;
}


static uint32_t finish_sx( uint32_t pref, uint32_t reg ) {

	if ( pref & RCONST ) {
		// non-destructive operation
		return reg;
	}

	if ( search_opstack( TYPE_SX, reg ) ) {
		// another instance is present on opStack
		if ( pref & FORCED ) {
			// nothing should left for a FORCED register
			flush_items( TYPE_SX, reg );
		} else {
			// must be copied
			int sx = alloc_sx( S2 );
			emit(VMOV(sx, reg));
			unmask_sx( reg );
			return sx;
		}
	}

	wipe_sx_meta( reg );
	return reg;
}


static void load_rx_opstack2( uint32_t *dst, uint32_t dst_pref, uint32_t *src, uint32_t src_pref )
{
#if 0
	*dst = *src = load_rx_opstack( src_pref &= ~RCONST ); // source, target = *opstack
#else
	*dst = *src = load_rx_opstack( src_pref | RCONST ); // source, target = *opstack
	if ( search_opstack( TYPE_RX, *src ) || find_free_rx() ) {
		// *src is duplicated on opStack or there is a free register
		*dst = alloc_rx( dst_pref & ~RCONST ); // allocate new register for the target
	} else {
		// will be overwritten, wipe metadata
		wipe_rx_meta( *dst );
	}
#endif
}


// we must unmask register manually after allocation/loading
static uint32_t load_sx_opstack( uint32_t pref )
{
	opstack_t *it = opstackv + opstack;
	uint32_t reg = pref & RMASK;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack*4 );
#endif

	// scalar register on the stack
	if ( it->type == TYPE_SX ) {
#ifdef DYN_ALLOC_SX
		if ( !( pref & FORCED ) ) {
			mask_sx( it->value );
			it->type = TYPE_RAW;
			return finish_sx( pref, it->value );
		}
#endif
		// FORCED flag: return exact target register
		if ( it->value == reg ) {
			mask_sx( it->value );
			it->type = TYPE_RAW;
			return finish_sx( pref, reg );
		} else {
			// allocate target register
			reg = alloc_sx( pref );

			// copy source to target
			emit(VMOV(reg, it->value));

			it->type = TYPE_RAW;
			return reg;
		}
	}

	// integer register on the stack
	if ( it->type == TYPE_RX ) {
		// move from general-purpose to scalar register
		// should never happen with FPU type promotion, except syscalls
		reg = alloc_sx( pref );

		emit(VMOVass(reg, it->value)); // arm core register -> singe precision register

		it->type = TYPE_RAW;
		return reg;
	}

	if ( ( pref & RCONST ) == 0 ) {
		pref |= XMASK;
	} // else we can search for constants in masked registers

	if ( it->type == TYPE_CONST ) {
		// move constant to scalar register
		reg = alloc_sx_const( pref, it->value );
		it->type = TYPE_RAW;
		return finish_sx( pref, reg );
	}

	if ( it->type == TYPE_LOCAL ) {
		uint32_t rx;

		// bogus case: local address casted to float
		reg = alloc_sx( pref );
		rx = alloc_rx_local( R2 | RCONST, it->value );
		emit(VMOVass(reg, rx)); // arm core register -> singe precision register
		unmask_rx( rx );

		it->type = TYPE_RAW;
		return reg;
	}

	// default raw type, explicit load from opStack
	reg = alloc_sx( pref );
	emit(VLDRai(reg, rOPSTACK, opstack * sizeof( int32_t ))); // sX = *opstack
	it->type = TYPE_RAW;
	return reg;
}


static void load_sx_opstack2( uint32_t *dst, uint32_t dst_pref, uint32_t *src, uint32_t src_pref )
{
#if 0
	*dst = *src = load_sx_opstack( src_pref ); // source, target = *opstack
#else
	*dst = *src = load_sx_opstack( src_pref | RCONST ); // source, target = *opstack
	if ( search_opstack( TYPE_SX, *src ) || find_free_sx() ) {
		// *src is duplicated on opStack or there is a free register
		*dst = alloc_sx( dst_pref &= ~RCONST ); // allocate new register for the target
	} else {
		// will be overwritten, wipe metadata
		wipe_sx_meta( *dst );
	}
#endif
}


static uint32_t get_comp( int op )
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
		case OP_LTF: return MI;
		case OP_LEF: return LS;
		case OP_GTF: return GT;
		case OP_GEF: return GE;
		default: DROP( "unexpected op %i", op );
	}

	return 0;
}


static uint32_t encode_offset( uint32_t ofs )
{
	const uint32_t x = (ofs - 8) >> 2;
	const uint32_t t = x >> 24;
	if ( t != 0x3F && t != 0x00 )
		DROP( "%s: can't encode %i", __func__, ofs );
	return x & 0x00FFFFFF;
}


static void emitAlign( const uint32_t align )
{
	while ( compiledOfs & (align-1) )
		emit(NOP);
}


static void emitFuncOffset( uint32_t comp, vm_t *vm, offset_t func )
{
	uint32_t offset = savedOffset[ func ] - compiledOfs;

	emit( cond( comp, BLi( encode_offset( offset ) ) ) );
}


static void emit_CheckReg( vm_t *vm, uint32_t reg, offset_t func )
{
	if ( vm->forceDataMask || !( vm_rtChecks->integer & VM_RTCHECK_DATA ) ) {
		emit(AND(reg, rDATAMASK, reg));    // rN = rN & rDATAMASK
		return;
	}

	emit( CMP( reg, rDATAMASK ) );
	emitFuncOffset( HI, vm, func );
}


static void emit_CheckJump( vm_t *vm, uint32_t reg, int proc_base, int proc_len )
{
	if ( ( vm_rtChecks->integer & VM_RTCHECK_JUMP ) == 0 ) {
		return;
	}

	if ( proc_base != -1 ) {
		uint32_t rx[2];
		// allow jump within local function scope only
		// r2 = ip - proc_base
		rx[0] = alloc_rx( R2 | TEMP );
		if ( can_encode( proc_base ) )
			emit(SUBi(rx[0], reg, proc_base)); // r2 = reg - procBase
		else {
			emit_MOVRxi(rx[0], proc_base);     // r2 = procBase
			emit(SUB(rx[0], reg, rx[0]));      // r2 = reg - r2
		}
		// (ip > proc_len) ?
		if ( can_encode( proc_len ) ) {
			emit(CMPi(rx[0], proc_len));
		} else {
			rx[1] = alloc_rx_const( R1, proc_len ); // r1 = procLen
			emit(CMP(rx[0], rx[1]));
			unmask_rx( rx[1] );
		}
		emitFuncOffset( HI, vm, FUNC_OUTJ ); // error if unsigned higher
		unmask_rx( rx[0] );
	} else {
		uint32_t rx = alloc_rx( R2 | TEMP );
		// check if reg >= vm->instructionCount
		emit_MOVRxi(rx, vm->instructionCount);
		//emit(LDRai(rx, rVMBASE, offsetof(vm_t, instructionCount)));
		emit(CMP(reg, rx));
		emitFuncOffset( HS, vm, FUNC_OUTJ ); // error if unsigned higher or same
		unmask_rx( rx );
	}
}


static void emit_CheckProc( vm_t *vm, instruction_t *inst )
{
	// programStack overflow check
	if ( vm_rtChecks->integer & VM_RTCHECK_PSTACK ) {
		// check if pStack < vm->stackBottom
		uint32_t rx = alloc_rx( R2 | TEMP );
		emit(LDRai(rx, rVMBASE, offsetof(vm_t, stackBottom))); // r1 = vm->stackBottom
		emit(CMP(rPSTACK, rx));
		emitFuncOffset( LT, vm, FUNC_PSOF );
		unmask_rx( rx );
	}

	// opStack overflow check
	if ( vm_rtChecks->integer & VM_RTCHECK_OPSTACK ) {
		uint32_t n = inst->opStack;          // proc->opStack carries max.used opStack value
		uint32_t rx = alloc_rx( R2 | TEMP );
		if ( can_encode( n ) ) {
			emit(ADDi(rx, rOPSTACK, n)); // r2 = opstack + n;
		} else {
			emit_MOVRxi(rx, n);          // r2 = n
			emit(ADD(rx, rOPSTACK, rx)); // r2 = opstack + r2;
		}
		emit(CMP(rx, rOPSTACKTOP));
		emitFuncOffset( HI, vm, FUNC_OSOF ); // error if unsigned higher
		unmask_rx( rx );
	}
}


static void emitCallFunc( vm_t *vm )
{
	static int bytes_to_skip = -1;
	static unsigned start_block = -1;

	init_opstack(); // to avoid any side-effects on emit_CheckJump()

savedOffset[ FUNC_CALL ] = compiledOfs; // to jump from OP_CALL

	emit(CMPi(R0, 0)); // check if syscall

	if (start_block == -1)
		start_block = compiledOfs;

	emit(cond(LT, Bi(encode_offset(bytes_to_skip))));

	// check if R0 >= header->instructionCount
	mask_rx( R0 );
	emit_CheckJump( vm, R0, -1, 0 );
	unmask_rx( R0 );

	// local function call
	emit(LDRa(R12, rINSPOINTERS, rLSL(2, R0))); // r12 = instructionPointers[r0]
	emit(BX(R12)); // keep LR so OP_LEAVE will return directly to our caller
	//emit(BKPT(0));

	// syscall
	if (bytes_to_skip == -1)
		bytes_to_skip = compiledOfs - start_block;

savedOffset[ FUNC_SYSC ] = compiledOfs; // to jump from OP_CALL

	emit(MVN(R0, R0));   // r0 = ~r0

savedOffset[ FUNC_SYSF ] = compiledOfs; // to jump from ConstOptimize()

	// save LR because it will be clobbered by BLX instruction
	emit(PUSH((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rPROCBASE)|(1<<LR)));

	// modify VM stack pointer for recursive VM entry

	//currentVM->programStack = pstack - 8;
	emit(SUBi(R1, rPSTACK, 8)); // r1 = pstack - 8
	emit(STRai(R1, rVMBASE, offsetof(vm_t, programStack))); // vm->programStack = r1

	//argPosition = (intptr_t *)((byte *)currentVM->dataBase + pstack + 4);
	emit(ADDi(R2,rPROCBASE,4)); // r2 = rPROCBASE + 4

	//argPosition[0] = call;
	emit(STRai(R0, R2, 0)); // r2[0] = r0

	emit(MOV(R0,R2));

	//ret = currentVM->systemCall( argPosition );
	emit(LDRai(R12, rVMBASE, offsetof(vm_t,systemCall))); // r12 = vm->systemCall
	emit(BLX(R12)); // call [r12]( r0 )

	emit(POP((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rPROCBASE))|(1<<LR));

	// store return value
	emit(STRai(R0, rOPSTACK, 4)); // *(opstack+4) = r0

	// return to caller
	emit(MOV(PC,LR));
}


// R0 - src, R1 - dst, R2 - count, R3 - scratch
static void emitBlockCopyFunc( vm_t *vm )
{
	// adjust R2 if needed
	emit(AND(R0, R0, rDATAMASK)); // r0 &= dataMask
	emit(AND(R1, R1, rDATAMASK)); // r1 &= dataMask

	emit(ADD(R3, R0, R2));        // r3 = r0 + r2
	emit(AND(R3, R3, rDATAMASK)); // r3 &= dataMask
	emit(SUB(R2, R3, R0));        // r2 = r3 - r0

	emit(ADD(R3, R1, R2));        // r3 = r1 + r2
	emit(AND(R3, R3, rDATAMASK)); // r3 &= dataMask
	emit(SUB(R2, R3, R1));        // r2 = r3 - r1

	emit(ADD(R0, R0, rDATABASE)); // r0 += dataBase
	emit(ADD(R1, R1, rDATABASE)); // r1 += dataBase

	// word copy loop
	emit(CMPi(R2, 4));
	emit(cond(LT, Bi( encode_offset( +20 ))));
	emit(LDRTaiw(R3, R0, 4));      // r2 = [r0]; r0 += 4
	emit(STRTaiw(R3, R1, 4));      // [r1] = r2; r1 += 4
	emit(SUBi(R2, R2, 4));         // r3 -= 4
	emit(Bi(encode_offset( -20 )));

	// byte copy loop
	emit(CMPi(R2, 1));
	emit(cond(LT, Bi(encode_offset( +20 ))));
	emit(LDRBTaiw(R3, R0, 1));     // r2 = [r0]; r0 += 1
	emit(STRBTaiw(R3, R1, 1));     // [r1] = r2; r1 += 1
	emit(SUBi(R2, R2, 1));         // r3 -= 1
	emit(Bi(encode_offset( -20 )));

	emit(MOV(PC, LR));
}


static void emitBlockCopy( vm_t *vm, const uint32_t count )
{
	if ( count <= 12 )
	{
		uint32_t rx[4];
		int safe_arg[2];
		int i;

		rx[0] = load_rx_opstack( R0 ); // src: r0 = *opstack;
		safe_arg[0] = is_safe_arg();
		dec_opstack(); // opstack -= 4

		rx[1] = load_rx_opstack( R1 ); // dst: r1 = *opstack
		safe_arg[1] = is_safe_arg();
		dec_opstack(); // opstack -= 4

		if ( !safe_arg[0] ) {
			emit(AND(rx[0], rx[0], rDATAMASK)); // r0 &= dataMask
		}

		if ( !safe_arg[1] ) {
			emit(AND(rx[1], rx[1], rDATAMASK)); // r1 &= dataMask
		}

		rx[2] = alloc_rx( R2 | TEMP ); // scratch

		emit(ADD(rx[0], rx[0], rDATABASE)); // r0 += dataBase
		emit(ADD(rx[1], rx[1], rDATABASE)); // r1 += dataBase

		i = 0;

		// copy words
		for ( ; i < (count & ~3); i += 4 ) {
			emit(LDRai(rx[2], rx[0], i)); // r2 = dataBase[r0+i];
			emit(STRai(rx[2], rx[1], i)); // dataBase[r1+i] = r2;
		}

		// copy tail bytes
		for ( ; i < count; i += 1 ) {
			emit(LDRBai(rx[2], rx[0], i)); // r2 = dataBase[r0+i];
			emit(STRBai(rx[2], rx[1], i)); // dataBase[r1+i] = r2;
		}

		unmask_rx( rx[2] );
		unmask_rx( rx[1] );
		unmask_rx( rx[0] );

		return;
	}

	load_rx_opstack( R0 | FORCED ); dec_opstack(); // src: r0 = *opstack; opstack -= 4
	load_rx_opstack( R1 | FORCED ); dec_opstack(); // dst: r1 = *opstack; opstack -= 4
	alloc_rx( R2 | FORCED ); // counter
	alloc_rx( R3 | FORCED ); // scratch
	emit_MOVRxi(R2, count);
	emitFuncOffset(AL, vm, FUNC_BCPY);
	unmask_rx( R3 );
	unmask_rx( R2 );
	unmask_rx( R1 );
	unmask_rx( R0 );
}


#ifdef CONST_OPTIMIZE
static qboolean ConstOptimize( vm_t *vm, instruction_t *ci, instruction_t *ni )
{
	uint32_t rx[2];
	uint32_t sx[2];

	switch ( ni->op ) {

	case OP_ADD:
	case OP_SUB:
	case OP_BAND:
	case OP_BOR:
	case OP_BXOR:
		if ( can_encode( ci->value ) ) {
			//rx[1] = rx[0] = load_rx_opstack( R0 );	// r0 = *opstack
			load_rx_opstack2( &rx[1], R1, &rx[0], R0 ); // r1 = r0 = *opstack
			switch ( ni->op ) {
				case OP_ADD: emit( ADDi( rx[1], rx[0], ci->value ) ); break;  // r1 = r0 + x
				case OP_SUB: emit( SUBi( rx[1], rx[0], ci->value ) ); break;  // r1 = r0 - x
				case OP_BAND: emit( ANDi( rx[1], rx[0], ci->value ) ); break; // r1 = r0 & x
				case OP_BOR: emit( ORRi( rx[1], rx[0], ci->value ) ); break;  // r1 = r0 | x
				case OP_BXOR: emit( EORi( rx[1], rx[0], ci->value ) ); break; // r1 = r0 ^ x
			}
			if ( rx[0] != rx[1] ) {
				unmask_rx( rx[0] );
			}
			store_rx_opstack( rx[1] ); // *opstack = r1
			ip += 1; // OP_ADD
			return qtrue;
		}
		break;

	case OP_LSH:
	case OP_RSHI:
	case OP_RSHU:
		if ( ci->value <= 0 || ci->value > 31 )
			break;
		//rx[1] = rx[0] = load_rx_opstack( R0 );	// r0 = *opstack
		load_rx_opstack2( &rx[1], R1, &rx[0], R0 ); // r1 = r0 = *opstack
		switch ( ni->op ) {
			case OP_LSH: emit( LSLi( rx[1], rx[0], ci->value ) ); break; // r1 = r0 << x
			case OP_RSHI: emit( ASRi( rx[1], rx[0], ci->value ) ); break; // r1 = r0 >> x
			case OP_RSHU: emit( LSRi( rx[1], rx[0], ci->value ) ); break; // r1 = (unsigned)r0 >> x
		}
		if ( rx[0] != rx[1] ) {
			unmask_rx( rx[0] );
		}
		store_rx_opstack( rx[1] ); // *opstack = r1
		ip += 1;
		return qtrue;

	case OP_JUMP:
		flush_volatile();
		emit(Bi(encode_offset(vm->instructionPointers[ ci->value ] - compiledOfs)));
		ip += 1; // OP_JUMP
		return qtrue;

	case OP_CALL:
		inc_opstack(); // opstack += 4
		if ( ci->value == ~TRAP_SQRT ) {
			sx[0] = alloc_sx( S0 | TEMP );
			emit(VLDRai(sx[0], rPROCBASE, 8)); // s0 = [procBase + 8]
			emit(VSQRT_F32(sx[0], sx[0]));     // s0 = sqrt(s0)
			store_sx_opstack( sx[0] );
			ip += 1;
			return qtrue;
		}
		flush_volatile();
		if ( ci->value == ~TRAP_SIN || ci->value == ~TRAP_COS ) {
#if (__ARM_PCS_VFP)
			// -mfloat-abi=hard
			sx[0] = S0; mask_sx( sx[0] );
			rx[0] = alloc_rx( R12 );
			emit(VLDRai(sx[0], rPROCBASE, 8)); // s0 = [procBase + 8]
			if ( ci->value == ~TRAP_SIN )
				emit_MOVRxi(rx[0], (intptr_t)sinf);
			else
				emit_MOVRxi(rx[0], (intptr_t)cosf);
			emit(BLX(rx[0]));
			unmask_rx( rx[0] );
			store_sx_opstack( sx[0] );         // *opstack = s0
#else
			// -mfloat-abi=soft or softfp
			rx[0] = R0; mask_rx( rx[0] );
			rx[1] = R12; mask_rx( rx[1] );
			emit(LDRai(rx[0], rPROCBASE, 8));  // r0 = [procBase + 8]
			if ( ci->value == ~TRAP_SIN )
				emit_MOVRxi(rx[1], (intptr_t)sinf);
			else
				emit_MOVRxi(rx[1], (intptr_t)cosf);
			emit(BLX(rx[1]));
			unmask_rx( rx[1] );
			store_rx_opstack( rx[0] );         // *opstack = r0
#endif
			ip += 1; // OP_CALL
			return qtrue;
		}
		if ( ci->value < 0 ) { // syscall
			mask_rx( R0 );
			emit_MOVRxi(R0, ~ci->value); // r0 = syscall number
			if ( opstack != 1 ) {
				emit( ADDi( rOPSTACK, rOPSTACK, (opstack-1)*sizeof(int32_t) ) );
				emitFuncOffset( AL, vm, FUNC_SYSF );
				emit( SUBi( rOPSTACK, rOPSTACK, (opstack-1)*sizeof(int32_t) ) );
			} else {
				emitFuncOffset( AL, vm, FUNC_SYSF );
			}
			ip += 1; // OP_CALL;
			store_syscall_opstack();
			return qtrue;
		}
		if ( opstack != 1 ) {
			emit( ADDi( rOPSTACK, rOPSTACK, (opstack-1)*sizeof(int32_t) ) );
			emit(BLi(encode_offset(vm->instructionPointers[ ci->value ] - compiledOfs)));
			emit( SUBi( rOPSTACK, rOPSTACK, (opstack-1)*sizeof(int32_t) ) );
		} else {
			emit(BLi(encode_offset(vm->instructionPointers[ ci->value ] - compiledOfs)));
		}
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
	case OP_LTI:
		if ( can_encode( ci->value ) ) {
			uint32_t comp = get_comp( ni->op );
			rx[0] = load_rx_opstack( R0 | RCONST ); dec_opstack(); // r0 = *opstack; opstack -= 4
			emit( CMPi( rx[0], ci->value ) );
			emit( cond( comp, Bi( encode_offset( vm->instructionPointers[ni->value] - compiledOfs ) ) ) );
			unmask_rx( rx[0] );
			ip += 1; // OP_cond
			return qtrue;
		}
		break;

	default:
		break;
	}

	return qfalse;
}
#endif // CONST_OPTIMIZE


#ifdef DUMP_CODE
static void dump_code( const char *vmname, uint32_t *code, int32_t code_len )
{
	const char *filename = va( "vm-%s.hex", vmname );
	fileHandle_t fh = FS_FOpenFileWrite( filename );
	if ( fh != FS_INVALID_HANDLE ) {
		uint32_t i;
		for ( i = 0; i < code_len; i++ ) {
			FS_Printf( fh, "%02x %02x %02x %02x\n", ( code[i] >> 0 ) & 0xFF, ( code[i] >> 8 ) & 0xFF, ( code[i] >> 16 ) & 0xFF, ( code[i] >> 24 ) & 0xFF );
		}
		FS_FCloseFile( fh );
	}
}
#endif


qboolean VM_Compile( vm_t *vm, vmHeader_t *header )
{
	const char *errMsg;
	var_addr_t var;
	reg_t *reg;
	int proc_base;
	int proc_len;
	uint32_t rx[3];
	uint32_t sx[3];
	opcode_t sign_extend;
	int var_size;
	int i;

	if ( ( CPU_Flags & ( CPU_ARMv7 | CPU_VFPv3 ) ) != ( CPU_ARMv7 | CPU_VFPv3 ) ) {
		// ARMv7+ is required for MOVW/MOVT/MLS
		// VFPv3 is required for VMOVi
		return qfalse;
	}

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

	emit(PUSH(R4_R11|(1<<LR))); // push R4-R11, LR
	emit(SUBi(SP, SP, 12));     // align stack to 16 bytes

	// these are constant at compile time
	emit_MOVRxi(rVMBASE, (intptr_t)vm);
	emit(LDRai(rINSPOINTERS, rVMBASE, offsetof(vm_t, instructionPointers)));
	emit(LDRai(rDATABASE, rVMBASE, offsetof(vm_t, dataBase)));
	emit(LDRai(rDATAMASK, rVMBASE, offsetof(vm_t, dataMask)));

	// these are volatile variables
	emit(LDRai(rPSTACK, rVMBASE, offsetof(vm_t, programStack)));
	emit(LDRai(rOPSTACK, rVMBASE, offsetof(vm_t, opStack)));
	emit(LDRai(rOPSTACKTOP, rVMBASE, offsetof(vm_t, opStackTop)));

	emitFuncOffset( AL, vm, FUNC_ENTR );  // call vmMain()

#ifdef DEBUG_VM
	emit(STRai(rPSTACK, rVMBASE, offsetof(vm_t, programStack))); // vm->programStack = rPSTACK;
#endif

	emit(ADDi(SP, SP, 12));    // align stack to 16 bytes
	emit(POP(R4_R11|(1<<PC))); // pop R4-R11, LR -> PC

	//emit(BKPT(0));

#ifdef FUNC_ALIGN
	emitAlign( FUNC_ALIGN );
#endif

	savedOffset[ FUNC_ENTR ] = compiledOfs; // offset to vmMain() entry point

	while ( ip < header->instructionCount ) {

		ci = &inst[ ip + 0 ];

#ifdef REGS_OPTIMIZE
		if ( ci->jused )
#endif
		{
			// we can safely perform register optimizations only in case if
			// we are 100% sure that current instruction is not a jump label
			flush_volatile();
		}

		vm->instructionPointers[ ip++ ] = compiledOfs;

		switch ( ci->op )
		{
			case OP_UNDEF:
				emit(BKPT(1));
				break;

			case OP_IGNORE:
				break;

			case OP_BREAK:
				emit(BKPT(3));
				break;

			case OP_ENTER:
#ifdef FUNC_ALIGN
				emitAlign( FUNC_ALIGN );
#endif
				vm->instructionPointers[ ip - 1 ] = compiledOfs;

				proc_base = ip; // this points on next instruction after OP_ENTER
				// locate endproc
				for ( proc_len = -1, i = ip; i < header->instructionCount; i++ ) {
					if ( inst[ i ].op == OP_PUSH && inst[ i + 1 ].op == OP_LEAVE ) {
						proc_len = i - proc_base;
						break;
					}
				}

				emit(PUSH((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rPROCBASE)|(1<<LR)));

				if ( can_encode( ci->value ) ) {
					emit(SUBi(rPSTACK, rPSTACK, ci->value));    // pstack -= arg
				} else {
					rx[0] = alloc_rx_const( R2, ci->value );    // r2 = arg
					emit(SUB(rPSTACK, rPSTACK, rx[0])); // pstack -= r2
					unmask_rx( rx[0] );
				}

				emit_CheckProc( vm, ci );

				emit(ADD(rPROCBASE, rPSTACK, rDATABASE));
				break;

			case OP_LEAVE:
				flush_opstack();
				dec_opstack(); // opstack -= 4
#ifdef DEBUG_VM
				if ( opstack != 0 )
					DROP( "opStack corrupted on OP_LEAVE" );
#endif
				emit(POP((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rPROCBASE)|(1<<PC)));
				break;

			case OP_CALL:
				rx[0] = load_rx_opstack( R0 | FORCED ); // r0 = *opstack
				flush_volatile();
				if ( opstack != 1 ) {
					emit( ADDi( rOPSTACK, rOPSTACK, (opstack-1)*sizeof(int32_t) ) );
					emitFuncOffset(AL, vm, FUNC_CALL);
					emit( SUBi( rOPSTACK, rOPSTACK, (opstack-1)*sizeof(int32_t) ) );
				} else {
					emitFuncOffset(AL, vm, FUNC_CALL);
				}
				unmask_rx( rx[0] );
				break;

			case OP_PUSH:
				inc_opstack(); // opstack += 4
				if ( (ci + 1)->op == OP_LEAVE ) {
					proc_base = -1;
				}
				break;

			case OP_POP:
				dec_opstack_discard(); // opstack -= 4
				break;

			case OP_CONST:
#ifdef CONST_OPTIMIZE
				if ( ConstOptimize( vm, ci + 0, ci + 1 ) )
					break;
#endif
				inc_opstack(); // opstack += 4
				store_item_opstack( ci );
				break;

			case OP_LOCAL:
				inc_opstack(); // opstack += 4
				store_item_opstack( ci );
				break;

			case OP_JUMP:
				rx[0] = load_rx_opstack( R0 | RCONST ); dec_opstack(); // r0 = *opstack; opstack -= 4
				flush_volatile();
				emit_CheckJump( vm, rx[0], proc_base, proc_len ); // check if r0 is within current proc
				rx[1] = alloc_rx( R12 );
				emit(LDRa(rx[1], rINSPOINTERS, rLSL(2, rx[0]))); // r12 = instructionPointers[ r0 ]
				emit(BX(rx[1]));

				unmask_rx( rx[1] );
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
				rx[0] = load_rx_opstack( R0 | RCONST ); dec_opstack(); // r0 = *opstack; opstack -= 4
				rx[1] = load_rx_opstack( R1 | RCONST ); dec_opstack(); // r1 = *opstack; opstack -= 4
				unmask_rx( rx[0] );
				unmask_rx( rx[1] );
				emit(CMP(rx[1], rx[0]));
				emit(cond(comp, Bi(encode_offset(vm->instructionPointers[ci->value] - compiledOfs))));
				}
				break;

			case OP_EQF:
			case OP_NEF:
			case OP_LTF:
			case OP_LEF:
			case OP_GTF:
			case OP_GEF: {
				uint32_t comp = get_comp( ci->op );
				sx[0] = load_sx_opstack( S0 | RCONST ); dec_opstack(); // s0 = *opstack; opstack -= 4
				sx[1] = load_sx_opstack( S1 | RCONST ); dec_opstack(); // s1 = *opstack; opstack -= 4
				unmask_sx( sx[0] );
				unmask_sx( sx[1] );
				emit(VCMP_F32(sx[1], sx[0]));
				emit(VMRS(APSR_nzcv));
				emit(cond(comp, Bi(encode_offset(vm->instructionPointers[ci->value] - compiledOfs))));
				}
				break;

			case OP_LOAD1:
			case OP_LOAD2:
			case OP_LOAD4:
#ifdef FPU_OPTIMIZE
				if ( ci->op == OP_LOAD4 && ci->fpu ) {
					if ( addr_on_top( &var ) ) {
						// address specified by CONST/LOCAL
						discard_top();
						var.size = 4;
						if ( find_sx_var( &sx[0], &var ) ) {
							// already cached in some register
							mask_sx( sx[0] );
						} else {
							// not cached, perform load
							sx[0] = alloc_sx( S0 );
							if ( var.addr < 1024 && ( var.addr & 3 ) == 0 ) {
								// short offset
								emit( VLDRai( sx[0], var.base, var.addr ) ); // s0 = var.base[var.addr]
							} else {
								// long offset
								if ( can_encode( var.addr ) ) {
									rx[1] = alloc_rx( R1 );
									emit( ADDi( rx[1], var.base, var.addr ) ); // r1 = var.base + var.addr
									emit( VLDRai( sx[0], rx[1], 0 ) );		// s0 = [r1]
									unmask_rx( rx[1] );
								} else {
									rx[1] = alloc_rx_const( R1, var.addr );	// r1 = var.addr
									rx[2] = alloc_rx( R2 );
									emit( ADD( rx[2], rx[1], var.base ) );	// r2 = r1 + var.base
									emit( VLDRai( sx[0], rx[2], 0 ) );		// s = [r2]
									unmask_rx( rx[1] );
									unmask_rx( rx[2] );
								}
							}
							set_sx_var( sx[0], &var );						// update metadata, this may wipe constant
						}
					} else {
						// address specified by register
						rx[0] = load_rx_opstack( R0 );     // r0 = *opstack
						emit_CheckReg( vm, rx[0], FUNC_BADR );
						sx[0] = alloc_sx( S0 );
						// no indexing register mode for VLDR...
						emit( ADD( rx[0], rx[0], rDATABASE ) ); // r0 = r0 + database
						emit( VLDRai( sx[0], rx[0], 0 ) );      // s0 = [r0]
						unmask_rx( rx[0] );
					}
					store_sx_opstack( sx[0] );          // *opstack = s0
					break;
				}
#endif
				switch ( ci->op ) {
					case OP_LOAD1: var_size = 1; sign_extend = OP_SEX8; break;
					case OP_LOAD2: var_size = 2; sign_extend = OP_SEX16; break;
					default:       var_size = 4; sign_extend = OP_UNDEF; break;
				}
				// integer path
				if ( addr_on_top( &var ) ) {
					// address specified by CONST/LOCAL
					discard_top();
					var.size = var_size;
					if ( ( reg = find_rx_var( &rx[0], &var ) ) != NULL ) {
						// already cached in some register
						// do zero extension if needed
						switch ( ci->op ) {
							case OP_LOAD1:
								if ( reg->ext != Z_EXT8 ) {
									emit( UXTB( rx[0], rx[0] ) ); // r0 = (unsigned byte) r0
									// invalidate any mappings that overlaps with high [8..31] bits
									//var.addr += 1; var.size = 3;
									//wipe_reg_range( rx_regs + rx[0], &var );
									reduce_map_size( reg, 1 );
									// modify constant
									reg->cnst.value &= 0xFF;
									reg->ext = Z_EXT8;
								}
								break;
							case OP_LOAD2:
								if ( reg->ext != Z_EXT16 ) {
									emit( UXTH( rx[0], rx[0] ) ); // r0 = (unsigned short) r0
									// invalidate any mappings that overlaps with high [16..31] bits 
									//var.addr += 2; var.size = 2;
									//wipe_reg_range( rx_regs + rx[0], &var );
									reduce_map_size( reg, 2 );
									// modify constant
									reg->cnst.value &= 0xFFFF;
									reg->ext = Z_EXT16;
								}
								break;
							case OP_LOAD4:
								reg->ext = Z_NONE;
								break;
						}
						mask_rx( rx[0] );
					} else {
						// not cached, perform load
						int max_offset;
						rx[0] = alloc_rx( R0 );
						switch ( ci->op ) {
							case OP_LOAD1: max_offset = 4096; break;
							case OP_LOAD2: max_offset = 256; break;
							default:       max_offset = 4096; break;
						}
						if ( ( ci + 1 )->op == sign_extend && sign_extend != OP_UNDEF ) {
							// load with sign-extension
							if ( var.addr < 256 ) {
								// short offset
								switch ( ci->op ) {
									case OP_LOAD1: emit( LDRSBai( rx[0], var.base, var.addr ) ); set_rx_ext( rx[0], S_EXT8 ); break; // r0 = (signed char)var.base[var.addr]
									case OP_LOAD2: emit( LDRSHai( rx[0], var.base, var.addr ) ); set_rx_ext( rx[0], S_EXT16 ); break; // r0 = (signed short)var.base[var.addr]
								}
							} else {
								// long offset
								rx[1] = alloc_rx_const( R1, var.addr );
								switch ( ci->op ) {
									case OP_LOAD1: emit( LDRSBa( rx[0], var.base, rx[1] ) ); set_rx_ext( rx[0], S_EXT8 ); break; // r0 = (signed char)var.base[r1]
									case OP_LOAD2: emit( LDRSHa( rx[0], var.base, rx[1] ) ); set_rx_ext( rx[0], S_EXT16 ); break; // r0 = (signed short)var.base[r1]
								}
								unmask_rx( rx[1] );
							}
							ip += 1; // OP_SEX8 | OP_SEX16
						} else {
							// load with zero-extension
							if ( var.addr < max_offset ) {
								// short offset
								switch ( ci->op ) {
									case OP_LOAD1: emit( LDRBai( rx[0], var.base, var.addr ) ); set_rx_ext( rx[0], Z_EXT8 ); break; // r0 = (unsigned char)var.base[var.addr]
									case OP_LOAD2: emit( LDRHai( rx[0], var.base, var.addr ) ); set_rx_ext( rx[0], Z_EXT16 ); break; // r0 = (unsigned short)var.base[var.addr]
									default:       emit( LDRai( rx[0], var.base, var.addr ) ); set_rx_ext( rx[0], Z_NONE ); break; // r0 = var.base[var.addr]
								}
							} else {
								// long offset
								rx[1] = alloc_rx_const( R1, var.addr );
								switch ( ci->op ) {
									case OP_LOAD1: emit( LDRBa( rx[0], var.base, rx[1] ) ); set_rx_ext( rx[0], Z_EXT8 ); break; // r0 = (unsigned char)var.base[r1]
									case OP_LOAD2: emit( LDRHa( rx[0], var.base, rx[1] ) ); set_rx_ext( rx[0], Z_EXT16 ); break; // r0 = (unsigned short)var.base[r1]
									default:       emit( LDRa( rx[0], var.base, rx[1] ) ); set_rx_ext( rx[0], Z_NONE ); break; // r0 = var.base[r1]
								}
								unmask_rx( rx[1] );
							}
						} // load with zero-extension
						set_rx_var( rx[0], &var );
					} // not cached, perform load
				} else {
					// address specified by register
					rx[0] = load_rx_opstack( R0 ); // r0 = *opstack
					emit_CheckReg( vm, rx[0], FUNC_BADR );
					if ( (ci+1)->op == sign_extend && sign_extend != OP_UNDEF ) {
						// merge with following sign-extension instruction
						switch ( ci->op ) {
							case OP_LOAD1: emit( LDRSBa( rx[0], rDATABASE, rx[0] ) ); set_rx_ext( rx[0], S_EXT8 ); break; // r0 = (signed char)dataBase[r0]
							case OP_LOAD2: emit( LDRSHa( rx[0], rDATABASE, rx[0] ) ); set_rx_ext( rx[0], S_EXT16 ); break; // r0 = (signed short)dataBase[r0]
						}
						ip += 1; // OP_SEX8/OP_SEX16
					} else {
						// usual load with zero-extension
						switch ( ci->op ) {
							case OP_LOAD1: emit( LDRBa( rx[0], rDATABASE, rx[0] ) ); set_rx_ext( rx[0], Z_EXT8 ); break; // r0 = (unsigned char)dataBase[r0]
							case OP_LOAD2: emit( LDRHa( rx[0], rDATABASE, rx[0] ) ); set_rx_ext( rx[0], Z_EXT16 ); break; // r0 = (unsigned short)dataBase[r0]
							default:       emit( LDRa( rx[0], rDATABASE, rx[0] ) );	set_rx_ext( rx[0], Z_NONE ); break; // r0 = dataBase[r0]
						}
					}
				}
				store_rx_opstack( rx[0] ); // *opstack = target
				break;

			case OP_STORE1:
			case OP_STORE2:
			case OP_STORE4:
				if ( scalar_on_top() && ci->op == OP_STORE4 ) {
					sx[0] = load_sx_opstack( S0 | RCONST ); dec_opstack(); // s0 = *opstack; opstack -= 4
					if ( addr_on_top( &var ) ) {
						// address specified by CONST/LOCAL
						discard_top(); dec_opstack();
						var.size = 4;
						if ( var.addr < 1024 && (var.addr & 3) == 0 ) {
							// short offset
							emit( VSTRai( sx[0], var.base, var.addr ) ); // var.base[var.addr] = s0
						} else {
							// long offset
							if ( can_encode( var.addr ) ) {
								rx[1] = alloc_rx( R1 );
								emit( ADDi( rx[1], var.base, var.addr ) ); // r1 = var.base + var.addr
								emit( VSTRai( sx[0], rx[1], 0 ) );		// [r1] = s0
								unmask_rx( rx[1] );
							} else {
								rx[1] = alloc_rx_const( R1, var.addr );	// r1 = var.addr
								rx[2] = alloc_rx( R2 );
								emit( ADD( rx[2], rx[1], var.base ) );	// r2 = r1 + var.base
								emit( VSTRai( sx[0], rx[2], 0 ) );		// [r2] = s0
								unmask_rx( rx[1] );
								unmask_rx( rx[2] );
							}
						}
						wipe_var_range( &var );
						set_sx_var( sx[0], &var );									// update metadata
					} else {
						// address specified by register
						rx[1] = load_rx_opstack( R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
						emit_CheckReg( vm, rx[1], FUNC_BADW );
						emit( ADD( rx[1], rx[1], rDATABASE ) ); // r1 = r1 + dataBase
						emit( VSTRai( sx[0], rx[1], 0 ) );      // [r1] = s0
						unmask_rx( rx[1] );
						wipe_vars(); // unknown/dynamic address, wipe all register mappings
					}
					unmask_sx( sx[0] );
				} else {
					// integer path
					rx[0] = load_rx_opstack( R0 | RCONST ); dec_opstack(); // r0 = *opstack; opstack -= 4
					if ( addr_on_top( &var ) ) {
						// address specified by CONST/LOCAL
						int max_offset;
						discard_top(); dec_opstack();
						switch ( ci->op ) {
							case OP_STORE1: var.size = 1; max_offset = 4096; break;
							case OP_STORE2: var.size = 2; max_offset = 256; break;
							default:        var.size = 4; max_offset = 4096; break;
						}
						if ( var.addr < max_offset ) {
							// short offset
							switch ( ci->op ) {
								case OP_STORE1: emit( STRBai( rx[0], var.base, var.addr ) ); break; // (byte*)var.base[var.addr] = r0
								case OP_STORE2: emit( STRHai( rx[0], var.base, var.addr ) ); break; // (short*)var.base[var.addr] = r0
								default:        emit( STRai( rx[0], var.base, var.addr ) ); break; // var.base[var.addr] = r0
							}
						} else {
							// long offset
							rx[1] = alloc_rx_const( R1, var.addr );
							switch ( ci->op ) {
								case OP_STORE1: emit( STRBa( rx[0], var.base, rx[1] ) ); break; // (byte*)var.base[r1] = r0
								case OP_STORE2: emit( STRHa( rx[0], var.base, rx[1] ) ); break; // (short*)var.base[r1] = r0
								default:        emit( STRa( rx[0], var.base, rx[1] ) ); break; // var.base[r1] = r0
							}
							unmask_rx( rx[1] );
						}
						wipe_var_range( &var );
						set_rx_var( rx[0], &var ); // update metadata
					} else {
						// address specified by register
						rx[1] = load_rx_opstack( R1 | RCONST ); dec_opstack(); // r1 = *opstack; opstack -= 4
						emit_CheckReg( vm, rx[1], FUNC_BADW );
						switch ( ci->op ) {
							case OP_STORE1: emit( STRBa( rx[0], rDATABASE, rx[1] ) ); break; // (byte*)database[r1] = r0
							case OP_STORE2: emit( STRHa( rx[0], rDATABASE, rx[1] ) ); break; // (short*)database[r1] = r0
							default:        emit( STRa( rx[0], rDATABASE, rx[1] ) ); break; // database[r1] = r0
						}
						unmask_rx( rx[1] );
						wipe_vars(); // unknown/dynamic address, wipe all register mappings
					}
					unmask_rx( rx[0] );
				}
				break;

			case OP_ARG:
				var.base = rPROCBASE;
				var.addr = ci->value;
				var.size = 4;
				wipe_var_range( &var );
				if ( scalar_on_top() ) {
					sx[0] = load_sx_opstack( S0 | RCONST ); dec_opstack(); // s0 = *opstack; opstack -=4
					// v is in range [8..252] so it is fit in VSTRai immediate encoding
					emit(VSTRai(sx[0], var.base, var.addr)); // [procBase + v] = s0
					unmask_sx( sx[0] );
				} else {
					rx[0] = load_rx_opstack( R0 | RCONST ); dec_opstack(); // r0 = *opstack; opstack -=4
					emit(STRai(rx[0], var.base, var.addr)); // [procBase + v] = r0
					unmask_rx( rx[0] );
				}
				break;

			case OP_BLOCK_COPY:
				emitBlockCopy( vm, ci->value );
				wipe_vars();
				break;

			case OP_SEX8:
			case OP_SEX16:
			case OP_NEGI:
			case OP_BCOM:
				if ( ci->op == OP_SEX8 || ci->op == OP_SEX16 ) {
					// skip sign-extension for `if ( var == 0 )` tests if we already zero-extended
					reg = rx_on_top();
					if ( reg && (ci+1)->op == OP_CONST && (ci+1)->value == 0 && ( (ci+2)->op == OP_EQ || (ci+2)->op == OP_NE ) ) {
						if ( !(ci+1)->jused && !(ci+2)->jused ) {
							if ( ci->op == OP_SEX8 && reg->ext == Z_EXT8 ) {
								break;
							}
							if ( ci->op == OP_SEX16 && reg->ext == Z_EXT16 ) {
								break;
							}
						}
					}
				}
				//rx[1] = rx[0] = load_rx_opstack( R0 ); // r0 = *opstack
				load_rx_opstack2( &rx[1], R0, &rx[0], R1 ); // rx1 = r0 = *opstack
				switch ( ci->op ) {
					case OP_SEX8:  emit(SXTB(rx[1], rx[0])); break; // r1 = sign extend r0
					case OP_SEX16: emit(SXTH(rx[1], rx[0])); break; // r1 = sign extend r0
					case OP_NEGI:  emit(RSBi(rx[1], rx[0], 0)); break; // r1 = -r0
					case OP_BCOM:  emit( MVN(rx[1], rx[0])); break; // r1 = ~r0
				}
				if ( rx[0] != rx[1] ) {
					unmask_rx( rx[0] );
				}
				store_rx_opstack( rx[1] ); // *opstack = r1
				break;

			case OP_DIVI:
			case OP_DIVU:
				if ( CPU_Flags & CPU_IDIVA ) {
					rx[0] = load_rx_opstack( R0 ); dec_opstack(); // r0 = *opstack
					rx[1] = load_rx_opstack( R1 ); // opstack-=4; r1 = *opstack
					if ( ci->op == OP_DIVI ) {
						emit(SDIV(rx[0], rx[1], rx[0]));
					} else {
						emit(UDIV(rx[0], rx[1], rx[0]));
					}
					store_rx_opstack( rx[0] ); // *opstack = r0
					unmask_rx( rx[1] );
				} else {
					rx[1] = load_rx_opstack( R1 | FORCED ); dec_opstack(); // r1 = *opstack
					rx[0] = load_rx_opstack( R0 | FORCED ); // opstack-=4; r0 = *opstack
					rx[2] = alloc_rx( R12 );
					flush_volatile();
					if ( ci->op == OP_DIVI )
						emit_MOVRxi(rx[2], (intptr_t)__aeabi_idiv);
					else
						emit_MOVRxi(rx[2], (intptr_t)__aeabi_uidiv);
					emit(BLX(rx[2]));
					store_rx_opstack( rx[0] ); // *opstack = r0
					unmask_rx( rx[1] );
					unmask_rx( rx[2] );
				}
				break;

			case OP_MODI:
			case OP_MODU:
				if ( CPU_Flags & CPU_IDIVA ) {
					rx[0] = load_rx_opstack( R0 ); dec_opstack(); // r0 = *opstack
					rx[1] = load_rx_opstack( R1 ); // opstack-=4; r1 = *opstack
					rx[2] = alloc_rx( R2 | TEMP );
					if ( ci->op == OP_MODI ) {
						emit(SDIV(rx[2], rx[1], rx[0])); // r2 = r1 / r0
					} else {
						emit(UDIV(rx[2], rx[1], rx[0])); // r2 = (unsigned)r1 / r0
					}
					emit(MLS(rx[0], rx[0], rx[2], rx[1]));   // r0 = r1 - r0 * r2
					store_rx_opstack( rx[0] ); // *opstack = r0
					unmask_rx( rx[1] );
					unmask_rx( rx[2] );
				} else {
					rx[1] = load_rx_opstack( R1 | FORCED ); dec_opstack(); // r1 = *opstack
					rx[0] = load_rx_opstack( R0 | FORCED ); // opstack-=4; r0 = *opstack
					rx[2] = alloc_rx( R12 );
					flush_volatile();
					if ( ci->op == OP_MODI )
						emit_MOVRxi(rx[2], (intptr_t)__aeabi_idivmod);
					else
						emit_MOVRxi(rx[2], (intptr_t)__aeabi_uidivmod);
					emit(BLX(rx[2]));
					store_rx_opstack( rx[1] ); // *opstack = r1
					unmask_rx( rx[0] );
					unmask_rx( rx[2] );
				}
				break;

			case OP_ADD:
			case OP_SUB:
			case OP_MULI:
			case OP_MULU:
			case OP_BAND:
			case OP_BOR:
			case OP_BXOR:
			case OP_LSH:
			case OP_RSHI:
			case OP_RSHU:
				//rx[2] = rx[0] = load_rx_opstack( R0 ); dec_opstack(); // r0 = *opstack
				load_rx_opstack2( &rx[2], R0, &rx[0], R2 ); dec_opstack(); // r2 = r0 = *opstack
				rx[1] = load_rx_opstack( R1 | RCONST ); // opstack-=4; r1 = *opstack
				switch ( ci->op ) {
					case OP_ADD:  emit(ADD(rx[2], rx[1], rx[0])); break; // r2 = r1 + r0
					case OP_SUB:  emit(SUB(rx[2], rx[1], rx[0])); break; // r2 = r1 - r0
					case OP_MULI:
					case OP_MULU: emit(MUL(rx[2], rx[1], rx[0])); break; // r2 = r1 * r0
					case OP_BAND: emit(AND(rx[2], rx[1], rx[0])); break; // r2 = r1 & r0
					case OP_BOR:  emit(ORR(rx[2], rx[1], rx[0])); break; // r2 = r1 | r0
					case OP_BXOR: emit(EOR(rx[2], rx[1], rx[0])); break; // r2 = r1 ^ r0
					case OP_LSH:  emit(LSL(rx[2], rx[1], rx[0])); break; // r2 = r1 << r0
					case OP_RSHI: emit(ASR(rx[2], rx[1], rx[0])); break; // r2 = r1 >> r0
					case OP_RSHU: emit(LSR(rx[2], rx[1], rx[0])); break; // r2 = (unsigned)r1 >> r0
				}
				if ( rx[0] != rx[2] ) {
					unmask_rx( rx[0] );
				}
				unmask_rx( rx[1] );
				store_rx_opstack( rx[2] ); // *opstack = r2
				break;

			case OP_ADDF:
			case OP_SUBF:
			case OP_MULF:
			case OP_DIVF:
				//sx[2] = sx[0] = load_sx_opstack( S0 ); dec_opstack(); // s0 = *opstack
				load_sx_opstack2( &sx[2], S0, &sx[0], S2 ); dec_opstack(); // s2 = s0 = *opstack
				sx[1] = load_sx_opstack( S1 | RCONST ); // opstack -= 4; s1 = *opstack
				switch ( ci->op ) {
					case OP_ADDF: emit(VADD_F32(sx[2], sx[1], sx[0])); break; // s2 = s1 + s0
					case OP_SUBF: emit(VSUB_F32(sx[2], sx[1], sx[0])); break; // s2 = s1 - s0
					case OP_MULF: emit(VMUL_F32(sx[2], sx[1], sx[0])); break; // s2 = s1 * s0
					case OP_DIVF: emit(VDIV_F32(sx[2], sx[1], sx[0])); break; // s2 = s1 / s0
				}
				if ( sx[0] != sx[2] ) {
					unmask_sx( sx[0] );
				}
				unmask_sx( sx[1] );
				store_sx_opstack( sx[2] ); // *opstack = s2
				break;

			case OP_NEGF:
			case OP_CVIF:
			case OP_CVFI:
				sx[0] = load_sx_opstack( S0 );		// s0 = *opstack
				switch ( ci->op ) {
					case OP_NEGF: emit( VNEG_F32( sx[0], sx[0] ) ); break; // s0 = -s0
					case OP_CVIF: emit( VCVT_F32_S32( sx[0], sx[0] ) );	break; // s0 = (float)s0
					case OP_CVFI: emit( VCVT_S32_F32( sx[0], sx[0] ) );	break; // s0 = (int)s0
				}
				store_sx_opstack( sx[0] );			// *opstack = s0
				break;

		} // switch op
	} // ip
#ifdef FUNC_ALIGN
		emitAlign( FUNC_ALIGN );
#endif
		// it will set multiple offsets
		emitCallFunc( vm );
#ifdef FUNC_ALIGN
		emitAlign( FUNC_ALIGN );
#endif
		savedOffset[ FUNC_BCPY ] = compiledOfs;
		emitBlockCopyFunc( vm );

		savedOffset[ FUNC_BADJ ] = compiledOfs;
		emit_MOVRxi(R12, (intptr_t)BadJump);
		emit(BLX(R12));
		//emit(BKPT(0));

		savedOffset[ FUNC_OUTJ ] = compiledOfs;
		emit_MOVRxi(R12, (intptr_t)OutJump);
		emit(BLX(R12));
		//emit(BKPT(0));

		savedOffset[ FUNC_OSOF ] = compiledOfs;
		emit_MOVRxi(R12, (intptr_t)ErrBadOpStack);
		emit(BLX(R12));
		//emit(BKPT(0));

		savedOffset[ FUNC_PSOF ] = compiledOfs;
		emit_MOVRxi(R12, (intptr_t)ErrBadProgramStack);
		emit(BLX(R12));
		//emit(BKPT(0));

		savedOffset[FUNC_BADR] = compiledOfs;
		emit_MOVRxi( R12, (intptr_t) ErrBadDataRead );
		emit( BLX( R12 ) );

		savedOffset[FUNC_BADW] = compiledOfs;
		emit_MOVRxi( R12, (intptr_t) ErrBadDataWrite );
		emit( BLX( R12 ) );

		//emit(BKPT(0));

	} // pass

	if ( vm->codeBase.ptr == NULL ) {
#ifdef _WIN32
		vm->codeBase.ptr = VirtualAlloc( NULL, compiledOfs, MEM_COMMIT, PAGE_EXECUTE_READWRITE );
		if ( !vm->codeBase.ptr ) {
			VM_FreeBuffers();
			Com_Printf( S_COLOR_YELLOW "%s(%s): VirtualAlloc failed\n", __func__, vm->name );
			return qfalse;
		}
#else
		vm->codeBase.ptr = mmap( NULL, compiledOfs, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
		if ( vm->codeBase.ptr == MAP_FAILED ) {
			VM_FreeBuffers();
			Com_Printf( S_COLOR_YELLOW "%s(%s): mmap failed\n", __func__, vm->name );
			return qfalse;
		}
#endif

		vm->codeLength = compiledOfs;
		vm->codeSize = compiledOfs;
		code = (uint32_t*)vm->codeBase.ptr;
		goto __recompile;
	}

#ifdef DUMP_CODE
	dump_code( vm->name, code, compiledOfs / 4 );
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

#ifdef _WIN32
	{
		DWORD oldProtect = 0;
		// remove write permissions
		if ( !VirtualProtect( vm->codeBase.ptr, vm->codeLength, PAGE_EXECUTE_READ, &oldProtect ) ) {
			VM_Destroy_Compiled( vm );
			Com_Printf( S_COLOR_YELLOW "%s(%s): VirtualProtect failed\n", __func__, vm->name );
			return qfalse;
		}
	}
#else
	if ( mprotect( vm->codeBase.ptr, vm->codeLength, PROT_READ | PROT_EXEC ) ) {
		VM_Destroy_Compiled( vm );
		Com_Printf( S_COLOR_YELLOW "%s(%s): mprotect failed\n", __func__, vm->name );
		return qfalse;
	}

	// clear icache, http://blogs.arm.com/software-enablement/141-caches-and-self-modifying-code/
	__clear_cache( vm->codeBase.ptr, vm->codeBase.ptr + vm->codeLength );
#endif

	vm->destroy = VM_Destroy_Compiled;

	Com_Printf( "VM file %s compiled to %i bytes of code\n", vm->name, vm->codeLength );

	return qtrue;
}


int32_t VM_CallCompiled( vm_t *vm, int nargs, int32_t *args )
{
	int32_t		opStack[ MAX_OPSTACK_SIZE ];
	int32_t		stackOnEntry;
	int32_t		*image;
	int		i;

	// we might be called recursively, so this might not be the very top
	stackOnEntry = vm->programStack;

	vm->programStack -= ( MAX_VMMAIN_CALL_ARGS + 2 ) * sizeof( int32_t );

	// set up the stack frame
	image = (int32_t*) ( vm->dataBase + vm->programStack );
	for ( i = 0; i < nargs; i++ ) {
		image[i + 2] = args[i];
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

	if ( vm->programStack != stackOnEntry - ( MAX_VMMAIN_CALL_ARGS + 2 ) * sizeof( int32_t ) ) {
		Com_Error( ERR_DROP, "%s(%s): programStack corrupted in compiled code", __func__, vm->name );
	}
#endif

	vm->programStack = stackOnEntry;

	return opStack[1];
}
