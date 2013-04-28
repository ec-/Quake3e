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
// vm_x86.c -- load time compiler and execution environment for x86

#include "vm_local.h"
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#endif

#ifndef _WIN32
#include <sys/mman.h> // for PROT_ stuff
#endif

/* need this on NX enabled systems (i386 with PAE kernel or
 * noexec32=on x86_64) */
#if defined(__linux__) || defined(__FreeBSD__)
#define VM_X86_MMAP
#endif

//#define VM_LOG_SYSCALLS
#define JUMP_OPTIMIZE 0

#if JUMP_OPTIMIZE
#define NUM_PASSES 7
#else
#define NUM_PASSES 3
#endif

#define BCPY_PTR 1

static void *VM_Alloc_Compiled( vm_t *vm, int codeLength );
static void VM_Destroy_Compiled( vm_t *vm );

/*
  -------------
  eax	scratch
  ebx*	dataBase
  ecx	scratch (required for shifts)
  edx	scratch (required for divisions)
  esi*	program stack
  edi*	opstack
  ebp*  current proc stack ( dataBase + program stack )
  -------------
  rax	scratch
  rbx*	dataBase
  rcx	scratch (required for shifts)
  rdx	scratch (required for divisions)
  rsi*	programStack
  rdi*	opstack
  rbp*  current proc stack ( dataBase + program stack )
  r8	instructionPointers
  r9    dataMask
  r12*  systemCall
  r13*  stackBottom
  r14*  opStackTop

  Example how data segment will look like during vmMain execution:
  | .... |
  |------| vm->programStack -=36 (8+12+16) // set by vmMain
  | ???? | +0 - unused
  | ???? | +4 - unused
  |-------
  | arg0 | +8  \
  | arg4 | +12  | - passed arguments, accessible from subroutines
  | arg8 | +16 /
  |------|
  | loc0 | +20 \
  | loc4 | +24  \ - locals, accessible only from local scope
  | loc8 | +28  /
  | lc12 | +32 /
  |------| vm->programStack -= 48 (8+40) // set by VM_CallCompiled()
  | ???? | +0 - unused
  | ???? | +4 - unused
  | arg0 | +8
  | arg1 | +12
  | arg2 | +16
  | arg3 | +20
  | arg4 | +24
  | arg5 | +28
  | arg6 | +32
  | arg7 | +36
  | arg8 | +40
  | arg9 | +44
  |------| vm->programStack = vm->dataMask + 1 // set by VM_Create()

  jump/call opStack rules:

  1) opStack must be 8 before conditional jump
  2) opStack must be 4 before unconditional jump
  3) opStack must be >=4 before OP_CALL
  4) opStack must remain the same after OP_CALL
  5) you may not jump in/call locations with opStack != 0

*/

#define JUMP	(1<<0)
#define CALC	(1<<1)

#define ISS8(V) ( (V) >= -128 && (V) <= 127 )
#define ISU8(V) ( (V) >= 0 && (V) <= 127 )

typedef enum 
{
	REG_EAX = 0,
	REG_ECX
} reg_t;

typedef struct {
	int   size; 
	int	  stack;
	int   flags;
} opcode_info_t ;

const char *opname[OP_MAX] = 
{
	"undef",
	"ignore",
	"break",
	"enter",
	"leave",
	"call",
	"push",
	"pop",

	"const",
	"local",
	"jump",

	"eq",
	"ne",

	"lti",
	"lei",
	"gti",
	"gei",

	"ltu",
	"leu",
	"gtu",
	"geu",

	"eqf",
	"nef",

	"ltf",
	"lef",
	"gtf",
	"gef",

	"load1",
	"load2",
	"load4",
	"store1",
	"store2",
	"store4",
	"arg",
	"bcopy",

	"sex8",
	"sex16",

	"negi",
	"add",
	"sub",
	"divi",
	"divu",
	"modi",
	"modu",
	"muli",
	"mulu",

	"band",
	"bor", 
	"bxor",
	"bcom",

	"lsh",
	"rshi",
	"rshu",

	"negf",
	"addf",
	"subf",
	"divf",
	"mulf",

	"cvif",
	"cvfi",
};

opcode_info_t ops[OP_MAX] = 
{
	{ 0, 0, 0 }, // undef
	{ 0, 0, 0 }, // ignore
	{ 0, 0, 0 }, // break

	{ 4, 0, 0 }, // enter
	{ 4,-4, 0 }, // leave
	{ 0, 0, 0 }, // call
	{ 0, 4, 0 }, // push
	{ 0,-4, 0 }, // pop

	{ 4, 4, 0 }, // const
	{ 4, 4, 0 }, // local
	{ 0,-4, 0 }, // jump

	{ 4,-8, JUMP }, // eq
	{ 4,-8, JUMP }, // ne

	{ 4,-8, JUMP }, // lti
	{ 4,-8, JUMP }, // lei
	{ 4,-8, JUMP }, // gti
	{ 4,-8, JUMP }, // gei

	{ 4,-8, JUMP }, // ltu
	{ 4,-8, JUMP }, // leu
	{ 4,-8, JUMP }, // gtu
	{ 4,-8, JUMP }, // geu

	{ 4,-8, JUMP }, // eqf
	{ 4,-8, JUMP }, // nef

	{ 4,-8, JUMP }, // ltf
	{ 4,-8, JUMP }, // lef
	{ 4,-8, JUMP }, // gtf
	{ 4,-8, JUMP }, // gef

	{ 0, 0, 0 }, // load1
	{ 0, 0, 0 }, // load2
	{ 0, 0, 0 }, // load4
	{ 0,-8, 0 }, // store1
	{ 0,-8, 0 }, // store2
	{ 0,-8, 0 }, // store4
	{ 1,-4, 0 }, // arg
	{ 4,-8, 0 }, // bcopy

	{ 0, 0, 0 }, // sex8
	{ 0, 0, 0 }, // sex16

	{ 0, 0, 0 }, // negi
	{ 0,-4, 0 }, // add
	{ 0,-4, 0 }, // sub
	{ 0,-4, 0 }, // divi
	{ 0,-4, 0 }, // divu
	{ 0,-4, 0 }, // modi
	{ 0,-4, 0 }, // modu
	{ 0,-4, 0 }, // muli
	{ 0,-4, 0 }, // mulu

	{ 0,-4, 0 }, // band
	{ 0,-4, 0 }, // bor
	{ 0,-4, 0 }, // bxor
	{ 0, 0, 0 }, // bcom

	{ 0,-4, 0 }, // lsh
	{ 0,-4, 0 }, // rshi
	{ 0,-4, 0 }, // rshu

	{ 0, 0, 0 }, // negf
	{ 0,-4, CALC }, // addf
	{ 0,-4, CALC }, // subf
	{ 0,-4, CALC }, // divf
	{ 0,-4, CALC }, // mulf

	{ 0, 0, 0 }, // cvif
	{ 0, 0, 0 } // cvfi
};


// macro opcode sequences
typedef enum {
	MOP_UNDEF = OP_MAX,
	MOP_IGNORE4,
	MOP_ADD4,
	MOP_SUB4,
	MOP_BAND4,
	MOP_BOR4,
	MOP_CALCF4,
} macro_op_t;


typedef struct {
	int   value;    // 32
	byte  op;	 	// 8
	byte  mop;		// 8
	byte  opStack;  // 8
	int jused:1;
	int jump:1;
	int swtch:1;
} instruction_t;


typedef enum 
{
	LAST_COMMAND_NONE = 0,
	LAST_COMMAND_MOV_EDI_EAX,
	LAST_COMMAND_MOV_EDI_CONST,
	LAST_COMMAND_MOV_EAX_EDI,
	LAST_COMMAND_MOV_EAX_EDI_CALL,
	LAST_COMMAND_SUB_DI_4,
	LAST_COMMAND_SUB_DI_8,
	LAST_COMMAND_FSTP_EDI
} ELastCommand;

typedef enum 
{
	FUNC_ENTR = 0,
	FUNC_CALL,
	FUNC_SYSC,
	FUNC_FTOL,
	FUNC_BCPY,
	FUNC_PSOF,
	FUNC_OSOF,
	FUNC_BADJ,
	FUNC_ERRJ,
	FUNC_DATA,
	FUNC_LAST
} func_t;

static	byte    *code;
static	int     compiledOfs;

static  instruction_t *inst = NULL;
static  instruction_t *ci;
static  instruction_t *ni;

static int fp_cw[2] = { 0x0000, 0x0F7F }; // [0] - current value, [1] - round towards zero

static	int	ip, pass;
static	int	lastConst;
static	int	pop1;
static	int jlabel;

static	ELastCommand	LastCommand;

int		funcOffset[FUNC_LAST];

#ifdef DEBUG_VM
static int	errParam = 0;
#endif

static void ErrJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code outside VM" ); 
	exit(1);
}


static void BadJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code at bad location inside VM" ); 
	exit(1);
}


static void BadStack( void )
{
	Com_Error( ERR_DROP, "program tried to overflow program stack" ); 
	exit(1);
}


static void BadOpStack( void )
{
	Com_Error( ERR_DROP, "program tried to overflow opcode stack" ); 
	exit(1);
}


static void BadData( void )
{
#ifdef DEBUG_VM	
	Com_Error( ERR_DROP, "program tried to read/write out of data segment at %i", errParam ); 
#else
	Com_Error( ERR_DROP, "program tried to read/write out of data segment" ); 
#endif
	exit(1);
}


static void (*const errJumpPtr)(void) = ErrJump;
static void (*const badJumpPtr)(void) = BadJump;
static void (*const badStackPtr)(void) = BadStack;
static void (*const badOpStackPtr)(void) = BadOpStack;
static void (*const badDataPtr)(void) = BadData;


static void VM_FreeBuffers( void ) 
{
	// should be freed in reversed allocation order
	Z_Free( inst ); 
}


static void Emit1( int v ) 
{
	if ( code ) 
	{
		code[ compiledOfs ] = v;
	}
	compiledOfs++;

	LastCommand = LAST_COMMAND_NONE;
}


