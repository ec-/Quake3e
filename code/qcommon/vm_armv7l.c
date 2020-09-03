/*
===========================================================================
Copyright (C) 2009 David S. Miller <davem@davemloft.net>
Copyright (C) 2013,2014 SUSE Linux Products GmbH

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
#define REG0_OPTIMIZE
#define S0_OPTIMIZE
#define CONST_OPTIMIZE
#define VM_OPTIMIZE
#define MACRO_OPTIMIZE

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
	FUNC_ENTR = 0,
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
//static	int	lastConst;
//static	opcode_t	pop1;
static	ELastCommand	LastCommand;
uint32_t	funcOffset[FUNC_LAST];


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

#define R0	0
#define R1	1
#define R2	2
#define R3	3
#define R4	4
#define R5	5
#define R6	6
#define R7	7
#define R8	8
#define R9	9
#define R10	10
#define R11	11
#define R12	12

#define FP	R11
#define SP	13
#define LR	14
#define PC	15

#define APSR_nzcv	15

#define S0      0
#define S1      1

#define rVMBASE		R4
#define rOPSTACK	R5
#define rOPSTACKTOP	R6
#define rINSPOINTERS	R7
#define rPSTACK		R8
#define rDATABASE	R9
#define rDATAMASK	R10
#define rPROCBASE	FP

#define SAVE_REGS (1<<R4)|(1<<R5)|(1<<R6)|(1<<R7)|(1<<R8)|(1<<R9)|(1<<R10)|(1<<R11)

/* arm eabi, builtin gcc functions */
int __aeabi_idiv (int, int);
unsigned __aeabi_uidiv (unsigned, unsigned);
void __aeabi_idivmod(void);
void __aeabi_uidivmod(void);

float sinf(float x);
float cosf(float x);

