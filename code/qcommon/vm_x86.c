/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2011-2021 Quake3e project

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

// load time compiler and execution environment for x86_64
// with dynamic register allocation and various optimizations

#include "vm_local.h"
#include "../ui/ui_public.h"
#include "../cgame/cg_public.h"
#include "../game/g_public.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#endif

#ifndef _WIN32
#include <sys/mman.h> // for PROT_ stuff
#endif

/* need this on NX enabled systems (i386 with PAE kernel or noexec32=on x86_64) */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
#define VM_X86_MMAP
#endif

#define DEBUG_VM

//#define DEBUG_INT

//#define DUMP_CODE

//#define VM_LOG_SYSCALLS
#define JUMP_OPTIMIZE 1

#if JUMP_OPTIMIZE
#define NUM_PASSES       3
#define PASS_INIT        0
#define PASS_COMPRESS    1
#define PASS_EXPAND_ONLY 2
#define NUM_COMPRESSIONS 2
#define FJUMP_THRESHOLD 48
#else
#define NUM_PASSES       1
#endif

#define DYN_ALLOC_RX
#define DYN_ALLOC_SX

#define CONST_CACHE_RX
#define CONST_CACHE_SX

#define REGS_OPTIMIZE
#define ADDR_OPTIMIZE
#define LOAD_OPTIMIZE
#define FPU_OPTIMIZE
#define CONST_OPTIMIZE
//#define RET_OPTIMIZE   // increases code size
//#define MACRO_OPTIMIZE // slows down a bit?

// allow sharing both variables and constants in registers
#define REG_TYPE_MASK
// number of variables/memory mappings per register
#define REG_MAP_COUNT 4

#define R_PSTACK		R_ESI
#define R_OPSTACK		R_EDI
#define R_DATABASE		R_EBX
#define R_PROCBASE		R_EBP
#if idx64
#define R_DATAMASK		R_R11
#define R_INSPOINTERS	R_R12
#define R_SYSCALL		R_R13
#define R_STACKBOTTOM	R_R14
#define R_OPSTACKTOP	R_R15
#endif

#define FUNC_ALIGN		4

/*
  -------------
  eax	scratch
  ebx*	dataBase
  ecx	scratch (required for shifts)
  edx	scratch (required for divisions)
  esi*	programStack
  edi*	opStack
  ebp*  procStack ( dataBase + programStack )
  -------------
  rax   scratch
  rbx*  dataBase
  rcx   scratch (required for shifts)
  rdx   scratch (required for divisions)
  rsi*  programStack
  rdi*  opStack
  rbp*  procStack ( dataBase + programStack )
  rsp*
  r8    scratch
  r9    scratch
  r10   scratch
  r11   scratch - dataMask
  r12*  instructionPointers
  r13*  systemCall
  r14*  stackBottom
  r15*  opStackTop
  xmm0  scratch
  xmm1  scratch
  xmm2  scratch
  xmm3  scratch
  xmm4  scratch
  xmm5  scratch

  Windows  ABI: you are required to preserve the XMM6-XMM15 registers
  System V ABI: you don't have to preserve any of the XMM registers, RSI/RDI are callee-saved

  Example how data segment will look like during vmMain execution:
  | .... |
  |------| vm->programStack -=36 (8+12+16) // set by vmMain
  | ???? | +0 - unused, reserved for interpreter
  | ???? | +4 - unused, reserved for interpreter
  |-------
  | arg0 | +8  \
  | arg4 | +12  | - passed arguments, accessible from subroutines
  | arg8 | +16 /
  |------|
  | loc0 | +20 \
  | loc4 | +24  \ - locals, accessible only from local scope
  | loc8 | +28  /
  | lc12 | +32 /
  |------| vm->programStack -= 24 ( 8 + MAX_VMMAIN_CALL_ARGS*4 ) // set by VM_CallCompiled()
  | ???? | +0 - unused, reserved for interpreter
  | ???? | +4 - unused, reserved for interpreter
  | arg0 | +8  \
  | arg1 | +12  \ - passed arguments, accessible from vmMain
  | arg2 | +16  /
  | arg3 | +20 /
  |------| vm->programStack = vm->dataMask + 1 // set by VM_Create()

  jump/call opStack rules:

  1) opStack must be 8 before conditional jump
  2) opStack must be 4 before unconditional jump
  3) opStack must be >=4 before OP_CALL
  4) opStack must remain the same after OP_CALL
  5) you may not jump in/call locations with opStack != 0

*/

typedef enum
{
	FUNC_ENTR = 0,
	FUNC_CALL,
	FUNC_SYSC,
	FUNC_BCPY,
	FUNC_PSOF,
	FUNC_OSOF,
	FUNC_BADJ,
	FUNC_ERRJ,
	FUNC_DATR,
	FUNC_DATW,
	FUNC_LAST
} func_t;

// macro opcode sequences
#ifdef MACRO_OPTIMIZE
typedef enum {
	MOP_UNDEF = OP_MAX,
	MOP_ADD,
	MOP_SUB,
	MOP_BAND,
	MOP_BOR,
	MOP_BXOR
} macro_op_t;
#endif

static	byte     *code;
static	int      compiledOfs;
static	int      *instructionOffsets;
static	intptr_t *instructionPointers;

static  instruction_t *inst = NULL;

static	int	ip, pass;
#if JUMP_OPTIMIZE
static	int jumpSizeChanged;
#endif

static	int	funcOffset[ FUNC_LAST ];


static void *VM_Alloc_Compiled( vm_t *vm, int codeLength, int tableLength );
static void VM_Destroy_Compiled( vm_t *vm );
static void VM_FreeBuffers( void );

static void Emit1( int v );
static void Emit2( int16_t v );
static void Emit4( int32_t v );
#if idx64
static void Emit8( int64_t v );
#endif

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

#define SWAP_INT( X, Y ) do { int T = X; X = Y; Y = T; } while ( 0 )

// x86 instruction encoding

typedef enum {
	R_EAX = 0x00,
	R_ECX = 0x01,
	R_EDX = 0x02,
	R_EBX = 0x03,
	R_ESP = 0x04,
	R_EBP = 0x05,
	R_ESI = 0x06,
	R_EDI = 0x07,
#if idx64
	R_R8 =  0x08,
	R_R9 =  0x09,
	R_R10 = 0x0A,
	R_R11 = 0x0B,
	R_R12 = 0x0C,
	R_R13 = 0x0D,
	R_R14 = 0x0E,
	R_R15 = 0x0F,
	R_MASK = 0x0F,
	R_REX = 0x10 // mask to force 64-bit operation
#else
	R_MASK = 0x07,
	R_REX = 0x00
#endif
} intreg_t;

#define RNN_SHIFT 3
#define REX_SHIFT 4

typedef enum {
	R_XMM0 = 0x00,
	R_XMM1 = 0x01,
	R_XMM2 = 0x02,
	R_XMM3 = 0x03,
	R_XMM4 = 0x04,
	R_XMM5 = 0x05
} xmmreg_t;

typedef union {
	struct {
		unsigned b : 1; // 1-bit extension of MODRM.r_m or SIB.base
		unsigned x : 1; // 1-bit extension of SIB.index
		unsigned r : 1; // 1-bit extension of MODRM.r_x
		unsigned w : 1; // 64-bit operand size or default (0)
		unsigned f : 4; // always 0x4 (0b0100)
	} s;
	uint8_t v;
} rex_t;

typedef enum {
	MOD_NO_DISP = 0,			// 0-byte displacement
	MOD_DISP4_ONLY_RM_5 = 0,	// displacement-only mode with r_m = 5 (101)
	MOD_SIB_NO_DISP_RM_4 = 0,	// SIB with no displacement with r_m = 4 (100)
	MOD_DISP1 = 1,				// 1-byte signed displacement
	MOD_DISP4 = 2,				// 4-byte signed displacement
	MOD_REG_TO_REG = 3			// register addressing mode
} mod_t;

typedef union {
	struct {
		unsigned r_m : 3; // direct or indirect register operand with opt.displacement, REX.b can extend this by 1 bit
		unsigned r_x : 3; // register or opcode extension, REX.r can extend this by 1 bit
		unsigned mod : 2; // see mod_t
	} s;
	uint8_t v;
} modrm_t;

typedef union {
	struct {
		unsigned base : 3;  // base register to use, REX.b can extend by 1 bit
		unsigned index : 3; // index register to use, REX.x can extend by 1 bit
		unsigned scale : 2; // 1 2 4 8
	} s;
	uint8_t v;
} sib_t;

#if idx64
static void emit_rex3( const uint32_t base, const uint32_t reg, const uint32_t idx )
{
	rex_t rex;
	rex.v = 0x40;

	rex.s.b = ( base >> RNN_SHIFT ) & 1;
	rex.s.r = ( reg >> RNN_SHIFT ) & 1;
	rex.s.x = ( idx >> RNN_SHIFT ) & 1;
	rex.s.w = ( ( base | reg | idx ) >> REX_SHIFT ) & 1;

	if ( rex.v != 0x40 )
		Emit1( rex.v );
}

static void emit_rex2( const uint32_t base, const uint32_t reg )
{
	rex_t rex;
	rex.v = 0x40;

	rex.s.b = ( base >> RNN_SHIFT ) & 1;
	rex.s.r = ( reg >> RNN_SHIFT ) & 1;
	rex.s.w = ( ( base | reg ) >> REX_SHIFT ) & 1;

	if ( rex.v != 0x40 )
		Emit1( rex.v );
}

static void emit_rex1( const uint32_t base )
{
	rex_t rex;
	rex.v = 0x40;

	rex.s.b = ( base >> RNN_SHIFT ) & 1;
	rex.s.w = ( base >> REX_SHIFT ) & 1;

	if ( rex.v != 0x40 )
		Emit1( rex.v );
}
#endif

 // reg <-> [offset]
static void emit_modrm_offset( uint32_t reg, int32_t offset )
{
	modrm_t modrm;

	modrm.s.mod = MOD_DISP4_ONLY_RM_5; // displacement-only mode with ( r_m == 0x5 )	
	modrm.s.r_m = 5; // 101
	modrm.s.r_x = reg;

	Emit1( modrm.v );
	Emit4( offset );
}

 // reg <-> [base + offset]
static void emit_modrm_base_offset( uint32_t reg, uint32_t base, int32_t offset )
{
	modrm_t modrm;

	modrm.s.r_m = base;
	modrm.s.r_x = reg;

	if ( offset >= -127 && offset < 128 ) {
		if ( offset != 0 || modrm.s.r_m == 0x5 /* 101 */ ) {
			modrm.s.mod = MOD_DISP1; // 1-byte displacement
			Emit1( modrm.v );
			if ( modrm.s.r_m == 0x4 /* 100 */ ) {
				Emit1( 0x24 ); // SIB: 00:100:100
			}
			Emit1( offset );
		} else {
			modrm.s.mod = MOD_NO_DISP; // 0-byte displacement
			Emit1( modrm.v );
			if ( modrm.s.r_m == 0x4 /* 100 */ ) {
				Emit1( 0x24 ); // SIB: 00:100:100
			}
		}
	} else {
		modrm.s.mod = MOD_DISP4; // 4-byte displacement	
		Emit1( modrm.v );
		if ( modrm.s.r_m == 0x4 /* 100 */ ) {
			Emit1( 0x24 ); // SIB: 00:100:100
		}
		Emit4( offset );
	}
}

// reg <-> [base + index*scale + disp]
static void emit_modrm_base_index( uint32_t reg, uint32_t base, uint32_t index, int scale, int32_t disp )
{
	modrm_t modrm;
	sib_t sib;

	sib.s.base = base;
	sib.s.index = index;

	switch ( scale ) {
		case 8: sib.s.scale = 3; break;
		case 4: sib.s.scale = 2; break;
		case 2: sib.s.scale = 1; break;
		default: sib.s.scale = 0; break;
	}

	modrm.s.r_x = reg;
	modrm.s.r_m = 4; // 100
	
	if ( disp == 0 ) {
		if ( sib.s.base == 5 /* 101 */ ) {
			modrm.s.mod = MOD_DISP1; // dummy 1-byte displacement
			Emit1( modrm.v );
			Emit1( sib.v );
			Emit1( 0x0 ); // displacement
		} else {
			modrm.s.mod = MOD_SIB_NO_DISP_RM_4; // SIB with no displacement ( r_m == 0x4 )
			Emit1( modrm.v );
			Emit1( sib.v );
		}
	} else {
		if ( disp >= -128 && disp <= 127 ) {
			modrm.s.mod = MOD_DISP1;
			Emit1( modrm.v );
			Emit1( sib.v );
			Emit1( disp );
		} else {
			modrm.s.mod = MOD_DISP4;
			Emit1( modrm.v );
			Emit1( sib.v );
			Emit4( disp );
		}
	}
}


// reg <-> reg
static void emit_modrm_reg( uint32_t base, uint32_t regx )
{
	modrm_t modrm;

	modrm.s.mod = MOD_REG_TO_REG;
	modrm.s.r_m = base;
	modrm.s.r_x = regx;

	Emit1( modrm.v );
}

static void emit_op_reg( int prefix, int opcode, uint32_t base, uint32_t reg )
{
#if idx64
	emit_rex2( base, reg );
#endif
	if ( prefix != 0 )
		Emit1( prefix );

	Emit1( opcode );

	emit_modrm_reg( base, reg );
}

// offset is RIP-related in 64-bit mode
static void emit_op_reg_offset( int prefix, int opcode, uint32_t reg, int32_t offset )
{
#if idx64
	emit_rex2( 0x0, reg );
#endif
	if ( prefix != 0 )
		Emit1( prefix );

	Emit1( opcode );

	emit_modrm_offset( reg, offset );
}

static void emit_op_reg_base_offset( int prefix, int opcode, uint32_t reg, uint32_t base, int32_t offset )
{
#if idx64
	emit_rex2( base, reg );
#endif
	if ( prefix != 0 )
		Emit1( prefix );

	Emit1( opcode );

	emit_modrm_base_offset( reg, base, offset );
}

static void emit_op2_reg_base_offset( int prefix, int opcode, int opcode2, uint32_t reg, uint32_t base, int32_t offset )
{
#if idx64
	emit_rex2( base, reg );
#endif
	if ( prefix != 0 )
		Emit1( prefix );

	Emit1( opcode );

	Emit1( opcode2 );

	emit_modrm_base_offset( reg, base, offset );
}


static void emit_op_reg_base_index( int prefix, int opcode, uint32_t reg, uint32_t base, uint32_t index, int scale, int32_t disp )
{
	if ( ( index & R_MASK ) == 4 ) {
		if ( scale == 1 && ( base & R_MASK ) != 4 ) {
			SWAP_INT( index, base ); // swap index with base
		} else {
#ifndef DEBUG_INT
			DROP( "incorrect index register" );
#else
			return; // R_ESP cannot be used as index register
#endif
		}
	} else if ( disp != 0 && ( base & 7 ) == 5 && scale == 1 && ( index & R_MASK ) != 4 ) {
		SWAP_INT( index, base ); // avoid using dummy displacement with R_EBP
	}
#if idx64
	emit_rex3( base, reg, index );
#endif
	if ( prefix != 0 )
		Emit1( prefix );

	Emit1( opcode );

	emit_modrm_base_index( reg, base, index, scale, disp );
}


