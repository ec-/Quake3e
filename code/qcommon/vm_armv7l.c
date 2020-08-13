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

#include "vm_local.h"


static vm_t *currentVM = NULL; // needed only for VM_BlockCopy

//staticuint32_t *code;
static	uint32_t  compiledOfs;
//static	uint32_t *instructionOffsets;
//static	intptr_t *instructionPointers;

static  instruction_t *inst = NULL;
static  instruction_t *ci;
//static  instruction_t *ni;

static	uint32_t	ip;
//static	int	pass;
//static	int	lastConst;
//static	opcode_t	pop1;
//static	ELastCommand	LastCommand;
//int		funcOffset[FUNC_LAST];


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

#define R12	12

#define FP	11
#define SP	13
#define LR	14
#define PC	15

#define APSR_nzcv	15

#define S14     14
#define S15     15

#define rOPSTACK	5
#define rOPSTACKBASE	6
#define rCODEBASE	7
#define rPSTACK		8
#define rDATABASE	9
#define rDATAMASK	10

#define bit(x) (1<<x)

/* arm eabi, builtin gcc functions */
int __aeabi_idiv (int, int);
unsigned __aeabi_uidiv (unsigned, unsigned);
void __aeabi_idivmod(void);
void __aeabi_uidivmod(void);

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


/*
=================
ErrJump
Error handler for jump/call to invalid instruction number
=================
*/
static void __attribute__((__noreturn__)) ErrJump(unsigned num)
{
	Com_Error(ERR_DROP, "program tried to execute code outside VM (%x)", num);
}


static int asmcall( int call, int pstack )
{
	// save currentVM so as to allow for recursive VM entry
	vm_t *savedVM = currentVM;
	intptr_t *argPosition;
	int ret;

	// modify VM stack pointer for recursive VM entry
	currentVM->programStack = pstack - 4;

	argPosition = (intptr_t *)((byte *)currentVM->dataBase + pstack + 4);
	argPosition[0] = ~call;
	ret = currentVM->systemCall( argPosition );

	currentVM = savedVM;

	return ret;
}

#include <unistd.h>

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

	if ( pass )
		memcpy( vm->codeBase.ptr + compiledOfs, &isn, 4 );

	compiledOfs += 4;
}

#define emit(isn) _emit(vm, isn, pass)

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
		val =  (val&3)<<30 | val>>2;
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

#define PREINDEX (1<<24)

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

// XXX: v not correctly computed
#define BKPT(v) (AL | 0b10010<<20 | ((v&~0xF)<<4) | 0b0111<<4 | (v&0xF))

#define YIELD (0b110010<<20 | 0b1111<<12 | 1)
#define NOP cond(AL, YIELD)

// immediate value must fit in 0xFF!
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

#define LDRa(dst, base, off)   (AL | (0b011<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRx(dst, base, off)   (AL | (0b011<<25) | (0b1000<<21) | (1<<20) | base<<16 | dst<<12 | off)

#define LDRai(dst, base, off)  (AL | (0b010<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | rimm(off))
#define LDRxi(dst, base, off)  (AL | (0b010<<25) | (0b1000<<21) | (1<<20) | base<<16 | dst<<12 | rimm(off))
#define LDRxiw(dst, base, off) (AL | (0b010<<25) | (0b1001<<21) | (1<<20) | base<<16 | dst<<12 | rimm(off))

#define LDRTa(dst, base, off)  (AL | (0b011<<25) | (0b0101<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRTx(dst, base, off)  (AL | (0b011<<25) | (0b0001<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRTai(dst, base, off) (AL | (0b010<<25) | (0b0101<<21) | (1<<20) | base<<16 | dst<<12 | rimm(off))
#define LDRTxi(dst, base, off) (AL | (0b010<<25) | (0b0001<<21) | (1<<20) | base<<16 | dst<<12 | rimm(off))

