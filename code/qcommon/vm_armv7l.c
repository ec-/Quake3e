/*
===========================================================================
Copyright (C) 2009 David S. Miller <davem@davemloft.net>
Copyright (C) 2013,2014 SUSE Linux Products GmbH
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

ARMv7l VM by Ludwig Nussel <ludwig.nussel@suse.de>

TODO: optimization

Docu:
http://www.coranac.com/tonc/text/asm.htm
http://www.heyrick.co.uk/armwiki/Category:Opcodes
ARMv7-A_ARMv7-R_DDI0406_2007.pdf
*/

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <stddef.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#include "vm_local.h"

#define NUM_PASSES 1

// additional integrity checks
#define DEBUG_VM

// various optimizations
#define DYN_ALLOC_RX
#define DYN_ALLOC_SX

#define FPU_OPTIMIZE
#define CONST_OPTIMIZE
#define MISC_OPTIMIZE
#define MACRO_OPTIMIZE

//#define DUMP_CODE

typedef enum
{
	FUNC_ENTR = 0,
	FUNC_BCPY,
	FUNC_CALL,
	FUNC_SYSC,
	FUNC_SYSF,
	FUNC_PSOF,
	FUNC_OSOF,
	FUNC_BADJ,
	FUNC_OUTJ,
	FUNC_BADD,
	FUNC_LAST
} func_t;


// macro opcode sequences
typedef enum {
	MOP_UNDEF = OP_MAX,
	MOP_ADD4,
	MOP_SUB4,
	MOP_BAND4,
	MOP_BOR4,
} macro_op_t;

static uint32_t *code;
static uint32_t compiledOfs;
//static	uint32_t *instructionOffsets;
//static	intptr_t *instructionPointers;

static  instruction_t *inst = NULL;
static  instruction_t *ci;
static  instruction_t *ni;

static	uint32_t	ip;
static	uint32_t	pass;
static	uint32_t	funcOffset[FUNC_LAST];


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

#define R0	0  // scratch
#define R1	1  // scratch
#define R2	2  // scratch
#define R3	3  // scratch, opStackShift
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

#define rOPSTACKSHIFT	R3

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

/* exit() won't be called but use it because it is marked with noreturn */
#define DIE( reason, args... ) \
	do { \
		Com_Error(ERR_DROP, "vm_arm compiler error: " reason, ##args); \
		exit(1); \
	} while(0)


#define DROP( reason, args... ) \
	do { \
		VM_FreeBuffers(); \
		Com_Error( ERR_DROP, "%s: " reason, __func__, ##args ); \
	} while(0)


static void VM_Destroy_Compiled(vm_t *vm)
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


static unsigned char off8( unsigned val )
{
	if ( val & 3 )
		DROP( "offset must be multiple of four" );

	if ( val > 1020 )
		DROP( "offset %i is too large", val );

	return val >> 2;
}


// ARM is really crazy ...
static unsigned short rimm(unsigned val)
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
static unsigned short can_encode(unsigned val)
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

#define YIELD (0b110010<<20 | 0b1111<<12 | 1)
#define NOP cond(AL, YIELD)

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
#define LDRai(dst, base, off)  (AL | (0b010<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRBai(dst, base, off) (AL | (0b010<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | off)

// load word with pre-decrement
#define LDRx(dst, base, off)   (AL | (0b011<<25) | (0b1000<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRxi(dst, base, off)  (AL | (0b010<<25) | (0b1000<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRxiw(dst, base, off) (AL | (0b010<<25) | (0b1001<<21) | (1<<20) | base<<16 | dst<<12 | off)

// load word with post-increment
#define LDRTaw(dst, base, off)  (AL | (0b011<<25) | (0b0101<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRTaiw(dst, base, off) (AL | (0b010<<25) | (0b0101<<21) | (1<<20) | base<<16 | dst<<12 | off)

// load/store byte with post-increment
#define LDRBTaiw(dst, base, off)(AL | (0b010<<25) | (0b0111<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define STRBTaiw(dst, base, off)(AL | (0b010<<25) | (0b0111<<21) | (0<<20) | base<<16 | dst<<12 | off)

// load word with post-decrement
#define LDRTx(dst, base, off)  (AL | (0b011<<25) | (0b0001<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRTxi(dst, base, off) (AL | (0b010<<25) | (0b0001<<21) | (1<<20) | base<<16 | dst<<12 | off)

// load byte with pre-increment
#define LDRBa(dst, base, off)   (AL | (0b011<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRSBa(dst, base, off)  (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | 0b1101<<4 | off)
#define LDRSBai(dst, base, off) (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | ((((off)>>4)&0xF)<<8) | 0b1101<<4 | ((off)&0xF))

// load half-word with pre-increment
#define LDRHa(dst, base, off)   (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | (0b1011<<4) | off)
#define LDRSHa(dst, base, off)  (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | (0b1111<<4) | off)
#define LDRHai(dst, base, off)  (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | ((((off)>>4)&0xF)<<8) | (0b1011)<<4 | ((off)&0xF) )
#define LDRSHai(dst, base, off) (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | ((((off)>>4)&0xF)<<8) | (0b1111)<<4 | ((off)&0xF) )

// store byte/half-word with pre-increment
#define STRBa(dst, base, off)   (AL | (0b011<<25) | (0b1110<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRHa(dst, base, off)   (AL | (0b000<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | (0b1011<<4) | off)

// store word with pre-increment
#define STRa(dst, base, off)   (AL | (0b011<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRai(dst, base, off)  (AL | (0b010<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRaiw(dst, base, off) (AL | (0b010<<25) | (0b1101<<21) | (0<<20) | base<<16 | dst<<12 | off)

// store byte with pre-increment
#define STRBai(dst, base, off)  (AL | (0b010<<25) | (0b1110<<21) | (0<<20) | base<<16 | dst<<12 | off)

// store word with post-increment
#define STRTaiw(dst, base, off) (AL | (0b010<<25) | (0b0101<<21) | (0<<20) | base<<16 | dst<<12 | off)

// store word with pre-decrement
#define STRx(dst, base, off)   (AL | (0b011<<25) | (0b1000<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRxi(dst, base, off)  (AL | (0b010<<25) | (0b1000<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRxiw(dst, base, off) (AL | (0b010<<25) | (0b1001<<21) | (0<<20) | base<<16 | dst<<12 | off)

// sign-extend byte to word
#define SXTB(Rd, Rm)       (AL | (0b01101<<23) | (0b010<<20) | (0b1111<<16) | (Rd<<12) | (0b00000111<<4) | Rm)
// sign-extend short to word
#define SXTH(Rd, Rm)       (AL | (0b01101<<23) | (0b011<<20) | (0b1111<<16) | (Rd<<12) | (0b00000111<<4) | Rm)

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

#define VLDRai(Vd, Rn, i) (AL|(0b1101<<24)|1<<23|((Vd&1)<<22)|1<<20|(Rn<<16)|((Vd>>1)<<12)|(0b1010<<8)|off8(i))
#define VSTRai(Vd, Rn, i) (AL|(0b1101<<24)|1<<23|((Vd&1)<<22)|0<<20|(Rn<<16)|((Vd>>1)<<12)|(0b1010<<8)|off8(i))
#define VSTRxi(Vd, Rn, i) (AL|(0b1101<<24)|0<<23|((Vd&1)<<22)|0<<20|(Rn<<16)|((Vd>>1)<<12)|(0b1010<<8)|off8(i))

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
static uint32_t alloc_sx( uint32_t pref );

// ---------------- register allocation --------------------

// register allocation preferences

#define FORCED 0x20 // load function must return specified register
#define TEMP   0x40 // hint: temporary allocation, will not be pushed on stack

#define RMASK  0x1F

// general-purpose register list available for dynamic allocation
static const uint32_t rx_list[] = {
	R0, R1, R2, R3, R12 // R0-R3 are required minimum
};

// FPU scalar register list available for dynamic allocation
static const uint32_t sx_list[] = {
	S0, S1, 2, 3, 4, 5, 6, 7 // S0 and S1 are required minimum
};

// types of items on the stack
typedef enum {
	TYPE_RAW,        // stored value
	TYPE_CONST,      // constant
	TYPE_LOCAL,      // address
	TYPE_RX,         // volatile - general-purpose register
	TYPE_SX,         // volatile - FPU scalar register
} opstack_value_t;


typedef struct opstack_s {
	uint32_t value;
	uint32_t offset;
	opstack_value_t type;
	int safe_arg;
} opstack_t;


static int opstack;
static opstack_t opstackv[ PROC_OPSTACK_SIZE + 1 ];

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

	rx = alloc_rx( R2 | TEMP );
	emit_MOVRxi( rx, imm ); // rx = imm
	emit(VMOVass(reg, rx)); // arm core register -> singe precision register
	unmask_rx( rx );
}


static void flush_item( opstack_t *it )
{
	uint32_t rx;

	switch ( it->type ) {

		case TYPE_RX:
			if ( it->offset != ~0U )
				emit(STRai(it->value, rOPSTACK, it->offset)); // *opstack = rX
			unmask_rx( it->value );
			break;

		case TYPE_SX:
			emit(VSTRai(it->value, rOPSTACK, it->offset)); // *opstack = sX
			unmask_sx( it->value );
			break;

		case TYPE_CONST:
			rx = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx, it->value);            // r2 = const
			emit(STRai(rx, rOPSTACK, it->offset)); // *opstack = r2
			unmask_rx( rx );
			break;

		case TYPE_LOCAL:
			rx = alloc_rx( R2 | TEMP );
			if ( can_encode( it->value ) ) {
				emit(ADDi(rx, rPSTACK, it->value)); // r2 = pstack + arg
			} else {
				emit_MOVRxi(rx, it->value);         // r2 = arg
				emit(ADD(rx, rPSTACK, rx));         // r2 = pstack + r2
			}
			emit(STRai(rx, rOPSTACK, it->offset));      // *opstack = r2
			unmask_rx( rx );
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
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack*4 );
#endif
	if ( opstackv[ opstack ].type == TYPE_SX )
		return qtrue;
	else
		return qfalse;
}