static void emit_op_reg_index_offset( int opcode, uint32_t reg, uint32_t index, int scale, int32_t offset )
{
	modrm_t modrm;
	sib_t sib;

	if ( ( index & R_MASK ) == 4 )
		return;

#if idx64
	emit_rex3( 0x0, reg, index );
#endif

	// modrm = 00:<reg>:100
	modrm.s.mod = MOD_SIB_NO_DISP_RM_4;
	modrm.s.r_x = reg;
	modrm.s.r_m = 4;

	// sib = <scale>:<index>:101
	sib.s.base = 5; // 101 - (index*scale + disp4) mode
	sib.s.index = index;
	switch ( scale ) {
		case 8: sib.s.scale = 3; break;
		case 4: sib.s.scale = 2; break;
		case 2: sib.s.scale = 1; break;
		default: sib.s.scale = 0; break;
	}

	Emit1( opcode );
	Emit1( modrm.v );
	Emit1( sib.v );
	Emit4( offset );
}

static void emit_lea( uint32_t reg, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0x8D, reg, base, offset );
}

static void emit_lea_base_index( uint32_t reg, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0, 0x8D, reg, base, index, 1, 0 );
}

#if 0
static void emit_lea_index_scale( uint32_t reg, uint32_t index, int scale, int32_t offset )
{
	emit_op_reg_index_offset( 0x8D, reg, index, scale, offset );
}
#endif

static void emit_lea_base_index_offset( uint32_t reg, uint32_t base, uint32_t index, int32_t offset )
{
	emit_op_reg_base_index( 0, 0x8D, reg, base, index, 1, offset );
}

static void emit_mov_rx( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0, 0x89, base, reg );
}

static void emit_sex8( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0x0F, 0xBE, reg, base );
}

static void emit_zex8( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0x0F, 0xB6, reg, base );
}

static void emit_sex16( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0x0F, 0xBF, reg, base );
}

static void emit_zex16( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0x0F, 0xB7, reg, base );
}

static void emit_test_rx( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0, 0x85, base, reg );
}

static void emit_test_rx_imm32( uint32_t base, int32_t imm32 )
{
	if ( (base & R_MASK) == R_EAX ) {
#if idx64
		emit_rex1( base );
#endif
		Emit1( 0xA9 );
	} else {
		emit_op_reg( 0, 0xF7, base, 0x0 );
	}
	Emit4( imm32 );
}

static void emit_cmp_rx( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0, 0x39, base, reg );
}

/*static*/ void emit_cmp_rx_mem( uint32_t reg, int32_t offset )
{
	emit_op_reg_offset( 0, 0x3B, reg, offset );
}

static void emit_and_rx( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0, 0x21, base, reg );
}

static void emit_or_rx( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0, 0x09, base, reg );
}

static void emit_xor_rx( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0, 0x31, base, reg );
}

static void emit_mov_rx_imm32( uint32_t reg, int32_t imm32 )
{
#if idx64
	emit_rex1( reg & ~R_REX );
#endif
	Emit1( 0xB8 + ( reg & 7 ) );
	Emit4( imm32 );
}

#if idx64
static void emit_mov_rx_imm64( uint32_t reg, int64_t imm64 )
{
	emit_rex1( reg | R_REX );
	Emit1( 0xB8 + ( reg & 7 ) );
	Emit8( imm64 );
}
#endif

// wrapper function
static void mov_rx_imm32( uint32_t reg, int32_t imm32 )
{
	if ( imm32 == 0 ) {
		emit_xor_rx( reg & ~R_REX, reg & ~R_REX );
	} else {
		emit_mov_rx_imm32( reg, imm32 );
	}
}

#if idx64
// wrapper function
static void mov_rx_imm64( uint32_t reg, int64_t imm64 )
{
	if ( imm64 == 0 ) {
		emit_xor_rx( reg & ~R_REX, reg & ~R_REX );
	} else {
		if ( (uint64_t) imm64 > 0xFFFFFFFFULL ) {
			// move to 64-bit register
			if ( imm64 < 0 && imm64 >= -0x80000000LL ) {
				// sign-extend to 64 bit
				emit_rex1( reg | R_REX );
				Emit1( 0xC7 );
				Emit1( 0xC0 + ( reg & 7 ) ); // modrm: 11.000.reg
				Emit4( (int32_t)imm64 );
			} else {
				// worst case
				emit_mov_rx_imm64( reg, imm64 );
			}
		} else {
			// move to 32-bit register with implicit zero-extension to 64-bits
			emit_mov_rx_imm32( reg, (int32_t)imm64 );
		}
	}
}
#endif

static void mov_rx_ptr( uint32_t reg, const void *ptr )
{
#if idx64
	mov_rx_imm64( reg, (intptr_t) ptr );
#else
	mov_rx_imm32( reg, (intptr_t) ptr );
#endif
}

static void emit_not_rx( uint32_t reg )
{
	modrm_t modrm;
#if idx64
	emit_rex1( reg );
#endif
	Emit1( 0xF7 );

	modrm.v = 0xD0;
	modrm.s.r_m = reg;
	Emit1( modrm.v );
}

static void emit_neg_rx( uint32_t reg )
{
	modrm_t modrm;
#if idx64
	emit_rex1( reg );
#endif
	Emit1( 0xF7 );

	modrm.v = 0xD8;
	modrm.s.r_m = reg;
	Emit1( modrm.v );
}

static void emit_load1( uint32_t reg, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0x0F, 0xB6, reg, base, offset );
}

static void emit_load1_sex( uint32_t reg, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0x0F, 0xBE, reg, base, offset );
}

static void emit_load2( uint32_t reg, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0x0F, 0xB7, reg, base, offset );
}

static void emit_load2_sex( uint32_t reg, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0x0F, 0xBF, reg, base, offset );
}

static void emit_load4( uint32_t reg, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0x8B, reg, base, offset );
}

/*static*/ void emit_load_rx_offset( uint32_t reg, int32_t offset )
{
	emit_op_reg_offset( 0, 0x8B, reg, offset );
}

static void emit_load1_index( uint32_t reg, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0x0F, 0xB6, reg, base, index, 1, 0 );
}

static void emit_load1_sex_index( uint32_t reg, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0x0F, 0xBE, reg, base, index, 1, 0 );
}

static void emit_load2_index( uint32_t reg, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0x0F, 0xB7, reg, base, index, 1, 0 );
}

static void emit_load2_sex_index( uint32_t reg, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0x0F, 0xBF, reg, base, index, 1, 0 );
}

static void emit_load4_index( uint32_t reg, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0, 0x8B, reg, base, index, 1, 0 );
}

#if 0
static void emit_load4_index_offset( uint32_t reg, uint32_t base, uint32_t index, int scale, int32_t offset )
{
	emit_op_reg_base_index( 0, 0x8B, reg, base, index, scale, offset );
}
#endif

// R_REX prefix flag in [reg] may expand store to 8 bytes
static void emit_store_rx( uint32_t reg, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0x89, reg, base, offset );
}

/*static*/ void emit_store_rx_offset( uint32_t reg, int32_t offset )
{
	emit_op_reg_offset( 0, 0x89, reg, offset );
}

static void emit_store_imm32( int32_t imm32, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xC7, 0, base, offset );
	Emit4( imm32 );
}

static void emit_store_imm32_index( int32_t imm32, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0, 0xC7, 0, base, index, 1, 0 );
	Emit4( imm32 );
}

static void emit_store2_rx( uint32_t reg, uint32_t base, int32_t offset )
{
	Emit1( 0x66 );
	emit_op_reg_base_offset( 0, 0x89, reg, base, offset );
}

static void emit_store2_imm16( int imm16, uint32_t base, int32_t offset )
{
	Emit1( 0x66 );
	emit_op_reg_base_offset( 0, 0xC7, 0x0, base, offset );
	Emit2( imm16 );
}

static void emit_store2_imm16_index( int imm16, uint32_t base, uint32_t index )
{
	Emit1( 0x66 );
	emit_op_reg_base_index( 0, 0xC7, 0x0, base, index, 1, 0 );
	Emit2( imm16 );
}

static void emit_store1_rx( int reg, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0x88, reg, base, offset );
}

static void emit_store1_imm8( int imm8, uint32_t base, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xC6, 0x0, base, offset );
	Emit1( imm8 );
}

static void emit_store1_imm8_index( int imm8, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0, 0xC6, 0x0, base, index, 1, 0 );
	Emit1( imm8 );
}

static void emit_store4_index( uint32_t reg, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0, 0x89, reg, base, index, 1, 0 );
}

static void emit_store2_index( uint32_t reg, uint32_t base, uint32_t index )
{
	Emit1( 0x66 );
	emit_op_reg_base_index( 0, 0x89, reg, base, index, 1, 0 );
}

static void emit_store1_index( uint32_t reg, uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0, 0x88, reg, base, index, 1, 0 );
}

/*static*/ void emit_jump_index( uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0, 0xFF, 0x4, base, index, sizeof( void* ), 0 );
}

/*static*/ void emit_jump_index_offset( int32_t offset, uint32_t index )
{
	emit_op_reg_index_offset( 0xFF, 0x4, index, sizeof( void * ), offset );
}

void emit_call_index( uint32_t base, uint32_t index )
{
	emit_op_reg_base_index( 0, 0xFF, 0x2, base, index, sizeof( void* ), 0 );
}

/*static*/ void emit_call_index_offset( int32_t offset, uint32_t index )
{
	emit_op_reg_index_offset( 0xFF, 0x2, index, sizeof( void * ), offset );
}

/*static*/ void emit_call_indir( int32_t offset )
{
	Emit1( 0xFF );
	Emit1( 0x15 );
	Emit4( offset );
}

/*static*/ void emit_call_rx( uint32_t reg )
{
	emit_op_reg( 0, 0xFF, reg & ~R_REX, 0x2 );
}

static void emit_add_rx( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0, 0x01, base, reg );
}

/*static*/ void emit_pushad( void )
{
	Emit1( 0x60 );
}

/*static*/ void emit_popad( void )
{
	Emit1( 0x61 );
}

static void emit_push( uint32_t reg )
{
#if idx64
	emit_rex1( reg & ~R_REX );
#endif
	Emit1( 0x50 + ( reg & 7 ) );
}

static void emit_pop( uint32_t reg )
{
#if idx64
	emit_rex1( reg & ~R_REX );
#endif
	Emit1( 0x58 + ( reg & 7 ) );
}

static void emit_ret( void )
{
	Emit1( 0xC3 );
}

static void emit_nop( void )
{
	Emit1( 0x90 );
}

static void emit_brk( void )
{
	Emit1( 0xCC );
}

enum {
	X_ADD,
	X_OR,
	X_ADC,
	X_SBB,
	X_AND,
	X_SUB,
	X_XOR,
	X_CMP
};

static void emit_op_rx_imm32( int xop, uint32_t reg, int32_t imm32 )
{
	if ( imm32 < -128 || imm32 > 127 ) {
		if ( reg == R_EAX ) {
			switch ( xop ) {
				case X_ADD: Emit1( 0x05 ); break;
				case X_OR:  Emit1( 0x0D ); break;
				case X_ADC: Emit1( 0x15 ); break;
				case X_SBB: Emit1( 0x1D ); break;
				case X_AND: Emit1( 0x25 ); break;
				case X_SUB: Emit1( 0x2D ); break;
				case X_XOR: Emit1( 0x35 ); break;
				case X_CMP: Emit1( 0x3D ); break;
				default: DROP( "unknown xop %i", xop );
			}
		} else {
			emit_op_reg( 0, 0x81, reg, xop );
		}
		Emit4( imm32 );
	} else {
		emit_op_reg( 0, 0x83, reg, xop );
		Emit1( imm32 );
	}
}

#ifdef MACRO_OPTIMIZE
static void emit_op_mem_imm( int xop, int base, int32_t offset, int32_t imm32 )
{
	if ( imm32 < -128 || imm32 > 127 ) {
		emit_op_reg_base_offset( 0, 0x81, xop, base, offset );
		Emit4( imm32 );
	} else {
		emit_op_reg_base_offset( 0, 0x83, xop, base, offset );
		Emit1( imm32 );
	}
}
#endif

static void emit_mul_rx_imm( int reg, int32_t imm32 )
{
	if ( imm32 < -128 || imm32 > 127 ) {
		emit_op_reg( 0, 0x69, reg, reg );
		Emit4( imm32 );
	} else {
		emit_op_reg( 0, 0x6B, reg, reg );
		Emit1( imm32 );
	}
}

static void emit_shl_rx_imm( int reg, int8_t imm8 )
{
	emit_op_reg( 0, 0xC1, reg, 4 );
	Emit1( imm8 );
}

static void emit_shr_rx_imm( int reg, int8_t imm8 )
{
	emit_op_reg( 0, 0xC1, reg, 5 );
	Emit1( imm8 );
}

static void emit_sar_rx_imm( int reg, int8_t imm8 )
{
	emit_op_reg( 0, 0xC1, reg, 7 );
	Emit1( imm8 );
}

static void emit_sub_rx( int base, int reg )
{
	emit_op_reg( 0, 0x29, base, reg );
}

static void emit_mul_rx( int base, int reg )
{
	emit_op_reg( 0x0F, 0xAF, reg, base );
}

static void emit_cdq( void )
{
	Emit1( 0x99 );
}

static void emit_idiv_rx( uint32_t reg )
{
#if idx64
	emit_rex1( reg );
#endif
	Emit1( 0xF7 );
	Emit1( 0xF8 + ( reg & 7 ) );
}

static void emit_udiv_rx( uint32_t reg )
{
#if idx64
	emit_rex1( reg );
#endif
	Emit1( 0xF7 );
	Emit1( 0xF0 + ( reg & 7 ) );
}

static void emit_shl_rx( uint32_t reg )
{
#if idx64
	emit_rex1( reg );
#endif
	Emit1( 0xD3 );
	Emit1( 0xE0 + ( reg & 7 ) );
}

static void emit_shr_rx( uint32_t reg )
{
#if idx64
	emit_rex1( reg );
#endif
	Emit1( 0xD3 );
	Emit1( 0xE8 + ( reg & 7 ) );
}

static void emit_sar_rx( uint32_t reg )
{
#if idx64
	emit_rex1( reg );
#endif
	Emit1( 0xD3 );
	Emit1( 0xF8 + ( reg & 7 ) );
}

#if 0
static void emit_xchg_rx( uint32_t reg1, uint32_t reg2 )
{
	emit_op_reg( 0, 0x87, reg2, reg1 );
}
#endif

/* FPU functions */

static void emit_mov_sx( uint32_t dst, uint32_t src )
{
	emit_op_reg( 0x0F, 0x28, src, dst );
}

static void emit_mov_sx_rx( uint32_t xmmreg, uint32_t intreg )
{
	Emit1( 0x66 );
	emit_op_reg( 0x0F, 0x6E, intreg, xmmreg );
}

static void emit_mov_rx_sx( uint32_t intreg, uint32_t xmmreg )
{
	Emit1( 0x66 );
	emit_op_reg( 0x0F, 0x7E, intreg, xmmreg );
}