/* exit() won't be called but use it because it is marked with noreturn */
#define DIE( reason, args... ) \
	do { \
		Com_Error(ERR_DROP, "vm_arm compiler error: " reason, ##args); \
		exit(1); \
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

void _emit( vm_t *vm, uint32_t isn, int pass )
{
#if 0
	static int fd = -2;
	static int cnt = 0;
	char buf[32];
	if ( fd == -2 )
		fd = open( "code.bin", O_TRUNC|O_WRONLY|O_CREAT, 0644 );
	if ( fd > 0 ) {
		int len;
		len = sprintf( buf, "%i %08x\n", pass, isn );
		//write( fd, &isn, 4 );
		write( fd, buf, len );
	}
#endif

	//if ( pass )
	if ( vm->codeBase.ptr )
	{
		memcpy( vm->codeBase.ptr + compiledOfs, &isn, 4 );
	}

	compiledOfs += 4;

	LastCommand = LAST_COMMAND_NONE;
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

//#define emit(isn) _emit(vm, isn, pass)

static unsigned char off8(unsigned val)
{
	if (val&3)
		DIE("offset must be multiple of four");
	if (val > 1020)
		DIE("offset too large");
	return val>>2;
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
		DIE("immediate cannot be encoded (%d, %d)\n", shift, val);
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
#define LDRTa(dst, base, off)  (AL | (0b011<<25) | (0b0101<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRTai(dst, base, off) (AL | (0b010<<25) | (0b0101<<21) | (1<<20) | base<<16 | dst<<12 | off)

// load word with post-decrement
#define LDRTx(dst, base, off)  (AL | (0b011<<25) | (0b0001<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRTxi(dst, base, off) (AL | (0b010<<25) | (0b0001<<21) | (1<<20) | base<<16 | dst<<12 | off)

// load byte with pre-increment
#define LDRBa(dst, base, off)   (AL | (0b011<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRSBa(dst, base, off)  (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | 0b1101<<4 | off)
#define LDRSBai(dst, base, off) (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | ((off&0xF0)<<8) | 0b1101<<4 | (off&0x0F))

// load half-word with pre-increment
#define LDRHa(dst, base, off)   (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | (0b1011<<4) | off)
#define LDRSHa(dst, base, off)  (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b0000<<8) | (0b1111<<4) | off)
#define LDRHai(dst, base, off)  (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | (((off)&0xF0)<<8) | (0b1011)<<4 | ((off)&0x0F) )
#define LDRSHai(dst, base, off) (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | (((off)&0xF0)<<8) | (0b1111)<<4 | ((off)&0x0F) )

// store byte/half-word with pre-increment
#define STRBa(dst, base, off)   (AL | (0b011<<25) | (0b1110<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRHa(dst, base, off)   (AL | (0b000<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | (0b1011<<4) | off)

// store word with pre-increment
#define STRa(dst, base, off)   (AL | (0b011<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRai(dst, base, off)  (AL | (0b010<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRaiw(dst, base, off) (AL | (0b010<<25) | (0b1101<<21) | (0<<20) | base<<16 | dst<<12 | off)

// store word with pre-decrement
#define STRx(dst, base, off)   (AL | (0b011<<25) | (0b1000<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRxi(dst, base, off)  (AL | (0b010<<25) | (0b1000<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRxiw(dst, base, off) (AL | (0b010<<25) | (0b1001<<21) | (0<<20) | base<<16 | dst<<12 | off)



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

// puts integer in R0
#define emit_MOVR0i(arg) emit_MOVRxi(R0, arg)

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

#define VLDRa(Vd, Rn, i) (AL|(0b1101<<24)|1<<23|((Vd&1)<<22)|1<<20|(Rn<<16)|((Vd>>1)<<12)|(0b1010<<8)|off8(i))
#define VSTRa(Vd, Rn, i) (AL|(0b1101<<24)|1<<23|((Vd&1)<<22)|0<<20|(Rn<<16)|((Vd>>1)<<12)|(0b1010<<8)|off8(i))

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


static void emit_MOVRxi( uint32_t reg, uint32_t imm )
{
	if ( imm <= 0xFFFF ) {
		emit( MOVW( reg, imm ) );
#if 1
	} else if ( can_encode( imm ) ) {
		emit( MOVi( reg, imm ) );
	} else if ( can_encode( ~imm ) ) {
		emit( MVNi( reg, ~imm ) );
#endif
	} else {
		emit( MOVW( reg, (imm&0xFFFF) ) );
		emit( MOVT( reg, (((imm>>16)&0xFFFF)) ) );
	}
}


#if defined(REG0_OPTIMIZE) || defined(S0_OPTIMIZE)
static void rewind4( vm_t *vm )
{
	compiledOfs -= 4;
	vm->instructionPointers[ ip-1 ] = compiledOfs;
	LastCommand = LAST_COMMAND_NONE;
}
#endif

static void emit_store_opstack_r0( vm_t *vm )
{
	emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
	LastCommand = LAST_COMMAND_STORE_OPSTACK_R0;
}


static void emit_store_opstack_p4_r0( vm_t *vm )
{
	emit(STRaiw(R0, rOPSTACK, 4)); // opstack+=4; *opstack = r0
	LastCommand = LAST_COMMAND_STORE_OPSTACK_P4_R0;
}


static void emit_load_r0_opstack( vm_t *vm )
{
#ifdef REG0_OPTIMIZE
	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_R0 ) // *opstack = r0;
	{
		rewind4( vm );
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_P4_R0 ) // opstack +=4; *opstack = r0;
	{
		rewind4( vm );
		emit(ADDi(rOPSTACK, rOPSTACK, 4)); // opstack += 4;
		return;
	}
#endif
	emit(LDRai(R0, rOPSTACK, 0)); // r0 = *opstack
}


static void emit_load_r0_opstack_m4( vm_t *vm )
{
#ifdef REG0_OPTIMIZE
	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_R0 ) // *opstack = r0;
	{
		rewind4( vm );
		emit(SUBi(rOPSTACK, rOPSTACK, 4)); // opstack -= 4;
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_P4_R0 ) // opstack +=4; *opstack = r0;
	{
		rewind4( vm );
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_R0_SYSCALL ) // [opstack -= 4; *opstack = r0;]
	{
		emit(SUBi(rOPSTACK, rOPSTACK, 4)); // opstack -= 4;
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_S0 )         // *opstack = s0
	{
		rewind4( vm );
		emit(VMOVssa(R0,S0));              // r0 = s0
		emit(SUBi(rOPSTACK, rOPSTACK, 4)); // opstack -= 4
		return;
	}
#endif
	emit(LDRTxi(R0, rOPSTACK, 4));  // r0 = *opstack; rOPSTACK -= 4
}


static void emit_load_opstack_s0( vm_t *vm )
{
#ifdef S0_OPTIMIZE
	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_S0 ) // *((float*)opstack) = s0
	{
		rewind4( vm );
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_R0 ) // *opstack = r0;
	{
		rewind4( vm );
		emit(VMOVass(S0,R0));   // s0 = r0
		return;
	}

	if ( LastCommand == LAST_COMMAND_STORE_OPSTACK_P4_R0 ) // opstack+=4; *opstack = r0
	{
		emit(VMOVass(S0,R0));   // s0 = r0
		return;
	}
#endif
	emit(VLDRa(S0, rOPSTACK, 0));   // s0 = *((float*)opstack)
}


static void emit_store_opstack_s0( vm_t *vm )
{
	emit(VSTRa(S0, rOPSTACK, 0));   // *((float*)opstack) = s0
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


static uint32_t encode_offset( uint32_t ofs )
{
	const uint32_t x = (ofs - 8) >> 2;
	const uint32_t t = x >> 24;
	if ( t != 0x3F && t != 0x00 )
		Com_Error( ERR_DROP, "can't encode offsset %i", ofs );
	return x & 0x00FFFFFF;
}


static void emitFuncOffset( uint32_t comp, vm_t *vm, func_t func )
{
	uint32_t offset = funcOffset[ func ] - compiledOfs;

	emit( cond( comp, BLi( encode_offset( offset ) ) ) );
}


static void emitCallFunc( vm_t *vm )
{
	static int bytes_to_skip = -1;
	static unsigned start_block = -1;

funcOffset[ FUNC_CALL ] = compiledOfs; // to jump from OP_CALL

	emit(CMPi(R0, 0)); // check if syscall

	if (start_block == -1)
		start_block = compiledOfs;

	emit(cond(LT, Bi(encode_offset(bytes_to_skip))));

	if ( vm_rtChecks->integer & 4 ) {
		// check if R0 >= header->instructionCount
		//emit_MOVRxi(R1, (unsigned)header->instructionCount);
		emit(LDRai(R1, rVMBASE, offsetof(vm_t, instructionCount)));
		emit(CMP(R0, R1));
		emitFuncOffset( HS, vm, FUNC_OUTJ );
	}

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

	// save LR because it will be clobbered by BLX instruction
	emit(PUSH((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rPROCBASE)|(1<<LR)));

	// modify VM stack pointer for recursive VM entry

	//currentVM->programStack = pstack - 4;
	emit(SUBi(R1, rPSTACK, 4)); // r1 = pstack - 4
	emit(STRai(R1, rVMBASE, offsetof(vm_t, programStack))); // vm->programStack = r1

	//argPosition = (intptr_t *)((byte *)currentVM->dataBase + pstack + 4);
	emit(ADDi(R2,rPROCBASE,4)); /// r2 = rPROCBASE + 4

	//argPosition[0] = call;
	emit(STRai(R0, R2, 0)); // r2[0] = r0

	emit(MOV(R0,R2));

	//ret = currentVM->systemCall( argPosition );
	emit(LDRai(R12, rVMBASE, offsetof(vm_t,systemCall))); // r12 = vm->systemCall
	emit(BLX(R12)); // call [r12]( r0 )

	emit(POP((1<<rOPSTACK)|(1<<rPSTACK)|(1<<rPROCBASE))|(1<<LR));

	// store return value
	emit_store_opstack_p4_r0( vm ); // opstack+=4; *opstack = r0;

	// return to caller
	emit(MOV(PC,LR));

	emit(BKPT(0));
}


static void emit_CheckReg( vm_t *vm, instruction_t *ins, uint32_t reg )
{
	if ( ins->safe ) {
		return;
	}

	if ( !( vm_rtChecks->integer & 8 ) || vm->forceDataMask ) {
		if ( vm->forceDataMask ) {
			emit(AND(reg, rDATAMASK, reg));    // rN = rN & rDATAMASK
		}
		return;
	}

	emit(CMP(reg, rDATAMASK));
	emitFuncOffset(HI, vm, FUNC_BADD);
}


#ifdef CONST_OPTIMIZE
static qboolean ConstOptimize( vm_t *vm )
{
	uint32_t x;

	switch ( ni->op ) {

	case OP_LOAD4:
		x = ci->value;
		if ( x < 4096 ) {
			emit(LDRai(R0, rDATABASE, x)); // r0 = [dataBase + v]
		} else {
			emit_MOVRxi(R1, x);
			emit(LDRa(R0, rDATABASE, R1)); // r0 = [dataBase + r1]
		}
		emit_store_opstack_p4_r0( vm ); // opstack +=4 ; *opstack = r0;
		ip += 1; // OP_LOAD4
		return qtrue;

	case OP_LOAD2:
		x = ci->value;
		if ( (ci+2)->op == OP_SEX16 ) {
			if ( x < 256 ) {
				emit(LDRSHai(R0, rDATABASE, x)); // r0 = (signed short*)[dataBase + v]
			} else {
				emit_MOVRxi(R1, x);
				emit(LDRSHa(R0, rDATABASE, R1)); // r0 = (signed short*)[dataBase + r1]
			}
			ip += 2; // OP_LOAD2 + OP_SEX16
		} else {
			if ( x < 256 ) {
				emit(LDRHai(R0, rDATABASE, x));  // r0 = (unsigned short*)[dataBase + v]
			} else {
				emit_MOVRxi(R1, x);
				emit(LDRHa(R0, rDATABASE, R1));  // r0 = (unsigned short*)[dataBase + r1]
			}
			ip += 1; // OP_LOAD2
		}
		emit_store_opstack_p4_r0( vm ); // opstack +=4 ; *opstack = r0;
		return qtrue;

	case OP_LOAD1:
		x = ci->value;
		if ( (ci+2)->op == OP_SEX8 ) {
			if ( x < 256 ) {
				emit(LDRSBai(R0, rDATABASE, x)); // r0 = (signed char*)[dataBase + x]
			} else {
				emit_MOVRxi(R1, x);
				emit(LDRSBa(R0, rDATABASE, R1)); // r0 = (signed char*)[dataBase + r1]
			}
			ip += 2; // OP_LOAD1 + OP_SEX8
		} else {
			if ( x < 4096 ) {
				emit(LDRBai(R0, rDATABASE, x));  // r0 = (byte*)[dataBase + x]
			} else {
				emit_MOVRxi(R1, x);
				emit(LDRBa(R0, rDATABASE, R1));  // r0 = (byte*)[dataBase + r1]
			}
			ip += 1; // OP_LOAD1
		}
		emit_store_opstack_p4_r0( vm ); // opstack +=4 ; *opstack = r0;
		return qtrue;

	case OP_STORE4:
		x = ci->value;
		emit_load_r0_opstack_m4( vm ); // r0 = *opstack; opstack -=4;
		emit_CheckReg(vm, ni, R0);
		emit_MOVRxi(R1, x);
		emit(STRa(R1, rDATABASE, R0)); // [dataBase + r0] = r1;
		ip += 1; // OP_STORE4
		return qtrue;

	case OP_STORE2:
		x = ci->value;
		emit_load_r0_opstack_m4( vm ); // r0 = *opstack; opstack -=4;
		emit_CheckReg(vm, ni, R0);
		emit_MOVRxi(R1, x);
		emit(STRHa(R1, rDATABASE, R0)); // (word*)[dataBase + r0] = r1;
		ip += 1; // OP_STORE2
		return qtrue;

	case OP_STORE1:
		x = ci->value;
		emit_load_r0_opstack_m4( vm ); // r0 = *opstack; opstack -=4;
		emit_CheckReg(vm, ni, R0);
		emit_MOVRxi(R1, x);
		emit(STRBa(R1, rDATABASE, R0)); // (byte*)[dataBase + r0] = r1;
		ip += 1; // OP_STORE1
		return qtrue;

	case OP_ADD:
		x = ci->value;
		emit_load_r0_opstack( vm ); // r0 = *opstack;
		if ( can_encode( x ) ) {
			emit(ADDi(R0, R0, x));
		} else {
			emit_MOVRxi(R1, x);
			emit(ADD(R0, R1, R0));
		}
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_ADD
		return qtrue;

	case OP_SUB:
		x = ci->value;
		emit_load_r0_opstack( vm ); // r0 = *opstack;
		if ( can_encode( x ) ) {
			emit(SUBi(R0, R0, x));
		} else {
			emit_MOVRxi(R1, x);
			emit(SUB(R0, R0, R1));
		}
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_SUB
		return qtrue;

	case OP_MULI:
	case OP_MULU:
		emit_load_r0_opstack( vm );     // r0 = *opstack;
		x = ci->value;
		emit_MOVRxi(R1, x);
		emit(MUL(R0, R1, R0));
		emit_store_opstack_r0( vm );    // *opstack = r0;
		ip += 1; // OP_MULI|OP_MULU
		return qtrue;

	case OP_DIVI:
	case OP_DIVU:
		if ( !(CPU_Flags & CPU_IDIVA) ) {
			return qfalse;
		}
		emit_load_r0_opstack( vm );     // r0 = *opstack;
		x = ci->value;
		emit_MOVRxi(R1, x);
		if ( ni->op == OP_DIVI ) {
			emit(SDIV(R0, R0, R1));     // r0 = r0 / r1
		} else {
			emit(UDIV(R0, R0, R1));
		}
		emit_store_opstack_r0( vm );    // *opstack = r0;
		ip += 1;
		return qtrue;

	case OP_MODI:
	case OP_MODU:
		if ( !(CPU_Flags & CPU_IDIVA) ) {
			return qfalse;
		}
		emit_load_r0_opstack( vm );     // r0 = *opstack;
		x = ci->value;
		emit_MOVRxi(R1, x);
		if ( ni->op == OP_MODI ) {
			emit(SDIV(R2, R0, R1));     // r2 = r0 / r1
		} else {
			emit(UDIV(R2, R0, R1));
		}
		emit(MLS(R0, R1, R2, R0));      // r0 = r0 - r1 * r2
		emit_store_opstack_r0( vm );    // *opstack = r0;
		ip += 1;
		return qtrue;

	case OP_LSH:
		x = ci->value;
		if ( x < 0 || x > 31 )
			break;
		emit_load_r0_opstack( vm );  // r0 = *opstack;
		emit(LSLi(R0, R0, x));       // r0 = r1 << r0
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_LSH
		return qtrue;

	case OP_RSHI:
		x = ci->value;
		if ( x < 0 || x > 31 )
			break;
		emit_load_r0_opstack( vm );  // r0 = *opstack;
		emit(ASRi(R0, R0, x));       // r0 = r0 >> x
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_RSHI
		return qtrue;

	case OP_RSHU:
		x = ci->value;
		if ( x < 0 || x > 31 )
			break;
		emit_load_r0_opstack( vm );  // r0 = *opstack;
		emit(LSRi(R0, R0, x));       // r0 = (unsigned)r0 >> x
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_RSHU
		return qtrue;

	case OP_BAND:
		x = ci->value;
		emit_load_r0_opstack( vm );  // r0 = *opstack;
		if ( can_encode( x ) ) {
			emit(ANDi(R0, R0, x));       // r0 = r0 & x
		} else {
			emit_MOVRxi(R1, x);
			emit(AND(R0, R0, R1));       // r0 = r0 & r1
		}
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_BAND
		return qtrue;

	case OP_BOR:
		x = ci->value;
		emit_load_r0_opstack( vm );    // r0 = *opstack;
		if ( can_encode( x ) ) {
			emit(ORRi(R0, R0, x)); // r0 = r0 | x
		} else {
			emit_MOVRxi(R1, x);
			emit(ORR(R0, R0, R1)); // r0 = r0 | r1
		}
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_BOR
		return qtrue;

	case OP_BXOR:
		x = ci->value;
		emit_load_r0_opstack( vm );    // r0 = *opstack;
		if ( can_encode( x ) ) {
			emit(EORi(R0, R0, x)); // r0 = r0 ^ x
		} else {
			emit_MOVRxi(R1, x);
			emit(EOR(R0, R0, R1)); // r0 = r0 ^ r1
		}
		emit_store_opstack_r0( vm ); // *opstack = r0;
		ip += 1; // OP_BXOR
		return qtrue;

	case OP_JUMP:
		emit(Bi(encode_offset(vm->instructionPointers[ ci->value ] - compiledOfs)));
		ip += 1; // OP_JUMP
		return qtrue;

	case OP_CALL:
		if ( ci->value == ~TRAP_SQRT ) {
			emit(LDRai(R0, rPROCBASE, 8));     // r0 = [procBase + 8]
			emit(VMOVass(S0,R0));              // s0 = r0
			emit(ADDi(rOPSTACK, rOPSTACK, 4)); // opstack += 4
			emit(VSQRT_F32(S0, S0));           // s0 = sqrt(s0)
			emit_store_opstack_s0( vm );       // *((float*)opstack) = s0
			ip += 1;
			return qtrue;
		}
		if ( ci->value == ~TRAP_SIN || ci->value == ~TRAP_COS ) {
			emit(VLDRa(S0, rPROCBASE, 8));     // s0 = [procBase + 8]
			emit(ADDi(rOPSTACK, rOPSTACK, 4)); // opstack += 4
			if ( ci->value == ~TRAP_SIN )
				emit_MOVRxi(R12, (unsigned)sinf);
			else
				emit_MOVRxi(R12, (unsigned)cosf);
			emit(BLX(R12));
			emit_store_opstack_s0( vm );       // *opstack = s0
			ip += 1;
			return qtrue;
		}
		if ( ci->value < 0 ) // syscall
		{
			x = ~ci->value;
			emit_MOVRxi(R0, x);
			emitFuncOffset( AL, vm, FUNC_SYSF );
			ip += 1; // OP_CALL;
			LastCommand = LAST_COMMAND_STORE_OPSTACK_R0_SYSCALL; // we have result in r0
			return qtrue;
		}
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
		emit_load_r0_opstack_m4( vm ); // r0 = *opstack; rOPSTACK -= 4
		x = ci->value;
		if ( can_encode( x ) ) {
			emit(CMPi(R0, x));
		} else {
			emit_MOVRxi(R1, x);
			emit(CMP(R0, R1));
		}
		emit(cond(comp, Bi(encode_offset(vm->instructionPointers[ ni->value ] - compiledOfs))));
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
		x = ci->value;
		emit_load_r0_opstack_m4( vm );     // r0 = *opstack; rOPSTACK -= 4
		emit_MOVRxi(R1, x);                // r1 = ci->value
		emit(VMOVass(S0, R0));             // s0 = r0
		emit(VMOVass(S1, R1));             // s1 = r1
		emit(VCMP_F32(S0, S1));
		emit(VMRS(APSR_nzcv));
		emit(cond(comp, Bi(encode_offset(vm->instructionPointers[ ni->value ] - compiledOfs))));
		ip += 1; // OP_cond
		return qtrue;
		}

	case OP_ADDF:
	case OP_SUBF:
	case OP_MULF:
	case OP_DIVF:
		emit_load_opstack_s0( vm );        // s0 = *opstack;
		x = ci->value;
		emit_MOVRxi(R1, x);                // r1 = ci->value
		emit(VMOVass(S1,R1));              // s1 = r1
		switch ( ni->op ) {
			case OP_ADDF: emit(VADD_F32(S0, S0, S1)); break; // s0 = s0 + s1
			case OP_SUBF: emit(VSUB_F32(S0, S0, S1)); break; // s0 = s0 - s1
			case OP_MULF: emit(VMUL_F32(S0, S0, S1)); break; // s0 = s0 * s1
			case OP_DIVF: emit(VDIV_F32(S0, S0, S1)); break; // s0 = s0 / s1
			default: break;
		}
		emit_store_opstack_s0( vm );       // *((float*)opstack) = s0
		ip += 1; // OP_XXXF
		return qtrue;

	default:
		break;
	}

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
	switch ( op )
	{
		//[local] += CONST
		case MOP_ADD4:
		case MOP_SUB4:
		case MOP_BAND4:
		case MOP_BOR4:
			value = inst[ip+2].value;
			addr = ci->value;

			// load
			if ( addr < 4096 ) {
				emit(LDRai(R0, rPROCBASE, addr)); // r0 = [procBase + addr]
			} else {
				emit_MOVRxi(R2, addr);            // r2 = addr
				emit(LDRa(R0, rPROCBASE, R2));    // r0 = [procBase + r2]
			}

			// modify
			switch ( op ) {
				case MOP_ADD4:
					if ( can_encode( value ) ) {
						emit(ADDi(R0, R0, value));        // r0 += value;
					} else {
						emit_MOVRxi(R1, value);           // r1 = value
						emit(ADD(R0, R0, R1));            // r0 += r1
					} break;
				case MOP_SUB4:
					if ( can_encode( value ) ) {
						emit(SUBi(R0, R0, value));        // r0 += value;
					} else {
						emit_MOVRxi(R1, value);           // r1 = value
						emit(SUB(R0, R0, R1));            // r0 += r1
					} break;
				case MOP_BAND4:
					if ( can_encode( value ) ) {
						emit(ANDi(R0, R0, value));        // r0 += value;
					} else {
						emit_MOVRxi(R1, value);           // r1 = value
						emit(AND(R0, R0, R1));            // r0 += r1
					} break;
				case MOP_BOR4:
					if ( can_encode( value ) ) {
						emit(ORRi(R0, R0, value));        // r0 += value;
					} else {
						emit_MOVRxi(R1, value);           // r1 = value
						emit(ORR(R0, R0, R1));            // r0 += r1
					} break;
			}

			// store
			if ( addr < 4096 ) {
				emit(STRai(R0, rPROCBASE, addr)); // [procBase + addr] = r0
			} else {
				emit(STRa(R0, rPROCBASE, R2));    // [procBase + r2] = r0
			}

			ip += 5;
			return qtrue;

		default: Com_Error( ERR_DROP, "Unknown macro opcode %i", op );
			break;
	};

	return qfalse;
}
#endif


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


qboolean VM_Compile( vm_t *vm, vmHeader_t *header )
{
	const char *errMsg;
	int proc_base;
	int proc_len;
	int i;

	if ( ( CPU_Flags & ( CPU_ARMv7 | CPU_VFPv3 ) ) != ( CPU_ARMv7 | CPU_VFPv3 ) ) {
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
	LastCommand = LAST_COMMAND_NONE;

	proc_base = -1;
	proc_len = 0;

	emit(PUSH(SAVE_REGS|(1<<LR))); // push R4-R11, LR
	emit(SUBi(SP, SP, 12));        // align stack to 16 bytes

	// these are constant at compile time
	emit_MOVRxi(rVMBASE, (unsigned)vm);
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
	emit(STRai(rOPSTACK, rVMBASE, offsetof(vm_t, opStack)));     // vm->opStack = rOPSTACK;
#endif

	emit(ADDi(SP, SP, 12));       // align stack to 16 bytes
	emit(POP(SAVE_REGS|(1<<PC))); // pop R4-R11, LR -> PC
	emit(BKPT(0));

	funcOffset[ FUNC_ENTR ] = compiledOfs; // offset to vmMain() entry point

	while ( ip < header->instructionCount ) {

		uint32_t v;
		vm->instructionPointers[ ip ] = compiledOfs;

		ci = &inst[ ip + 0 ];
		ni = &inst[ ip + 1 ];

		if ( ci->jused )
		{
			// we can safely perform rewind-optimizations only in case if
			// we are 100% sure that current instruction is not a jump label
			LastCommand = LAST_COMMAND_NONE;
		}

		ip++;
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

				proc_base = ip;
				// locate endproc
				for ( proc_len = -1, i = ip; i < header->instructionCount; i++ ) {
					if ( inst[ i ].op == OP_PUSH && inst[ i + 1 ].op == OP_LEAVE ) {
						proc_len = i - proc_base;
						break;
					}
				}

				emit(PUSH((1<<R12)|(1<<rPSTACK)|(1<<rPROCBASE)|(1<<LR)));
				if ( can_encode( v ) ) {
					emit(SUBi(rPSTACK, rPSTACK, v)); // pstack -= arg
				} else {
					emit_MOVR0i(v);                  // r0 = arg
					emit(SUB(rPSTACK, rPSTACK, R0)); // pstack -= r0
				}

				// programStack overflow check
				if ( vm_rtChecks->integer & 1 ) {
					// check if pStack < vm->stackBottom
					emit(LDRai(R1, rVMBASE, offsetof(vm_t, stackBottom))); // r1 = vm->stackBottom
					emit(CMP(rPSTACK, R1));
					emitFuncOffset( LO, vm, FUNC_PSOF );
				}

				// opStack overflow check
				if ( vm_rtChecks->integer & 2 ) {
					uint32_t n = ci->opStack;            // proc->opStack carries max.used opStack value
					if ( can_encode( n ) ) {
						emit(ADDi(R2, rOPSTACK, n)); // r2 = opstack + n;
					} else {
						emit_MOVRxi(R2, n);          // r2 = n
						emit(ADD(R2, rOPSTACK, R2)); // r2 = opstack + r2;
					}
					emit(CMP(R2, rOPSTACKTOP));
					emitFuncOffset( HI, vm, FUNC_OSOF );
				}

				emit(ADD(rPROCBASE, rPSTACK, rDATABASE));
				break;

			case OP_LEAVE:
				emit(POP((1<<R12)|(1<<rPSTACK)|(1<<rPROCBASE)|(1<<PC)));
				break;

			case OP_CALL:
#if 0
				// save next instruction
				emit_MOVR0i(ip);
				emit(STRa(R0, rDATABASE, rPSTACK));      // dataBase[pstack] = r0
#endif
				emit_load_r0_opstack_m4( vm );           // r0 = *opstack; rOPSTACK -= 4
				emitFuncOffset(AL, vm, FUNC_CALL);
				break;

			case OP_PUSH:
				emit(ADDi(rOPSTACK, rOPSTACK, 4)); // opstack += 4
				if ( ni->op == OP_LEAVE ) {
					proc_base = -1;
				}
				break;

			case OP_POP:
				emit(SUBi(rOPSTACK, rOPSTACK, 4)); // opstack -= 4
				break;

			case OP_CONST:
#ifdef CONST_OPTIMIZE
				if ( ConstOptimize( vm ) )
					break;
#endif
				emit_MOVR0i(v);                     // mov r0, 0x12345678
				emit_store_opstack_p4_r0( vm );     // opstack+=4; *opstack = r0
				break;

			case OP_LOCAL:
#ifdef VM_OPTIMIZE
				if ( ni->op == OP_LOAD4 ) // merge OP_LOCAL + OP_LOAD4
				{
					if ( v < 4096 ) {
						emit(LDRai(R0, rPROCBASE, v)); // r0 = [procBase + v]
					} else {
						emit_MOVRxi(R1, v);
						emit(LDRa(R0, rPROCBASE, R1)); // r0 = [procBase+r1]
					}
					emit_store_opstack_p4_r0( vm ); // opstack+=4; *opstack = r0
					ip++; // OP_LOAD4
					break;
				}

				if ( ni->op == OP_LOAD2 ) // merge OP_LOCAL + OP_LOAD2
				{
					if ( v < 256 ) {
						emit(LDRHai(R0, rPROCBASE, v)); // r0 = (unsigned short*)[procBase + v]
					} else {
						emit_MOVRxi(R1, v);
						emit(LDRHa(R0, rPROCBASE, R1)); // r0 = (unsigned short*)[procBase+r1]
					}
					emit_store_opstack_p4_r0( vm ); // opstack+=4; *opstack = r0
					ip++; // OP_LOAD2
					break;
				}

				if ( ni->op == OP_LOAD1 ) // merge OP_LOCAL + OP_LOAD1
				{
					if ( v < 4096 ) {
						emit(LDRBai(R0, rPROCBASE, v)); // r0 = (byte*)[procBase+v]
					} else {
						emit_MOVRxi(R1, v);
						emit(LDRBa(R0, rPROCBASE, R1)); // r0 = (byte*)[procBase+r1]
					}
					emit_store_opstack_p4_r0( vm ); // opstack+=4; *opstack = r0
					ip++; // OP_LOAD1
					break;
				}
#endif // VM_OPTIMIZE

				if ( can_encode( v ) ) {
					emit(ADDi(R0, rPSTACK, v));     // r0 = pstack + arg
				} else {
					emit_MOVR0i(v);                 // r0 = arg;
					emit(ADD(R0, rPSTACK, R0));     // r0 = pstack + r0
				}

				emit_store_opstack_p4_r0( vm );     // opstack+=4; *opstack = r0
				break;

			case OP_JUMP:
				emit_load_r0_opstack_m4( vm );  // r0 = *opstack; rOPSTACK -= 4
				if ( vm_rtChecks->integer & 4 ) {
					if ( proc_base != -1 ) {
						// allow jump within local function scope only
						// R2 = ip - proc_base
						if ( can_encode( proc_base ) )
							emit(SUBi(R2, R0, proc_base));
						else {
							emit_MOVRxi(R1, proc_base);
							emit(SUB(R2, R0, R1));
						}
						// ip > proc_len
						if ( can_encode( proc_len ) ) {
							emit(CMPi(R2, proc_len));
						} else {
							emit_MOVRxi(R1, proc_len);
							emit(CMP(R2, R1));
						}
						emitFuncOffset( HI, vm, FUNC_OUTJ );
					} else {
						// check if r0 >= header->instructionCount
						emit_MOVRxi(R1, (unsigned)header->instructionCount);
						//emit(LDRai(R1, rVMBASE, offsetof(vm_t, instructionCount)));
						emit(CMP(R0, R1));
						emitFuncOffset( HS, vm, FUNC_OUTJ );
					}
				}
				emit(LDRa(R12, rINSPOINTERS, rLSL(2, R0))); // r12 = instructionPointers[ r0 ]
				emit(BX(R12));
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
				emit_load_r0_opstack_m4( vm ); // r0 = *opstack; rOPSTACK -= 4
				emit(LDRTxi(R1, rOPSTACK, 4)); // r1 = *opstack; rOPSTACK -= 4
				emit(CMP(R1, R0));
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
				emit(SUBi(rOPSTACK, rOPSTACK, 8));
				emit(VLDRa(S1, rOPSTACK, 4));
				emit(VLDRa(S0, rOPSTACK, 8));
				emit(VCMP_F32(S1, S0));
				emit(VMRS(APSR_nzcv));
				emit(cond(comp, Bi(encode_offset(vm->instructionPointers[v] - compiledOfs))));
				}
				break;

			case OP_LOAD1:
				emit_load_r0_opstack( vm );      // r0 = *opstack;
				emit_CheckReg(vm, ci, R0);
				emit(LDRBa(R0, rDATABASE, R0));  // r0 = (unsigned char)dataBase[r0]
				emit_store_opstack_r0( vm );     // *opstack = r0;
				break;

			case OP_LOAD2:
				emit_load_r0_opstack( vm );
				emit_CheckReg(vm, ci, R0);
				emit(LDRHa(R0, rDATABASE, R0));  // r0 = (unsigned short)dataBase[r0]
				emit_store_opstack_r0( vm );     // *opstack = r0
				break;

			case OP_LOAD4:
				emit_load_r0_opstack( vm );      // r0 = *opstack
				emit_CheckReg(vm, ci, R0);
				emit(LDRa(R0, rDATABASE, R0));   // r0 = dataBase[r0]
				emit_store_opstack_r0( vm );     // *opstack = r0
				break;

			case OP_STORE1:
				emit_load_r0_opstack_m4( vm );   // r0 = *opstack; rOPSTACK -= 4
				emit(LDRTxi(R1, rOPSTACK, 4));   // r1 = *opstack; rOPSTACK -= 4
				emit_CheckReg(vm, ci, R1);
				emit(STRBa(R0, rDATABASE, R1));  // database[r1] = r0
				break;

			case OP_STORE2:
				emit_load_r0_opstack_m4( vm );  // r0 = *opstack; rOPSTACK -= 4
				emit(LDRTxi(R1, rOPSTACK, 4));  // r1 = *opstack; rOPSTACK -= 4
				emit_CheckReg(vm, ci, R1);
				emit(STRHa(R0, rDATABASE, R1)); // database[r1] = r0
				break;

			case OP_STORE4:
				emit_load_r0_opstack_m4( vm );  // r0 = *opstack; rOPSTACK -= 4
				emit(LDRTxi(R1, rOPSTACK, 4));  // r1 = *opstack; rOPSTACK -= 4
				emit_CheckReg(vm, ci, R1);
				emit(STRa(R0, rDATABASE, R1)); // database[r1] = r0
				break;

			case OP_ARG:
				emit_load_r0_opstack_m4( vm );  // r0 = *opstack; rOPSTACK -= 4
#ifdef VM_OPTIMIZE
				if ( v < 4096 ) {
					emit(STRai(R0, rPROCBASE, v)); // [procBase + v] = r0;
				} else {
					emit_MOVRxi(R1, v);
					emit(STRa(R0, rPROCBASE, R1)); // [procBase + r1] = r0;
				}
#else
				emit(ADDi(R1, rPSTACK, v));     // r1 = programStack+arg
				emit(STRa(R0, rDATABASE, R1));  // dataBase[r1] = r0
#endif
				break;

			case OP_BLOCK_COPY:
				// src: opStack[0]
				// dst: opstack[-4]
				emit_load_r0_opstack_m4( vm );  // src: r0 = *opstack; rOPSTACK -= 4
				emit(LDRTxi(R1, rOPSTACK, 4));  // dst: r1 = *opstack; rOPSTACK -= 4
				emit_MOVRxi(R2, v);
				emit(MOV(R3,rVMBASE));
				emit_MOVRxi(R12, (unsigned)VM_BlockCopy);
				emit(BLX(R12));
				break;

			case OP_SEX8:
				emit(LDRSBai(R0, rOPSTACK, 0)); // r0 = sign extend *opstack
				emit_store_opstack_r0( vm );    // *opstack = r0
				break;

			case OP_SEX16:
				emit(LDRSHai(R0, rOPSTACK, 0)); // r0 = sign extend *opstack
				emit_store_opstack_r0( vm );    // *opstack = r0
				break;

			case OP_NEGI:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(RSBi(R0, R0, 0));         // r0 = -r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_ADD:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(ADD(R0, R1, R0));         // r0 = r1 + r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_SUB:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(SUB(R0, R1, R0));         // r0 = r1 - r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_DIVI:
			case OP_DIVU:
				if ( CPU_Flags & CPU_IDIVA ) {
					emit_load_r0_opstack( vm );    // r0 = *opstack
					emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
					if ( ci->op == OP_DIVI ) {
						emit(SDIV(R0, R1, R0));
					} else {
						emit(UDIV(R0, R1, R0));
					}
				} else {
					emit(LDRai(R1, rOPSTACK, 0));  // r1 = *opstack
					emit(LDRxiw(R0, rOPSTACK, 4)); // opstack-=4; r0 = *opstack
					if ( ci->op == OP_DIVI )
						emit_MOVRxi(R12, (unsigned)__aeabi_idiv);
					else
						emit_MOVRxi(R12, (unsigned)__aeabi_uidiv);
					emit(BLX(R12));
				}
				emit_store_opstack_r0( vm );    // *opstack = r0
				break;

			case OP_MODI:
			case OP_MODU:
				if ( CPU_Flags & CPU_IDIVA ) {
					emit_load_r0_opstack( vm );
					emit(LDRxiw(R1, rOPSTACK, 4));  // opstack-=4; r1 = *opstack
					if ( ci->op == OP_MODI ) {
						emit(SDIV(R2, R1, R0)); // r2 = r1 / r0
					} else {
						emit(UDIV(R2, R1, R0));
					}
					emit(MLS(R0, R0, R2, R1));      // r0 = r1 - r0 * r2
					emit_store_opstack_r0( vm );    // *opstack = r0
				} else {
					emit(LDRai(R1, rOPSTACK, 0));   // r1 = *opstack
					emit(LDRxiw(R0, rOPSTACK, 4));  // opstack-=4; r0 = *opstack
					if ( ci->op == OP_MODI )
						emit_MOVRxi(R12, (unsigned)__aeabi_idivmod);
					else
						emit_MOVRxi(R12, (unsigned)__aeabi_uidivmod);
					emit(BLX(R12));
					emit(STRai(R1, rOPSTACK, 0));   // *opstack = r1
				}
				break;

			case OP_MULI:
			case OP_MULU:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(MUL(R0, R1, R0));         // r0 = r1 * r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_BAND:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(AND(R0, R1, R0));         // r0 = r1 & r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_BOR:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(ORR(R0, R1, R0));         // r0 = r1 | r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_BXOR:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(EOR(R0, R1, R0));         // r0 = r1 ^ r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_BCOM:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(MVN(R0, R0));             // r0 = ~r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_LSH:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(LSL(R0, R1, R0));         // r0 = r1 << r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_RSHI:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(ASR(R0, R1, R0));         // r0 = r1 >> r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_RSHU:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(LSR(R0, R1, R0));         // r0 = (unsigned)r1 >> r0
				emit_store_opstack_r0( vm );   // *opstack = r0
				break;

			case OP_NEGF:
				emit_load_opstack_s0( vm );    // s0 = *((float*)opstack)
				emit(VNEG_F32(S0, S0));        // s0 = -s0
				emit_store_opstack_s0( vm );   // *((float*)opstack) = s0
				break;

			case OP_ADDF:
				emit_load_opstack_s0( vm );    // s0 = *((float*)opstack)
				// vldr can't modify rOPSTACK so
				// we'd either need to change it
				// with sub or use regular ldr+vmov
				emit(LDRxiw(R0, rOPSTACK, 4)); // opstack-=4; r0 = *opstack
				emit(VMOVass(S1, R0));         // s1 = r0
				emit(VADD_F32(S0, S1, S0));    // s0 = s1 + s0
				emit_store_opstack_s0( vm );   // *((float*)opstack) = s0
				break;

			case OP_SUBF:
				emit_load_opstack_s0( vm );    // s0 = *((float*)opstack)
				// see OP_ADDF
				emit(LDRxiw(R0, rOPSTACK, 4)); // opstack-=4; r0 = *opstack
				emit(VMOVass(S1, R0));         // s1 = r0
				emit(VSUB_F32(S0, S1, S0));    // s0 = s1 - s0
				emit_store_opstack_s0( vm );   // *((float*)opstack) = s0
				break;

			case OP_DIVF:
				emit_load_opstack_s0( vm );    // s0 = *((float*)opstack)
				// see OP_ADDF
				emit(LDRxiw(R0, rOPSTACK, 4)); // opstack-=4; r0 = *opstack
				emit(VMOVass(S1, R0));         // s1 = r0
				emit(VDIV_F32(S0, S1, S0));    // s0 = s1 / s0
				emit_store_opstack_s0( vm );   // *((float*)opstack) = s0
				break;

			case OP_MULF:
				emit_load_opstack_s0( vm );    // s0 = *((float*)opstack)
				// see OP_ADDF
				emit(LDRxiw(R0, rOPSTACK, 4)); // opstack-=4; r0 = *opstack
				emit(VMOVass(S1, R0));         // s1 = r0
				emit(VMUL_F32(S0, S1, S0))   ; // s0 = s1 * s0
				emit_store_opstack_s0( vm );   // *((float*)opstack) = s0
				break;

			case OP_CVIF:
				emit_load_r0_opstack( vm );    // r0 = *opstack
				emit(VMOVass(S0, R0));         // s0 = r0
				emit(VCVT_F32_S32(S0, S0));    // s0 = (float)s0
				emit_store_opstack_s0( vm );   // *((float*)opstack) = s0
				break;

			case OP_CVFI:
				emit_load_opstack_s0( vm );   // s0 = *((float*)opstack)
				emit(VCVT_S32_F32(S0, S0));   // s0 = (int)s0
				emit(VMOVssa(R0, S0));        // s0 = r0
				emit_store_opstack_r0( vm );  // *opstack = r0
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
		emitCallFunc( vm );
		emit(BKPT(0));

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