static void Emit4( int v ) 
{
	Emit1( v & 255 );
	Emit1( ( v >> 8 ) & 255 );
	Emit1( ( v >> 16 ) & 255 );
	Emit1( ( v >> 24 ) & 255 );
}


#if idx64
static void Emit8( int64_t v ) 
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
#endif


static void EmitPtr( const void *ptr ) 
{
#if idx64
	Emit8( (intptr_t)ptr );
#else
	Emit4( (intptr_t)ptr );
#endif
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


static void EmitRexString( int prefix, const char *string ) 
{
#if idx64
	Emit1( prefix );
#endif
	EmitString( string );
}


static void EmitCommand( ELastCommand command )
{
	switch(command)
	{
		case LAST_COMMAND_MOV_EDI_EAX:
			EmitString( "89 07" );		// mov dword ptr [edi], eax
			break;

		case LAST_COMMAND_MOV_EAX_EDI:
			EmitString( "8B 07" );		// mov eax, dword ptr [edi]
			break;

		case LAST_COMMAND_SUB_DI_4:
			EmitRexString( 0x48, "83 EF 04" );	// sub edi, 4
			break;

		case LAST_COMMAND_SUB_DI_8:
			EmitRexString( 0x48, "83 EF 08" );	// sub edi, 8
			break;

		case LAST_COMMAND_FSTP_EDI:
			EmitString( "D9 1F" );		// fstp dword ptr [edi]
			break;

		default:
			break;
	}
	LastCommand = command;
}


static void EmitAddEDI4( vm_t *vm ) 
{
	if ( jlabel ) 
	{
		EmitRexString( 0x48, "83 C7 04" );		//	add edi,4
		return;
	}

	if ( LastCommand == LAST_COMMAND_SUB_DI_4 ) // sub edi, 4
	{
#if idx64
		compiledOfs -= 4;
#else
		compiledOfs -= 3;
#endif
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		LastCommand = LAST_COMMAND_NONE;
		return;
	}

	if ( LastCommand == LAST_COMMAND_SUB_DI_8 ) // sub edi, 8
	{	
#if idx64
		compiledOfs -= 4;
#else
		compiledOfs -= 3;
#endif
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitCommand( LAST_COMMAND_SUB_DI_4 );
		return;
	}

	EmitRexString( 0x48, "83 C7 04" );	//	add edi,4
}


static void EmitMovEAXEDI( vm_t *vm ) 
{
	opcode_t pop = pop1;
	pop1 = OP_UNDEF;

	if ( jlabel ) 
	{
		EmitString( "8B 07" );		// mov eax, dword ptr [edi]
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI )
		return;

	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI_CALL )
		return;

	if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) // mov [edi], eax
	{	
		compiledOfs -= 2;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		LastCommand = LAST_COMMAND_NONE; 
		return;
	}

	if ( pop == OP_DIVI || pop == OP_DIVU || pop == OP_MULI || pop == OP_MULU ||
		pop == OP_STORE4 || pop == OP_STORE2 || pop == OP_STORE1 ) 
	{	
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EDI_CONST ) // mov dword ptr [edi], 0x12345678
	{	
		compiledOfs -= 6;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		if ( lastConst == 0 ) {
			EmitString( "31 C0" );		// xor eax, eax
		} else {
			EmitString( "B8" );			// mov	eax, 0x12345678
			Emit4( lastConst );
		}
		return;
	}

	EmitString( "8B 07" );		    // mov eax, dword ptr [edi]
}


void EmitMovECXEDI( vm_t *vm ) 
{
	opcode_t pop = pop1;
	pop1 = OP_UNDEF;

	if ( jlabel ) 
	{
		EmitString( "8B 0F" );		// mov ecx, dword ptr [edi]
		pop1 = OP_UNDEF;
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI_CALL ) 
	{
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI )  
	{
		compiledOfs -= 2;			// mov eax, dword ptr [edi]
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitString( "8B 0F" );		// mov ecx, dword ptr [edi]
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) // mov [edi], eax
	{
		compiledOfs -= 2;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}

	if (pop == OP_DIVI || pop == OP_DIVU || pop == OP_MULI || pop == OP_MULU ||
		pop == OP_STORE4 || pop == OP_STORE2 || pop == OP_STORE1 ) 
	{	
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EDI_CONST ) // mov dword ptr [edi], 0x12345678
	{	
		compiledOfs -= 6;			
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitString( "B9" );			// mov ecx, 0x12345678
		Emit4( lastConst );
		return;
	}

	EmitString( "8B 0F" );		    // mov ecx, dword ptr [edi]
}


static void EmitCheckReg( vm_t *vm, int reg, int size )  
{
	int n;

	if ( !( vm_rtChecks->integer & 8 ) )
		return;

#ifdef DEBUG_VM
	EmitString( "50" );			// push eax
	EmitRexString( 0x48, "B8" );// mov eax, &errParam
	EmitPtr( &errParam );
	EmitString( "C7 00" );		// mov [rax], ip-1
	Emit4( ip-1 );
	EmitString( "58" );			// pop eax
#endif

	if ( reg == REG_EAX )
		EmitString( "3D" );		// cmp eax, 0x12345678
	else
		EmitString( "81 F9" );	// cmp ecx, 0x12345678

	Emit4( vm->dataMask - ( size - 1 ) ); // FIXME: use vm->dataSize?

	// error reporting
	EmitString( "0F 87" );		// ja +errorFunction
	n = funcOffset[FUNC_DATA] - compiledOfs;
	Emit4( n - 6 );
}


static int EmitFldEDI( vm_t *vm ) 
{
	if ( jlabel ) 
	{
		EmitString( "D9 07" );		// fld dword ptr [edi]
		return 0;
	}

	if ( LastCommand == LAST_COMMAND_FSTP_EDI )	// fstp dword ptr [edi]
	{ 	
		if ( !vm )
			return 1;				// just report
		compiledOfs -= 2;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		LastCommand = LAST_COMMAND_NONE;
		return 1;
	}

	EmitString( "D9 07" );			// fld dword ptr [edi]
	return 0;
}


#if JUMP_OPTIMIZE
const char *NearJumpStr( int op ) 
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


const char *FarJumpStr( int op, int *n ) 
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


void EmitJump( vm_t *vm, instruction_t *i, int op, int addr ) 
{
	const char *str;
	int v, jump_size;

	v = vm->instructionPointers[ addr ] - compiledOfs;

#if JUMP_OPTIMIZE
	if ( i->njump ) {
		// can happen
		if ( v < -126 || v > 129 ) {
			str = FarJumpStr( op, &jump_size );	
			EmitString( str );
			Emit4( v - 4 - jump_size ); 
			i->njump = 0;
			return;
		}
		EmitString( NearJumpStr( op ) );
		Emit1( v - 2 );
		return;
	}

	if ( pass >= 2 && pass < NUM_PASSES-2 ) {
		if ( v >= -126 && v <= 129 ) {
			EmitString( NearJumpStr( op ) );
			Emit1( v - 2 ); 
			i->njump = 1;
			return;
		}
	}
#endif

	str = FarJumpStr( op, &jump_size );	
	EmitString( str );
	Emit4( v - 4 - jump_size );
}


void EmitFloatJump( vm_t *vm, instruction_t *i, int op, int addr ) 
{
	switch ( op ) {
		case OP_EQF:
			EmitString( "80 E4 40" );	// and ah,0x40
			EmitJump( vm, i, OP_NE, addr );
			break;			

		case OP_NEF:
			EmitString( "80 E4 40" );	// and ah,0x40
			EmitJump( vm, i, OP_EQ, addr );
			break;			

		case OP_LTF:
			EmitString( "80 E4 01" );	// and ah,0x01
			EmitJump( vm, i, OP_NE, addr );
			break;			

		case OP_LEF:
			EmitString( "80 E4 41" );	// and ah,0x41
			EmitJump( vm, i, OP_NE, addr );
			break;			

		case OP_GTF:
			EmitString( "80 E4 41" );	// and ah,0x41
			EmitJump( vm, i, OP_EQ, addr );
			break;			

		case OP_GEF:
			EmitString( "80 E4 01" );	// and ah,0x01
			EmitJump( vm, i, OP_EQ, addr );
			break;			
	};

}


void EmitCallAddr( vm_t *vm, int addr ) 
{
	int v;
	v = vm->instructionPointers[ addr ] - compiledOfs;
	EmitString( "E8" );
	Emit4( v - 5 ); 
}


void EmitCallOffset( func_t Func ) 
{
	int  v;
	v = funcOffset[ Func ] - compiledOfs;
	EmitString( "E8" );		// call +funcOffset[ Func ]
	Emit4( v - 5 );
}


#ifdef __linux__
#define SHADOW_BASE 8
#else
#define SHADOW_BASE 40
#endif
#define PUSH_STACK  32
#define PARAM_STACK 128