static int is_safe_arg( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack*4 );
#endif
	return opstackv[ opstack ].safe_arg;
}


static void inc_opstack( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE )
		DROP( "opstack overflow - %i", opstack*4 );
#endif

	opstack += 1;

#ifdef DEBUG_VM
	if ( opstackv[ opstack ].type != TYPE_RAW )
		DROP( "bad item type %i at opstack %i", opstackv[ opstack ].type, opstack*4 );
#endif
}


static void dec_opstack( void )
{
#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "opstack underflow - %i", opstack*4 );
#endif

	flush_item( opstackv + opstack ); // in case if it was not consumed by any load function

	opstack -= 1;
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
		DROP( "no free registers, pref %x, opStack %i, mask %04x", pref, opstack*4, rx_mask );
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
	DROP( "no free registers, pref %x, opStack %i, mask %04x", pref, opstack*4, sx_mask );
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


// save all volatile items to opstack memory
// this is required before calling any external function
static void flush_volatile( void )
{
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_RX || it->type == TYPE_SX ) {
			flush_item( it );
		}
	}
}


static void flush_opstack( void )
{
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		flush_item( it );
	}
}


static void store_rx_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack*4 );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack );
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
		DROP( "bad opstack %i", opstack );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack );
#endif

	it->type = TYPE_RX;
	it->offset = ~0U; // opstack * sizeof( int32_t );
	it->value = R0;
	it->safe_arg = 0;

	unmask_rx( R0 ); // so it can be flushed on demand
}


static void store_sx_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack*4 );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack*4 );
#endif

	it->type = TYPE_SX;
	it->offset = opstack * sizeof( int32_t );
	it->value = reg;
	it->safe_arg = 0;

	unmask_sx( reg ); // so it can be flushed on demand
}


static void store_item_opstack( instruction_t *inst )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack*4 );
#endif
	switch ( inst->op ) {
		case OP_CONST: it->type = TYPE_CONST; break;
		case OP_LOCAL: it->type = TYPE_LOCAL; break;
		default: DROP( "incorrect opcode %i", inst->op );
	}

	it->offset = opstack * sizeof( int32_t );
	it->value = inst->value;
	it->safe_arg = inst->safe;
}


// we must unmask register manually after allocation/loading
static uint32_t load_rx_opstack( vm_t *vm, uint32_t pref )
{
	opstack_t *it = opstackv + opstack;
	uint32_t reg = pref & RMASK;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack*4 );