static void emit_xor_sx( uint32_t dst, uint32_t src )
{
	emit_op_reg( 0x0F, 0x57, src, dst );
}

static void emit_ucomiss( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0x0F, 0x2E, reg, base );
}

static void emit_comiss( uint32_t base, uint32_t reg )
{
	emit_op_reg( 0x0F, 0x2F, reg, base );
}

static void emit_load_sx( uint32_t reg, uint32_t base, int32_t offset )
{
	Emit1( 0xF3 );
	emit_op_reg_base_offset( 0x0F, 0x10, reg, base, offset );
}

static void emit_load_sx_index( uint32_t reg, uint32_t base, uint32_t index )
{
	Emit1( 0xF3 );
	emit_op_reg_base_index( 0x0F, 0x10, reg, base, index, 1, 0 );
}

#if 0
static void emit_load_sx_index_offset( uint32_t reg, uint32_t base, uint32_t index, int scale, int32_t offset )
{
	Emit1( 0xF3 );
	emit_op_reg_base_index( 0x0F, 0x10, reg, base, index, scale, offset );
}
#endif

static void emit_store_sx( uint32_t reg, uint32_t base, int32_t offset )
{
	Emit1( 0xF3 );
	emit_op_reg_base_offset( 0x0F, 0x11, reg, base, offset );
}

static void emit_store_sx_index( uint32_t reg, uint32_t base, uint32_t index )
{
	Emit1( 0xF3 );
	emit_op_reg_base_index( 0x0F, 0x11, reg, base, index, 1, 0 );
}

static void emit_add_sx( uint32_t dst, uint32_t src )
{
	emit_op_reg( 0x0F, 0x58, src, dst );
}

static void emit_sub_sx( uint32_t dst, uint32_t src )
{
	emit_op_reg( 0x0F, 0x5C, src, dst );
}

static void emit_mul_sx( uint32_t dst, uint32_t src )
{
	emit_op_reg( 0x0F, 0x59, src, dst );
}

static void emit_div_sx( uint32_t dst, uint32_t src )
{
	emit_op_reg( 0x0F, 0x5E, src, dst );
}

#if 0
static void emit_add_sx_mem( uint32_t reg, uint32_t base, int32_t offset )
{
	Emit1( 0xF3 );
	emit_op_reg_base_offset( 0x0F, 0x58, reg, base, offset );
}

static void emit_sub_sx_mem( uint32_t reg, uint32_t base, int32_t offset )
{
	Emit1( 0xF3 );
	emit_op_reg_base_offset( 0x0F, 0x5C, reg, base, offset );
}

static void emit_mul_sx_mem( uint32_t reg, uint32_t base, int32_t offset )
{
	Emit1( 0xF3 );
	emit_op_reg_base_offset( 0x0F, 0x59, reg, base, offset );
}

static void emit_div_sx_mem( uint32_t reg, uint32_t base, int32_t offset )
{
	Emit1( 0xF3 );
	emit_op_reg_base_offset( 0x0F, 0x5E, reg, base, offset );
}
#endif

static void emit_cvtsi2ss( uint32_t xmmreg, uint32_t intreg )
{
	Emit1( 0xF3 );
	emit_op_reg( 0x0F, 0x2A, intreg, xmmreg );
}

static void emit_cvttss2si( uint32_t intreg, uint32_t xmmreg )
{
	Emit1( 0xF3 );
	emit_op_reg( 0x0F, 0x2C, xmmreg, intreg );
}

static void emit_sqrt( uint32_t xmmreg, uint32_t base, int32_t offset )
{
	Emit1( 0xF3 );
	emit_op_reg_base_offset( 0x0F, 0x51, xmmreg, base, offset );
}

static void emit_floor( uint32_t xmmreg, uint32_t base, int32_t offset )
{
	Emit1( 0x66 );
	emit_op2_reg_base_offset( 0x0F, 0x3A, 0x0A, xmmreg, base, offset );
	Emit1( 0x01 ); // exceptions not masked
}

static void emit_ceil( uint32_t xmmreg, uint32_t base, int32_t offset )
{
	Emit1( 0x66 );
	emit_op2_reg_base_offset( 0x0F, 0x3A, 0x0A, xmmreg, base, offset );
	Emit1( 0x02 ); // exceptions not masked
}

// legacy x87 functions

static void emit_fld( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xD9, 0x0, reg, offset );
}

static void emit_fstp( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xD9, 0x3, reg, offset );
}

static void emit_fild( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xDB, 0x0, reg, offset );
}

static void emit_fistp( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xDB, 0x3, reg, offset );
}

static void emit_fadd( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xD8, 0x0, reg, offset );
}

static void emit_fsub( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xD8, 0x4, reg, offset );
}

static void emit_fmul( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xD8, 0x1, reg, offset );
}

static void emit_fdiv( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xD8, 0x6, reg, offset );
}

static void emit_fcomp( uint32_t reg, int32_t offset )
{
	emit_op_reg_base_offset( 0, 0xD8, 0x3, reg, offset );
}


// -------------- virtual opStack management ---------------

static uint32_t alloc_rx( uint32_t pref );
static uint32_t alloc_rx_const( uint32_t pref, uint32_t imm );
static uint32_t alloc_rx_local( uint32_t pref, uint32_t imm );
static uint32_t alloc_sx( uint32_t pref );

// ---------------- register allocation --------------------

// register allocation preferences

#define FORCED 0x20  // load function must return specified register
#define TEMP   0x40  // hint: temporary allocation, will not be stored on opStack
#define RCONST 0x80  // register value will be not modified
#define XMASK  0x100 // exclude masked registers
#define SHIFT4 0x200 // load bottom item

#define RMASK  0x0F

// array sizes for cached/meta registers
#if idx64
#define NUM_RX_REGS 11 // [EAX..R10]
#define NUM_SX_REGS 6 // [XMM0..XMM5]
#else
#define NUM_RX_REGS 3 // EAX, ECX, EDX
#define NUM_SX_REGS 6 // [XMM0..XMM5]
#endif

// general-purpose register list available for dynamic allocation
static const uint32_t rx_list_alloc[] = {
	R_EAX, R_EDX, R_ECX
#if idx64
	,R_R8, R_R9, R_R10
#endif
};

// FPU scalar register list available for dynamic allocation
static const uint32_t sx_list_alloc[] = {
	R_XMM0, R_XMM1, R_XMM2,
	R_XMM3, R_XMM4, R_XMM5
};

#ifdef CONST_CACHE_RX
static const uint32_t rx_list_cache[] = {
	R_EDX, R_ECX, R_EAX
};
#endif

#ifdef CONST_CACHE_SX
static const uint32_t sx_list_cache[] = {
	R_XMM0, R_XMM1, R_XMM2,
	R_XMM3, R_XMM4, R_XMM5
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
#if 1
	for ( i = 0; i < ARRAY_LEN( rx_list_alloc ); i++ ) {
		n = rx_list_alloc[i];
		if ( rx_regs[n].type_mask == RTYPE_UNUSED ) {
			return qtrue;
		}
	}
#endif
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


static void mov_sx_imm( uint32_t reg, uint32_t imm32 )
{
	if ( imm32 == 0 ) {
		emit_xor_sx( reg, reg );
	} else {
		uint32_t rx = alloc_rx_const( R_ECX | TEMP, imm32 ); // ecx = imm32
		emit_mov_sx_rx( reg, rx ); // xmmX = ecx
		unmask_rx( rx );
	}
}


static void set_local_address( uint32_t reg, const uint32_t addr )
{
	emit_lea( reg, R_PSTACK, addr ); // reg = programStack + addr
}


static void flush_item( opstack_t *it )
{
	switch ( it->type ) {

		case TYPE_RX:
			if ( it->offset >= 0 )
				emit_store_rx( it->value, R_OPSTACK, it->offset );	// opStack[ it->offset ] = eax
			break;

		case TYPE_SX:
			emit_store_sx( it->value, R_OPSTACK, it->offset );		// opStack[ it->offset ] = xmm0
			break;

		case TYPE_CONST:
			if ( it->value == 0 ) {
				uint32_t rx = alloc_rx_const( R_EAX, it->value );	// mov eax, const
				emit_store_rx( rx, R_OPSTACK, it->offset );		// opStack[ it->offset ] = eax
				unmask_rx( rx );
			} else {
				emit_store_imm32( it->value, R_OPSTACK, it->offset ); // opStack[ it->offset ] = const
			}
			break;

		case TYPE_LOCAL: {
			uint32_t rx = alloc_rx_local( R_EAX, it->value );	// lea eax, [pStack + address]
			emit_store_rx( rx, R_OPSTACK, it->offset );		// opStack[ it->offset ] = eax
			unmask_rx( rx );
			break;
		}

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
		addr->base = R_DATABASE;
		addr->size = 0;
		return qtrue;
	}
	if ( opstackv[ opstack ].type == TYPE_LOCAL ) {
		addr->addr = opstackv[opstack].value;
		addr->base = R_PROCBASE;
		addr->size = 0;
		return qtrue;
	}
#endif
	return qfalse;
}


static qboolean const_on_top( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
#ifdef ADDR_OPTIMIZE
	if ( opstackv[ opstack ].type == TYPE_CONST )
		return qtrue;
#endif
	return qfalse;
}


static int32_t top_value( void )
{
	opstack_t *it = &opstackv[ opstack ];
	return it->value;
}


static void discard_top( void )
{
	opstack_t *it = &opstackv[ opstack ];
	it->type = TYPE_RAW;
	it->safe_arg = 0;
}

#if 0
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


// returns qtrue if specified constant is found
static reg_t *find_rx_const( uint32_t imm, uint32_t mask )
{
#ifdef CONST_CACHE_RX
	uint32_t i, n;
	reg_t *r;

	for ( i = 0; i < ARRAY_LEN( rx_list_cache ); i++ ) {
		n = rx_list_cache[ i ];
		r = &rx_regs[ n ];
		if ( r->type_mask & RTYPE_CONST && ( r->cnst.value & mask ) == imm ) {
			return r;
		}
	}
#endif
	return NULL;
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
			mov_rx_imm32( idx, imm );
			mask_rx( idx );
			return idx;
		}
		// else go to usual allocation to handle register spilling
	}
#endif // DYN_ALLOC_RX
#endif // CONST_CACHE_RX

	rx = alloc_rx( pref );

	mov_rx_imm32( rx, imm );

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
			mov_sx_imm( idx, imm );
			mask_sx( idx );
			return idx;
		}
		// else go to usual allocation to handle register spilling
	}
#endif // DYN_ALLOC_SX
#endif // CONST_CACHE_SX

	sx = alloc_sx( pref );

	mov_sx_imm( sx, imm );

#ifdef CONST_CACHE_SX
	r = &sx_regs[sx];
	//memset( &r->vars, 0, sizeof( r->vars ) );
	r->type_mask = RTYPE_CONST;
	r->cnst.value = imm;
	r->refcnt = 1;
	r->ip = ip;
	//r->ext = Z_NONE;
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
	it->value = R_EAX;
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
			int rx = alloc_rx( R_ECX );
			emit_mov_rx( rx, reg );
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
	opstack_t *it;
	uint32_t opsv;
	uint32_t reg;

	if ( pref & SHIFT4 ) {
		opsv = opstack - 1;
	} else {
		opsv = opstack;
	}

#ifdef DEBUG_VM
	if ( opsv <= 0 )
		DROP( "bad opstack %i",  opsv * 4 );
#endif

	it = &opstackv[ opsv ];
	reg = pref & RMASK;

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
			emit_mov_rx( reg, it->value );

			it->type = TYPE_RAW;
			return reg;
		}
	} // it->type == TYPE_RX

	// scalar register on the stack
	if ( it->type == TYPE_SX ) {
		// move from scalar to general-purpose register
		reg = alloc_rx( pref );

		emit_mov_rx_sx( reg, it->value ); // intreg <- xmmreg

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

	emit_load4( reg, R_OPSTACK, opsv * sizeof( int32_t ) ); // reg32 = *opstack

	it->type = TYPE_RAW;
	return reg;
}


static void load_rx_opstack2( uint32_t *dst, uint32_t dst_pref, uint32_t *src, uint32_t src_pref )
{
#if 0
	*dst = *src = load_rx_opstack( src_pref & ~RCONST ); // source, target = *opstack
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
			int sx = alloc_sx( R_XMM2 );
			emit_mov_sx( sx, reg );
			unmask_sx( reg );
			return sx;
		}
	}

	wipe_sx_meta( reg );
	return reg;
}


static void flush_opstack_top( void )
{
#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
	flush_item( &opstackv[ opstack ] );
}


// we must unmask register manually after allocation/loading
static uint32_t load_sx_opstack( uint32_t pref )
{
	opstack_t *it;
	uint32_t reg;
	uint32_t opsv;

	if ( pref & SHIFT4 ) {
		opsv = opstack - 1;
	} else {
		opsv = opstack;
	}

#ifdef DEBUG_VM
	if ( opsv <= 0 )
		DROP( "bad opstack %i", opsv * 4 );
#endif

	it = &opstackv[ opsv ];
	reg = pref & RMASK;

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
			emit_mov_sx( reg, it->value );

			it->type = TYPE_RAW;
			return reg;
		}
	}

	// integer register on the stack
	if ( it->type == TYPE_RX ) {
		// move from general-purpose to scalar register
		// should never happen with FPU type promotion, except syscalls
		reg = alloc_sx( pref );

		emit_mov_sx_rx( reg, it->value );

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
		rx = alloc_rx_local( R_ECX | RCONST, it->value );

		emit_mov_sx_rx( reg, rx ); // move from integer to scalar

		unmask_rx( rx );

		it->type = TYPE_RAW;
		return reg;
	}

	// default raw type, explicit load from opStack
	reg = alloc_sx( pref );

	emit_load_sx( reg, R_OPSTACK, opsv * sizeof( int32_t ) ); // xmm_reg = *opstack

	it->type = TYPE_RAW;
	return reg;
}


static void ErrJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code outside VM" );
}


static void BadJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code at bad location inside VM" );
}


static void BadStack( void )
{
	Com_Error( ERR_DROP, "program tried to overflow program stack" );
}


static void BadOpStack( void )
{
	Com_Error( ERR_DROP, "program tried to overflow opcode stack" );
}


static void BadDataRead( void )
{
	Com_Error( ERR_DROP, "program tried to read out of data segment" );
}


static void BadDataWrite( void )
{
	Com_Error( ERR_DROP, "program tried to write out of data segment" );
}


static void( *const errJumpPtr )( void ) = ErrJump;
static void( *const badJumpPtr )( void ) = BadJump;
static void( *const badStackPtr )( void ) = BadStack;
static void( *const badOpStackPtr )( void ) = BadOpStack;
static void( *const badDataReadPtr )( void ) = BadDataRead;
static void( *const badDataWritePtr )( void ) = BadDataWrite;


static void VM_FreeBuffers( void )
{
	// should be freed in reversed allocation order
	Z_Free( instructionOffsets );
	Z_Free( inst );
}


static const ID_INLINE qboolean HasFCOM( void )
{
#if id386
	return ( CPU_Flags & CPU_FCOM );
#else
	return qtrue; // assume idx64
#endif
}