void EmitCallFunc( vm_t *vm ) 
{
	static int sysCallOffset = 0;
	int n;

	EmitString( "85 C0" );				// test eax, eax
	EmitString( "7C" );					// jl +offset (SystemCall) 
	Emit1( sysCallOffset );				// will be valid after first pass
sysCallOffset = compiledOfs;				

	// jump target range check
	if ( vm_rtChecks->integer & 4 ) {
		EmitString( "3D" );					// cmp eax, vm->instructionCount
		Emit4( vm->instructionCount );
		EmitString( "0F 83" );				// jae +funcOffset[FUNC_ERRJ]
		n = funcOffset[FUNC_ERRJ] - compiledOfs;
		Emit4( n - 6 );
	}

	EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4

	// save proc base and programStack
	EmitString( "55" );				// push ebp
	EmitString( "56" );				// push esi

	// calling another vm function
#if idx64
	EmitString( "41 FF 14 C0" );		// call dword ptr [r8+rax*8]
#else
	EmitString( "8D 0C 85" );			// lea ecx, [vm->instructionPointers+eax*4]
	EmitPtr( vm->instructionPointers );
	EmitString( "FF 11" );				// call dword ptr [ecx]
#endif

	// restore proc base and programStack so there is 
	// no need to validate programStack anymore
	EmitString( "5E" );				// pop esi
	EmitString( "5D" );				// pop ebp

	EmitString( "C3" );				// ret

sysCallOffset = compiledOfs - sysCallOffset;

	// systemCall:
	// convert negative num to system call number
	// and store right before the first arg
	EmitString( "F7 D0" );          // not eax

	EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4

	// we may jump here from ConstOptimize() also
funcOffset[FUNC_SYSC] = compiledOfs;

#if idx64
	// allocate stack for shadow(win32)+parameters
	EmitString( "48 81 EC" );				// sub rsp, 200
	Emit4( SHADOW_BASE + PUSH_STACK + PARAM_STACK );

	// save scratch registers
	EmitString( "48 8D 54 24" );			// lea rdx, [rsp+SHADOW_BASE]
	Emit1( SHADOW_BASE );
	EmitString( "48 89 32" );				// mov [rdx+00], rsi
	EmitString( "48 89 7A 08" );			// mov [rdx+08], rdi
	EmitString( "4C 89 42 10" );			// mov [rdx+16], r8
	EmitString( "4C 89 4A 18" );			// mov [rdx+24], r9

	// ecx = &int64_params[0]
	EmitString( "48 8D 4C 24" );			// lea rcx, [rsp+SHADOW_BASE+PUSH_STACK]
	Emit1( SHADOW_BASE + PUSH_STACK );

	// save syscallNum
	EmitString( "48 89 01" );				// mov [rcx], rax

	// vm->programStack = programStack - 4;
	EmitString( "48 BA" );					// mov rdx, &vm->programStack
	EmitPtr( &vm->programStack );
	EmitString( "8D 46 FC" );				// lea eax, [esi-4]
	EmitString( "89 02" );					// mov [rdx], eax
	//EmitString( "89 32" );				// mov dword ptr [rdx], esi

	// params = (vm->dataBase + programStack + 8);
	EmitString( "48 8D 74 33 08" );			// lea rsi, [rbx+rsi+8]

	// rcx = &int64_params[1]
	EmitString( "48 83 C1 08" );			// add rcx, 8

	// dest_params[1-15] = params[1-15];
	EmitString( "31 D2" );					// xor edx, edx
	// loop
	EmitString( "48 63 04 96" );			// movsxd rax, dword [rsi+rdx*4]
	EmitString( "48 89 04 D1" );			// mov qword ptr[rcx+rdx*8], rax
	EmitString( "48 83 C2 01" );			// add rdx, 1
	EmitString( "48 83 FA" );				// cmp rdx, 15
	Emit1( (PARAM_STACK/8) - 1 );
	EmitString( "7C EE" );					// jl -18

#ifdef __linux__
	// rdi = &int64_params[0]
	EmitString( "48 8D 79 F8" );			// lea rdi, [rcx-8]
#else
	// rcx = &int64_params[0]
	EmitString( "48 83 E9 08" );			// sub rcx, 8
#endif
	
	// currentVm->systemCall( param );
	EmitString( "41 FF 14 24" );			// call qword [r12]

	// restore registers
	EmitString( "48 8D 54 24" );			// lea rdx, [rsp+SHADOW_BASE]
	Emit1( SHADOW_BASE );
	EmitString( "48 8B 32" );				// mov rsi, [rdx+00]
	EmitString( "48 8B 7A 08" );			// mov rdi, [rdx+08]
	EmitString( "4C 8B 42 10" );			// mov r8,  [rdx+16]
	EmitString( "4C 8B 4A 18" );			// mov r9,  [rdx+24]

	// we added the return value: *(opstack+1) = eax
	EmitAddEDI4( vm );						// add edi, 4
	EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov [edi], eax

	// return stack
	EmitString( "48 81 C4" );				// add rsp, 200
	Emit4( SHADOW_BASE + PUSH_STACK + PARAM_STACK );

	EmitRexString( 0x48, "8D 2C 33" );		// lea rbp, [rbx+rsi]

	EmitString( "C3" );						// ret

#else // i386

	// function prologue
	EmitString( "55" );					// push ebp
	EmitRexString( 0x48, "89 E5" );		// mov ebp, esp
	EmitRexString( 0x48, "83 E4 F0" );	// and esp, -16
	//EmitString( "56" );				// push esi
	//EmitString( "57" );				// push edi
	
	// save syscallNum
	EmitString( "89 C1" );				// mov ecx, eax

	// currentVM->programStack = programStack - 4;
	EmitString( "8D 46 FC" );			// lea eax, [esi-4]
	EmitString( "A3" );					// mov [vm->programStack], eax 
	EmitPtr( &vm->programStack );

	// params = (int *)((byte *)currentVM->dataBase + programStack + 4);
	EmitString( "8D 44 33 04" );		// lea eax, [ebx+esi+4]  

	// params[0] = syscallNum
	EmitString( "89 08" );				// mov [eax], ecx

	// cdecl - push params
	EmitString( "50" );					// push eax

	// currentVm->systemCall( param );
	EmitString( "FF 15" );				// call dword ptr [&currentVM->systemCall]
	EmitPtr( &vm->systemCall );
	
	// cdecl - pop params
	EmitString( "83 C4 04" );			// add esp, 4

	//EmitString( "5F" );				// pop edi
	//EmitString( "5E" );				// pop esi
	
	// we added the return value: *(opstack+1) = eax
	EmitAddEDI4( vm );							// add edi, 4
	EmitCommand( LAST_COMMAND_MOV_EDI_EAX );	// mov [edi], eax

	// function epilogue
	EmitRexString( 0x48, "89 EC" );		// mov esp, ebp
	EmitString( "5D" );					// pop ebp
	EmitString( "C3" );					// ret
#endif
}


void EmitFTOLFunc( vm_t *vm ) 
{
	EmitRexString( 0x48, "B8" );// mov eax, &fp_cw[0]
	EmitPtr( &fp_cw[0] );		
	EmitString( "9B D9 38" );	// fnstcw word ptr [eax]
	EmitString( "D9 68 04" );	// fldcw word ptr [eax+4]
	EmitString( "DB 1F" );		// fistp dword ptr [edi]
	EmitString( "D9 28" );		// fldcw word ptr [eax]
	EmitString( "C3" );			// ret
}


#if BCPY_PTR
void EmitBCPYFunc( vm_t *vm ) 
{
	// FIXME: range check
	EmitString( "56" );						// push esi
	EmitString( "57" );						// push edi
	EmitString( "8B 37" );					// mov esi,[edi] 
	EmitString( "8B 7F FC" );				// mov edi,[edi-4] 
	//EmitString( "B9" );					// mov ecx,0x12345678
	//Emit4( ci->value >> 2 );
	EmitString( "B8" );						// mov eax, datamask
	Emit4( vm->dataMask );
	//EmitString( "BB" );					// mov ebx, database
	//Emit4( (int)vm->dataBase );
	//EmitString( "23 F0" );				// and esi, eax - WTF???
	//EmitString( "23 F8" );				// and edi, eax - WTF???
	EmitString( "21 C6" );					// and esi, eax
	EmitString( "21 C7" );					// and edi, eax
#if idx64
	EmitString( "48 01 DE" );				// add rsi, rbx
	EmitString( "48 01 DF" );				// add rdi, rbx
#else
	EmitString( "03 F3" );					// add esi, ebx
	EmitString( "03 FB" );					// add edi, ebx
#endif
	EmitString( "F3 A5" );					// rep movsd
	EmitString( "5F" );						// pop edi
	EmitString( "5E" );						// pop esi
	EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
	EmitString( "C3" );						// ret
}
#endif