#endif

	if ( it->type == TYPE_RX ) {
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
			emit(MOV(reg, it->value));
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
		emit(VMOVssa(reg, it->value)); // singe precision register -> arm core register
		// release source
		unmask_sx( it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	if ( it->type == TYPE_CONST ) {
		// move constant to general-purpose register
		reg = alloc_rx( pref );
		emit_MOVRxi( reg, it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	if ( it->type == TYPE_LOCAL ) {
		reg = alloc_rx( pref );
		if ( can_encode( it->value ) ) {
			emit(ADDi(reg, rPSTACK, it->value)); // rX = pstack + arg
		} else {
			emit_MOVRxi(reg, it->value);           // rX = arg;
			emit(ADD(reg, rPSTACK, reg));          // rX = pstack + rX
		}
		it->type = TYPE_RAW;
		return reg;
	}

	// default raw type, explicit load
	reg = alloc_rx( pref );
	emit(LDRai(reg, rOPSTACK, opstack * sizeof( int32_t ))); // rX = *opstack
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
		DROP( "bad opstack %i", opstack*4 );
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
			emit(VMOV(reg, it->value));
			// release source
			unmask_sx( it->value );
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
		// release source
		unmask_rx( it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	if ( it->type == TYPE_CONST ) {
		// move constant to scalar register
		reg = alloc_sx( pref );
		emit_MOVSxi( reg, it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	if ( it->type == TYPE_LOCAL ) {
		reg = alloc_sx( pref );
		rx = alloc_rx( R2 | TEMP );
		if ( can_encode( it->value ) ) {
			emit(ADDi(rx, rPSTACK, it->value)); // r2 = pstack + arg
		} else {
			emit_MOVRxi(rx, it->value);         // r2 = arg
			emit(ADD(rx, rPSTACK, rx));         // r2 = pstack + r2
		}
		emit(VMOVass(reg, rx)); // arm core register -> singe precision register
		unmask_rx( rx );
		it->type = TYPE_RAW;
		return reg;
	}

	// default raw type, explicit load
	reg = alloc_sx( pref );
	emit(VLDRai(reg, rOPSTACK, opstack * sizeof( int32_t ))); // sX = *opstack
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


static void emitFuncOffset( uint32_t comp, vm_t *vm, func_t func )
{
	uint32_t offset = funcOffset[ func ] - compiledOfs;

	emit( cond( comp, BLi( encode_offset( offset ) ) ) );
}


static void emit_CheckReg( vm_t *vm, instruction_t *ins, uint32_t reg )
{
	if ( ins->safe ) {
		return;
	}
#ifdef DEBUG_VM
	if ( !( vm_rtChecks->integer & 8 ) || vm->forceDataMask ) {
		if ( vm->forceDataMask ) {
			emit(AND(reg, rDATAMASK, reg));    // rN = rN & rDATAMASK
		}
		return;
	}

	emit(CMP(reg, rDATAMASK));
	emitFuncOffset(HI, vm, FUNC_BADD);
#else
	emit(AND(reg, rDATAMASK, reg));    // rN = rN & rDATAMASK
#endif
}


static void emit_CheckJump( vm_t *vm, uint32_t reg, int proc_base, int proc_len )
{
	qboolean masked = is_masked_rx( reg );

	if ( ( vm_rtChecks->integer & 4 ) == 0 ) {
		return;
	}

	if ( !masked ) {
		mask_rx( reg ); // so allocator will not chose it
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
		// r2 > proc_len
		if ( can_encode( proc_len ) ) {
			emit(CMPi(rx[0], proc_len));
		} else {
			rx[1] = alloc_rx( R1 | TEMP );
			emit_MOVRxi(rx[1], proc_len);
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

	if ( !masked ) {
		unmask_rx( reg );
	}
}


static void emit_CheckProc( vm_t *vm, instruction_t *inst )
{
	// programStack overflow check
	if ( vm_rtChecks->integer & 1 ) {
		// check if pStack < vm->stackBottom
		uint32_t rx = alloc_rx( R2 | TEMP );
		emit(LDRai(rx, rVMBASE, offsetof(vm_t, stackBottom))); // r1 = vm->stackBottom
		emit(CMP(rPSTACK, rx));
		emitFuncOffset( LT, vm, FUNC_PSOF );
		unmask_rx( rx );
	}

	// opStack overflow check
	if ( vm_rtChecks->integer & 2 ) {
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

funcOffset[ FUNC_CALL ] = compiledOfs; // to jump from OP_CALL

	emit(CMPi(R0, 0)); // check if syscall

	if (start_block == -1)
		start_block = compiledOfs;

	emit(cond(LT, Bi(encode_offset(bytes_to_skip))));

	// check if R0 >= header->instructionCount
	emit_CheckJump( vm, R0, -1, 0 );

	// local function call
	emit(LDRa(R12, rINSPOINTERS, rLSL(2, R0))); // r12 = instructionPointers[r0]
	emit(BX(R12)); // keep LR so OP_LEAVE will return directly to our caller
	emit(BKPT(0));

	// syscall
	if (bytes_to_skip == -1)
		bytes_to_skip = compiledOfs - start_block;

funcOffset[ FUNC_SYSC ] = compiledOfs; // to jump from OP_CALL

	emit(MVN(R0, R0));   // r0 = ~r0

funcOffset[ FUNC_SYSF ] = compiledOfs; // to jump from ConstOptimize()

	emit(ADDi(rOPSTACKSHIFT, rOPSTACKSHIFT, 4));

	// save LR because it will be clobbered by BLX instruction
	emit(PUSH((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rOPSTACKSHIFT)|(1<<LR)));

	// modify VM stack pointer for recursive VM entry

	//currentVM->programStack = pstack - 8;
	emit(SUBi(R1, rPSTACK, 8)); // r1 = pstack - 8
	emit(STRai(R1, rVMBASE, offsetof(vm_t, programStack))); // vm->programStack = r1

	//argPosition = (intptr_t *)((byte *)currentVM->dataBase + pstack + 4);
	emit(ADDi(R2,rPROCBASE,4)); /// r2 = rPROCBASE + 4

	//argPosition[0] = call;
	emit(STRai(R0, R2, 0)); // r2[0] = r0

	emit(MOV(R0,R2));

	//ret = currentVM->systemCall( argPosition );
	emit(LDRai(R12, rVMBASE, offsetof(vm_t,systemCall))); // r12 = vm->systemCall
	emit(BLX(R12)); // call [r12]( r0 )

	emit(POP((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rOPSTACKSHIFT))|(1<<LR));

	// store return value
	emit(STRa(R0, rOPSTACK, rOPSTACKSHIFT)); // *(opstack+shift) = r0;

	// return to caller
	emit(MOV(PC,LR));

	emit(BKPT(0));
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

		rx[0] = load_rx_opstack( vm, R0 ); // src: r0 = *opstack;
		safe_arg[0] = is_safe_arg();
		dec_opstack(); // opstack -= 4

		rx[1] = load_rx_opstack( vm, R1 ); // dst: r1 = *opstack
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

	load_rx_opstack( vm, R0 | FORCED ); dec_opstack(); // src: r0 = *opstack; opstack -= 4
	load_rx_opstack( vm, R1 | FORCED ); dec_opstack(); // dst: r1 = *opstack; opstack -= 4
	alloc_rx( R2 | FORCED ); // counter
	alloc_rx( R3 | FORCED ); // scratch
	emit_MOVRxi(R2, count);
	emitFuncOffset(AL, vm, FUNC_BCPY);
	unmask_rx( R3 );
	unmask_rx( R2 );
	unmask_rx( R0 );
	unmask_rx( R1 );
}



#ifdef CONST_OPTIMIZE
static qboolean ConstOptimize( vm_t *vm )
{
	uint32_t rx[2];
	uint32_t sx[2];
	uint32_t x;

	switch ( ni->op ) {

	case OP_LOAD4:
		x = ci->value;
#ifdef FPU_OPTIMIZE
		if ( ni->fpu ) {
			sx[0] = alloc_sx( S0 );
			if ( x < 1024 && (x & 3) == 0 ) {
				emit(VLDRai(sx[0], rDATABASE, x));           // s0 = [dataBase + x]
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				if ( can_encode( x ) )
					emit(ADDi(rx[1], rDATABASE, x));     // r2 = dataBase + x
				else {
					emit_MOVRxi(rx[1], x);               // r2 = x
					emit(ADD(rx[1], rx[1], rDATABASE)); // r2 = r2 + dataBase
				}
				emit(VLDRai(sx[0], rx[1], 0));               // s0 = [r2]
				unmask_rx( rx[1] );
			}
			inc_opstack(); store_sx_opstack(sx[0]); // opstack += 4; *opstack = s0
			ip += 1; // OP_LOAD4
			return qtrue;
		}
#endif
		rx[0] = alloc_rx( R0 );
		if ( x < 4096 ) {
			emit(LDRai(rx[0], rDATABASE, x));    // r0 = [dataBase + v]
		} else {
			rx[1] = alloc_rx( R1 | TEMP );
			emit_MOVRxi(rx[1], x);               // r1 = v
			emit(LDRa(rx[0], rDATABASE, rx[1])); // r0 = [dataBase + r1]
			unmask_rx( rx[1] );
		}
		inc_opstack(); // opstack +=4
		store_rx_opstack( rx[0] );
		ip += 1; // OP_LOAD4
		return qtrue;

	case OP_LOAD2:
		x = ci->value;
		rx[0] = alloc_rx( R0 );
		if ( (ci+2)->op == OP_SEX16 ) {
			if ( x < 256 ) {
				emit(LDRSHai(rx[0], rDATABASE, x));    // r0 = (signed short*)[dataBase + v]
			} else {
				rx[1] = alloc_rx( R1 | TEMP );
				emit_MOVRxi(rx[1], x);                 // r1 = v
				emit(LDRSHa(rx[0], rDATABASE, rx[1])); // r0 = (signed short*)[dataBase + r1]
				unmask_rx( rx[1] );
			}
			ip += 2; // OP_LOAD2 + OP_SEX16
		} else {
			if ( x < 256 ) {
				emit(LDRHai(rx[0], rDATABASE, x));    // r0 = (unsigned short*)[dataBase + v]
			} else {
				rx[1] = alloc_rx( R1 | TEMP );
				emit_MOVRxi(rx[1], x);                // r1 = v
				emit(LDRHa(rx[0], rDATABASE, rx[1])); // r0 = (unsigned short*)[dataBase + r1]
				unmask_rx( rx[1] );
			}
			ip += 1; // OP_LOAD2
		}
		inc_opstack();             // opstack += 4
		store_rx_opstack( rx[0] ); // *opstack = r0
		return qtrue;

	case OP_LOAD1:
		x = ci->value;
		rx[0] = alloc_rx( R0 );
		if ( (ci+2)->op == OP_SEX8 ) {
			if ( x < 256 ) {
				emit(LDRSBai(rx[0], rDATABASE, x));    // r0 = (signed char*)[dataBase + x]
			} else {
				rx[1] = alloc_rx( R1 | TEMP );
				emit_MOVRxi(rx[1], x);                 // r1 = v
				emit(LDRSBa(rx[0], rDATABASE, rx[1])); // r0 = (signed char*)[dataBase + r1]
				unmask_rx( rx[1] );
			}
			ip += 2; // OP_LOAD1 + OP_SEX8
		} else {
			if ( x < 4096 ) {
				emit(LDRBai(rx[0], rDATABASE, x));     // r0 = (byte*)[dataBase + x]
			} else {
				rx[1] = alloc_rx( R1 | TEMP );
				emit_MOVRxi(rx[1], x);                 // r1 = v
				emit(LDRBa(rx[0], rDATABASE, rx[1]));  // r0 = (byte*)[dataBase + r1]
				unmask_rx( rx[1] );
			}
			ip += 1; // OP_LOAD1
		}
		inc_opstack();             // opstack += 4
		store_rx_opstack( rx[0] ); // *opstack = r0
		return qtrue;

	case OP_STORE4:
		rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
		emit_CheckReg(vm, ni, rx[0]);
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRxi(rx[1], ci->value);       // r2 = 0x12345678
		emit(STRa(rx[1], rDATABASE, rx[0])); // [dataBase + r0] = r2;
		unmask_rx( rx[0] );
		unmask_rx( rx[1] );
		ip += 1; // OP_STORE4
		return qtrue;

	case OP_STORE2:
		rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
		emit_CheckReg(vm, ni, rx[0]);
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRxi(rx[1], ci->value);        // r2 = 0x12345678
		emit(STRHa(rx[1], rDATABASE, rx[0])); // [dataBase + r0] = r2;
		unmask_rx( rx[0] );
		unmask_rx( rx[1] );
		ip += 1; // OP_STORE2
		return qtrue;

	case OP_STORE1:
		rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
		emit_CheckReg(vm, ni, rx[0]);
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRxi(rx[1], ci->value);       // r2 = 0x12345678
		emit(STRBa(rx[1], rDATABASE, rx[0])); // [dataBase + r0] = r2;
		unmask_rx( rx[0] );
		unmask_rx( rx[1] );
		ip += 1; // OP_STORE1
		return qtrue;

	case OP_ADD:
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		if ( can_encode( ci->value ) ) {
			emit(ADDi(rx[0], rx[0], ci->value));
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], ci->value);
			emit(ADD(rx[0], rx[0], rx[1]));
			unmask_rx( rx[1] );
		}
		store_rx_opstack( rx[0] ); // *opstack = r0
		ip += 1; // OP_ADD
		return qtrue;

	case OP_SUB:
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		if ( can_encode( ci->value ) ) {
			emit(SUBi(rx[0], rx[0], ci->value)); // r0 = r0 - const
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], ci->value);  // r2 = const
			emit(SUB(rx[0], rx[0], rx[1])); // r0 = r0 - r2
			unmask_rx( rx[1] );
		}
		store_rx_opstack( rx[0] ); // *opstack = r0
		ip += 1; // OP_SUB
		return qtrue;


	case OP_MULI:
	case OP_MULU:
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRxi(rx[1], ci->value);  // r2 = const
		emit(MUL(rx[0], rx[1], rx[0])); // r0 = r2 * r0
		store_rx_opstack( rx[0] );      // *opstack = r0
		unmask_rx( rx[1] );
		ip += 1; // OP_MULI|OP_MULU
		return qtrue;

	case OP_DIVI:
	case OP_DIVU:
		if ( !(CPU_Flags & CPU_IDIVA) ) {
			return qfalse;
		}
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		rx[1] = alloc_rx( R2 | TEMP );
		emit_MOVRxi(rx[1], ci->value); // r2 = const
		if ( ni->op == OP_DIVI ) {
			emit(SDIV(rx[0], rx[0], rx[1])); // r0 = r0 / r2
		} else {
			emit(UDIV(rx[0], rx[0], rx[1])); // r0 = (unsigned) r0 / r2
		}
		store_rx_opstack( rx[0] ); // *opstack = r0
		unmask_rx( rx[1] );
		ip += 1;
		return qtrue;

	case OP_MODI:
	case OP_MODU:
		if ( !(CPU_Flags & CPU_IDIVA) ) {
			return qfalse;
		}
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		rx[1] = alloc_rx( R1 | TEMP );
		rx[2] = alloc_rx( R2 | TEMP );
		emit_MOVRxi(rx[1], ci->value);
		if ( ni->op == OP_MODI ) {
			emit(SDIV(rx[2], rx[0], rx[1])); // r2 = r0 / r1
		} else {
			emit(UDIV(rx[2], rx[0], rx[1])); // r2 = (unsigned)r0 / r1
		}
		emit(MLS(rx[0], rx[1], rx[2], rx[0]));   // r0 = r0 - r1 * r2
		store_rx_opstack( rx[0] );               // *opstack = r0
		unmask_rx( rx[1] );
		unmask_rx( rx[2] );
		ip += 1;
		return qtrue;

	case OP_LSH:
		x = ci->value;
		if ( x <= 0 || x > 31 )
			break;
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		emit(LSLi(rx[0], rx[0], x));       // r0 = r1 << r0
		store_rx_opstack( rx[0] );         // *opstack = r0
		ip += 1; // OP_LSH
		return qtrue;

	case OP_RSHI:
		x = ci->value;
		if ( x <= 0 || x > 31 )
			break;
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		emit(ASRi(rx[0], rx[0], x));       // r0 = r0 >> x
		store_rx_opstack( rx[0] );         // *opstack = r0
		ip += 1; // OP_RSHI
		return qtrue;

	case OP_RSHU:
		x = ci->value;
		if ( x <= 0 || x > 31 )
			break;
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack
		emit(LSRi(rx[0], rx[0], x));       // r0 = (unsigned)r0 >> x
		store_rx_opstack( rx[0] );         // *opstack = r0
		ip += 1; // OP_RSHU
		return qtrue;

	case OP_BAND:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 );      // r0 = *opstack
		if ( can_encode( x ) ) {
			emit(ANDi(rx[0], rx[0], x));    // r0 = r0 & x
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], x);          // r2 = x
			emit(AND(rx[0], rx[0], rx[1])); // r0 = r0 & x
			unmask_rx( rx[1] );
		}
		store_rx_opstack( rx[0] );              // *opstack = r0
		ip += 1; // OP_BAND
		return qtrue;

	case OP_BOR:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 );      // r0 = *opstack
		if ( can_encode( x ) ) {
			emit(ORRi(rx[0], rx[0], x));    // r0 = r0 | x
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], x);          // r2 = x
			emit(ORR(rx[0], rx[0], rx[1])); // r0 = r0 | x
			unmask_rx( rx[1] );
		}
		store_rx_opstack( rx[0] );              // *opstack = r0
		ip += 1; // OP_BOR
		return qtrue;

	case OP_BXOR:
		x = ci->value;
		rx[0] = load_rx_opstack( vm, R0 );      // r0 = *opstack
		if ( can_encode( x ) ) {
			emit(EORi(rx[0], rx[0], x));    // r0 = r0 ^ x
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], x);          // r2 = x
			emit(EOR(rx[0], rx[0], rx[1])); // r0 = r0 ^ x
			unmask_rx( rx[1] );
		}
		store_rx_opstack( rx[0] );              // *opstack = r0
		ip += 1; // OP_BXOR
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

			sx[0] = alloc_sx( S0 | TEMP );
			rx[0] = alloc_rx( R12 );
			emit(VLDRai(sx[0], rPROCBASE, 8)); // s0 = [procBase + 8]
			if ( ci->value == ~TRAP_SIN )
				emit_MOVRxi(rx[0], (unsigned)sinf);
			else
				emit_MOVRxi(rx[0], (unsigned)cosf);
			emit(BLX(rx[0]));
			store_sx_opstack( sx[0] );
			unmask_rx( rx[0] );
			ip += 1;
			return qtrue;
		}
		if ( ci->value < 0 ) // syscall
		{
			x = ~ci->value;
			emit_MOVRxi(R0, x);
			emit_MOVRxi(rOPSTACKSHIFT, (opstack-1)*sizeof(int32_t)); // opStack shift
			emitFuncOffset( AL, vm, FUNC_SYSF );
			ip += 1; // OP_CALL;
			store_syscall_opstack();
			return qtrue;
		}
		emit_MOVRxi(rOPSTACKSHIFT, (opstack-1)*sizeof(int32_t)); // opStack shift
		emit(BLi(encode_offset(vm->instructionPointers[ ci->value ] - compiledOfs)));
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
		rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack;
		dec_opstack();                 // opstack -= 4
		x = ci->value;
		if ( can_encode( x ) ) {
			emit(CMPi(rx[0], x));
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], x);
			emit(CMP(rx[0], rx[1]));
			unmask_rx( rx[1] );
		}
		emit(cond(comp, Bi(encode_offset(vm->instructionPointers[ ni->value ] - compiledOfs))));
		unmask_rx( rx[0] );
		}
		ip += 1; // OP_cond
		return qtrue;

	case OP_EQF:
	case OP_NEF:
	case OP_LTF:
	case OP_LEF:
	case OP_GTF:
	case OP_GEF: {
		uint32_t comp = get_comp( ni->op );
		sx[0] = load_sx_opstack( vm, S0 ); dec_opstack(); // *s0 = opstack
		sx[1] = alloc_sx( S1 | TEMP ); // opstack -= 4; s1 = const
		emit_MOVSxi( sx[1], ci->value );
		emit(VCMP_F32(sx[0], sx[1]));
		emit(VMRS(APSR_nzcv));
		emit(cond(comp, Bi(encode_offset(vm->instructionPointers[ ni->value ] - compiledOfs))));
		unmask_sx( sx[0] );
		unmask_sx( sx[1] );
		ip += 1; // OP_cond
		return qtrue;
		}

	case OP_ADDF:
	case OP_SUBF:
	case OP_MULF:
	case OP_DIVF:
		sx[0] = load_sx_opstack( vm, S0 ); // *s0 = opstack
		sx[1] = alloc_sx( S1 | TEMP );
		emit_MOVSxi(sx[1], ci->value);     // s1 = ci->value
		switch ( ni->op ) {
			case OP_ADDF: emit(VADD_F32(sx[0], sx[0], sx[1])); break; // s0 = s0 + s1
			case OP_SUBF: emit(VSUB_F32(sx[0], sx[0], sx[1])); break; // s0 = s0 - s1
			case OP_MULF: emit(VMUL_F32(sx[0], sx[0], sx[1])); break; // s0 = s0 * s1
			case OP_DIVF: emit(VDIV_F32(sx[0], sx[0], sx[1])); break; // s0 = s0 / s1
			default: break;
		}
		store_sx_opstack( sx[0] );
		unmask_sx( sx[1] );
		ip += 1; // OP_XXXF
		return qtrue;

	default:
		break;
	}
	return qfalse;
}
#endif // CONST_OPTIMIZE