static const ID_INLINE qboolean HasSSEFP( void )
{
#if id386
	return ( CPU_Flags & CPU_SSE );
#else
	return qtrue; // assume idx64
#endif
}


static void Emit1( int v )
{
	if ( code )
	{
		code[ compiledOfs ] = v;
	}
	compiledOfs++;
}


static void Emit2( int16_t v )
{
	Emit1( v & 255 );
	Emit1( ( v >> 8 ) & 255 );
}


static void Emit4( int32_t v )
{
	Emit1( v & 255 );
	Emit1( ( v >> 8 ) & 255 );
	Emit1( ( v >> 16 ) & 255 );
	Emit1( ( v >> 24 ) & 255 );
}


void Emit8( int64_t v )
{
	Emit1( ( v >> 0 ) & 255 );
	Emit1( ( v >> 8 ) & 255 );
	Emit1( ( v >> 16 ) & 255 );
	Emit1( ( v >> 24 ) & 255 );
	Emit1( ( v >> 32 ) & 255 );
	Emit1( ( v >> 40 ) & 255 );
	Emit1( ( v >> 48 ) & 255 );
	Emit1( ( v >> 56 ) & 255 );
}


static int Hex( int c )
{
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'A' && c <= 'F' ) {
		return 10 + c - 'A';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return 10 + c - 'a';
	}

	VM_FreeBuffers();
	Com_Error( ERR_DROP, "Hex: bad char '%c'", c );

	return 0;
}


static void EmitString( const char *string )
{
	int		c1, c2;
	int		v;

	while ( 1 ) {
		c1 = string[0];
		c2 = string[1];

		v = ( Hex( c1 ) << 4 ) | Hex( c2 );
		Emit1( v );

		if ( !string[2] ) {
			break;
		}
		string += 3;
	}
}


static void EmitAlign( int align )
{
	int i, n;

	n = compiledOfs & ( align - 1 );

	for ( i = 0; i < n; i++ )
		emit_nop();
}


#if JUMP_OPTIMIZE
static const char *NearJumpStr( int op )
{
	switch ( op )
	{
		case OP_EQF:
		case OP_EQ:  return "74"; // je

		case OP_NEF:
		case OP_NE:  return "75"; // jne

		case OP_LTI: return "7C"; // jl
		case OP_LEI: return "7E"; // jle
		case OP_GTI: return "7F"; // jg
		case OP_GEI: return "7D"; // jge

		case OP_LTF:
		case OP_LTU: return "72"; // jb

		case OP_LEF:
		case OP_LEU: return "76"; // jbe

		case OP_GTF:
		case OP_GTU: return "77"; // ja

		case OP_GEF:
		case OP_GEU: return "73"; // jae

		case OP_JUMP: return "EB";   // jmp

		//default:
		//	Com_Error( ERR_DROP, "Bad opcode %i", op );
	};
	return NULL;
}
#endif


static const char *FarJumpStr( int op, int *n )
{
	switch ( op )
	{
		case OP_EQF:
		case OP_EQ:  *n = 2; return "0F 84"; // je

		case OP_NEF:
		case OP_NE:  *n = 2; return "0F 85"; // jne

		case OP_LTI: *n = 2; return "0F 8C"; // jl
		case OP_LEI: *n = 2; return "0F 8E"; // jle
		case OP_GTI: *n = 2; return "0F 8F"; // jg
		case OP_GEI: *n = 2; return "0F 8D"; // jge

		case OP_LTF:
		case OP_LTU: *n = 2; return "0F 82"; // jb

		case OP_LEF:
		case OP_LEU: *n = 2; return "0F 86"; // jbe

		case OP_GTF:
		case OP_GTU: *n = 2; return "0F 87"; // ja

		case OP_GEF:
		case OP_GEU: *n = 2; return "0F 83"; // jae

		case OP_JUMP: *n = 1; return "E9";   // jmp
	};
	return NULL;
}


static void EmitJump( instruction_t *i, int op, int addr )
{
	const char *str;
	int v, jump_size = 0;
	qboolean shouldNaNCheck = qfalse;

	v = instructionOffsets[addr] - compiledOfs;

	if ( HasFCOM() ) {
		// EQF, LTF and LEF use je/jb/jbe to conditional branch. je/jb/jbe branch if CF/ZF
		// is set. comiss/fucomip was used to perform the compare, so if any of the
		// operands are NaN, ZF, CF and PF will be set and je/jb/jbe would branch.
		// However, according to IEEE 754, when the operand is NaN for these comparisons,
		// the result must be false. So, we emit `jp` before je/jb/jbe to skip
		// the branch if the result is NaN.
		if ( op == OP_EQF || op == OP_LTF || op == OP_LEF ) {
			shouldNaNCheck = qtrue;
		}
	} else {
		// Similar to above, NaN needs to be accounted for. When HasFCOM() is false,
		// fcomp is used to perform the compare and EmitFloatJump is called. Which in turn,
		// preserves C2 when masking and calls EmitJump with OP_NE. When any of the operands
		// are NaN, C2 and C0/C3 (whichever was also masked) will be set. So like the previous
		// case, we can use PF to skip the branch if the result is NaN.
		if ( op == OP_EQF || op == OP_LTF || op == OP_LEF ) {
			shouldNaNCheck = qtrue;
		}
	}

	if ( shouldNaNCheck ) {
		v -= 2;			// 2 bytes needed to account for NaN
		Emit1( 0x7A );	// jp, target will be filled once we know if next inst is a near or far jump
	}

#if JUMP_OPTIMIZE
	if ( i->njump ) {
		// expansion, can happen
		if ( pass != PASS_INIT && ( v < -126 || v > 129 ) ) {
			str = FarJumpStr( op, &jump_size );
			if ( shouldNaNCheck ) {
				Emit1( jump_size + 4 ); // target for NaN branch
			}
			EmitString( str );
			Emit4( v - 4 - jump_size );
			i->njump = 0;
			jumpSizeChanged++;
			return;
		}
		if ( shouldNaNCheck ) {
			Emit1( 0x02 ); // target for NaN branch
		}
		EmitString( NearJumpStr( op ) );
		Emit1( v - 2 );
		return;
	}

	if ( pass == PASS_COMPRESS || ( pass == PASS_INIT && addr < ip ) ) {
		if ( v >= -126 && v <= 129 ) {
			if ( shouldNaNCheck ) {
				Emit1( 0x02 ); // target for NaN branch
			}
			EmitString( NearJumpStr( op ) );
			Emit1( v - 2 );
			if ( !i->njump )
				jumpSizeChanged++;
			i->njump = 1;
			return;
		}
	}
#endif

	str = FarJumpStr( op, &jump_size );
	if ( jump_size == 0 ) {
		Com_Error( ERR_DROP, "VM_CompileX86 error: %s\n", "bad jump size" );
		return;
	}
	if ( shouldNaNCheck ) {
		Emit1( jump_size + 4 ); // target for NaN branch
	}
	EmitString( str );
	Emit4( v - 4 - jump_size );
}


static void EmitCallAddr( vm_t *vm, int addr )
{
	const int v = instructionOffsets[ addr ] - compiledOfs;
	EmitString( "E8" );
	Emit4( v - 5 );
}


static void EmitCallOffset( func_t Func )
{
	const int v = funcOffset[ Func ] - compiledOfs;
	EmitString( "E8" );		// call +funcOffset[ Func ]
	Emit4( v - 5 );
}


static void emit_CheckReg( vm_t *vm, uint32_t reg, func_t func )
{
	if ( vm->forceDataMask || !( vm_rtChecks->integer & VM_RTCHECK_DATA ) )
	{
#if idx64
		emit_and_rx( reg, R_DATAMASK );					// reg = reg & dataMask
#else
		emit_op_rx_imm32( X_AND, reg, vm->dataMask );	// reg = reg & vm->dataMask
#endif
		return;
	}

#if idx64
	emit_cmp_rx( reg, R_DATAMASK );					// cmp reg, dataMask
#else
	emit_op_rx_imm32( X_CMP, reg, vm->dataMask );	// cmp reg, vm->dataMask
#endif

	// error reporting
	EmitString( "0F 87" );			// ja +errorFunction
	Emit4( funcOffset[ func ] - compiledOfs - 6 );
}


static void emit_CheckJump( vm_t *vm, uint32_t reg, int32_t proc_base, int32_t proc_len )
{
	if ( ( vm_rtChecks->integer & VM_RTCHECK_JUMP ) == 0 ) {
		return;
	}

	if ( proc_base != -1 ) {
		uint32_t rx;

		// allow jump within local function scope only
		// check if (reg - proc_base) > proc_len
		rx = alloc_rx( R_EDX | TEMP );
		emit_lea( rx, reg, -proc_base );			// lea edx, [reg - procBase]
		emit_op_rx_imm32( X_CMP, rx, proc_len );	// cmp edx, proc_len
		unmask_rx( rx );

		EmitString( "0F 87" );						// ja +funcOffset[FUNC_BADJ]
		Emit4( funcOffset[ FUNC_BADJ ] - compiledOfs - 6 );
	} else {
		// check if reg >= instructionCount
		emit_op_rx_imm32( X_CMP, reg, vm->instructionCount );	// cmp reg, vm->instructionCount
		EmitString( "0F 83" );									// jae +funcOffset[ FUNC_ERRJ ]
		Emit4( funcOffset[ FUNC_ERRJ ] - compiledOfs - 6 );
	}
}


static void emit_CheckProc( vm_t *vm, instruction_t *ins )
{
	// programStack overflow check
	if ( vm_rtChecks->integer & VM_RTCHECK_PSTACK ) {
#if idx64
		emit_cmp_rx( R_PSTACK, R_STACKBOTTOM );	// cmp programStack, stackBottom
#else
		emit_op_rx_imm32( X_CMP, R_PSTACK, vm->stackBottom ); // cmp programStack, vm->stackBottom
#endif
		EmitString( "0F 8C" );					// jl +funcOffset[ FUNC_PSOF ]
		Emit4( funcOffset[ FUNC_PSOF ] - compiledOfs - 6 );
	}

	// opStack overflow check
	if ( vm_rtChecks->integer & VM_RTCHECK_OPSTACK ) {
		uint32_t rx = alloc_rx( R_EDX | TEMP );

		// proc->opStack carries max.used opStack value
		emit_lea( rx | R_REX, R_OPSTACK, ins->opStack ); // rdx = opStack + max.opStack
		
		// check if rdx > opstackTop
#if idx64
		emit_cmp_rx( rx | R_REX, R_OPSTACKTOP );			// cmp rdx, opStackTop
#else
		emit_cmp_rx_mem( rx, (intptr_t) &vm->opStackTop );	// cmp edx, [&vm->opStackTop]
#endif

		EmitString( "0F 87" );			// ja +funcOffset[FUNC_OSOF]
		Emit4( funcOffset[ FUNC_OSOF ] - compiledOfs - 6 );

		unmask_rx( rx );
	}
}


#ifdef _WIN32
#define SHADOW_BASE 40
#else // linux/*BSD ABI
#define SHADOW_BASE 8
#endif

#define PUSH_STACK  32

#define PARAM_STACK 128


static void EmitCallFunc( vm_t *vm )
{
	static int sysCallOffset = 0;

	init_opstack(); // to avoid any side-effects on emit_CheckJump()

	emit_test_rx( R_EAX, R_EAX );		// test eax, eax
	EmitString( "7C" );					// jl +offset (SystemCall)
	Emit1( sysCallOffset );				// will be valid after first pass
	sysCallOffset = compiledOfs;

	// jump target range check
	mask_rx( R_EAX );
	emit_CheckJump( vm, R_EAX, -1, 0 );
	unmask_rx( R_EAX );

	// save procBase and programStack
	//emit_push( R_PROCBASE );			// procBase
	//emit_push( R_PSTACK );			// programStack

	// calling another vm function
#if idx64
	emit_call_index( R_INSPOINTERS, R_EAX ); // call qword ptr [instructionPointers+rax*8]
#else
	emit_call_index_offset( (intptr_t)instructionPointers, R_EAX ); // call dword ptr [vm->instructionPointers + eax*8]
#endif

	// restore proc base and programStack so there is
	// no need to validate programStack anymore
	//emit_pop( R_PSTACK );				// pop rsi // programStack
	//emit_pop( R_PROCBASE );			// pop rbp // procBase

	emit_ret();							// ret

	sysCallOffset = compiledOfs - sysCallOffset;

	// systemCall:
	// convert negative num to system call number
	// and store right before the first arg
	emit_not_rx( R_EAX );				// not eax

	// we may jump here from ConstOptimize() also
	funcOffset[FUNC_SYSC] = compiledOfs;

#if idx64
	// allocate stack for shadow(win32)+parameters
	emit_op_rx_imm32( X_SUB, R_ESP | R_REX, SHADOW_BASE + PUSH_STACK + PARAM_STACK ); // sub rsp, 200

	emit_lea( R_EDX | R_REX, R_ESP, SHADOW_BASE ); // lea rdx, [ rsp + SHADOW_BASE ]

	// save scratch registers
	emit_store_rx( R_ESI | R_REX, R_EDX, 0 );	// mov [rdx+00], rsi
	emit_store_rx( R_EDI | R_REX, R_EDX, 8 );	// mov [rdx+08], rdi
	emit_store_rx( R_R11 | R_REX, R_EDX, 16 );	// mov [rdx+16], r11 - dataMask

	// ecx = &int64_params[0]
	emit_lea( R_ECX | R_REX, R_ESP, SHADOW_BASE + PUSH_STACK ); // lea rcx, [rsp+SHADOW_BASE+PUSH_STACK]

	// save 64-bit syscallNum
	emit_store_rx( R_EAX | R_REX, R_ECX, 0 );	// mov [rcx], rax

	// vm->programStack = programStack - 4; // or 8
	mov_rx_ptr( R_EDX, &vm->programStack ); // mov rdx, &vm->programStack

	emit_lea( R_EAX, R_PSTACK, -8 );		// lea eax, [programStack-8]
	emit_store_rx( R_EAX, R_EDX, 0 );		// mov [rdx], eax

	// params = procBase + 8
	emit_lea( R_ESI | R_REX, R_PROCBASE, 8 );	// lea rsi, [procBase + 8]

	// rcx = &int64_params[1]
	emit_op_rx_imm32( X_ADD, R_ECX | R_REX, 8 ); // add rcx, 8

	// dest_params[1-15] = params[1-15];
	emit_xor_rx( R_EDX, R_EDX );			// xor edx, edx
	// loop
	EmitString( "48 63 04 96" );			// movsxd rax, dword [rsi+rdx*4]
	EmitString( "48 89 04 D1" );			// mov qword ptr[rcx+rdx*8], rax
	EmitString( "83 C2 01" );				// add edx, 1
	EmitString( "83 FA" );					// cmp edx, 15
	Emit1( ( PARAM_STACK / 8 ) - 1 );
	EmitString( "7C F0" );					// jl -16

#ifdef _WIN32
	// rcx = &int64_params[0]
	emit_op_rx_imm32( X_SUB, R_ECX | R_REX, 8 );	// sub rcx, 8
#else // linux/*BSD ABI
	// rdi = &int64_params[0]
	emit_lea( R_EDI | R_REX, R_ECX, -8 );		// lea rdi, [rcx-8]
#endif

	// currentVm->systemCall( param );
	emit_call_rx( R_SYSCALL );					// call r12

	// restore registers
	emit_lea( R_EDX | R_REX, R_ESP, SHADOW_BASE ); // lea rdx, [rsp + SHADOW_BASE]

	emit_load4( R_ESI | R_REX, R_EDX, 0 );	// mov rsi, [rdx+00]
	emit_load4( R_EDI | R_REX, R_EDX, 8 );	// mov rdi, [rdx+08]
	emit_load4( R_R11 | R_REX, R_EDX, 16 );	// mov r11, [rdx+16]

	// store result in opStack[4]
	emit_store_rx( R_EAX, R_OPSTACK, 4 ); // *opstack[ opStack + 4 ] = eax

	// return stack
	emit_op_rx_imm32( X_ADD, R_ESP | R_REX, SHADOW_BASE + PUSH_STACK + PARAM_STACK ); // add rsp, 200

	emit_ret();								// ret

#else // id386

	// params = (int *)((byte *)currentVM->dataBase + programStack + 4);
	emit_lea( R_ECX, R_EBP, 4 );			// lea ecx, [ebp+4]

	// function prologue
	emit_push( R_EBP );						// push ebp
	emit_mov_rx( R_EBP, R_ESP );			// mov ebp, esp
	emit_op_rx_imm32( X_SUB, R_ESP, 4 );	// sub esp, 4

	// align stack before call
	emit_op_rx_imm32( X_AND, R_ESP, -16 );	// and esp, -16

	// ABI note: esi/edi must not change during call!

	// currentVM->programStack = programStack - 4;
	emit_lea( R_EDX, R_PSTACK, -8 );		// lea edx, [esi-8]
	emit_store_rx_offset( R_EDX, (intptr_t) &vm->programStack ); // mov[ &vm->programStack ], edx

	// params[0] = syscallNum
	emit_store_rx( R_EAX, R_ECX, 0 );		// mov [ecx], eax

	// cdecl - set params
	emit_store_rx( R_ECX, R_ESP, 0 );		// mov [esp], ecx

	// currentVm->systemCall( param );
	emit_call_indir( (intptr_t) &vm->systemCall ); // call dword ptr [&currentVM->systemCall]

	// store result in opStack[4]
	emit_store_rx( R_EAX, R_OPSTACK, 4 );	// *opstack[ 4 ] = eax

	// function epilogue
	emit_mov_rx( R_ESP, R_EBP );			// mov esp, ebp
	emit_pop( R_EBP );						// pop ebp
	emit_ret();								// ret
#endif
}