void EmitPSOFFunc( vm_t *vm ) 
{
	EmitRexString( 0x48, "B8" );	// mov eax, badJumpPtr
	EmitPtr( &badStackPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


void EmitOSOFFunc( vm_t *vm ) 
{
	EmitRexString( 0x48, "B8" );	// mov eax, badOptackPtr
	EmitPtr( &badOpStackPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


void EmitBADJFunc( vm_t *vm ) 
{
	EmitRexString( 0x48, "B8" );	// mov eax, badJumpPtr
	EmitPtr( &badJumpPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


void EmitERRJFunc( vm_t *vm ) 
{
	EmitRexString( 0x48, "B8" );	// mov eax, badJumpPtr
	EmitPtr( &errJumpPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


void EmitDATAFunc( vm_t *vm ) 
{
	EmitRexString( 0x48, "B8" );	// mov eax, badDataPtr
	EmitPtr( &badDataPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


/*
=================
EmitFCalc
=================
*/
void EmitFCalc( int op ) 
{
	switch ( op )
	{
		case OP_ADDF: EmitString( "D8 07" ); break; // fadd dword ptr [edi]
		case OP_SUBF: EmitString( "D8 27" ); break;	// fsub dword ptr [edi]
		case OP_MULF: EmitString( "D8 0F" ); break; // fmul dword ptr [edi]
		case OP_DIVF: EmitString( "D8 37" ); break; // fdiv dword ptr [edi]
		default: Com_Error( ERR_DROP, "bad float op" ); break;
	};
}


/*
=================
FloatMerge
=================
*/
static int FloatMerge( instruction_t *curr, instruction_t *next ) 
{
	EmitString( "D9 47 F8" );				// fld dword ptr [edi-8]
	EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
	EmitFCalc( curr->op );
	switch ( next->op ) {
		case OP_ADDF: EmitString( "DE C1" ); break; // faddp
		case OP_SUBF: EmitString( "DE E9" ); break; // fsubp
		case OP_MULF: EmitString( "DE C9" ); break; // fmulp
		case OP_DIVF: EmitString( "DE F9" ); break; // fdivp
		default: Com_Error( ERR_DROP, "bad merge op2" ); break;
	}
	EmitString( "D9 5F F8" );				// fstp dword ptr [edi-8]
	EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
	ip += 1;

	return 1;
}


/*
=================
ConstOptimize
=================
*/
qboolean ConstOptimize( vm_t *vm ) 
{
	int v;
	int op1;

	op1 = ni->op;

	switch ( op1 ) {

	case OP_LOAD4:
		EmitAddEDI4( vm );
		if ( ISS8( ci->value ) ) {
			EmitString( "8B 43" );		// mov eax, dword ptr [ebx+0x7F]
			Emit1( ci->value );
		} else {
			EmitString( "8B 83" );		// mov eax, dword ptr [ebx+0x12345678]
			Emit4( ci->value );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_LOAD2:
		EmitAddEDI4( vm );
		if ( ISS8( ci->value ) ) {
			EmitString( "0F B7 43" );	// movzx eax, word ptr [ebx+0x7F]
			Emit1( ci->value );
		} else {
			EmitString( "0F B7 83" );	// movzx eax, word ptr [ebx+0x12345678]
			Emit4( ci->value );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_LOAD1:
		EmitAddEDI4( vm );
		if ( ISS8( ci->value ) ) {
			EmitString( "0F B6 43" );	// movzx eax, byte ptr [ebx+0x7F]
			Emit1( ci->value );			
		} else {
			EmitString( "0F B6 83" );	// movzx eax, word ptr [ebx+0x12345678]
			Emit4( ci->value );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_STORE4:
		EmitMovEAXEDI( vm ); // FIXME: and eax, (vm->dataMask & ~3)
		if ( !ci->value ) {
			EmitString( "31 C9" );		// xor ecx, ecx
		} else {
			EmitString( "B9" );			// mov	ecx, 0x12345678
			Emit4( ci->value );
		}
		EmitCheckReg( vm, REG_EAX, 4 );
		EmitString( "89 0C 03" );     // mov dword ptr [ebx + eax], ecx
		EmitCommand( LAST_COMMAND_SUB_DI_4 );		// sub edi, 4
		ip += 1;
		return qtrue;

	case OP_STORE2:
		EmitMovEAXEDI( vm ); // FIXME: and eax, (vm->dataMask & ~1)
		if ( !ci->value ) {
			EmitString( "31 C9" );		// xor ecx, ecx
		} else {
			EmitString( "B9" );			// mov	ecx, 0x12345678
			Emit4( ci->value );
		}
		EmitCheckReg( vm, REG_EAX, 2 );
		EmitString( "66 89 0C 03" );   // mov word ptr [ebx + eax], cx
		EmitCommand( LAST_COMMAND_SUB_DI_4 ); // sub edi, 4
		ip += 1;
		return qtrue;

	case OP_STORE1:
		EmitMovEAXEDI( vm ); // FIXME: and eax, (vm->dataMask)
		if ( !ci->value ) {
			EmitString( "31 C9" );		// xor ecx, ecx
		} else {
			EmitString( "B9" );			// mov	ecx, 0x12345678
			Emit4( ci->value );
		}
		EmitCheckReg( vm, REG_EAX, 1 );
		EmitString( "88 0C 03" );		// mov byte ptr [ebx + eax], cl
		EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
		ip += 1;
		return qtrue;
	
	case OP_ADD:
		v = ci->value;
		EmitMovEAXEDI( vm ); 
		if ( ISS8( v ) ) {
			EmitString( "83 C0" );	// add eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "05" );	    // add eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1; // OP_ADD
		return qtrue;

	case OP_SUB:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISS8( v ) ) {
			EmitString( "83 E8" );	// sub eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "2D" );		// sub eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_MULI:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISS8( v ) ) {
			EmitString( "6B C0" );	// imul eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "69 C0" );	// imul eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_LSH:
		v = ci->value;
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 E0" );	// shl eax, 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1; // OP_LSH
		return qtrue;

	case OP_RSHI:
		v = ci->value;
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 F8" );	// sar eax, 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_RSHU:
		v = ci->value;
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 E8" );	// shr eax, 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_BAND:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISU8( v ) ) {
			EmitString( "83 E0" ); // and eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "25" ); // and eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_BOR:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISU8( v ) ) {
			EmitString( "83 C8" ); // or eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "0D" );    // or eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_BXOR:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISU8( v ) ) {
			EmitString( "83 F0" ); // xor eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "35" );    // xor eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_JUMP:
		//JMP();
		EmitJump( vm, ni, ni->op, ci->value );
		ip += 1; // OP_JUMP
		return qtrue;

	case OP_CALL:
		v = ci->value;
		// try to inline some syscalls
		if ( v == ~TRAP_SIN || v == ~TRAP_COS || v == ~TRAP_SQRT ) {
			EmitString( "D9 45 08" );		// fld dword ptr [ebp + 8]
			switch ( v ) {
				case ~TRAP_SQRT: EmitString( "D9 FA" ); break; // fsqrt
				case ~TRAP_SIN: EmitString( "D9 FE" ); break;  // fsin
				case ~TRAP_COS: EmitString( "D9 FF" ); break;  // fcos
			}
			EmitAddEDI4( vm );						// add edi, 4
			EmitCommand( LAST_COMMAND_FSTP_EDI ); 	// fstp dword ptr[edi]
			ip += 1;
			return qtrue;
		}

		if ( v < 0 ) // syscall
		{
			EmitString( "B8" );		// mov eax, 0x12345678
			Emit4( ~v );
			EmitCallOffset( FUNC_SYSC );
			LastCommand = LAST_COMMAND_MOV_EAX_EDI_CALL;
			ip += 1; // OP_CALL
			return qtrue;
		}
#ifdef VM_LOG_SYSCALLS
		EmitString( "C7 86" );    // mov dword ptr [esi+database],0x12345678
		Emit4( (int)vm->dataBase );
		Emit4( ip );
#endif
		EmitString( "55" );	// push ebp
		EmitString( "56" );	// push rsi
		EmitString( "53" );	// push rbx
		EmitCallAddr( vm, v ); // call +addr
		EmitString( "5B" );	// pop rbx
		EmitString( "5E" );	// pop rsi
		EmitString( "5D" );	// pop ebp
		ip += 1; // OP_CALL
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
		EmitMovEAXEDI( vm );
		EmitCommand( LAST_COMMAND_SUB_DI_4 );
		v = ci->value;
		if ( v == 0 && ( op1 == OP_EQ || op1 == OP_NE ) ) {
			EmitString( "85 C0" );       // test eax, eax
		} else {
			if ( ISS8( v ) ) {
				EmitString( "83 F8" );   // cmp eax, 0x7F
				Emit1( v );
			} else {
				EmitString( "3D" );      // cmp eax, 0xFFFFFFFF
				Emit4( v );
			}
		}
		EmitJump( vm, ni, ni->op, ni->value );
		ip += 1; 
		return qtrue;

	default:
		break;
	}

	return qfalse;
}


/*
=================
LocalOptimize
=================
*/
qboolean LocalOptimize( vm_t *vm ) 
{
	int v, n;
	switch ( ci->mop ) 
	{
		//[local] += CONST
		case MOP_ADD4:
			n = inst[ip+2].value;
			v = ci->value; // local variable address
			if ( ISS8( n ) ) {
				if ( ISS8( v ) ) {
					EmitString( "83 45" );	// add dword ptr [ebp + 0x7F], 0x12
					Emit1( v );
					Emit1( n );
				} else {
					EmitString( "83 85" );	// add dword ptr [ebp + 0x12345678], 0x12
					Emit4( v );
					Emit1( n );
				}
			} else {
				if ( ISS8( v ) ) {
					EmitString( "81 45" );	// add dword ptr [ebp + 0x7F], 0x12345678
					Emit1( v );
					Emit4( n );
				} else {
					EmitString( "81 85" );	// add dword ptr [ebp + 0x12345678], 0x12345678
					Emit4( v );
					Emit4( n );
				}
			}
			ip += 5;
			return qtrue;

		//[local] -= CONST
		case MOP_SUB4:
			n = inst[ip+2].value;
			v = ci->value; // local variable address
			if ( ISS8( n ) ) {
				if ( ISS8( v ) ) {
					EmitString( "83 6D" );	// sub dword ptr [ebp + 0x7F], 0x12
					Emit1( v );
					Emit1( n );
				} else {
					EmitString( "83 AD" );	// sub dword ptr [ebp + 0x12345678], 0x12
					Emit4( v );
					Emit1( n );
				}
			} else {
				if ( ISS8( v ) ) {
					EmitString( "81 6D" );	// sub dword ptr [ebp + 0x7F], 0x12345678
					Emit1( v );
					Emit4( n );
				} else {
					EmitString( "81 AD" );	// sub dword ptr[esi+0x12345678], 0x12345678
					Emit4( v );
					Emit4( n );
				}
			}
			ip += 5;
			return qtrue;

		//[local] &= CONST
		case MOP_BAND4:
			n = inst[ip+2].value;
			v = ci->value; // local variable address
			if ( ISS8( n ) ) {
				if ( ISS8( v ) ) {
					EmitString( "83 65" );	// and dword ptr [ebp + 0x7F], 0x12
					Emit1( v );
					Emit1( n );
				} else {
					EmitString( "83 A5" );	// and dword ptr [ebp + 0x12345678], 0x12
					Emit4( v );
					Emit1( n );
				}
			} else {
				if ( ISS8( v ) ) {
					EmitString( "81 65" );	// and dword ptr [ebp + 0x7F], 0x12345678
					Emit1( v );
					Emit4( n );
				} else {
					EmitString( "81 A5" );	// and dword ptr [ebp + 0x12345678], 0x12345678
					Emit4( v );
					Emit4( n );
				}
			}
			ip += 5;
			return qtrue;

		//[local] |= CONST
		case MOP_BOR4:
			n = inst[ip+2].value;
			v = ci->value; // local variable address
			if ( ISS8( n ) ) {
				if ( ISS8( v ) ) {
					EmitString( "83 4D" );	// or dword ptr [ebp + 0x7F], 0x12
					Emit1( v );
					Emit1( n );
				} else {
					EmitString( "83 8D" );	// or dword ptr [ebp + 0x12345678], 0x12
					Emit4( v );
					Emit1( n );
				}
			} else {
				if ( ISS8( v ) ) {
					EmitString( "81 4D" );	// or dword ptr [ebp + 0x7F], 0x12345678
					Emit1( v );
					Emit4( n );
				} else {
					EmitString( "81 8D" );	// or dword ptr [ebp + 0x12345678], 0x12345678
					Emit4( v );
					Emit4( n );
				}
			}
			ip += 5;
			return qtrue;

		// [local] = [local]
		case MOP_IGNORE4:
			ip += 3;
			return qtrue;

	};
	return qfalse;
}


char *VM_LoadInstructions( vmHeader_t *header, instruction_t *buf, 
						  byte *jumpTableTargets, int numJumpTableTargets, int dataLength ) 
{
	static char errBuf[128];
	byte *code_pos, *code_start, *code_end;
	int i, n, v, op0, op1, opStack, pstack;
	instruction_t *ci, *proc;
	int startp, endp;
	
	code_pos = (byte *) header + header->codeOffset;
	code_start = code_pos; // for printing
	code_end =  (byte *) header + header->codeOffset + header->codeLength;

	ci = buf;
	opStack = 0;
	op1 = OP_UNDEF;

	startp = 0;
	endp = header->instructionCount - 1;

	// load instructions and perform some initial calculations/checks
	for ( i = 0; i < header->instructionCount; i++, ci++, op1 = op0 ) {
		op0 = *code_pos;
		if ( op0 < 0 || op0 >= OP_MAX ) {
			sprintf( errBuf, "bad opcode %02X at offset %i", op0,  code_pos - code_start );
			return errBuf;
		}
		n = ops[ op0 ].size;
		if ( code_pos + 1 + n  > code_end ) {
			sprintf( errBuf, "code_pos > code_end" );
			return errBuf;
		}
		code_pos++;
		ci->op = op0;
		if ( n == 4 ) {
			ci->value = LittleLong( *((int*)code_pos) );
			code_pos += 4;
		} else if ( n == 1 ) { 
			ci->value = *((char*)code_pos);
			code_pos += 1;
		} else {
			ci->value = 0;
		}

		// setup jump value from previous const
		if ( op0 == OP_JUMP && op1 == OP_CONST ) {
			ci->value = (ci-1)->value;
		}

		ci->opStack = opStack;
		opStack += ops[ op0 ].stack;

		// check opstack bounds
		if ( opStack < 0 ) {
			sprintf( errBuf, "opStack underflow at %i", i ); 
			return errBuf;
		}
		if ( opStack >= PROC_OPSTACK_SIZE * 4 ) {
			sprintf( errBuf, "opStack overflow at %i", i ); 
			return errBuf;
		}

		// set some bits for easy access
		if ( ops[ op0 ].flags & JUMP ) 
			ci->jump = 1;
		else
			ci->jump = 0;

	//	op1 = op0;
	//	ci++;
	}

	ci = buf;
	pstack = 0;
	op1 = OP_UNDEF;
	proc = NULL;
	
	// Additional security checks

	for ( i = 0; i < header->instructionCount; i++, ci++, op1 = op0  ) {
		op0 = ci->op;

		// function entry
		if ( op0 == OP_ENTER ) {
			// missing block end 
			if ( proc || ( pstack && op1 != OP_LEAVE ) ) {
				sprintf( errBuf, "missing proc end before %i", i ); 
				return errBuf;
			}
			if ( ci->opStack != 0 ) {
				v = ci->opStack;
				sprintf( errBuf, "bad entry opstack %i at %i", v, i ); 
				return errBuf;
			}
			v = ci->value;
			if ( v < 0 || v >= PROGRAM_STACK_SIZE || (v & 3) ) {
				sprintf( errBuf, "bad entry programStack %i at %i", v, i ); 
				return errBuf;
			}
			
			pstack = ci->value;
			
			// mark jump target
			ci->jused = 1;
			proc = ci;
			startp = i + 1;

			// locate endproc
			for ( endp = 0, n = i+1 ; n < header->instructionCount; n++ ) {
				if ( buf[n].op == OP_PUSH && buf[n+1].op == OP_LEAVE ) {
					endp = n;
					break;
				}
			}

			if ( endp == 0 ) {
				sprintf( errBuf, "missing end proc for %i", i ); 
				return errBuf;
			}

			continue;
		}

		// proc opstack will carry max.possible opstack alue
		if ( proc && ci->opStack > proc->opStack ) 
			proc->opStack = ci->opStack;

		// function return
		if ( op0 == OP_LEAVE ) {
			// bad return programStack
			if ( pstack != ci->value ) {
				v = ci->value;
				sprintf( errBuf, "bad programStack %i at %i", v, i ); 
				return errBuf;
			}
			// bad opStack before return
			if ( ci->opStack != 4 ) {
				v = ci->opStack;
				sprintf( errBuf, "bad opStack %i at %i", v, i );
				return errBuf;
			}
			v = ci->value;
			if ( v < 0 || v >= PROGRAM_STACK_SIZE || (v & 3) ) {
				sprintf( errBuf, "bad return programStack %i at %i", v, i ); 
				return errBuf;
			}
			if ( op1 == OP_PUSH ) {
				if ( proc == NULL ) {
					sprintf( errBuf, "unexpected proc end at %i", i ); 
					return errBuf;
				}
				proc = NULL;
				startp = i + 1; // next instruction
				endp = header->instructionCount - 1; // end of the image
			}
			continue;
		}

		// conditional jumps
		if ( ci->jump ) {
			v = ci->value;
			// conditional jumps should have opStack == 8
			if ( ci->opStack != 8 ) {
				sprintf( errBuf, "bad jump opStack %i at %i", ci->opStack, i ); 
				return errBuf;
			}
			//if ( v >= header->instructionCount ) {
			// allow only local proc jumps
			if ( v < startp || v > endp ) {
				sprintf( errBuf, "jump target %i at %i is out of range (%i,%i)", v, i-1, startp, endp );
				return errBuf;
			}
			if ( buf[v].opStack != 0 ) {
				n = buf[v].opStack;
				sprintf( errBuf, "jump target %i has bad opStack %i", v, n ); 
				return errBuf;
			}
			// mark jump target
			buf[v].jused = 1;
			continue;
		}

		// unconditional jumps
		if ( op0 == OP_JUMP ) {
			// jumps should have opStack == 4
			if ( ci->opStack != 4 ) {
				sprintf( errBuf, "bad jump opStack %i at %i", ci->opStack, i ); 
				return errBuf;
			}
			if ( op1 == OP_CONST ) {
				v = buf[i-1].value;
				// allow only local jumps
				if ( v < startp || v > endp ) {
					sprintf( errBuf, "jump target %i at %i is out of range (%i,%i)", v, i-1, startp, endp );
					return errBuf;
				}
				if ( buf[v].opStack != 0 ) {
					n = buf[v].opStack;
					sprintf( errBuf, "jump target %i has bad opStack %i", v, n ); 
					return errBuf;
				}
				if ( buf[v].op == OP_ENTER ) {
					n = buf[v].op;
					sprintf( errBuf, "jump target %i has bad opcode %i", v, n ); 
					return errBuf;
				}
				if ( v == (i-1) ) {
					sprintf( errBuf, "self loop at %i", v ); 
					return errBuf;
				}
				// mark jump target
				buf[v].jused = 1;
			} else {
				if ( proc )
					proc->swtch = 1;
				else
					ci->swtch = 1;
			}
			continue;
		}

		if ( op0 == OP_CALL ) {
			if ( ci->opStack < 4 ) {
				sprintf( errBuf, "bad call opStack at %i", i ); 
				return errBuf;
			}
			if ( op1 == OP_CONST ) {
				v = buf[i-1].value;
				// analyse only local function calls
				if ( v >= 0 ) {
					if ( v >= header->instructionCount ) {
						sprintf( errBuf, "call target %i is out of range", v ); 
						return errBuf;
					}
					if ( buf[v].op != OP_ENTER ) {
						n = buf[v].op;
						sprintf( errBuf, "call target %i has bad opcode %i", v, n );
						return errBuf;
					}
					if ( v == 0 ) {
						sprintf( errBuf, "explicit vmMain call inside VM" );
						return errBuf;
					}
					// mark jump target
					buf[v].jused = 1;
				}
			}
			continue;
		}

		if ( ci->op == OP_ARG ) {
			v = ci->value & 255;
			// argument can't exceed programStack frame
			if ( v < 8 || v > pstack - 4 || (v & 3) ) {
				sprintf( errBuf, "bad argument address %i at %i", v, i );
				return errBuf;
			}
			continue;
		}

		if ( ci->op == OP_LOCAL ) {
			v = ci->value;
			if ( proc == NULL ) {
				sprintf( errBuf, "missing proc frame for local %i at %i", v, i );
				return errBuf;
			}
			if ( (ci+1)->op == OP_LOAD1 || (ci+1)->op == OP_LOAD2 || (ci+1)->op == OP_LOAD4 ) {
				// FIXME: alloc 256 bytes of programStack in VM_CallCompiled()?
				if ( v < 8 || v >= proc->value + 256 ) {
					sprintf( errBuf, "bad local address %i at %i", v, i );
					return errBuf;
				}
			}
		}

		if ( ci->op == OP_LOAD4 && op1 == OP_CONST ) {
			v = (ci-1)->value;
			if ( v < 0 || v > dataLength - 4 ) {
				sprintf( errBuf, "bad load4 address %i at %i", v, i - 1 );
				return errBuf;
			}
		}

		if ( ci->op == OP_LOAD2 && op1 == OP_CONST ) {
			v = (ci-1)->value;
			if ( v < 0 || v > dataLength - 2 ) {
				sprintf( errBuf, "bad load2 address %i at %i", v, i - 1 );
				return errBuf;
			}
		}

		if ( ci->op == OP_LOAD1 && op1 == OP_CONST ) {
			v =  (ci-1)->value;
			if ( v < 0 || v > dataLength - 1 ) {
				sprintf( errBuf, "bad load1 address %i at %i", v, i - 1 );
				return errBuf;
			}
		}

//		op1 = op0;
//		ci++;
	}

	if ( op1 != OP_UNDEF && op1 != OP_LEAVE ) {
		sprintf( errBuf, "missing return instruction at the end of the image" );
		return errBuf;
	}

	// ensure that the optimization pass knows about all the jump table targets
	if ( jumpTableTargets ) {
		for( i = 0; i < numJumpTableTargets; i++ ) {
			n = *(int *)(jumpTableTargets + ( i * sizeof( int ) ) );
			if ( n < 0 || n >= header->instructionCount ) {
				sprintf( errBuf, "jump target %i at %i is out of range [0..%i]", n, i, header->instructionCount - 1 ); 
				return errBuf;
			}
			if ( buf[n].opStack != 0 ) {
				opStack = buf[n].opStack;
				sprintf( errBuf, "jump target set on instruction %i with bad opStack %i", n, opStack ); 
				return errBuf;
			}
			buf[n].jused = 1;
		}
	} else {
		v = 0;
		// instructions with opStack > 0 can't be jump labels so its safe to optimize/merge
		for ( i = 0, ci = buf; i < header->instructionCount; i++, ci++ ) {
			if ( ci->op == OP_ENTER ) {
				v = ci->swtch;
				continue;
			}
			// if there is a switch statement in function -
			// mark all potential jump labels
			if ( ci->swtch )
				v = ci->swtch;
			if ( ci->opStack > 0 )
				ci->jused = 0;
			else if ( v )
				ci->jused = 1;
		}
	}

	return NULL;
}


void VM_FindMOps( vmHeader_t *header, instruction_t *buf ) 
{
	int i, v, op0;
	instruction_t *ci;
	
	ci = buf;

	// Search for known macro-op sequences
	i = 0;

	while ( i < header->instructionCount )
	{
		op0 = ci->op;
		if ( op0 == OP_LOCAL ) {
			// OP_LOCAL + OP_LOCAL + OP_LOAD4 + OP_CONST + OP_XXX + OP_STORE4
			if ( (ci+1)->op == OP_LOCAL && ci->value == (ci+1)->value && (ci+2)->op == OP_LOAD4 && (ci+3)->op == OP_CONST && (ci+4)->op != OP_UNDEF && (ci+5)->op == OP_STORE4 ) {
				v = (ci+4)->op;
				if ( v == OP_ADD ) {
					ci->mop = MOP_ADD4;
					ci += 6; i += 6;
					continue;
				}
				if ( v == OP_SUB ) {
					ci->mop = MOP_SUB4;
					ci += 6; i += 6;
					continue;
				}
				if ( v == OP_BAND ) {
					ci->mop = MOP_BAND4;
					ci += 6; i += 6;
					continue;
				}
				if ( v == OP_BOR ) {
					ci->mop = MOP_BOR4;
					ci += 6; i += 6;
					continue;
				}
			}

			// skip useless sequences
			if ( (ci+1)->op == OP_LOCAL && (ci+0)->value == (ci+1)->value && (ci+2)->op == OP_LOAD4 && (ci+3)->op == OP_STORE4 ) {
				ci->mop = MOP_IGNORE4;
				ci += 4; i += 4;
				continue;
			}
		}

		if ( (ops[ ci->op ].flags & CALC) && (ops[(ci+1)->op].flags & CALC) && !(ci+1)->jused ) {
			ci->mop = MOP_CALCF4;
			ci += 2; i += 2;
			continue;
		}

		ci++;
		i++;
	}
}


/*
=================
VM_Compile
=================
*/
qboolean VM_Compile( vm_t *vm, vmHeader_t *header ) {
	int		v, n;
	int		i;
	int     instructionCount;
	char	*errMsg;

	inst = Z_Malloc( (header->instructionCount + 8 ) * sizeof( instruction_t ) );

	errMsg = VM_LoadInstructions( header, inst, vm->jumpTableTargets, vm->numJumpTableTargets, vm->dataLength );

	if ( errMsg ) {
		Z_Free( inst );
		Com_Printf( "VM_CompileX86 error: %s\n", errMsg );
		return qfalse;
	}

	VM_FindMOps( header, inst );

	code = NULL; // we will allocate memory later, after last defined pass

	memset( funcOffset, 0, sizeof( funcOffset ) );

	instructionCount = header->instructionCount;

	for( pass = 0; pass < NUM_PASSES; pass++ ) 
	{
__compile:
	pop1 = OP_UNDEF;
	lastConst = 0;

	// translate all instructions
	ip = 0;
	compiledOfs = 0;
	LastCommand = LAST_COMMAND_NONE;
	
#if idx64
	EmitString( "53" );				// push rbx
	EmitString( "56" );				// push rsi
	EmitString( "57" );				// push rdi
	EmitString( "55" );				// push rbp
	EmitString( "41 54" );			// push r12
	EmitString( "41 55" );			// push r13
	EmitString( "41 56" );			// push r14
	EmitString( "41 57" );			// push r15

	EmitRexString( 0x48, "BB" );	// mov rbx, vm->dataBase
	EmitPtr( vm->dataBase );

	EmitString( "49 B8" );			// mov r8, vm->instructionPointers
	EmitPtr( vm->instructionPointers );

	EmitString( "49 C7 C1" );		// mov r9, vm->dataMask
	Emit4( vm->dataMask );

	EmitString( "49 BC" );			// mov r12, vm->systemCall
	EmitPtr( &vm->systemCall );

	EmitString( "49 C7 C5" );		// mov r13, vm->stackBottom
	Emit4( vm->stackBottom );

	EmitRexString( 0x48, "B8" );	// mov rax, &vm->programStack
	EmitPtr( &vm->programStack );
	EmitString( "8B 30" );			// mov esi, [rax]

	EmitRexString( 0x48, "B8" );	// mov rax, &vm->opStack
	EmitPtr( &vm->opStack );
	EmitRexString( 0x48, "8B 38" );	// mov rdi, [rax]

	EmitRexString( 0x48, "B8" );	// mov rax, &vm->opStackTop
	EmitPtr( &vm->opStackTop );
	EmitString( "4C 8B 30" );		// mov r14, [rax]

#else
	EmitString( "60" );				// pushad
	//EmitString( "53" );		// push ebx
	//EmitString( "56" );		// push esi
	//EmitString( "57" );		// push edi
	//EmitString( "55" );		// push ebp

	EmitRexString( 0x48, "BB" );	// mov ebx, vm->dataBase
	EmitPtr( vm->dataBase );

	EmitString( "8B 35" );			// mov esi, [vm->programStack]
	EmitPtr( &vm->programStack );
	
	EmitString( "8B 3D" );			// mov edi, [vm->opStack]
	EmitPtr( &vm->opStack );
#endif

	EmitCallOffset( FUNC_ENTR );

#if idx64

#ifdef DEBUG_VM
	EmitRexString( 0x48, "B8" );	// mov rax, &vm->programStack
	EmitPtr( &vm->programStack );
	EmitString( "89 30" );			// mov [rax], esi
#endif

	EmitRexString( 0x48, "B8" );	// mov rax, &vm->opStack
	EmitPtr( &vm->opStack );
	EmitRexString( 0x48, "89 38" );	// mov [rax], rdi

	EmitString( "41 5F" );			// pop r15
	EmitString( "41 5E" );			// pop r14
	EmitString( "41 5D" );			// pop r13
	EmitString( "41 5C" );			// pop r12
	EmitString( "5D" );				// pop rbp
	EmitString( "5F" );				// pop rdi
	EmitString( "5E" );				// pop rsi
	EmitString( "5B" );				// pop rbx
#else

#ifdef DEBUG_VM
	EmitString( "89 35" );		// [vm->programStack], esi
	EmitPtr( &vm->programStack );
#endif

	EmitString( "89 3D" );		// [vm->opStack], edi
	EmitPtr( &vm->opStack );

	//EmitString( "5D" );			// pop ebp
	//EmitString( "5F" );			// pop edi
	//EmitString( "5E" );			// pop esi
	//EmitString( "5B" );			// pop ebx
	EmitString( "61" );				// popad
#endif
	
	EmitString( "C3" );			// ret
	
	funcOffset[FUNC_ENTR] = compiledOfs;
	
	while( ip < instructionCount )
	{
		vm->instructionPointers[ ip ] = compiledOfs;

		ci = &inst[ ip++ ];
		ni = &inst[ ip ];

		jlabel = ci->jused;

		if ( jlabel ) {
			pop1 = OP_UNDEF;
			LastCommand = LAST_COMMAND_NONE;
		}
		
		switch ( ci->op ) {

		case OP_UNDEF:
		case OP_IGNORE:
			break;

		case OP_BREAK:
			EmitString( "CC" );			// int 3
			break;

		case OP_ENTER:
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "83 EE" );		// sub	esi, 0x12
				Emit1( v );
			} else {
				EmitString( "81 EE" );		// sub	esi, 0x12345678
				Emit4( v );
			}

			// programStack overflow check
			if ( vm_rtChecks->integer & 1 ) {
#if idx64
				EmitString( "4C 39 EE" );	// cmp	rsi, r13
#else
				EmitString( "81 FE" );		// cmp	esi, vm->stackBottom
				Emit4( vm->stackBottom );
#endif
				EmitString( "0F 82" );		// jb +funcOffset[FUNC_PSOF]
				n = funcOffset[FUNC_PSOF] - compiledOfs;
				Emit4( n - 6 );
			}

			// opStack overflow check
			if ( vm_rtChecks->integer & 2 ) {
				EmitRexString( 0x48, "8D 47" );		// lea eax, [edi+0x7F]
				Emit1( ci->opStack );
#if idx64
				EmitString( "4C 39 F0" );			// cmp rax, r14
#else
				EmitString( "3B 05" );				// cmp eax, [&vm->opStackTop]
				EmitPtr( &vm->opStackTop );
#endif
				EmitString( "0F 87" );				// ja +funcOffset[FUNC_OSOF]
				n = funcOffset[FUNC_OSOF] - compiledOfs;
				Emit4( n - 6 );
			}

			EmitRexString( 0x48, "8D 2C 33" ); // lea ebp, [ebx+esi]
			break;

		case OP_CONST:
			
			// we can safely perform optimizations only in case if 
			// we are 100% sure that next instruction is not a jump label
			if ( !ni->jused && ConstOptimize( vm ) )
				break;

			EmitAddEDI4( vm );
			EmitString( "C7 07" );		// mov	dword ptr [edi], 0x12345678
			lastConst = ci->value;
			Emit4( lastConst );
			LastCommand = LAST_COMMAND_MOV_EDI_CONST;
			break;

		case OP_LOCAL:

			if ( ci->mop != MOP_UNDEF && LocalOptimize( vm ) )
				break;
			
			// merge OP_LOCAL + OP_LOAD4
			if ( ni->op == OP_LOAD4 ) {
				EmitAddEDI4( vm );
				v = ci->value;
				if ( ISS8( v ) ) {
					EmitString( "8B 45" );	// mov eax, dword ptr [ebp + 0x7F]
					Emit1( v );
				} else {
					EmitString( "8B 85" );	// mov eax, dword ptr [ebp + 0x12345678]
					Emit4( v );
				}
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				ip++;
				break;
			}

			// merge OP_LOCAL + OP_LOAD2
			if ( ni->op == OP_LOAD2 ) {
				EmitAddEDI4( vm );
				v = ci->value;
				if ( ISS8( v ) ) {
					EmitString( "0F B7 45" );	// movzx eax, word ptr [ebp + 0x7F]
					Emit1( v );
				} else {
					EmitString( "0F B7 85" );	// movzx eax, word ptr [ebp + 0x12345678]
					Emit4( v );
				}
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				ip++;
				break;
			}

			// merge OP_LOCAL + OP_LOAD1
			if ( ni->op == OP_LOAD1 ) {
				EmitAddEDI4( vm );
				v = ci->value;
				if ( ISS8( v ) ) {
					EmitString( "0F B6 45" );		// movzx eax, byte ptr [ebp + 0x7F]
					Emit1( v );
				} else {
					EmitString( "0F B6 85" );		// movzx eax, byte ptr [ebp + 0x12345678]
					Emit4( v );
				}
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				ip++;
				break;
			}

			// TODO: i = j + k;
			// TODO: i = j - k;

			EmitAddEDI4(vm);
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "8D 46" );		// lea eax, [esi + 0x7F]
				Emit1( v );
			} else {
				EmitString( "8D 86" );		// lea eax, [esi + 0x12345678]
				Emit4( v );
			}
			EmitCommand(LAST_COMMAND_MOV_EDI_EAX);		// mov dword ptr [edi], eax
			break;

		case OP_ARG:
			EmitMovEAXEDI( vm );			// mov	eax, dword ptr [edi]
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "89 45" );		// mov	dword ptr [ebp + 0x7F], eax
				Emit1( v );
			} else {
				EmitString( "89 85" );		// mov	dword ptr [ebp + 0x12345678], eax
				Emit4( v );
			}
			EmitCommand( LAST_COMMAND_SUB_DI_4 );		// sub edi, 4
			break;

		case OP_CALL:
#ifdef VM_LOG_SYSCALLS
			EmitString( "C7 86" );		// mov dword ptr [esi + dataBase + 0 ],0x12345678
			Emit4( (int)vm->dataBase );
			Emit4( ip-1 );
#endif
			EmitMovEAXEDI( vm );
			EmitCallOffset( FUNC_CALL );
			break;

		case OP_PUSH:
			EmitAddEDI4(vm);
			break;

		case OP_POP:
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_LEAVE:
#ifdef DEBUG_VM
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "83 C6" );		// add	esi, 0x12
				Emit1( v );
			} else {
				EmitString( "81 C6" );		// add	esi, 0x12345678
				Emit4( v );
			}
#endif
			EmitString( "C3" );				// ret
			break;

		case OP_LOAD4:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				compiledOfs -= 2;
				vm->instructionPointers[ ip-1 ] = compiledOfs;
				EmitCheckReg( vm, REG_EAX, 4 );
				EmitString( "8B 04 03" );						// mov	eax, dword ptr [ebx + eax]
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );		// mov dword ptr [edi], eax
				break;
			}
			EmitMovECXEDI( vm );
			EmitCheckReg( vm, REG_ECX, 4 );
			EmitString( "8B 04 0B" );							// mov	eax, dword ptr [ebx + ecx]
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );			// mov dword ptr [edi], eax
			break;

		case OP_LOAD2:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				compiledOfs -= 2;
				vm->instructionPointers[ ip-1 ] = compiledOfs;
				EmitCheckReg( vm, REG_EAX, 2 );
				EmitString( "0F B7 04 03" );					// movzx eax, word ptr [ebx + eax]
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );		// mov dword ptr [edi], eax
				break;
			}
			EmitMovECXEDI( vm );								
			EmitCheckReg( vm, REG_ECX, 2 );
			EmitString( "0F B7 04 0B" );						// movzx eax, word ptr [ebx + ecx]
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );			// mov dword ptr [edi], eax
			break;

		case OP_LOAD1:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				compiledOfs -= 2;
				vm->instructionPointers[ ip-1 ] = compiledOfs;
				EmitCheckReg( vm, REG_EAX, 1 );
				EmitString( "0F B6 04 03" );				// movzx eax, byte ptr [ebx + eax]
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );	// mov dword ptr [edi], eax
				break;
			}
			EmitMovECXEDI( vm );
			EmitCheckReg( vm, REG_ECX, 1 );
			EmitString( "0F B6 04 0B" );				// movzx eax, byte ptr [ebx + ecx]
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );	// mov dword ptr [edi], eax
			break;

		case OP_STORE4:
			EmitMovEAXEDI( vm );
			EmitString( "8B 4F FC" );					// mov	ecx, dword ptr [edi-4]
			//if ( pop1 != OP_CALL ) {
				//EmitString( "81 E1" );						// and ecx, 0x12345678
				//Emit4( vm->dataMask & ~3 );	
			//}
			EmitCheckReg( vm, REG_ECX, 4 );				
			EmitString( "89 04 0B" );					// mov dword ptr [ebx + ecx], eax
			EmitCommand( LAST_COMMAND_SUB_DI_8 );		// sub edi, 8
			break;

		case OP_STORE2:
			EmitMovEAXEDI( vm );
			EmitString( "8B 4F FC" );					// mov ecx, dword ptr [edi-4]
			EmitCheckReg( vm, REG_ECX, 2 );				// range check
			EmitString( "66 89 04 0B" );				// mov word ptr [ebx + ecx], ax
			EmitCommand( LAST_COMMAND_SUB_DI_8 );		// sub edi, 8
			break;

		case OP_STORE1:
			EmitMovEAXEDI( vm );	
			EmitString( "8B 4F FC" );					// mov	ecx, dword ptr [edi-4]
			EmitCheckReg( vm, REG_ECX, 1 );				// range check
			EmitString( "88 04 0B" );					// mov byte ptr [ebx + ecx], eax
			EmitCommand( LAST_COMMAND_SUB_DI_8 );		// sub edi, 8
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
		case OP_GEU:
			EmitMovEAXEDI( vm );
			EmitCommand( LAST_COMMAND_SUB_DI_8 ); // sub edi, 8
			EmitString( "39 47 04" );			  // cmp dword ptr [edi+4], eax
			EmitJump( vm, ci, ci->op, ci->value );
			break;

		case OP_EQF:
		case OP_NEF:
		case OP_LTF:
		case OP_LEF:
		case OP_GTF:
		case OP_GEF:
			if ( CPU_Flags & CPU_FCOM ) {
				EmitFldEDI( vm );						// fld dword ptr [edi]
				EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
				//EmitString( "D9 47 08" );				// fld dword ptr [edi+8]
				EmitString( "D9 47 04" );				// fld dword ptr [edi+4]
				EmitString( "DF E9" );					// fucomip
				EmitString( "DD D8" );					// fstp st(0)
				EmitJump( vm, ci, ci->op, ci->value );
			} else {
				EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
				EmitString( "D9 47 04" );				// fld dword ptr [edi+4]
				EmitString( "D8 5F 08" );				// fcomp dword ptr [edi+8]
				EmitString( "DF E0" );					// fnstsw ax
				EmitFloatJump( vm, ci, ci->op, ci->value );
			}
			pop1 = OP_UNDEF;
			break;			

		case OP_NEGI:
			EmitMovEAXEDI( vm );
			EmitString( "F7 D8" );		// neg eax
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
			break;

		case OP_ADD:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "01 47 FC" );				// add dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_SUB:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "29 47 FC" );				// sub dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_DIVI:
			EmitString( "8B 47 FC" );	// mov eax,dword ptr [edi-4]
			EmitString( "99" );			// cdq
			EmitString( "F7 3F" );		// idiv dword ptr [edi]
			EmitString( "89 47 FC" );	// mov dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_DIVU:
			EmitString( "8B 47 FC" );	// mov eax,dword ptr [edi-4]
			EmitString( "33 D2" );		// xor edx, edx
			EmitString( "F7 37" );		// div dword ptr [edi]
			EmitString( "89 47 FC" );	// mov dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_MODI:
			EmitString( "8B 47 FC" );	// mov eax,dword ptr [edi-4]
			EmitString( "99" );			// cdq
			EmitString( "F7 3F" );		// idiv dword ptr [edi]
			EmitString( "89 57 FC" );	// mov dword ptr [edi-4],edx
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_MODU:
			EmitString( "8B 47 FC" );	// mov eax,dword ptr [edi-4]
			EmitString( "33 D2" );		// xor edx, edx
			EmitString( "F7 37" );		// div dword ptr [edi]
			EmitString( "89 57 FC" );	// mov dword ptr [edi-4],edx
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_MULI:
#if 0
			EmitString( "8B 47 FC" );	// mov eax,dword ptr [edi-4]
			EmitString( "F7 2F" );		// imul dword ptr [edi]