#ifdef MISC_OPTIMIZE
static qboolean LocalOptimize( vm_t *vm )
{
	uint32_t rx[2];
	uint32_t v = ci->value;

	if ( ni->op == OP_LOAD4 ) // merge OP_LOCAL + OP_LOAD4
	{
#ifdef FPU_OPTIMIZE
		if ( ni->fpu ) {
			uint32_t sx[1];
			sx[0] = alloc_sx( S0 );
			if ( v < 1024 && (v & 3) == 0 ) {
				emit(VLDRai(sx[0], rPROCBASE, v));           // s0 = [procBase + v]
			} else {
				rx[1] = alloc_rx( R2 | TEMP );
				if ( can_encode( v ) )
					emit(ADDi(rx[1], rPROCBASE, v));     // r2 = procBase + v
				else {
					emit_MOVRxi(rx[1], v);               // r2 = v
					emit(ADD(rx[1], rx[1], rPROCBASE)); // r2 = r2 + procBase
				}
				emit(VLDRai(sx[0], rx[1], 0));               // s0 = [r2]
				unmask_rx( rx[1] );
			}
			inc_opstack(); store_sx_opstack(sx[0]); // opstack += 4; *opstack = s0
			ip += 1; // OP_LOAD4
			return qtrue;
		}
#endif
		rx[0] = alloc_rx( R0 );
		if ( v < 4096 ) {
			emit(LDRai(rx[0], rPROCBASE, v));    // r0 = [procBase + v]
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], v);               // r2 = v
			emit(LDRa(rx[0], rPROCBASE, rx[1])); // r0 = [procBase + r2]
			unmask_rx( rx[1] );
		}
		inc_opstack();             // opstack += 4
		store_rx_opstack( rx[0] ); // *opstack = r0
		ip++; // OP_LOAD4
		return qtrue;
	}

	if ( ni->op == OP_LOAD2 ) // merge OP_LOCAL + OP_LOAD2
	{
		rx[0] = alloc_rx( R0 );
		if ( v < 256 ) {
			emit(LDRHai(rx[0], rPROCBASE, v));    // r0 = (unsigned short*)[procBase + v]
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], v);                // r2 = v
			emit(LDRHa(rx[0], rPROCBASE, rx[1])); // r0 = (unsigned short*)[procBase + r2]
			unmask_rx( rx[1] );
		}
		inc_opstack();             // opstack += 4
		store_rx_opstack( rx[0] ); // *opstack = r0
		ip++; // OP_LOAD2
		return qtrue;
	}

	if ( ni->op == OP_LOAD1 ) // merge OP_LOCAL + OP_LOAD1
	{
		rx[0] = alloc_rx( R0 );
		if ( v < 4096 ) {
			emit(LDRBai(rx[0], rPROCBASE, v));    // r0 = (byte*)[procBase + v]
		} else {
			rx[1] = alloc_rx( R2 | TEMP );
			emit_MOVRxi(rx[1], v);
			emit(LDRBa(rx[0], rPROCBASE, rx[1])); // r0 = (byte*)[procBase + r2]
			unmask_rx( rx[1] );
		}
		inc_opstack();             // opstack += 4
		store_rx_opstack( rx[0] ); // *opstack = r0
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

	switch ( op )
	{
		//[local] += CONST
		case MOP_ADD4:
		case MOP_SUB4:
		case MOP_BAND4:
		case MOP_BOR4:
			value = inst[ip+2].value;
			addr = ci->value;

			rx[0] = alloc_rx( R0 | TEMP );
			rx[1] = alloc_rx( R1 | TEMP );
			rx[2] = ~0U;

			// load
			if ( addr < 4096 ) {
				emit(LDRai(rx[0], rPROCBASE, addr)); // r0 = [procBase + addr]
			} else {
				rx[2] = alloc_rx( R2 );
				emit_MOVRxi(rx[2], addr);            // r2 = addr
				emit(LDRa(rx[0], rPROCBASE, rx[2])); // r0 = [procBase + r2]
			}

			// modify
			switch ( op ) {
				case MOP_ADD4:
					if ( can_encode( value ) ) {
						emit(ADDi(rx[0], rx[0], value));  // r0 += value;
					} else {
						emit_MOVRxi(rx[1], value);        // r1 = value
						emit(ADD(rx[0], rx[0], rx[1]));   // r0 += r1
					} break;
				case MOP_SUB4:
					if ( can_encode( value ) ) {
						emit(SUBi(rx[0], rx[0], value));  // r0 += value;
					} else {
						emit_MOVRxi(rx[1], value);        // r1 = value
						emit(SUB(rx[0], rx[0], rx[1]));   // r0 -= r1
					} break;
				case MOP_BAND4:
					if ( can_encode( value ) ) {
						emit(ANDi(rx[0], rx[0], value));   // r0 += value;
					} else {
						emit_MOVRxi(rx[1], value);        // r1 = value
						emit(AND(rx[0], rx[0], rx[1]));   // r0 &= r1
					} break;
				case MOP_BOR4:
					if ( can_encode( value ) ) {
						emit(ORRi(rx[0], rx[0], value));  // r0 += value;
					} else {
						emit_MOVRxi(rx[1], value);        // r1 = value
						emit(ORR(rx[0], rx[0], rx[1]));   // r0 |= r1
					} break;
			}

			// store
			if ( addr < 4096 ) {
				emit(STRai(rx[0], rPROCBASE, addr)); // [procBase + addr] = r0
			} else {
				emit(STRa(rx[0], rPROCBASE, rx[2]));    // [procBase + r2] = r0
				unmask_rx( rx[2] );
			}

			unmask_rx( rx[0] );
			unmask_rx( rx[1] );

			ip += 5;
			return qtrue;

		default: DROP( "%s: unknown macro opcode %i", __func__, op );
			break;
	};

	return qfalse;
}
#endif


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