static void EmitBCPYFunc( vm_t *vm )
{
	emit_push( R_ESI );						// push esi
	emit_push( R_EDI );						// push edi

	emit_mov_rx( R_ESI, R_EDX );			// mov esi, edx // top of opstack
	emit_mov_rx( R_EDI, R_EAX );			// mov edi, eax // bottom of opstack

	if ( vm_rtChecks->integer & VM_RTCHECK_DATA )
	{
		mov_rx_imm32( R_EAX, vm->dataMask );	// mov eax, datamask

		emit_and_rx( R_ESI, R_EAX );			// and esi, eax
		emit_and_rx( R_EDI, R_EAX );			// and edi, eax

		emit_lea_base_index( R_EDX, R_ESI, R_ECX ); // lea edx, [esi + ecx]
		emit_and_rx( R_EDX, R_EAX );			// and edx, eax - apply data mask
		emit_sub_rx( R_EDX, R_ESI );			// sub edx, esi - source-adjusted counter

		emit_add_rx( R_EDX, R_EDI );			// add edx, edi
		emit_and_rx( R_EDX, R_EAX );			// and edx, eax - apply data mask
		emit_sub_rx( R_EDX, R_EDI );			// sub edx, edi - destination-adjusted counter
		emit_mov_rx( R_ECX, R_EDX );			// mov ecx, edx
	}

	emit_add_rx( R_ESI | R_REX, R_EBX );	// add rsi, rbx
	emit_add_rx( R_EDI | R_REX, R_EBX );	// add rdi, rbx

	EmitString( "F3 A5" );					// rep movsd
	emit_pop( R_EDI );						// pop edi
	emit_pop( R_ESI );						// pop esi
	emit_ret();								// ret
}


static void EmitFloatJump( instruction_t *i, int op, int addr )
{
	switch ( op ) {
		case OP_EQF:
			EmitString( "80 E4 44" );	// and ah,0x44 (preserve C2 too)
			EmitJump( i, OP_NE, addr );
			break;

		case OP_NEF:
			EmitString( "80 E4 40" );	// and ah,0x40 
			EmitJump( i, OP_EQ, addr );
			break;

		case OP_LTF:
			EmitString( "80 E4 05" );	// and ah,0x05 (preserve C2 too)
			EmitJump( i, OP_NE, addr );
			break;

		case OP_LEF:
			EmitString( "80 E4 45" );	// and ah,0x45 (preserve C2 too)
			EmitJump( i, OP_NE, addr );
			break;

		case OP_GTF:
			EmitString( "80 E4 41" );	// and ah,0x41
			EmitJump( i, OP_EQ, addr );
			break;

		case OP_GEF:
			EmitString( "80 E4 01" );	// and ah,0x01
			EmitJump( i, OP_EQ, addr );
			break;
	};
}


static void EmitPSOFFunc( vm_t *vm )
{
	mov_rx_ptr( R_EAX, &badStackPtr ); // mov eax, &badStackPtr
	EmitString( "FF 10" );		// call [eax]
	emit_ret();					// ret
}


static void EmitOSOFFunc( vm_t *vm )
{
	mov_rx_ptr( R_EAX, &badOpStackPtr ); // mov eax, &badOpStackPtr
	EmitString( "FF 10" );		// call [eax]
	emit_ret();					// ret
}


static void EmitBADJFunc( vm_t *vm )
{
	mov_rx_ptr( R_EAX, &badJumpPtr ); // mov eax, &badJumpPtr
	EmitString( "FF 10" );		// call [eax]
	emit_ret();					// ret
}


static void EmitERRJFunc( vm_t *vm )
{
	mov_rx_ptr( R_EAX, &errJumpPtr ); // mov eax, &errJumpPtr
	EmitString( "FF 10" );		// call [eax]
	emit_ret();					// ret
}


static void EmitDATRFunc( vm_t *vm )
{
	mov_rx_ptr( R_EAX, &badDataReadPtr ); // mov eax, &badDataReadPtr
	EmitString( "FF 10" );		// call [eax]
	emit_ret();					// ret
}


static void EmitDATWFunc( vm_t *vm )
{
	mov_rx_ptr( R_EAX, &badDataWritePtr ); // mov eax, &badDataWritePtr
	EmitString( "FF 10" );		// call [eax]
	emit_ret();					// ret
}


#ifdef CONST_OPTIMIZE

static qboolean IsFloorTrap( const vm_t *vm, const int trap )
{
	if ( trap == ~CG_FLOOR && vm->index == VM_CGAME )
		return qtrue;

	if ( trap == ~UI_FLOOR && vm->index == VM_UI )
		return qtrue;

	if ( trap == ~G_FLOOR && vm->index == VM_GAME )
		return qtrue;

	return qfalse;
}


static qboolean IsCeilTrap( const vm_t *vm, const int trap )
{
	if ( trap == ~CG_CEIL && vm->index == VM_CGAME )
		return qtrue;

	if ( trap == ~UI_CEIL && vm->index == VM_UI )
		return qtrue;

	if ( trap == ~G_CEIL && vm->index == VM_GAME )
		return qtrue;

	return qfalse;
}


static qboolean NextLoad( const var_addr_t *v, const instruction_t *i, int op )
{
	if ( i->jused ) {
		return qfalse;
	}
	if ( v->addr == i->value ) {
		if ( i->op == OP_CONST ) {
			if ( v->base == R_DATABASE && (i+1)->op == op ) {
				return qtrue;
			}
		}
		if ( i->op == OP_LOCAL ) {
			if ( v->base == R_PROCBASE && (i+1)->op == op ) {
				return qtrue;
			}
		}
	}
	return qfalse;
}


static qboolean ConstOptimize( vm_t *vm, instruction_t *ci, instruction_t *ni )
{
	var_addr_t var;

	switch ( ni->op ) {

		case OP_STORE4:	{
			if ( ci->value == 0 ) {
				// "xor eax, eax" + non-const path is shorter
				return qfalse;
			}
			if ( addr_on_top( &var ) ) {
				if ( NextLoad( &var, ni + 1, OP_LOAD4 ) ) {
					return qfalse; // store value in a register
				}
				discard_top(); dec_opstack();						// v = *opstack; opstack -= 4
				emit_store_imm32( ci->value, var.base, var.addr );	// (dword*)base_reg[ v ] = 0x12345678
				var.size = 4;
				wipe_var_range( &var );
			} else {
				int rx = load_rx_opstack( R_EAX | RCONST ); dec_opstack(); // eax = *opstack; opstack -= 4
				emit_CheckReg( vm, rx, FUNC_DATW );
				emit_store_imm32_index( ci->value, R_DATABASE, rx ); // (dword*)dataBase[ eax ] = 0x12345678
				unmask_rx( rx );
				wipe_vars();
			}
			ip += 1; // OP_STORE4
			return qtrue;
		}

		case OP_STORE2:	{
			if ( addr_on_top( &var ) ) {
				if ( NextLoad( &var, ni + 1, OP_LOAD2 ) || find_rx_const( ci->value, 0xFFFF ) ) {
					return qfalse; // store value in a register
				}
				discard_top(); dec_opstack();						// v = *opstack; opstack -= 4
				emit_store2_imm16( ci->value, var.base, var.addr );	// (short*)var.base[ v ] = 0x1234
				var.size = 2;
				wipe_var_range( &var );
			} else {
				int rx = load_rx_opstack( R_EAX | RCONST ); dec_opstack(); // eax = *opstack; opstack -= 4
				emit_CheckReg( vm, rx, FUNC_DATW );
				emit_store2_imm16_index( ci->value, R_DATABASE, rx ); // (word*)dataBase[ eax ] = 0x12345678
				unmask_rx( rx );
				wipe_vars();
			}
			ip += 1; // OP_STORE2
			return qtrue;
		}

		case OP_STORE1: {
			if ( addr_on_top( &var ) ) {
				if ( NextLoad( &var, ni + 1, OP_LOAD1 ) || find_rx_const( ci->value, 0xFF ) ) {
					return qfalse; // store value in a register
				}
				discard_top(); dec_opstack();						// v = *opstack; opstack -= 4
				emit_store1_imm8( ci->value, var.base, var.addr );	// (byte*)base_reg[ v ] = 0x12
				var.size = 1;
				wipe_var_range( &var );
			} else {
				int rx = load_rx_opstack( R_EAX | RCONST ); dec_opstack(); // eax = *opstack; opstack -= 4
				emit_CheckReg( vm, rx, FUNC_DATW );
				emit_store1_imm8_index( ci->value, R_DATABASE, rx ); // (char*)dataBase[ eax ] = 0x12345678
				unmask_rx( rx );
				wipe_vars();
			}
			ip += 1; // OP_STORE1
			return qtrue;
		}

		case OP_ADD: {
			int rx = load_rx_opstack( R_EAX );			// eax = *opstack
			emit_op_rx_imm32( X_ADD, rx, ci->value );	// add eax, 0x12345678
			store_rx_opstack( rx );						// *opstack = eax
			ip += 1; // OP_ADD
			return qtrue;
		}

		case OP_SUB: {
			int rx = load_rx_opstack( R_EAX );			// eax = *opstack
			emit_op_rx_imm32( X_SUB, rx, ci->value );	// sub eax, 0x12345678
			store_rx_opstack( rx );						// *opstack = eax
			ip += 1; // OP_SUB
			return qtrue;
		}

		case OP_MULI:
		case OP_MULU: {
			int rx = load_rx_opstack( R_EAX );		// eax = *opstack
			emit_mul_rx_imm( rx, ci->value );		// imul eax, eax, 0x12345678
			store_rx_opstack( rx );					// *opstack = eax
			ip += 1; // OP_MUL
			return qtrue;
		}

		case OP_BAND: {
			int rx = load_rx_opstack( R_EAX );			// eax = *opstack
			if ( !(ni+1)->jused && (ni+1)->op == OP_CONST && (ni+1)->value == 0 && ops[(ni+2)->op].flags & JUMP ) {
				if ( (ni+2)->op == OP_EQ || (ni+2)->op == OP_NE ) {
					dec_opstack();
					emit_test_rx_imm32( rx, ci->value );// test eax, mask
					EmitJump( ni+2, (ni+2)->op, (ni+2)->value ); // jcc
					unmask_rx( rx );
					ip += 3; // OP_BAND + OP_CONST + OP_EQ/OP_NE
					return qtrue;
				}
			}
			emit_op_rx_imm32( X_AND, rx, ci->value );	// and eax, 0x12345678
			store_rx_opstack( rx );						// *opstack = eax
			ip += 1; // OP_BAND
			return qtrue;
		}

		case OP_BOR: {
			int rx = load_rx_opstack( R_EAX );			// eax = *opstack
			emit_op_rx_imm32( X_OR, rx, ci->value );	// or eax, 0x12345678
			store_rx_opstack( rx );						// *opstack = eax
			ip += 1; // OP_BOR
			return qtrue;
		}

		case OP_BXOR: {
			int rx = load_rx_opstack( R_EAX );			// eax = *opstack
			emit_op_rx_imm32( X_XOR, rx, ci->value );	// xor eax, 0x12345678
			store_rx_opstack( rx );						// *opstack = eax
			ip += 1; // OP_BXOR
			return qtrue;
		}

		case OP_LSH:
			if ( ci->value < 0 || ci->value > 31 )
				break; // undefined behavior
			if ( ci->value ) {
				int rx = load_rx_opstack( R_EAX );		// eax = *opstack
				emit_shl_rx_imm( rx, ci->value );		// eax = (unsigned)eax << x
				store_rx_opstack( rx );					// *opstack = eax
			}
			ip += 1; // OP_LSH
			return qtrue;

		case OP_RSHI:
			if ( ci->value < 0 || ci->value > 31 )
				break; // undefined behavior
			if ( ci->value ) {
				int rx = load_rx_opstack( R_EAX );		// eax = *opstack
				emit_sar_rx_imm( rx, ci->value );		// eax = eax >> x
				store_rx_opstack( rx );					// *opstack = eax
			}
			ip += 1; // OP_RSHI
			return qtrue;

		case OP_RSHU:
			if ( ci->value < 0 || ci->value > 31 )
				break; // undefined behavior
			if ( ci->value ) {
				int rx = load_rx_opstack( R_EAX );		// eax = *opstack
				emit_shr_rx_imm( rx, ci->value );		// eax = (unsigned)eax >> x
				store_rx_opstack( rx );					// *opstack = eax
			}
			ip += 1; // OP_RSHU
			return qtrue;

		case OP_CALL: {
			inc_opstack(); // opstack += 4

			if ( HasSSEFP() ) {
				if ( ci->value == ~TRAP_SQRT ) {
					int sx = alloc_sx( R_XMM0 );
					emit_sqrt( sx, R_PROCBASE, 8 );		// sqrtss xmm0, dword ptr [ebp + 8]
					store_sx_opstack( sx );				// *opstack = xmm0
					ip += 1; // OP_CALL
					return qtrue;
				}

				if ( IsFloorTrap( vm, ci->value ) && ( CPU_Flags & CPU_SSE41 ) ) {
					int sx = alloc_sx( R_XMM0 );
					emit_floor( sx, R_PROCBASE, 8 );	// roundss xmm0, dword ptr [ebp + 8], 1
					store_sx_opstack( sx );				// *opstack = xmm0
					ip += 1; // OP_CALL
					return qtrue;
				}

				if ( IsCeilTrap( vm, ci->value ) && ( CPU_Flags & CPU_SSE41 ) ) {
					int sx = alloc_sx( R_XMM0 );
					emit_ceil( sx, R_PROCBASE, 8 );		// roundss xmm0, dword ptr [ebp + 8], 2
					store_sx_opstack( sx );				// *opstack = xmm0
					ip += 1; // OP_CALL
					return qtrue;
				}
			} else {
				// legacy x87 path
			}

			flush_volatile();

			if ( ci->value < 0 ) { // syscall
				mask_rx( R_EAX );
				mov_rx_imm32( R_EAX, ~ci->value ); // eax - syscall number
				if ( opstack != 1 ) {
					emit_op_rx_imm32( X_ADD, R_OPSTACK | R_REX, (opstack-1) * sizeof( int32_t ) );
					EmitCallOffset( FUNC_SYSC );
					emit_op_rx_imm32( X_SUB, R_OPSTACK | R_REX, (opstack-1) * sizeof( int32_t ) );
				} else {
					EmitCallOffset( FUNC_SYSC );
				}
				ip += 1; // OP_CALL
				store_syscall_opstack();
				return qtrue;
			}
			emit_push( R_OPSTACK );	// push edi
			if ( opstack != 1 ) {
				emit_op_rx_imm32( X_ADD, R_OPSTACK | R_REX, (opstack-1) * sizeof( int32_t ) ); // add rdi, opstack-4
			}
			EmitCallAddr( vm, ci->value );	// call +addr
			emit_pop( R_OPSTACK );	// pop edi
			ip += 1; // OP_CALL
			return qtrue;
		}	

		case OP_JUMP:
			flush_volatile();
			EmitJump( ni, ni->op, ci->value );
			ip += 1; // OP_JUMP
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
			int rx = load_rx_opstack( R_EAX | RCONST ); dec_opstack(); // eax = *opstack; opstack -= 4
			if ( ci->value == 0 && ( ni->op == OP_EQ || ni->op == OP_NE ) ) {
				emit_test_rx( rx, rx );						// test eax, eax
			} else{
				emit_op_rx_imm32( X_CMP, rx, ci->value );	// cmp eax, 0x12345678
			}
			unmask_rx( rx );
			EmitJump( ni, ni->op, ni->value );			// jcc
			ip += 1; // OP_cond
			return qtrue;
		}

	}
	return qfalse;
}
#endif