#else
			EmitMovEAXEDI( vm );		// mov eax, dword ptr [edi]
			EmitString( "F7 6F FC" );	// imul eax, dword ptr [edi-4]
#endif
			EmitString( "89 47 FC" );	// mov dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_MULU:
			EmitString( "8B 47 FC" );	// mov eax,dword ptr [edi-4]
			EmitString( "F7 27" );		// mul dword ptr [edi]
			EmitString( "89 47 FC" );	// mov dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_BAND:
			EmitMovEAXEDI(vm);			// mov eax, dword ptr [edi]
			EmitString( "21 47 FC" );	// and dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_BOR:
			EmitMovEAXEDI(vm);			// mov eax, dword ptr [edi]
			EmitString( "09 47 FC" );	// or dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_BXOR:
			EmitMovEAXEDI(vm);			// mov eax, dword ptr [edi]
			EmitString( "31 47 FC" );	// xor dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_BCOM:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "F7 D0" );					// not eax
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
			break;

		case OP_LSH:
			EmitMovECXEDI( vm );
			EmitString( "D3 67 FC" );				// shl dword ptr [edi-4], cl
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_RSHI:
			EmitMovECXEDI( vm );
			EmitString( "D3 7F FC" );				// sar dword ptr [edi-4], cl
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_RSHU:
			EmitMovECXEDI( vm );
			EmitString( "D3 6F FC" );				// shr dword ptr [edi-4], cl
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_NEGF:
			EmitFldEDI( vm );						// fld dword ptr [edi]
			EmitString( "D9 E0" );					// fchs
			EmitCommand( LAST_COMMAND_FSTP_EDI );	// fstp dword ptr [edi]
			break;

		case OP_ADDF:
		case OP_SUBF:
		case OP_DIVF:
		case OP_MULF:
			if ( ci->mop == MOP_CALCF4 ) {
				FloatMerge( ci, ni );
				break;
			}
			EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
			EmitFCalc( ci->op );					// fadd|fsub|fmul|fdiv dword ptr [edi]
			EmitString( "D9 5F FC" );				// fstp dword ptr [edi-4]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_CVIF:
			EmitString( "DB 07" );					// fild dword ptr [edi]
			EmitCommand( LAST_COMMAND_FSTP_EDI );	// fstp dword ptr [edi]
			break;

		case OP_CVFI:
			EmitFldEDI( vm );			// fld dword ptr [edi]
			if ( CPU_Flags & CPU_SSE3 ) {
				// fast sse3 truncation
				EmitString( "DB 0F" );	// fisttp dword ptr [edi]
			} else {
				// call the library conversion function
				EmitCallOffset( FUNC_FTOL );
			}
			break;

		case OP_SEX8:
			EmitString( "0F BE 07" );	// movsx eax, byte ptr [edi]
			EmitCommand(LAST_COMMAND_MOV_EDI_EAX);		// mov dword ptr [edi], eax
			break;

		case OP_SEX16:
			EmitString( "0F BF 07" );	// movsx eax, word ptr [edi]
			EmitCommand(LAST_COMMAND_MOV_EDI_EAX);		// mov dword ptr [edi], eax
			break;

		case OP_BLOCK_COPY:
#if	BCPY_PTR
			EmitString( "B9" );			// mov ecx, 0x12345678
			Emit4( ci->value >> 2 );
			EmitCallOffset( FUNC_BCPY );
#else
			// FIXME: range check
			EmitString( "56" );			// push esi
			EmitString( "57" );			// push edi
			EmitString( "8B 37" );		// mov esi,[edi] 
			EmitString( "8B 7F FC" );	// mov edi,[edi-4] 
			EmitString( "B9" );			// mov ecx,0x12345678
			Emit4( ci->value >> 2 );
			EmitString( "B8" );			// mov eax, datamask
			Emit4( vm->dataMask );
			//EmitString( "BB" );			// mov ebx, database
			//Emit4( (int)vm->dataBase );
			EmitString( "23 F0" );		// and esi, eax
			EmitString( "03 F3" );		// add esi, ebx
			EmitString( "23 F8" );		// and edi, eax
			EmitString( "03 FB" );		// add edi, ebx
			EmitString( "F3 A5" );		// rep movsd
			EmitString( "5F" );			// pop edi
			EmitString( "5E" );			// pop esi
			EmitCommand( LAST_COMMAND_SUB_DI_8 );		// sub edi, 8
#endif
			break;

		 // FIXME: allow jump withing local function scope only
		case OP_JUMP:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4

			// jump target range check
			if ( vm_rtChecks->integer & 4 ) {
				EmitString( "3D" );						// cmp eax, 0x12345678
				Emit4( vm->instructionCount );
				EmitString( "0F 83" );					// jae +funcOffset[FUNC_BADJ]
				n = funcOffset[FUNC_BADJ] - compiledOfs;
				Emit4( n - 6 );
			}