#define LDRBa(dst, base, off)  (AL | (0b011<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | off)
#define LDRSBai(dst, base, off) (AL | (0b000<<25) | (0b0110<<21) | (1<<20) | base<<16 | dst<<12 | ((off&0xF0)<<4)|0b1101<<4|(off&0x0F))
#define STRBa(dst, base, off)  (AL | (0b011<<25) | (0b1110<<21) | (0<<20) | base<<16 | dst<<12 | off)

#define LDRHa(dst, base, off)   (AL | (0b000<<25) | (0b1100<<21) | (1<<20) | base<<16 | dst<<12 | (0b1011<<4) | off)
#define LDRSHai(dst, base, off) (AL | (0b000<<25) | (0b1110<<21) | (1<<20) | base<<16 | dst<<12 | ((off&0xF0)<<4)|0b1111<<4|(off&0x0F))
#define STRHa(dst, base, off)   (AL | (0b000<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | (0b1011<<4) | off)

#define STRa(dst, base, off)   (AL | (0b011<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRx(dst, base, off)   (AL | (0b011<<25) | (0b1000<<21) | (0<<20) | base<<16 | dst<<12 | off)
#define STRai(dst, base, off)  (AL | (0b010<<25) | (0b1100<<21) | (0<<20) | base<<16 | dst<<12 | rimm(off))
#define STRxi(dst, base, off)  (AL | (0b010<<25) | (0b1000<<21) | (0<<20) | base<<16 | dst<<12 | rimm(off))
#define STRaiw(dst, base, off) (AL | (0b010<<25) | (0b1101<<21) | (0<<20) | base<<16 | dst<<12 | rimm(off))
#define STRxiw(dst, base, off) (AL | (0b010<<25) | (0b1001<<21) | (0<<20) | base<<16 | dst<<12 | rimm(off))

// load with post-increment
#define POP1(reg)              (AL | (0b010<<25) | (0b0100<<21) | (1<<20) |   SP<<16 | reg<<12 | reg)
// store with post-increment
#define PUSH1(reg)             (AL | (0b010<<25) | (0b1001<<21) | (0<<20) |   SP<<16 | reg<<12 | 4)

// branch to target address (for small jumps)
#define Bi(i) \
	(AL | (0b10)<<26 | (1<<25) /*I*/ | (0<<24) /*L*/ | (i))
// call subroutine
#define BLi(i) \
	(AL | (0b10)<<26 | (1<<25) /*I*/ | (1<<24) /*L*/ | (i))
// branch and exchange (register)
#define BX(reg) \
	(AL | 0b00010010<<20 | 0b1111<<16 | 0b1111<<12 | 0b1111<<8| 0b0001<<4 | reg)
// call subroutine (register)
#define BLX(reg) \
	(AL | 0b00010010<<20 | 0b1111<<16 | 0b1111<<12 | 0b1111<<8| 0b0011<<4 | reg)

#define PUSH(mask)    (AL | (0b100100<<22) | (0b10<<20) | (0b1101<<16) |  mask)
#define PUSH2(r1, r2) (AL | (0b100100<<22) | (0b10<<20) | (0b1101<<16) |  1<<r1 | 1<<r2)
//#define PUSH1(reg) STRxiw(SP, reg, 4)

#define POP(mask)     (0xe8bd0000|mask)

#define STM(base, regs) \
	(AL | 0b100<<25 | 0<<24/*P*/| 0<<24/*U*/| 0<<24/*S*/| 0<<24/*W*/ | (base<<16) | (regs&~(1<<16)))

// note: op1 and op2 must not be the same
#define MUL(op1, op2, op3) \
	(AL | 0b0000000<<21 | (1<<20) /*S*/ | (op1<<16) | (op3<<8) | 0b1001<<4 | (op2))

// puts integer in R0
#define emit_MOVR0i(arg) emit_MOVRxi(R0, arg)