/*
=================
VM_FindMOps

Search for known macro-op sequences
=================
*/
static void VM_FindMOps( instruction_t *buf, int instructionCount )
{
	instruction_t *i;
	int n;

	i = buf;
	n = 0;

	while ( n < instructionCount )
	{
		if ( i->op == OP_LOCAL ) {
#ifdef MACRO_OPTIMIZE
			// OP_LOCAL + OP_LOCAL + OP_LOAD4 + OP_CONST + OP_XXX + OP_STORE4
			if ( ( i + 1 )->op == OP_LOCAL && i->value == ( i + 1 )->value && ( i + 2 )->op == OP_LOAD4 && ( i + 3 )->op == OP_CONST && ( i + 4 )->op != OP_UNDEF && ( i + 5 )->op == OP_STORE4 ) {
				int v = ( i + 4 )->op;
				if ( v == OP_ADD ) {
					i->op = MOP_ADD;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_SUB ) {
					i->op = MOP_SUB;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_BAND ) {
					i->op = MOP_BAND;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_BOR ) {
					i->op = MOP_BOR;
					i += 6; n += 6;
					continue;
				}
				if ( v == OP_BXOR ) {
					i->op = MOP_BXOR;
					i += 6; n += 6;
					continue;
				}
			}
#endif
			if ( (i+1)->op == OP_CONST && (i+2)->op == OP_CALL && (i+3)->op == OP_STORE4 && (i+4)->op == OP_LOCAL && (i+5)->op == OP_LOAD4 && (i+6)->op == OP_LEAVE ) {
				if ( i->value == (i+4)->value && !(i+4)->jused ) {
					(i+0)->op = OP_IGNORE; (i+0)->value = 0;
					(i+3)->op = OP_IGNORE; (i+3)->value = 0;
					(i+4)->op = OP_IGNORE; (i+4)->value = 0;
					(i+5)->op = OP_IGNORE; (i+5)->value = 0;
					i += 7;
					n += 7;
					continue;
				}
			}
		}
		i++;
		n++;
	}
}


#ifdef MACRO_OPTIMIZE
/*
=================
EmitMOPs
=================
*/
static qboolean EmitMOPs( vm_t *vm, instruction_t *ci, macro_op_t op )
{
	uint32_t reg_base;
	int n;

	if ( (ci + 1 )->op == OP_LOCAL )
		reg_base = R_PROCBASE;
	else
		reg_base = R_DATABASE;

	switch ( op )
	{
		//[local] += CONST
		case MOP_ADD:
			n = inst[ ip + 2 ].value;
			emit_op_mem_imm( X_ADD, R_PROCBASE, ci->value, n );
			ip += 5;
			return qtrue;

		//[local] -= CONST
		case MOP_SUB:
			n = inst[ ip + 2 ].value;
			emit_op_mem_imm( X_SUB, R_PROCBASE, ci->value, n );
			ip += 5;
			return qtrue;

		//[local] &= CONST
		case MOP_BAND:
			n = inst[ ip + 2 ].value;
			emit_op_mem_imm( X_AND, R_PROCBASE, ci->value, n );
			ip += 5;
			return qtrue;

		//[local] |= CONST
		case MOP_BOR:
			n = inst[ ip + 2 ].value;
			emit_op_mem_imm( X_OR, R_PROCBASE, ci->value, n );
			ip += 5;
			return qtrue;

		//[local] ^= CONST
		case MOP_BXOR:
			n = inst[ ip + 2 ].value;
			emit_op_mem_imm( X_XOR, R_PROCBASE, ci->value, n );
			ip += 5;
			return qtrue;
	}

	return qfalse;
}
#endif // MACRO_OPTIMIZE


#ifdef DUMP_CODE
static void dump_code( const char *vmname, uint8_t *c, int32_t code_len )
{
	const char *filename = va( "vm-%s.hex", vmname );
	fileHandle_t fh = FS_FOpenFileWrite( filename );
	if ( fh != FS_INVALID_HANDLE ) {
		while ( code_len >= 8 ) {
			FS_Printf( fh, "%02x %02x %02x %02x %02x %02x %02x %02x\n", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7] );
			code_len -= 8;
			c += 8;
		}
		while ( code_len > 0 ) {
			FS_Printf( fh, "%02x", c[0] );
			if ( code_len > 1 )
				FS_Write( " ", 1, fh );
			code_len -= 1;
			c += 1;
		}
		FS_FCloseFile( fh );
	}
}
#endif