#if idx64
				EmitString( "41 FF 24 C0" );			// jmp dword ptr [r8 + rax*8]
#else
				EmitString( "FF 24 85" );				// jmp dword ptr [instructionPointers + eax * 4]
				EmitPtr( vm->instructionPointers );
#endif
			break;

		default:
			Com_Error( ERR_FATAL, "VM_CompileX86: bad opcode %02X", ci->op );
			VM_FreeBuffers();
			return qfalse;
		}

		pop1 = ci->op;
	} // while( ip < header->instructionCount )

		// ****************
		// system functions
		// ****************

		funcOffset[FUNC_CALL] = compiledOfs;
		EmitCallFunc( vm );

		funcOffset[FUNC_FTOL] = compiledOfs;
#if FTOL_PTR
		EmitFTOLFunc( vm );
#endif

		funcOffset[FUNC_BCPY] = compiledOfs;
#if BCPY_PTR
		EmitBCPYFunc( vm );
#endif
		// ***************
		// error functions
		// ***************

		// bad jump
		funcOffset[FUNC_BADJ] = compiledOfs;
		EmitBADJFunc( vm );

		// error jump
		funcOffset[FUNC_ERRJ] = compiledOfs;
		EmitERRJFunc( vm );

		// programStack overflow
		funcOffset[FUNC_PSOF] = compiledOfs;
		EmitPSOFFunc( vm );

		// opStack overflow
		funcOffset[FUNC_OSOF] = compiledOfs;
		EmitOSOFFunc( vm );

		// read/write access violation
		funcOffset[FUNC_DATA] = compiledOfs;
		EmitDATAFunc( vm );


	} // for( pass = 0; pass < n; pass++ )

	if ( code == NULL ) {
		code = VM_Alloc_Compiled( vm, compiledOfs );
		if ( code == NULL ) {
			return qfalse;
		}
		pass = NUM_PASSES-1;
		goto __compile;
	}