// puts integer arg in register reg
#define emit_MOVRxi(reg, arg) do { \
	emit(MOVW(reg, (arg&0xFFFF))); \
	if (arg > 0xFFFF) \
		emit(MOVT(reg, (((arg>>16)&0xFFFF)))); \
	} while(0)

// puts integer arg in register reg. adds nop if only one instr is needed to
// make size constant
#define emit_MOVRxi_or_NOP(reg, arg) do { \
	emit(MOVW(reg, (arg&0xFFFF))); \
	if (arg > 0xFFFF) \
		emit(MOVT(reg, (((arg>>16)&0xFFFF)))); \
	else \
		emit(NOP); \
	} while(0)

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

#define _VCMP_F32(Vd, Vm, E) \
	(AL|(0b11101<<23)|((Vd&1)<<22)|(0b11<<20)|((0b0100)<<16)|((Vd>>1)<<12)|(0b101<<9)|(0<<8)|(E<<7)|(1<<6)|((Vm&1)<<5)|(Vm>>1))
#define VCMP_F32(Vd, Vm) _VCMP_F32(Vd, Vm, 0)

#define VMRS(Rt) \
	(AL|(0b11101111<<20)|(0b0001<<16)|(Rt<<12)|(0b1010<<8)|(1<<4))

// check if instruction in R0 is within range. Clobbers R1, R12
#define CHECK_JUMP do { \
	static int bytes_to_skip = -1; \
	static unsigned branch = -1; \
	emit_MOVRxi(R1, (unsigned)header->instructionCount); \
	emit(CMP(R0, R1)); \
	if (branch == -1) \
		branch = compiledOfs; \
	emit(cond(LT, Bi(j_rel(bytes_to_skip)))); \
	emit_MOVRxi_or_NOP(R12, (unsigned)ErrJump); \
	emit(BLX(R12)); \
	if (bytes_to_skip == -1) \
		bytes_to_skip = compiledOfs - branch; \
} while(0)

// optimize: use load multiple
#define IJ(comparator) do { \
	emit(LDRTxi(R0, rOPSTACK, 4)); \
	emit(LDRTxi(R1, rOPSTACK, 4));  \
	emit(CMP(R1, R0)); \
	emit(cond(comparator, Bi(j_rel(vm->instructionPointers[v] - compiledOfs)))); \
} while (0)

#define FJ(comparator) do { \
	emit(SUBi(rOPSTACK, rOPSTACK, 8)); \
	emit(VLDRa(S15, rOPSTACK, 4)); \
	emit(VLDRa(S14, rOPSTACK, 8)); \
	emit(VCMP_F32(S15, S14)); \
	emit(VMRS(APSR_nzcv)); \
	emit(cond(comparator, Bi(j_rel(vm->instructionPointers[v] - compiledOfs)))); \
} while (0)

static inline unsigned _j_rel(int x, int pc)
{
	if (x&3) goto err;
	x = (x>>2)-2;
	if (x < 0)
	{
		if ((x&(0xFF<<24)) != 0xFF<<24)
			goto err;
		x &= ~(0xFF<<24);
	}
	else if (x&(0xFF<<24))
		goto err;
	return x;
err:
	DIE("jump %d out of range at %d", x, pc);
}


/*
=================
VM_BlockCopy
Executes a block copy operation within currentVM data space
=================
*/
static void VM_BlockCopy( uint32_t dest, uint32_t src, uint32_t n )
{
	const uint32_t dataMask = currentVM->dataMask;

	if ((dest & dataMask) != dest
	|| (src & dataMask) != src
	|| ((dest + n) & dataMask) != dest + n
	|| ((src + n) & dataMask) != src + n)
	{
		Com_Error( ERR_DROP, "OP_BLOCK_COPY out of range for vm %s", currentVM->name );
	}

	Com_Memcpy( currentVM->dataBase + dest, currentVM->dataBase + src, n );
}