/*
=================
VM_Compile
=================
*/
qboolean VM_Compile( vm_t *vm, vmHeader_t *header ) {
	const char	*errMsg;
	int		instructionCount;
	instruction_t *ci;
	int		i, n;
	uint32_t rx[3];
	uint32_t sx[2];
	int proc_base;
	int proc_len;
#ifdef RET_OPTIMIZE
	int proc_end;
#endif
	var_addr_t var;
	opcode_t sign_extend;
	int var_size;
	reg_t *reg;
#if JUMP_OPTIMIZE
	int num_compress;
#endif

	inst = (instruction_t*)Z_Malloc( (header->instructionCount + 8 ) * sizeof( instruction_t ) );
	instructionOffsets = (int*)Z_Malloc( header->instructionCount * sizeof( int ) );

	errMsg = VM_LoadInstructions( (byte *) header + header->codeOffset, header->codeLength, header->instructionCount, inst );
	if ( !errMsg ) {
		errMsg = VM_CheckInstructions( inst, vm->instructionCount, vm->jumpTableTargets, vm->numJumpTableTargets, vm->exactDataLength );
	}
	if ( errMsg ) {
		VM_FreeBuffers();
		Com_Printf( "VM_CompileX86 error: %s\n", errMsg );
		return qfalse;
	}

	VM_ReplaceInstructions( vm, inst );

	VM_FindMOps( inst, vm->instructionCount );

#if JUMP_OPTIMIZE
	for ( i = 0; i < header->instructionCount; i++ ) {
		if ( ops[inst[i].op].flags & JUMP ) {
			int d = inst[i].value - i;
			// we can correctly calculate backward jump offsets even at initial pass
			// but for forward jumps we do some estimation
			// too low threshold will reduce compression
			// too high threshold may invoke extra expansion passes
			if ( d > 0 && d <= FJUMP_THRESHOLD ) {
				inst[i].njump = 1;
			}
		}
	}
	num_compress = 0;
#endif

	code = NULL; // we will allocate memory later, after last defined pass
	instructionPointers = NULL;

	memset( funcOffset, 0, sizeof( funcOffset ) );

	instructionCount = header->instructionCount;

	for( pass = 0; pass < NUM_PASSES; pass++ )
	{
__compile:

	// translate all instructions
	ip = 0;
	compiledOfs = 0;
#if JUMP_OPTIMIZE
	jumpSizeChanged = 0;
#endif

	proc_base = -1;
	proc_len = 0;
#ifdef RET_OPTIMIZE
	proc_end = 0;
#endif

	init_opstack();

#ifdef DEBUG_INT
	emit_brk();
#endif

#if idx64

	emit_push( R_EBP );				// push rbp
	emit_push( R_EBX );				// push rbx
	emit_push( R_ESI );				// push rsi
	emit_push( R_EDI );				// push rdi
	emit_push( R_R12 );				// push r12
	emit_push( R_R13 );				// push r13
	emit_push( R_R14 );				// push r14
	emit_push( R_R15 );				// push r15

	mov_rx_ptr( R_DATABASE, vm->dataBase );			// mov rbx, vm->dataBase

	// do not use wrapper, force constant size there
	emit_mov_rx_imm64( R_INSPOINTERS, (intptr_t) instructionPointers ); // mov r8, vm->instructionPointers

	mov_rx_imm32( R_DATAMASK, vm->dataMask );		// mov r11d, vm->dataMask
	mov_rx_imm32( R_STACKBOTTOM, vm->stackBottom );	// mov r14d, vm->stackBottom

	mov_rx_ptr( R_EAX, &vm->opStack );				// mov rax, &vm->opStack

	emit_load4( R_OPSTACK | R_REX, R_EAX, 0 );		// mov rdi, [rax]

	mov_rx_ptr( R_SYSCALL, vm->systemCall );		// mov r13, vm->systemCall

	mov_rx_ptr( R_EAX, &vm->programStack );			// mov rax, &vm->programStack

	emit_load4( R_PSTACK, R_EAX, 0 ); // mov esi, dword ptr [rax]

	emit_lea( R_OPSTACKTOP | R_REX, R_OPSTACK, sizeof( int32_t ) * ( MAX_OPSTACK_SIZE - 1 ) ); // lea r15, [opStack + opStackSize - 4]

	EmitCallOffset( FUNC_ENTR );

#ifdef DEBUG_VM
	mov_rx_ptr( R_EAX, &vm->programStack );		// mov rax, &vm->programStack
	emit_store_rx( R_PSTACK, R_EAX, 0 );		// mov [rax], esi
#endif

	emit_pop( R_R15 );				// pop r15
	emit_pop( R_R14 );				// pop r14
	emit_pop( R_R13 );				// pop r13
	emit_pop( R_R12 );				// pop r12
	emit_pop( R_EDI );				// pop rdi
	emit_pop( R_ESI );				// pop rsi
	emit_pop( R_EBX );				// pop rbx
	emit_pop( R_EBP );				// pop rbp
	emit_ret();						// ret

#else // id386

	emit_pushad();					// pushad

	mov_rx_ptr( R_DATABASE, vm->dataBase );	// mov ebx, vm->dataBase

	emit_load_rx_offset( R_PSTACK, (intptr_t) &vm->programStack ); // mov esi, [&vm->programStack]

	emit_load_rx_offset( R_OPSTACK, (intptr_t) &vm->opStack ); // mov edi, [&vm->opStack]

	EmitCallOffset( FUNC_ENTR );

#ifdef DEBUG_VM
	emit_store_rx_offset( R_PSTACK, (intptr_t) &vm->programStack ); // mov [&vm->programStack], esi 
#endif

	// emit_store_rx_offset( R_OPSTACK, (intptr_t) &vm->opStack ); // // [&vm->opStack], edi

	emit_popad();					// popad
	emit_ret();						// ret

#endif // id386

	EmitAlign( FUNC_ALIGN );

	// main function entry offset
	funcOffset[FUNC_ENTR] = compiledOfs;

	while ( ip < instructionCount ) {
		ci = &inst[ip + 0];

#ifdef REGS_OPTIMIZE
		if ( ci->jused )
#endif
		{
			// we can safely perform register optimizations only in case if
			// we are 100% sure that current instruction is not a jump label
			flush_volatile();
		}

		instructionOffsets[ ip++ ] = compiledOfs;

		switch ( ci->op ) {

			case OP_UNDEF:
				emit_brk();			// int 3
				break;

			case OP_IGNORE:
				break;

			case OP_BREAK:
				emit_brk();			// int 3
				break;

			case OP_ENTER:
				EmitAlign( FUNC_ALIGN );

				instructionOffsets[ ip-1 ] = compiledOfs;

				proc_base = ip; // this points on next instruction after OP_ENTER

				// locate endproc
				for ( proc_len = -1, i = ip; i < header->instructionCount; i++ ) {
					if ( inst[ i ].op == OP_PUSH && inst[ i + 1 ].op == OP_LEAVE ) {
						proc_len = i - proc_base;
#ifdef RET_OPTIMIZE
						proc_end = i + 1;
#endif
						break;
					}
				}

				if ( proc_len == 0 ) {
					// empty function, just return
					emit_ret();
					ip += 2; // OP_PUSH + OP_LEAVE
					break;
				}

				emit_push( R_PROCBASE );				// procBase
				emit_push( R_PSTACK );					// programStack

				emit_op_rx_imm32( X_SUB, R_PSTACK, ci->value );	// sub programStack, 0x12

				emit_lea_base_index( R_PROCBASE | R_REX, R_DATABASE, R_PSTACK ); // procBase = dataBase + programStack

				emit_CheckProc( vm, ci );
				break;

			case OP_LEAVE:
				flush_opstack();
				dec_opstack(); // opstack -= 4
#ifdef DEBUG_VM
				if ( opstack != 0 )
					DROP( "opStack corrupted on OP_LEAVE" );
#endif

#ifdef RET_OPTIMIZE
				if ( !ci->endp && proc_base >= 0 ) {
					// jump to last OP_LEAVE instruction in this function
					if ( inst[ ip + 0 ].op == OP_PUSH && inst[ ip + 1 ].op == OP_LEAVE ) {
						// next instruction is proc_end
					} else {
						EmitJump( ci, OP_JUMP, proc_end );
					}
					break;
				}
#endif

				emit_pop( R_PSTACK );			// pop rsi // programStack
				emit_pop( R_PROCBASE );			// pop rbp // procBase

				emit_ret();						// ret
				break;

			case OP_CALL:
				rx[0] = load_rx_opstack( R_EAX | FORCED ); // eax = *opstack
				flush_volatile();
				if ( opstack != 1 ) {
					emit_op_rx_imm32( X_ADD, R_OPSTACK | R_REX, ( opstack - 1 ) * sizeof( int32_t ) );
					EmitCallOffset( FUNC_CALL ); // call +FUNC_CALL
					emit_op_rx_imm32( X_SUB, R_OPSTACK | R_REX, ( opstack - 1 ) * sizeof( int32_t ) );
				} else {
					EmitCallOffset( FUNC_CALL ); // call +FUNC_CALL
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
				rx[0] = load_rx_opstack( R_EAX | RCONST ); dec_opstack(); // eax = *opstack; opstack -= 4
				flush_volatile();
				emit_CheckJump( vm, rx[0], proc_base, proc_len );		// check if eax is within current proc
#if idx64
				emit_jump_index( R_INSPOINTERS, rx[0] );				// jmp qword ptr [instructionPointers + rax*8]
#else
				emit_jump_index_offset( (intptr_t) instructionPointers, rx[0] ); // jmp dword ptr [instructionPointers + eax*4]
#endif
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
				rx[0] = load_rx_opstack( R_EAX | RCONST ); dec_opstack(); // eax = *opstack; opstack -= 4
				rx[1] = load_rx_opstack( R_EDX | RCONST ); dec_opstack(); // edx = *opstack; opstack -= 4
				emit_cmp_rx( rx[1], rx[0] ); // cmp edx, eax
				unmask_rx( rx[0] );
				unmask_rx( rx[1] );
				EmitJump( ci, ci->op, ci->value );
				break;
			}

			case OP_EQF:
			case OP_NEF:
			case OP_LTF:
			case OP_LEF:
			case OP_GTF:
			case OP_GEF: {
				if ( HasSSEFP() ) {
					sx[0] = load_sx_opstack( R_XMM0 | RCONST ); dec_opstack(); // xmm0 = *opstack; opstack -= 4
					sx[1] = load_sx_opstack( R_XMM1 | RCONST ); dec_opstack(); // xmm1 = *opstack; opstack -= 4
					if ( ci->op == OP_EQF || ci->op == OP_NEF ) {
						emit_ucomiss( sx[1], sx[0] );	// ucomiss xmm1, xmm0
					} else {
						emit_comiss( sx[1], sx[0] );	// comiss xmm1, xmm0
					}
					unmask_sx( sx[0] );
					unmask_sx( sx[1] );
					EmitJump( ci, ci->op, ci->value );
					break;
				} else {
					// legacy x87 path
					flush_opstack_top(); dec_opstack();
					flush_opstack_top(); dec_opstack();
					if ( HasFCOM() ) {
						emit_fld( R_OPSTACK, 8 );		// fld dword ptr [opStack+8]
						emit_fld( R_OPSTACK, 4 );		// fld dword ptr [opStack+4]
						EmitString( "DF E9" );			// fucomip
						EmitString( "DD D8" );			// fstp st(0)
						EmitJump( ci, ci->op, ci->value );
					} else {
						alloc_rx( R_EAX | FORCED );
						emit_fld( R_OPSTACK, 4 );		// fld dword ptr [opStack+4]
						emit_fcomp( R_OPSTACK, 8 );		// fcomp dword ptr [opStack+8]
						EmitString( "DF E0" );			// fnstsw ax
						EmitFloatJump( ci, ci->op, ci->value );
						unmask_rx( R_EAX );
					}
					break;
				}
			}
			case OP_LOAD1:
			case OP_LOAD2:
			case OP_LOAD4:
#ifdef FPU_OPTIMIZE
				if ( ci->op == OP_LOAD4 && ci->fpu && HasSSEFP() ) {
					if ( addr_on_top( &var ) ) {
						// address specified by CONST/LOCAL
						discard_top();
						var.size = 4;
						if ( find_sx_var( &sx[0], &var ) ) {
							// already cached in some register
							mask_sx( sx[0] );
						} else {
							// not cached, perform load
							sx[0] = alloc_sx( R_XMM0 );
							emit_load_sx( sx[0], var.base, var.addr );	// xmmm0 = var.base[var.addr]
							set_sx_var( sx[0], &var );
						}
					} else {
						// address stored in register
						rx[0] = load_rx_opstack( R_EAX | RCONST );		// eax = *opstack
						emit_CheckReg( vm, rx[0], FUNC_DATR );
						sx[0] = alloc_sx( R_XMM0 );
						emit_load_sx_index( sx[0], R_DATABASE, rx[0] ); // xmmm0 = dataBase[eax]
						unmask_rx( rx[0] );
					}
					store_sx_opstack( sx[0] );							// *opstack = xmm0
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
									emit_zex8( rx[0], rx[0] );  // movzx eax, al 
									// invalidate any mappings that overlaps with high [8..31] bits 
									//var.addr += 1; var.size = 3;
									//wipe_reg_range( rx_regs + rx[0], &var );
									// TODO: just reduce mapping size?
									reduce_map_size( reg, 1 );
									// modify constant
									reg->cnst.value &= 0xFF;
									reg->ext = Z_EXT8;
								}
								break;
							case OP_LOAD2:
								if ( reg->ext != Z_EXT16 ) {
									emit_zex16( rx[0], rx[0] );  // movzx eax, ax
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
						rx[0] = alloc_rx( R_EAX );			// allocate new register, wipe its metadata
						if ( (ci+1)->op == sign_extend && sign_extend != OP_UNDEF ) {
							// merge with following sign-extension instruction
							switch ( ci->op ) {
								case OP_LOAD1: emit_load1_sex( rx[0], var.base, var.addr );	var.size = 1; set_rx_ext( rx[0], S_EXT8 ); break; // eax = (signed byte)var.base[var.addr]
								case OP_LOAD2: emit_load2_sex( rx[0], var.base, var.addr );	var.size = 2; set_rx_ext( rx[0], S_EXT16 ); break; // eax = (signed short)var.base[var.addr]
							}
							ip += 1; // OP_SEX/OP_SEX16
						} else {
							// usual load with zero-extension
							switch ( ci->op ) {
								case OP_LOAD1: emit_load1( rx[0], var.base, var.addr );	var.size = 1; set_rx_ext( rx[0], Z_EXT8 ); break; // eax = (unsigned byte)var.base[var.addr]
								case OP_LOAD2: emit_load2( rx[0], var.base, var.addr );	var.size = 2; set_rx_ext( rx[0], Z_EXT16 ); break; // eax = (unsigned short)var.base[var.addr]
								case OP_LOAD4: emit_load4( rx[0], var.base, var.addr ); var.size = 4; set_rx_ext( rx[0], Z_NONE ); break; // eax = (dword)var.base[var.addr]
							}
						} // load with zero-extension
						set_rx_var( rx[0], &var );
					} // not cached, perform load
				} else {
					// address stored in register
					// rx[0] = rx[1] = load_rx_opstack( R_EAX );		// target, address = *opstack
					load_rx_opstack2( &rx[0], R_EDX, &rx[1], R_EAX ); // target, address = *opstack

					emit_CheckReg( vm, rx[1], FUNC_DATR );			// check address bounds
					if ( (ci+1)->op == sign_extend && sign_extend != OP_UNDEF ) {
						// merge with following sign-extension instruction
						switch ( ci->op ) {
							case OP_LOAD1: emit_load1_sex_index( rx[0], R_DATABASE, rx[1] ); set_rx_ext( rx[0], S_EXT8 ); break; // target = (signed byte)[dataBase + address]
							case OP_LOAD2: emit_load2_sex_index( rx[0], R_DATABASE, rx[1] ); set_rx_ext( rx[0], S_EXT16 ); break; // target = (unsigned short)[dataBase + address]
						}
						ip += 1; // OP_SEX8/OP_SEX16
					} else {
						// usual load with zero-extension
						switch ( ci->op ) {
							case OP_LOAD1: emit_load1_index( rx[0], R_DATABASE, rx[1] ); set_rx_ext( rx[0], Z_EXT8 ); break; // target = (unsigned byte)[dataBase + address]
							case OP_LOAD2: emit_load2_index( rx[0], R_DATABASE, rx[1] ); set_rx_ext( rx[0], Z_EXT16 ); break; // target = (unsigned short)[dataBase + address]
							default:       emit_load4_index( rx[0], R_DATABASE, rx[1] ); set_rx_ext( rx[0], Z_NONE ); break; // target = (dword)dataBase[dataBase + address]
						}
					}
					if ( rx[1] != rx[0] ) {
						unmask_rx( rx[1] );
					}
				}
				store_rx_opstack( rx[0] ); // *opstack = target
				break;

			case OP_STORE1:
			case OP_STORE2:
			case OP_STORE4:
				if ( scalar_on_top() && ci->op == OP_STORE4 && HasSSEFP() ) {
					sx[0] = load_sx_opstack( R_XMM0 | RCONST );	dec_opstack();		// xmm0 = *opstack; opstack -= 4
					if ( addr_on_top( &var ) ) {
						// address specified by CONST/LOCAL
						discard_top(); dec_opstack();
						emit_store_sx( sx[0], var.base, var.addr );					// baseReg[n] = xmm0
						var.size = 4;
						wipe_var_range( &var );
						set_sx_var( sx[0], &var );									// update metadata
					} else {
						rx[1] = load_rx_opstack( R_EDX | RCONST ); dec_opstack();	// edx = *opstack; opstack -= 4
						emit_CheckReg( vm, rx[1], FUNC_DATW );
						emit_store_sx_index( sx[0], R_DATABASE, rx[1] );			// dataBase[edx] = xmm0
						unmask_rx( rx[1] );
						wipe_vars(); // unknown/dynamic address, wipe all register mappings
					}
					unmask_sx( sx[0] );
				} else {
					// integer path
					rx[0] = load_rx_opstack( R_EAX | RCONST );	dec_opstack();		// eax = *opstack; opstack -= 4
					if ( addr_on_top( &var ) ) {
						// address specified by CONST/LOCAL
						discard_top(); dec_opstack();
						switch ( ci->op ) {
							case OP_STORE1:	emit_store1_rx( rx[0], var.base, var.addr ); var.size = 1; break; // (byte*)var.base[var.addr] = al
							case OP_STORE2:	emit_store2_rx( rx[0], var.base, var.addr ); var.size = 2; break; // (short*)var.base[var.addr] = ax
							default:        emit_store_rx( rx[0], var.base, var.addr ); var.size = 4; break;  // (dword*)var.base[var.addr] = eax
						}
						wipe_var_range( &var );
						set_rx_var( rx[0], &var ); // update metadata
					} else {
						rx[1] = load_rx_opstack( R_EDX | RCONST ); dec_opstack();	// edx = *opstack; opstack -= 4
						emit_CheckReg( vm, rx[1], FUNC_DATW );
						switch ( ci->op ) {
							case OP_STORE1: emit_store1_index( rx[0], R_DATABASE, rx[1] ); break;	// (byte*)dataBase[edx] = al
							case OP_STORE2: emit_store2_index( rx[0], R_DATABASE, rx[1] ); break;	// (short*)dataBase[edx] = ax
							default:        emit_store4_index( rx[0], R_DATABASE, rx[1] ); break;	// (dword*)dataBase[edx] = eax
						}
						unmask_rx( rx[1] );
						wipe_vars(); // unknown/dynamic address, wipe all register mappings
					}
					unmask_rx( rx[0] );
				}
				break;

			case OP_ARG:
				var.base = R_PROCBASE;
				var.addr = ci->value;
				var.size = 4;
				wipe_var_range( &var );
				if ( scalar_on_top() && HasSSEFP() ) {
					sx[0] = load_sx_opstack( R_XMM0 | RCONST ); dec_opstack();	// xmm0 = *opstack; opstack -=4
					emit_store_sx( sx[0], var.base, var.addr );					// [procBase + v] = xmm0
					unmask_sx( sx[0] );
				} else {
					if ( const_on_top() && top_value() != 0 ) {
						n = top_value(); discard_top(); dec_opstack();
						emit_store_imm32( n, var.base, var.addr );					// [procBase + v] = n
					} else {
						rx[0] = load_rx_opstack( R_EAX | RCONST ); dec_opstack();	// eax = *opstack; opstack -=4
						emit_store_rx( rx[0], var.base, var.addr );					// [procBase + v] = eax
						unmask_rx( rx[0] );
					}
				}
				break;

			case OP_BLOCK_COPY:
				rx[0] = load_rx_opstack( R_EDX | FORCED ); dec_opstack(); // edx - src
				rx[1] = load_rx_opstack( R_EAX | FORCED ); dec_opstack(); // eax - dst
				rx[2] = alloc_rx( R_ECX | FORCED );		// flush and reserve ecx register
				mov_rx_imm32( rx[2], ci->value >> 2 );	// mov ecx, 0x12345678 / 4
				EmitCallOffset( FUNC_BCPY );
				unmask_rx( rx[2] );
				unmask_rx( rx[1] );
				unmask_rx( rx[0] );
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
				rx[0] = load_rx_opstack( R_EAX ); // eax = *opstack
				switch ( ci->op ) {
					case OP_SEX8:  emit_sex8( rx[0], rx[0] ); break;	// movsx eax, al
					case OP_SEX16: emit_sex16( rx[0], rx[0] ); break;	// movsx eax, ax
					case OP_NEGI:  emit_neg_rx( rx[0] ); break;			// neg eax
					case OP_BCOM:  emit_not_rx( rx[0] ); break;			// not eax
				}
				store_rx_opstack( rx[0] ); // *opstack = eax
				break;

			case OP_ADD:
				rx[0] = load_rx_opstack( R_EAX | RCONST ); dec_opstack(); // eax = *opstack
				rx[1] = load_rx_opstack( R_ECX ); // opstack-=4; ecx = *opstack
#ifdef CONST_OPTIMIZE
				// optimize OP_ADD + OP_CONST + OP_ADD
				if ( !(ci+1)->jused && (ci+1)->op == OP_CONST && (ci+2)->op == OP_ADD ) {
					emit_lea_base_index_offset( rx[1], rx[1], rx[0], (ci+1)->value ); // lea ecx, [ecx + eax + const]
					ip += 2; // OP_CONST + OP_ADD
				} else
#endif
				emit_add_rx( rx[1], rx[0] ); // add ecx, eax
				unmask_rx( rx[0] );
				store_rx_opstack( rx[1] ); // *opstack = ecx
				break;

			//case OP_ADD:
			case OP_SUB:
			case OP_MULI:
			case OP_MULU:
			case OP_BAND:
			case OP_BOR:
			case OP_BXOR:
				rx[0] = load_rx_opstack( R_EAX | RCONST ); dec_opstack(); // eax = *opstack
				rx[1] = load_rx_opstack( R_ECX ); // opstack-=4; ecx = *opstack
				switch ( ci->op ) {
					//case OP_ADD: emit_add_rx( rx[1], rx[0] ); break;	// add ecx, eax
					case OP_SUB: emit_sub_rx( rx[1], rx[0] ); break;	// sub ecx, eax
					case OP_MULI: emit_mul_rx( rx[1], rx[0] ); break;	// imul ecx, eax
					case OP_MULU: emit_mul_rx( rx[1], rx[0] ); break;	// imul ecx, eax
					case OP_BAND: emit_and_rx( rx[1], rx[0] ); break;	// and ecx, eax
					case OP_BOR:  emit_or_rx(  rx[1], rx[0] ); break;	// or ecx, eax
					case OP_BXOR: emit_xor_rx( rx[1], rx[0] ); break;	// xor ecx, eax
				}
				unmask_rx( rx[0] );
				store_rx_opstack( rx[1] ); // *opstack = ecx
				break;

			case OP_LSH:
			case OP_RSHU:
			case OP_RSHI:
				rx[0] = load_rx_opstack( R_ECX | FORCED | RCONST ); dec_opstack(); // ecx = *opstack
				rx[1] = load_rx_opstack( R_EAX ); // opstack-=4; eax = *opstack
				switch ( ci->op ) {
					case OP_LSH: emit_shl_rx( rx[1] ); break;	// shl eax, cl
					case OP_RSHU: emit_shr_rx( rx[1] ); break;	// shr eax, cl
					case OP_RSHI: emit_sar_rx( rx[1] ); break;	// sar eax, cl
				}
				unmask_rx( rx[0] );
				store_rx_opstack( rx[1] ); // *opstack = eax
				break;

			case OP_DIVI:
			case OP_DIVU:
			case OP_MODI:
			case OP_MODU:
				rx[1] = load_rx_opstack( R_EAX | FORCED | SHIFT4 );	// eax = *(opstack-4)
				rx[2] = alloc_rx( R_EDX | FORCED );	// flush and reserve edx register
				rx[0] = load_rx_opstack( R_ECX | RCONST | XMASK ); dec_opstack(); // ecx = *opstack; opstack -= 4
				if ( rx[0] == rx[2] || rx[1] == rx[2] )
					DROP( "incorrect register setup, rx_mask=%04x", build_rx_mask() );
				if ( ci->op == OP_DIVI || ci->op == OP_MODI ) {
					emit_cdq();						// cdq
					emit_idiv_rx( rx[0] );			// idiv eax, ecx
				} else {
					emit_xor_rx( rx[2], rx[2] );	// xor edx, edx
					emit_udiv_rx( rx[0] );			// div ecx
				}
				unmask_rx( rx[0] );
				if ( ci->op == OP_DIVI || ci->op == OP_DIVU ) {
					unmask_rx( rx[2] );
					store_rx_opstack( rx[1] );	// *opstack = eax
				} else {
					unmask_rx( rx[1] );
					store_rx_opstack( rx[2] );	// *opstack = edx
				}
				break;

			case OP_ADDF:
			case OP_SUBF:
			case OP_MULF:
			case OP_DIVF:
				if ( HasSSEFP() ) {
					sx[0] = load_sx_opstack( R_XMM0 | RCONST ); dec_opstack(); // xmm0 = *opstack
					sx[1] = load_sx_opstack( R_XMM1 ); // opstack -= 4; xmm1 = *opstack
					switch ( ci->op ) {
						case OP_ADDF: emit_add_sx( sx[1], sx[0] ); break; // xmm1 = xmm1 + xmm0
						case OP_SUBF: emit_sub_sx( sx[1], sx[0] ); break; // xmm1 = xmm1 - xmm0
						case OP_MULF: emit_mul_sx( sx[1], sx[0] ); break; // xmm1 = xmm1 * xmm0
						case OP_DIVF: emit_div_sx( sx[1], sx[0] ); break; // xmm1 = xmm1 / xmm0
					}
					unmask_sx( sx[0] );
					store_sx_opstack( sx[1] ); // *opstack = xmm0
				} else {
					// legacy x87 path
					flush_opstack_top(); dec_opstack(); // value
					flush_opstack_top(); // target
					emit_fld( R_OPSTACK, opstack * sizeof( int32_t ) );
					switch ( ci->op ) {
						case OP_ADDF: emit_fadd( R_OPSTACK, ( opstack + 1 ) * sizeof( int32_t ) ); break;
						case OP_SUBF: emit_fsub( R_OPSTACK, ( opstack + 1 ) * sizeof( int32_t ) ); break;
						case OP_MULF: emit_fmul( R_OPSTACK, ( opstack + 1 ) * sizeof( int32_t ) ); break;
						case OP_DIVF: emit_fdiv( R_OPSTACK, ( opstack + 1 ) * sizeof( int32_t ) ); break;
					}
					emit_fstp( R_OPSTACK, opstack * sizeof( int32_t ) );
				}
				break;

			case OP_NEGF:
				if ( HasSSEFP() ) {
					sx[0] = load_sx_opstack( R_XMM0 | RCONST );	// xmm0 = *opstack
					sx[1] = alloc_sx( R_XMM1 );
					emit_xor_sx( sx[1], sx[1] );			// xorps xmm1, xmm1
					emit_sub_sx( sx[1], sx[0] );			// subps xmm1, xmm0
					unmask_sx( sx[0] );
					store_sx_opstack( sx[1] );				// *opstack = xmm1
				} else {
					// legacy x87 path
					flush_opstack_top();
					emit_fld( R_OPSTACK, opstack * sizeof( int32_t ) ); // fld dword ptr [opStack]
					EmitString( "D9 E0" );					// fchs
					emit_fstp( R_OPSTACK, opstack * sizeof( int32_t ) ); // fstp dword ptr [opStack]
				}
				break;

			case OP_CVIF:
				if ( HasSSEFP() ) {
					sx[0] = alloc_sx( R_XMM0 );
					rx[0] = load_rx_opstack( R_EAX | RCONST ); // eax = *opstack
					emit_cvtsi2ss( sx[0], rx[0] );			// cvtsi2ss xmm0, eax
					unmask_rx( rx[0] );
					store_sx_opstack( sx[0] );				// *opstack = xmm0
				} else {
					flush_opstack_top();
					emit_fild( R_OPSTACK, opstack * sizeof( int32_t ) ); // fild dword ptr [opStack]
					emit_fstp( R_OPSTACK, opstack * sizeof( int32_t ) ); // fstp dword ptr [opStack]
				}
				break;

			case OP_CVFI:
				if ( HasSSEFP() ) {
					rx[0] = alloc_rx( R_EAX );
					sx[0] = load_sx_opstack( R_XMM0 | RCONST ); // xmm0 = *opstack
					emit_cvttss2si( rx[0], sx[0] );			// cvttss2si xmm0, eax
					unmask_sx( sx[0] );
					store_rx_opstack( rx[0] );				// *opstack = eax
				} else {
					static int32_t fp_cw[2] = { 0x0000, 0x0F7F }; // [0] - current value, [1] - round towards zero
					flush_opstack_top();
					alloc_rx( R_EAX | FORCED );
					emit_fld( R_OPSTACK, opstack * sizeof( int32_t ) ); // fld dword ptr [opStack]
					mov_rx_ptr( R_EAX, &fp_cw );
					EmitString( "9B D9 38" );	// fnstcw word ptr [eax]
					EmitString( "D9 68 04" );	// fldcw word ptr [eax+4]
					emit_fistp( R_OPSTACK, opstack * sizeof( int32_t ) ); // fistp dword ptr [opStack]
					EmitString( "D9 28" );		// fldcw word ptr [eax]
					unmask_rx( R_EAX );
				}
				break;

#ifdef MACRO_OPTIMIZE
			case MOP_ADD:
			case MOP_SUB:
			case MOP_BAND:
			case MOP_BOR:
			case MOP_BXOR:
				if ( !EmitMOPs( vm, ci, ci->op ) )
					Com_Error( ERR_FATAL, "VM_CompileX86: bad opcode %02X", ci->op );
				break;
#endif
			default:
				Com_Error( ERR_FATAL, "VM_CompileX86: bad opcode %02X", ci->op );
				VM_FreeBuffers();
				return qfalse;
			}
		}

		// ****************
		// system functions
		// ****************
		EmitAlign( FUNC_ALIGN );
		funcOffset[FUNC_CALL] = compiledOfs;
		EmitCallFunc( vm );

		EmitAlign( FUNC_ALIGN );
		funcOffset[FUNC_BCPY] = compiledOfs;
		EmitBCPYFunc( vm );

		// ***************
		// error functions
		// ***************

		// bad jump
		EmitAlign( FUNC_ALIGN );
		funcOffset[FUNC_BADJ] = compiledOfs;
		EmitBADJFunc( vm );

		// error jump
		EmitAlign( FUNC_ALIGN );
		funcOffset[FUNC_ERRJ] = compiledOfs;
		EmitERRJFunc( vm );

		// programStack overflow
		EmitAlign( FUNC_ALIGN );
		funcOffset[FUNC_PSOF] = compiledOfs;
		EmitPSOFFunc( vm );

		// opStack overflow
		EmitAlign( FUNC_ALIGN );
		funcOffset[FUNC_OSOF] = compiledOfs;
		EmitOSOFFunc( vm );

		// read access range violation
		EmitAlign( FUNC_ALIGN );
		funcOffset[ FUNC_DATR ] = compiledOfs;
		EmitDATRFunc( vm );

		// write access range violation
		EmitAlign( FUNC_ALIGN );
		funcOffset[ FUNC_DATW ] = compiledOfs;
		EmitDATWFunc( vm );

		EmitAlign( sizeof( intptr_t ) ); // for instructionPointers

#if JUMP_OPTIMIZE
		if ( pass == PASS_COMPRESS && ++num_compress < NUM_COMPRESSIONS && jumpSizeChanged ) {
			pass = PASS_COMPRESS;
			goto __compile;
		}
		if ( jumpSizeChanged ) {
			if ( pass == PASS_EXPAND_ONLY ) {
				pass = PASS_EXPAND_ONLY;
				goto __compile;
			}
		}
#endif
	} // for( pass = 0; pass < n; pass++ )

	n = header->instructionCount * sizeof( intptr_t );

	if ( code == NULL ) {
		code = (byte*)VM_Alloc_Compiled( vm, PAD(compiledOfs,8), n );
		if ( code == NULL ) {
			return qfalse;
		}
		instructionPointers = (intptr_t*)(byte*)(code + PAD(compiledOfs,8));
		//vm->instructionPointers = instructionPointers; // for debug purposes?
		pass = NUM_PASSES-1; // repeat last pass
		goto __compile;
	}