#ifdef VM_X86_MMAP
	if ( mprotect( vm->codeBase.ptr, compiledOfs, PROT_READ|PROT_EXEC ) ) {
		VM_Destroy_Compiled( vm );
		Com_Error( ERR_FATAL, "VM_CompileX86: mprotect failed" );
		return qfalse;
	}
#elif _WIN32
	{
		DWORD oldProtect = 0;
		
		// remove write permissions.
		if ( !VirtualProtect( vm->codeBase.ptr, compiledOfs, PAGE_EXECUTE_READ, &oldProtect ) ) {
			VM_Destroy_Compiled( vm );
			Com_Error( ERR_FATAL, "VM_CompileX86: VirtualProtect failed" );
			return qfalse;
		}
	}
#endif
	
	// offset all the instruction pointers for the new location
	for ( i = 0 ; i < header->instructionCount ; i++ ) {
		if ( !inst[i].jused ) {
			vm->instructionPointers[i] = (intptr_t)badJumpPtr;
			continue;
		}
		vm->instructionPointers[i] += (intptr_t)vm->codeBase.ptr;
	}

	VM_FreeBuffers();

	vm->destroy = VM_Destroy_Compiled;

	Com_Printf( "VM file %s compiled to %i bytes of code\n", vm->name, compiledOfs );

	return qtrue;
}


/*
=================
VM_Alloc_Compiled
=================
*/
void *VM_Alloc_Compiled( vm_t *vm, int codeLength ) 
{
	vm->codeLength = codeLength;
#ifdef VM_X86_MMAP
	vm->codeBase.ptr = mmap( NULL, vm->codeLength, PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0 );
	if ( vm->codeBase.ptr == MAP_FAILED ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: can't mmap memory" );
		return NULL;
	}
#elif _WIN32
	// allocate memory with EXECUTE permissions under windows.
	vm->codeBase.ptr = VirtualAlloc( NULL, vm->codeLength, MEM_COMMIT, PAGE_EXECUTE_READWRITE );
	if ( !vm->codeBase.ptr ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: VirtualAlloc failed" );
		return NULL;
	}
#else
	vm->codeBase.ptr = malloc( vm->codeLength );
	if ( !vm->codeBase.ptr ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: malloc failed" );
		return NULL;
	}
#endif
	return vm->codeBase.ptr;
}


void VM_Destroy_Compiled( vm_t* vm )
{
#ifdef VM_X86_MMAP
	munmap( vm->codeBase.ptr, vm->codeLength );
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

#define CALL_PSTACK 48

int	VM_CallCompiled( vm_t *vm, int *args )
{
	int		opStack[MAX_OPSTACK_SIZE];
	int		stackOnEntry;
	int		*image;
	vm_t	*oldVM;
	int		*oldOpTop;

	oldVM = currentVM;
	currentVM = vm;

	// we might be called recursively, so this might not be the very top
	stackOnEntry = vm->programStack;
	oldOpTop = vm->opStackTop;
	vm->programStack -= CALL_PSTACK;

	// set up the stack frame 
	image = (int*)( vm->dataBase + vm->programStack );
	//image[11] = args[9];
	//image[10] = args[8];
	//image[9] = args[7];
	//image[8] = args[6];
	image[7] = args[5];
	image[6] = args[4];
	image[5] = args[3];
	image[4] = args[2];
	image[3] = args[1];
	image[2] = args[0];
	//image[1] =  0;	// return stack
	//image[0] = -1;	// will terminate loop on return

	vm->opStack = opStack;
	vm->opStackTop = opStack + MAX_OPSTACK_SIZE - 1;

	vm->codeBase.func(); // go into generated code

	if ( vm->opStack != &opStack[1] ) {
		Com_Error( ERR_DROP, "opStack corrupted in compiled code" );
	}

#ifdef DEBUG_VM
	if ( vm->programStack != stackOnEntry - CALL_PSTACK ) {
		Com_Error( ERR_DROP, "programStack corrupted in compiled code" );
	}
#endif

	vm->programStack = stackOnEntry;
	vm->opStackTop = oldOpTop;


	// in case we were recursively called by another vm
	currentVM = oldVM;

	return vm->opStack[0];
}