qboolean VM_Compile( vm_t *vm, vmHeader_t *header )
{
	const char *errMsg;
	int proc_base;
	int proc_len;
	uint32_t rx[3];
	uint32_t sx[2];
	qboolean scalar;
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

#ifdef MACRO_OPTIMIZE
	VM_FindMOps( inst, vm->instructionCount );
#endif

	memset( funcOffset, 0, sizeof( funcOffset ) );

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
	emit_MOVRxi(rVMBASE, (unsigned)vm);
	emit(LDRai(rINSPOINTERS, rVMBASE, offsetof(vm_t, instructionPointers)));
	emit(LDRai(rDATABASE, rVMBASE, offsetof(vm_t, dataBase)));
	emit(LDRai(rDATAMASK, rVMBASE, offsetof(vm_t, dataMask)));

	// these are volatile variables
	emit(LDRai(rPSTACK, rVMBASE, offsetof(vm_t, programStack)));
	emit(LDRai(rOPSTACK, rVMBASE, offsetof(vm_t, opStack)));
	emit(LDRai(rOPSTACKTOP, rVMBASE, offsetof(vm_t, opStackTop)));

	emit_MOVRxi(rOPSTACKSHIFT, 0); // opStack shift

	emitFuncOffset( AL, vm, FUNC_ENTR );  // call vmMain()

#ifdef DEBUG_VM
	emit(STRai(rPSTACK, rVMBASE, offsetof(vm_t, programStack))); // vm->programStack = rPSTACK;
#endif

	emit(ADDi(SP, SP, 12));    // align stack to 16 bytes
	emit(POP(R4_R11|(1<<PC))); // pop R4-R11, LR -> PC

	emit(BKPT(0));

	emitAlign( 16 ); // align to quadword boundary

	funcOffset[ FUNC_ENTR ] = compiledOfs; // offset to vmMain() entry point

	while ( ip < header->instructionCount ) {

		uint32_t v;

		ci = &inst[ ip + 0 ];
		ni = &inst[ ip + 1 ];

		if ( ci->jused )
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
				emit(BKPT(1));
				break;

			case OP_IGNORE:
				break;

			case OP_BREAK:
				emit(BKPT(3));
				break;

			case OP_ENTER:
				emitAlign( 16 ); // align to quadword boundary
				vm->instructionPointers[ ip - 1 ] = compiledOfs;

				proc_base = ip;
				// locate endproc
				for ( proc_len = -1, i = ip; i < header->instructionCount; i++ ) {
					if ( inst[ i ].op == OP_PUSH && inst[ i + 1 ].op == OP_LEAVE ) {
						proc_len = i - proc_base;
						break;
					}
				}

				emit(PUSH((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rPROCBASE)|(1<<LR)));

				if ( ip != 1 ) {
					// apply opStack shift
					emit(ADD(rOPSTACK, rOPSTACK, rOPSTACKSHIFT));
				}

				if ( can_encode( v ) ) {
					emit(SUBi(rPSTACK, rPSTACK, v));    // pstack -= arg
				} else {
					rx[0] = alloc_rx( R0 | TEMP );
					emit_MOVRxi(rx[0], v);              // r0 = arg
					emit(SUB(rPSTACK, rPSTACK, rx[0])); // pstack -= r0
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
				rx[0] = load_rx_opstack( vm, R0 | FORCED ); // r0 = *opstack
				unmask_rx( rx[0] );
				flush_volatile();
				emit_MOVRxi(rOPSTACKSHIFT, opstack-4);
				emitFuncOffset(AL, vm, FUNC_CALL);
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
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
				rx[1] = load_rx_opstack( vm, R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
				unmask_rx( rx[0] );
				unmask_rx( rx[1] );
				emit(CMP(rx[1], rx[0]));
				emit(cond(comp, Bi(encode_offset(vm->instructionPointers[v] - compiledOfs))));
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
				emit(VCMP_F32(sx[1], sx[0]));
				emit(VMRS(APSR_nzcv));
				emit(cond(comp, Bi(encode_offset(vm->instructionPointers[v] - compiledOfs))));
				}
				break;

			case OP_LOAD1:
				rx[0] = load_rx_opstack( vm, R0 );    // r0 = *opstack;
				emit_CheckReg(vm, ci, rx[0]);
				emit(LDRBa(rx[0], rDATABASE, rx[0])); // r0 = (unsigned char)dataBase[r0]
				store_rx_opstack( rx[0] );            // *opstack = r0;
				break;

			case OP_LOAD2:
				rx[0] = load_rx_opstack( vm, R0 );    // r0 = *opstack;
				emit_CheckReg(vm, ci, rx[0]);
				emit(LDRHa(rx[0], rDATABASE, rx[0])); // r0 = (unsigned short)dataBase[r0]
				store_rx_opstack( rx[0] );            // *opstack = r0;
				break;

			case OP_LOAD4:
				rx[0] = load_rx_opstack( vm, R0 );     // r0 = *opstack;
				emit_CheckReg(vm, ci, rx[0]);
#ifdef FPU_OPTIMIZE
				if ( ci->fpu ) {
					sx[0] = alloc_sx( S0 );
					// no indexing register mode for VLDR...
					emit(ADD(rx[0], rx[0], rDATABASE)); // r0 = r0 + database
					emit(VLDRai(sx[0], rx[0], 0));      // s0 = [r0]
					store_sx_opstack( sx[0] );          // *opstack = s0;
					unmask_rx( rx[0] );
					break;
				}
#endif
				emit(LDRa(rx[0], rDATABASE, rx[0]));   // r0 = dataBase[r0]
				store_rx_opstack( rx[0] );             // *opstack = r0;
				break;

			case OP_STORE1:
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
				rx[1] = load_rx_opstack( vm, R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
				emit_CheckReg(vm, ci, rx[1]);
				emit(STRBa(rx[0], rDATABASE, rx[1]));     // (byte*)database[r1] = r0
				unmask_rx( rx[0] );
				unmask_rx( rx[1] );
				break;

			case OP_STORE2:
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -= 4
				rx[1] = load_rx_opstack( vm, R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
				emit_CheckReg(vm, ci, rx[1]);
				emit(STRHa(rx[0], rDATABASE, rx[1]));     // (short*)database[r1] = r0
				unmask_rx( rx[0] );
				unmask_rx( rx[1] );
				break;

			case OP_STORE4:
				if ((scalar = scalar_on_top())) {
					sx[0] = load_sx_opstack( vm, S0 ); // s0 = *opstack;
				} else {
					rx[0] = load_rx_opstack( vm, R0 ); // r0 = *opstack;
				}
				dec_opstack(); // opstack --=4
				rx[1] = load_rx_opstack( vm, R1 ); dec_opstack(); // r1 = *opstack; opstack -= 4
				emit_CheckReg(vm, ci, rx[1]);
				if ( scalar ) {
					emit(ADD(rx[1], rx[1], rDATABASE)); // r1 = r1 + dataBase
					emit(VSTRai(sx[0], rx[1], 0));      // [r1] = s0
					unmask_sx( sx[0] );
				} else {
					emit(STRa(rx[0], rDATABASE, rx[1])); // database[r1] = r0
					unmask_rx( rx[0] );
				}
				unmask_rx( rx[1] );
				break;

			case OP_ARG:
				if ( scalar_on_top() ) {
					sx[0] = load_sx_opstack( vm, S0 ); dec_opstack(); // s0 = *opstack; opstack -=4
					// v is in range [8..252] so it is fit in VSTRai immediate encoding
					emit(VSTRai(sx[0], rPROCBASE, v)); // [procBase + v] = s0
					unmask_sx( sx[0] );
				} else {
					rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack; opstack -=4
					emit(STRai(rx[0], rPROCBASE, v)); // [procBase + v] = r0
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
					case OP_SEX8:  emit(SXTB(rx[0], rx[0])); break; // r0 = sign extend r0
					case OP_SEX16: emit(SXTH(rx[0], rx[0])); break; // r0 = sign extend r0
					case OP_NEGI:  emit(RSBi(rx[0], rx[0], 0)); break; // r0 = -r0
					case OP_BCOM:  emit( MVN(rx[0], rx[0])); break; // r0 = ~r0
				}
				store_rx_opstack( rx[0] ); // *opstack = r0
				break;

			case OP_DIVI:
			case OP_DIVU:
				if ( CPU_Flags & CPU_IDIVA ) {
					rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack
					rx[1] = load_rx_opstack( vm, R1 ); // opstack-=4; r1 = *opstack
					if ( ci->op == OP_DIVI ) {
						emit(SDIV(rx[0], rx[1], rx[0]));
					} else {
						emit(UDIV(rx[0], rx[1], rx[0]));
					}
					store_rx_opstack( rx[0] ); // *opstack = r0
					unmask_rx( rx[1] );
				} else {
					rx[1] = load_rx_opstack( vm, R1 | FORCED ); dec_opstack(); // r1 = *opstack
					rx[0] = load_rx_opstack( vm, R0 | FORCED ); // opstack-=4; r0 = *opstack
					rx[2] = alloc_rx( R12 );
					flush_volatile();
					if ( ci->op == OP_DIVI )
						emit_MOVRxi(rx[2], (unsigned)__aeabi_idiv);
					else
						emit_MOVRxi(rx[2], (unsigned)__aeabi_uidiv);
					emit(BLX(rx[2]));
					store_rx_opstack( rx[0] ); // *opstack = r0
					unmask_rx( rx[1] );
					unmask_rx( rx[2] );
				}
				break;

			case OP_MODI:
			case OP_MODU:
				if ( CPU_Flags & CPU_IDIVA ) {
					rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack
					rx[1] = load_rx_opstack( vm, R1 ); // opstack-=4; r1 = *opstack
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
					rx[1] = load_rx_opstack( vm, R1 | FORCED ); dec_opstack(); // r1 = *opstack
					rx[0] = load_rx_opstack( vm, R0 | FORCED ); // opstack-=4; r0 = *opstack
					rx[2] = alloc_rx( R12 );
					flush_volatile();
					if ( ci->op == OP_MODI )
						emit_MOVRxi(rx[2], (unsigned)__aeabi_idivmod);
					else
						emit_MOVRxi(rx[2], (unsigned)__aeabi_uidivmod);
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
				rx[0] = load_rx_opstack( vm, R0 ); dec_opstack(); // r0 = *opstack
				rx[1] = load_rx_opstack( vm, R1 ); // opstack-=4; r1 = *opstack
				switch ( ci->op ) {
					case OP_ADD:  emit(ADD(rx[0], rx[1], rx[0])); break; // r0 = r1 + r0
					case OP_SUB:  emit(SUB(rx[0], rx[1], rx[0])); break; // r0 = r1 - r0
					case OP_MULI:
					case OP_MULU: emit(MUL(rx[0], rx[1], rx[0])); break; // r0 = r1 * r0
					case OP_BAND: emit(AND(rx[0], rx[1], rx[0])); break; // r0 = r1 & r0
					case OP_BOR:  emit(ORR(rx[0], rx[1], rx[0])); break; // r0 = r1 | r0
					case OP_BXOR: emit(EOR(rx[0], rx[1], rx[0])); break; // r0 = r1 ^ r0
					case OP_LSH:  emit(LSL(rx[0], rx[1], rx[0])); break; // r0 = r1 << r0
					case OP_RSHI: emit(ASR(rx[0], rx[1], rx[0])); break; // r0 = r1 >> r0
					case OP_RSHU: emit(LSR(rx[0], rx[1], rx[0])); break; // r0 = (unsigned)r1 >> r0
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
					case OP_ADDF: emit(VADD_F32(sx[0], sx[1], sx[0])); break; // s0 = s1 + s0
					case OP_SUBF: emit(VSUB_F32(sx[0], sx[1], sx[0])); break; // s0 = s1 - s0
					case OP_MULF: emit(VMUL_F32(sx[0], sx[1], sx[0])); break; // s0 = s1 * s0
					case OP_DIVF: emit(VDIV_F32(sx[0], sx[1], sx[0])); break; // s0 = s1 / s0
				}
				store_sx_opstack( sx[0] ); // *opstack = s0;
				unmask_sx( sx[1] );
				break;

			case OP_NEGF:
				sx[0] = load_sx_opstack( vm, S0 ); // s0 = *opstack
				emit(VNEG_F32(sx[0], sx[0]));      // s0 = -s0
				store_sx_opstack( sx[0] );         // *opstack = s0
				break;

			case OP_CVIF:
				sx[0] = load_sx_opstack( vm, S0 ); // s0 = *opstack
				emit(VCVT_F32_S32(sx[0], sx[0]));  // s0 = (float)s0
				store_sx_opstack( sx[0] );         // *opstack = s0
				break;

			case OP_CVFI:
				sx[0] = load_sx_opstack( vm, S0 ); // s0 = *opstack
				emit(VCVT_S32_F32(sx[0], sx[0]));  // s0 = (int)s0
				store_sx_opstack( sx[0] );         // *opstack = s0
				break;

#ifdef MACRO_OPTIMIZE
			case MOP_ADD4:
			case MOP_SUB4:
			case MOP_BAND4:
			case MOP_BOR4:
				EmitMOPs( vm, ci->op );
				break;
#endif
		} // switch op
	} // ip

		// it will set multiple offsets
		emitAlign( 16 ); // align to quadword boundary
		emitCallFunc( vm );
		emit(BKPT(0));

		emitAlign( 16 ); // align to quadword boundary
		funcOffset[ FUNC_BCPY ] = compiledOfs;
		emitBlockCopyFunc( vm );

		funcOffset[ FUNC_BADJ ] = compiledOfs;
		emit_MOVRxi(R12, (unsigned)BadJump);
		emit(BLX(R12));
		emit(BKPT(0));

		funcOffset[ FUNC_OUTJ ] = compiledOfs;
		emit_MOVRxi(R12, (unsigned)OutJump);
		emit(BLX(R12));
		emit(BKPT(0));

		funcOffset[ FUNC_OSOF ] = compiledOfs;
		emit_MOVRxi(R12, (unsigned)ErrBadOpStack);
		emit(BLX(R12));
		emit(BKPT(0));

		funcOffset[ FUNC_PSOF ] = compiledOfs;
		emit_MOVRxi(R12, (unsigned)ErrBadProgramStack);
		emit(BLX(R12));
		emit(BKPT(0));

		funcOffset[ FUNC_BADD ] = compiledOfs;
		emit_MOVRxi(R12, (unsigned)ErrBadData);
		emit(BLX(R12));
		emit(BKPT(0));

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

#ifdef DUMP_CODE
	dump_code( code, compiledOfs / 4 );
#endif

	// offset all the instruction pointers for the new location
	for ( i = 0; i < header->instructionCount; i++ ) {
		if ( !inst[i].jused ) {
			vm->instructionPointers[ i ] = (unsigned)BadJump;
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
	int32_t		opStack[ MAX_OPSTACK_SIZE ];
	unsigned int stackOnEntry;
	int32_t		*image;
	int		i;

	// we might be called recursively, so this might not be the very top
	stackOnEntry = vm->programStack;

	vm->programStack -= (MAX_VMMAIN_CALL_ARGS+2)*4;

	// set up the stack frame
	image = (int32_t*)( vm->dataBase + vm->programStack );
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