#ifdef DUMP_CODE
	dump_code( vm->name, code, compiledOfs );
#endif

	// offset all the instruction pointers for the new location
	for ( i = 0; i < header->instructionCount; i++ ) {
		if ( !inst[i].jused ) {
			instructionPointers[ i ] = (intptr_t)badJumpPtr;
			continue;
		}
		instructionPointers[ i ] = (intptr_t)vm->codeBase.ptr + instructionOffsets[ i ];
	}

	VM_FreeBuffers();

#ifdef VM_X86_MMAP
	if ( mprotect( vm->codeBase.ptr, vm->codeSize, PROT_READ|PROT_EXEC ) ) {
		VM_Destroy_Compiled( vm );
		Com_Printf( S_COLOR_YELLOW "VM_CompileX86: mprotect failed\n" );
		return qfalse;
	}
#elif _WIN32
	{
		DWORD oldProtect = 0;

		// remove write permissions.
		if ( !VirtualProtect( vm->codeBase.ptr, vm->codeSize, PAGE_EXECUTE_READ, &oldProtect ) ) {
			VM_Destroy_Compiled( vm );
			Com_Printf( S_COLOR_YELLOW "VM_CompileX86: VirtualProtect failed\n" );
			return qfalse;
		}
	}
#endif

	vm->destroy = VM_Destroy_Compiled;

	Com_Printf( "VM file %s compiled to %i bytes of code\n", vm->name, compiledOfs );

	return qtrue;
}


/*
=================
VM_Alloc_Compiled
=================
*/
static void *VM_Alloc_Compiled( vm_t *vm, int codeLength, int tableLength )
{
	void	*ptr;
	int		length;

	length = codeLength + tableLength;
#ifdef VM_X86_MMAP
	ptr = mmap( NULL, length, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
	if ( ptr == MAP_FAILED ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: mmap failed" );
		return NULL;
	}
#elif _WIN32
	// allocate memory with EXECUTE permissions under windows.
	ptr = VirtualAlloc( NULL, length, MEM_COMMIT, PAGE_EXECUTE_READWRITE );
	if ( !ptr ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: VirtualAlloc failed" );
		return NULL;
	}
#else
	ptr = malloc( length );
	if ( !ptr ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: malloc failed" );
		return NULL;
	}
#endif
	vm->codeBase.ptr = (byte*)ptr;
	vm->codeLength = codeLength;
	vm->codeSize = length;

	return vm->codeBase.ptr;
}


/*
==============
VM_Destroy_Compiled
==============
*/
static void VM_Destroy_Compiled( vm_t* vm )
{
#ifdef VM_X86_MMAP
	munmap( vm->codeBase.ptr, vm->codeSize );
#elif _WIN32
	VirtualFree( vm->codeBase.ptr, 0, MEM_RELEASE );
#else
	free( vm->codeBase.ptr );
#endif
	vm->codeBase.ptr = NULL;
}


/*
==============
VM_CallCompiled

This function is called directly by the generated code
==============
*/
int32_t VM_CallCompiled( vm_t *vm, int nargs, int32_t *args )
{
	int32_t	opStack[MAX_OPSTACK_SIZE];
	int32_t	stackOnEntry;
	int32_t	*image;
#if id386
	int32_t	*oldOpTop;
#endif
	int		i;

	// we might be called recursively, so this might not be the very top
	stackOnEntry = vm->programStack;

#if id386
	oldOpTop = vm->opStackTop;
#endif

	vm->programStack -= ( MAX_VMMAIN_CALL_ARGS + 2 ) * sizeof( int32_t );

	// set up the stack frame
	image = (int32_t*) ( vm->dataBase + vm->programStack );
	for ( i = 0; i < nargs; i++ ) {
		image[i + 2] = args[i];
	}

	// these only needed for interpreter:
	// image[1] =  0;	// return stack
	// image[0] = -1;	// will terminate loop on return

#ifdef DEBUG_VM
	opStack[0] = 0xDEADC0DE;
#endif
	opStack[1] = 0;

	vm->opStack = opStack;

#if id386
	vm->opStackTop = opStack + ARRAY_LEN( opStack ) - 1;
#endif

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

#if id386
	vm->opStackTop = oldOpTop;
#endif

	return opStack[1];
}