qboolean VM_Compile( vm_t *vm, vmHeader_t *header )
{
	int pass;
	int codeoffsets[2]; // was 1024 but it's only used for OFF_CODE and OFF_IMMEDIATES

	const char *errMsg;

#define j_rel(x) (pass?_j_rel(x, ip-1):0xBAD)
#define OFFSET(i) (pass?(j_rel(codeoffsets[i] - compiledOfs)):(0xF000000F))
#define get_offset(i) (codeoffsets[i])
#define save_offset(i) (codeoffsets[i] = compiledOfs)
#define OFF_CODE 0
#define OFF_IMMEDIATES 1

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

	vm->compiled = qfalse;

	vm->codeBase.ptr = NULL;
	compiledOfs = 0;

	for ( pass = 0; pass < 2; pass++ ) {

	if ( pass ) {
		vm->codeBase.ptr = mmap( NULL, compiledOfs, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
		if ( vm->codeBase.ptr == MAP_FAILED ) {
			VM_FreeBuffers();
			Com_Printf( S_COLOR_YELLOW "%s(%s): mmap failed\n", __func__, vm->name );
			return qfalse;
		}
		vm->codeLength = compiledOfs;
		vm->codeSize = compiledOfs;
		compiledOfs = 0;
	}

	compiledOfs = 0;

	//int (*entry)(vm_t*, unsigned int*, int*);
	emit(PUSH((((1<<8)-1)<<4)|(1<<14))); // push R4-R11, LR
	emit(SUBi(SP, SP, 12)); // align stack!
	emit(LDRai(rCODEBASE, R0, offsetof(vm_t, codeBase)));
	emit(LDRai(rDATABASE, R0, offsetof(vm_t, dataBase)));
	emit(LDRai(rDATAMASK, R0, offsetof(vm_t, dataMask)));
	emit(LDRai(rPSTACK, R1, 0));
	emit(MOV(rOPSTACK, R2)); // TODO: reverse opstack to avoid writing to return address
	emit(MOV(rOPSTACKBASE, rOPSTACK));

	emit(BLi(OFFSET(OFF_CODE)));

	// save return value in r0
	emit(LDRTxi(R0, rOPSTACK, 4));  // r0 = *opstack; rOPSTACK -= 4

	emit(ADDi(SP, SP, 12)); // align stack!
	emit(POP((((1<<8)-1)<<4)|(1<<15))); // pop R4-R11, LR -> PC

	/* save some immediates here */
	emit(BKPT(0));
	emit(BKPT(0));
	save_offset(OFF_IMMEDIATES);
//	emit((unsigned)whatever);
	emit(BKPT(0));
	emit(BKPT(0));

	save_offset(OFF_CODE);
//	offsidx = OFF_IMMEDIATES+1;

	// translate all instructions
	ip = 0;
	while ( ip < header->instructionCount ) {

		uint32_t v;
		vm->instructionPointers[ ip ] = compiledOfs;

		ci = &inst[ ip ];
		ip++;
		v = ci->value;

		switch ( ci->op )
		{
			case OP_UNDEF:
				break;

			case OP_IGNORE:
				//NOTIMPL(op);
				break;

			case OP_BREAK:
				emit(BKPT(0));
				break;

			case OP_ENTER:
				emit(PUSH1(LR));
				emit(SUBi(SP, SP, 12)); // align stack
				if ( can_encode( v ) )
				{
					emit(SUBi(rPSTACK, rPSTACK, v)); // pstack -= arg
				}
				else
				{
					emit_MOVR0i(v);
					emit(SUB(rPSTACK, rPSTACK, R0)); // pstack -= arg
				}
				break;

			case OP_LEAVE:
				if ( can_encode( v ) )
				{
					emit(ADDi(rPSTACK, rPSTACK, v)); // pstack += arg
				}
				else
				{
					emit_MOVR0i(v);
					emit(ADD(rPSTACK, rPSTACK, R0)); // pstack += arg
				}
				emit(ADDi(SP, SP, 12));
				emit(0xe49df004); // pop pc
				break;

			case OP_CALL:
#if 0
				// save next instruction
				emit_MOVR0i(i_count);
				emit(STRa(R0, rDATABASE, rPSTACK));      // dataBase[pstack] = r0
#endif
				{
					static int bytes_to_skip = -1;
					static unsigned start_block = -1;
					// get instruction nr from stack
					emit(LDRTxi(R0, rOPSTACK, 4));  // r0 = *opstack; rOPSTACK -= 4
					emit(CMPi(R0, 0)); // check if syscall
					if (start_block == -1)
						start_block = compiledOfs;
					emit(cond(LT, Bi(j_rel(bytes_to_skip))));
					CHECK_JUMP;
					emit_MOVRxi_or_NOP(R1, (unsigned)vm->instructionPointers);
					emit(LDRa(R0, R1, rLSL(2, R0))); // r0 = ((int*)r1)[r0]
					emit(ADD(R0, rCODEBASE, R0)); // r0 = codeBase+r0
					emit(BLX(R0));
					emit(Bi(j_rel(vm->instructionPointers[ip] - compiledOfs)));
					if (bytes_to_skip == -1)
						bytes_to_skip = compiledOfs - start_block;
					emit(MOV(R1, rPSTACK));
					emit_MOVRxi(R12, (unsigned)asmcall);
					emit(BLX(R12));
					// store return value
					emit(STRaiw(R0, rOPSTACK, 4));      // opstack+=4; *opstack = r0
				}
				break;

			case OP_PUSH:
				emit(ADDi(rOPSTACK, rOPSTACK, 4));
				break;

			case OP_POP:
				emit(SUBi(rOPSTACK, rOPSTACK, 4));
				break;

			case OP_CONST:
				emit_MOVR0i(v);
				emit(STRaiw(R0, rOPSTACK, 4));      // opstack+=4; *opstack = r0
				break;

			case OP_LOCAL:
				if ( can_encode( v ) )
				{
					emit(ADDi(R0, rPSTACK, v));     // r0 = pstack+arg
				}
				else
				{
					emit_MOVR0i(v);
					emit(ADD(R0, rPSTACK, R0));     // r0 = pstack+arg
				}
				emit(STRaiw(R0, rOPSTACK, 4));      // opstack+=4; *opstack = r0
				break;

			case OP_JUMP:
				{
					emit(LDRTxi(R0, rOPSTACK, 4));  // r0 = *opstack; rOPSTACK -= 4
					CHECK_JUMP;
					emit_MOVRxi(R1, (unsigned)vm->instructionPointers);
					emit(LDRa(R0, R1, rLSL(2, R0))); // r0 = ((int*)r1)[r0]
					emit(ADD(R0, rCODEBASE, R0)); // r0 = codeBase+r0
					emit(BLX(R0));
				}
				break;

			case OP_EQ:
				IJ(EQ);
				break;

			case OP_NE:
				IJ(NE);
				break;

			case OP_LTI:
				IJ(LT);
				break;

			case OP_LEI:
				IJ(LE);
				break;

			case OP_GTI:
				IJ(GT);
				break;

			case OP_GEI:
				IJ(GE);
				break;

			case OP_LTU:
				IJ(LO);
				break;

			case OP_LEU:
				IJ(LS);
				break;

			case OP_GTU:
				IJ(HI);
				break;

			case OP_GEU:
				IJ(HS);
				break;

			case OP_EQF:
				FJ(EQ);
				break;

			case OP_NEF:
				FJ(NE);
				break;

			case OP_LTF:
				FJ(LT);
				break;

			case OP_LEF:
				FJ(LE);
				break;

			case OP_GTF:
				FJ(GT);
				break;

			case OP_GEF:
				FJ(GE);
				break;

			case OP_LOAD1:
				emit(LDRai(R0, rOPSTACK, 0));   // r0 = *opstack
				emit(AND(R0, rDATAMASK, R0));    // r0 = r0 & rDATAMASK
				emit(LDRBa(R0, rDATABASE, R0));  // r0 = (unsigned char)dataBase[r0]
				emit(STRai(R0, rOPSTACK, 0));   // *opstack = r0
				break;

			case OP_LOAD2:
				emit(LDRai(R0, rOPSTACK, 0));   // r0 = *opstack
				emit(AND(R0, rDATAMASK, R0));    // r0 = r0 & rDATAMASK
				emit(LDRHa(R0, rDATABASE, R0));  // r0 = (unsigned short)dataBase[r0]
				emit(STRai(R0, rOPSTACK, 0));   // *opstack = r0
				break;

			case OP_LOAD4:
				emit(LDRai(R0, rOPSTACK, 0));   // r0 = *opstack
				emit(AND(R0, rDATAMASK, R0));    // r0 = r0 & rDATAMASK
				emit(LDRa(R0, rDATABASE, R0));  // r0 = dataBase[r0]
				emit(STRai(R0, rOPSTACK, 0));   // *opstack = r0
				break;

			case OP_STORE1:
				emit(LDRTxi(R0, rOPSTACK, 4));  // r0 = *opstack; rOPSTACK -= 4
				emit(LDRTxi(R1, rOPSTACK, 4));  // r1 = *opstack; rOPSTACK -= 4
				emit(AND(R1, rDATAMASK, R1));    // r1 = r1 & rDATAMASK
				emit(STRBa(R0, rDATABASE, R1)); // database[r1] = r0
				break;

			case OP_STORE2:
				emit(LDRTxi(R0, rOPSTACK, 4));  // r0 = *opstack; rOPSTACK -= 4
				emit(LDRTxi(R1, rOPSTACK, 4));  // r1 = *opstack; rOPSTACK -= 4
				emit(AND(R1, rDATAMASK, R1));    // r1 = r1 & rDATAMASK
				emit(STRHa(R0, rDATABASE, R1)); // database[r1] = r0
				break;

			case OP_STORE4:
				// optimize: use load multiple
				// value
				emit(LDRTxi(R0, rOPSTACK, 4));  // r0 = *opstack; rOPSTACK -= 4
				// pointer
				emit(LDRTxi(R1, rOPSTACK, 4));  // r1 = *opstack; rOPSTACK -= 4
				emit(AND(R1, rDATAMASK, R1));    // r1 = r1 & rDATAMASK
				// store value at pointer
				emit(STRa(R0, rDATABASE, R1)); // database[r1] = r0
				break;

			case OP_ARG:
				emit(LDRTxi(R0, rOPSTACK, 4));      // r0 = *opstack; rOPSTACK -= 4
				emit(ADDi(R1, rPSTACK, v));  // r1 = programStack+arg
				emit(AND(R1, rDATAMASK, R1));       // r1 = r1 & rDATAMASK
				emit(STRa(R0, rDATABASE, R1));      // dataBase[r1] = r0
				break;

			case OP_BLOCK_COPY:
				emit(LDRTxi(R1, rOPSTACK, 4));  // r0 = *opstack; rOPSTACK -= 4
				emit(LDRTxi(R0, rOPSTACK, 4));
				emit_MOVRxi(R2, v);
				emit_MOVRxi(R12, (unsigned)VM_BlockCopy);
				emit(BLX(R12));
				break;

			case OP_SEX8:
				emit(LDRSBai(R0, rOPSTACK, 0));      // sign extend *opstack
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_SEX16:
				emit(LDRSHai(R0, rOPSTACK, 0));      // sign extend *opstack
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_NEGI:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(RSBi(R0, R0, 0));         // r0 = -r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_ADD:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(ADD(R0, R1, R0));         // r0 = r1 + r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_SUB:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(SUB(R0, R1, R0));         // r0 = r1 - r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_DIVI:
			case OP_DIVU:
				emit(LDRai(R1, rOPSTACK, 0));  // r1 = *opstack
				emit(LDRxiw(R0, rOPSTACK, 4)); // opstack-=4; r0 = *opstack
				if ( ci->op == OP_DIVI )
					emit_MOVRxi(R12, (unsigned)__aeabi_idiv);
				else
					emit_MOVRxi(R12, (unsigned)__aeabi_uidiv);
				emit(BLX(R12));
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_MODI:
			case OP_MODU:
				emit(LDRai(R1, rOPSTACK, 0));  // r1 = *opstack
				emit(LDRxiw(R0, rOPSTACK, 4)); // opstack-=4; r0 = *opstack
				if ( ci->op == OP_MODI )
					emit_MOVRxi(R12, (unsigned)__aeabi_idivmod);
				else
					emit_MOVRxi(R12, (unsigned)__aeabi_uidivmod);
				emit(BLX(R12));
				emit(STRai(R1, rOPSTACK, 0));  // *opstack = r1
				break;

			case OP_MULI:
			case OP_MULU:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(MUL(R0, R1, R0));         // r0 = r1 * r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_BAND:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(AND(R0, R1, R0));         // r0 = r1 & r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_BOR:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(ORR(R0, R1, R0));         // r0 = r1 | r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_BXOR:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(EOR(R0, R1, R0));         // r0 = r1 ^ r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_BCOM:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(MVN(R0, R0));             // r0 = ~r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_LSH:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(LSL(R0, R1, R0));         // r0 = r1 << r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_RSHI:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(ASR(R0, R1, R0));         // r0 = r1 >> r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_RSHU:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(LDRxiw(R1, rOPSTACK, 4)); // opstack-=4; r1 = *opstack
				emit(LSR(R0, R1, R0));         // r0 = (unsigned)r1 >> r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;

			case OP_NEGF:
				emit(VLDRa(S14, rOPSTACK, 0)); // s14 = *((float*)opstack)
				emit(VNEG_F32(S14, S14));      // s15 = -s14
				emit(VSTRa(S14, rOPSTACK, 0)); // *((float*)opstack) = s15
				break;

			case OP_ADDF:
				emit(VLDRa(S14, rOPSTACK, 0));   // s14 = *((float*)opstack)
				// vldr can't modify rOPSTACK so
				// we'd either need to change it
				// with sub or use regular ldr+vmov
				emit(LDRxiw(R0, rOPSTACK, 4));   // opstack-=4; r1 = *opstack
				emit(VMOVass(S15,R0));           // s15 = r0
				emit(VADD_F32(S14, S15, S14));   // s14 = s14 + s15
				emit(VSTRa(S14, rOPSTACK, 0));   // *((float*)opstack) = s15
				break;

			case OP_SUBF:
				emit(VLDRa(S14, rOPSTACK, 0));   // s14 = *((float*)opstack)
				// see OP_ADDF
				emit(LDRxiw(R0, rOPSTACK, 4));   // opstack-=4; r1 = *opstack
				emit(VMOVass(S15,R0));           // s15 = r0
				emit(VSUB_F32(S14, S15, S14));   // s14 = s14 - s15
				emit(VSTRa(S14, rOPSTACK, 0));   // *((float*)opstack) = s15
				break;

			case OP_DIVF:
				emit(VLDRa(S14, rOPSTACK, 0));   // s14 = *((float*)opstack)
				// see OP_ADDF
				emit(LDRxiw(R0, rOPSTACK, 4));   // opstack-=4; r1 = *opstack
				emit(VMOVass(S15,R0));           // s15 = r0
				emit(VDIV_F32(S14, S15, S14));   // s14 = s14 / s15
				emit(VSTRa(S14, rOPSTACK, 0));   // *((float*)opstack) = s15
				break;

			case OP_MULF:
				emit(VLDRa(S14, rOPSTACK, 0));   // s14 = *((float*)opstack)
				// see OP_ADDF
				emit(LDRxiw(R0, rOPSTACK, 4));   // opstack-=4; r1 = *opstack
				emit(VMOVass(S15,R0));           // s15 = r0
				emit(VMUL_F32(S14, S15, S14));   // s14 = s14 * s15
				emit(VSTRa(S14, rOPSTACK, 0));   // *((float*)opstack) = s15
				break;

			case OP_CVIF:
				emit(LDRai(R0, rOPSTACK, 0));  // r0 = *opstack
				emit(VMOVass(S14,R0));         // s14 = r0
				emit(VCVT_F32_S32(S14, S14));  // s15 = (float)s14
				emit(VSTRa(S14, rOPSTACK, 0)); // *((float*)opstack) = s15
				break;

			case OP_CVFI:
				emit(VLDRa(S14, rOPSTACK, 0)); // s14 = *((float*)opstack)
				emit(VCVT_S32_F32(S14, S14));  // s15 = (int)s14
				emit(VMOVssa(R0,S14));         // s14 = r0
				emit(STRai(R0, rOPSTACK, 0));  // *opstack = r0
				break;
		} // switch op
	} // ip

		// never reached
		emit(BKPT(0));
	} // pass

	VM_FreeBuffers();

	if ( mprotect( vm->codeBase.ptr, vm->codeLength, PROT_READ | PROT_EXEC ) ) {
		VM_Destroy_Compiled( vm );
		Com_Printf( S_COLOR_YELLOW "%s(%s): mprotect failed\n", __func__, vm->name );
		return qfalse;
	}

	// clear icache, http://blogs.arm.com/software-enablement/141-caches-and-self-modifying-code/ 
	__clear_cache( vm->codeBase.ptr, vm->codeBase.ptr + vm->codeLength );

	vm->destroy = VM_Destroy_Compiled;
	vm->compiled = qtrue;

	return qtrue;
}


int VM_CallCompiled( vm_t *vm, int nargs, int *args )
{
	int		opStack[MAX_OPSTACK_SIZE];
	unsigned int stackOnEntry;
	int		*image;
	int		*oldOpTop;
	int		i;

	currentVM = vm;

	// we might be called recursively, so this might not be the very top
	stackOnEntry = vm->programStack;
	oldOpTop = vm->opStackTop;

	vm->programStack -= (MAX_VMMAIN_CALL_ARGS+2)*4;

	// set up the stack frame
	image = (int*)( vm->dataBase + vm->programStack );
	for ( i = 0; i < nargs; i++ ) {
		image[ i + 2 ] = args[ i ];
	}

	image[1] =  0; // return stack
	image[0] = -1; // will terminate loop on return, only for interpreter modules

	opStack[1] = 0;

	vm->opStack = opStack; // unused atm
	vm->opStackTop = opStack + ARRAY_LEN( opStack ) - 1; // unused atm

	/* call generated code */
	{
		//int (*entry)(void *, int, void *, int);
		int (*entry)( vm_t*, unsigned int*, int* );

		entry = (void *)( vm->codeBase.ptr );
		//__asm__ volatile("bkpt");
		vm->opStack[0] = entry( vm, &vm->programStack, opStack );
	}

	//vm->codeBase.func(); // go into generated code

	//if ( vm->opStack != &opStack[1] ) { // not works atm
	//	Com_Error( ERR_DROP, "opStack corrupted in compiled code" );
	//}

#ifdef DEBUG_VM
	if ( vm->programStack != stackOnEntry - CALL_PSTACK ) {
		Com_Error( ERR_DROP, "programStack corrupted in compiled code" );
	}
#endif

	vm->programStack = stackOnEntry;
	vm->opStackTop = oldOpTop;

	return vm->opStack[0];
}
