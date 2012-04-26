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

//#define VM_DEBUG
//#define VM_LOG_SYSCALLS

#define MAX_OPSTACK 64

static void *VM_Alloc_Compiled( vm_t *vm, int codeLength );
static void VM_Destroy_Compiled( vm_t *vm );

/*

  eax	scratch
  ebx	scratch
  ecx	scratch (required for shifts) | currentVM
  edx	scratch (required for divisions)
  esi	program stack
  edi	opstack

*/

#define OPF_LOCAL      1
#define OPF_JUMP       2
#define OPF_ARGV       4
#define OPF_CALC       8
#define OPF_FLOAT      16

#define ISS8(V) ( (V) >= -128 && (V) <= 127 )

typedef struct {
	const char *name;
	int   size; 
	int	  stack;
	int   flags;
} opcode_info_t ;

opcode_info_t ops[OP_MAX] = 
{
	{ "undef",  0, 0, 0 },
	{ "ignore", 0, 0, 0 },
	{ "break",  0, 0, 0 },

	{ "enter",  4, 0, 0 },
	{ "leave",  4,-4, 0 },
	{ "call",   0, 0, 0 },
	{ "push",   0, 4, 0 },
	{ "pop",    0,-4, 0 },

	{ "const",  4, 4, 0 },
	{ "local",  4, 4, OPF_LOCAL },
	{ "jump",   0,-4, 0 },

	{ "eq",     4,-8, OPF_JUMP },
	{ "ne",     4,-8, OPF_JUMP },

	{ "lti",    4,-8, OPF_JUMP },
	{ "lei",    4,-8, OPF_JUMP },
	{ "gti",    4,-8, OPF_JUMP },
	{ "gei",    4,-8, OPF_JUMP },

	{ "ltu",    4,-8, OPF_JUMP },
	{ "leu",    4,-8, OPF_JUMP },
	{ "gtu",    4,-8, OPF_JUMP },
	{ "geu",    4,-8, OPF_JUMP },

	{ "eqf",    4,-8, OPF_JUMP | OPF_FLOAT },
	{ "nef",    4,-8, OPF_JUMP | OPF_FLOAT },

	{ "ltf",    4,-8, OPF_JUMP | OPF_FLOAT },
	{ "lef",    4,-8, OPF_JUMP | OPF_FLOAT },
	{ "gtf",    4,-8, OPF_JUMP | OPF_FLOAT },
	{ "gef",    4,-8, OPF_JUMP | OPF_FLOAT },

	{ "load1",  0, 0, 0 },
	{ "load2",  0, 0, 0 },
	{ "load4",  0, 0, 0 },
	{ "store1", 0,-8, 0 },
	{ "store2", 0,-8, 0 },
	{ "store4", 0,-8, 0 },
	{ "arg",    1,-4, OPF_ARGV },
	{ "bcopy",  4,-8, 0 },

	{ "sex8",   0, 0, 0 },
	{ "sex16",  0, 0, 0 },

	{ "negi",  0, 0, 0 },
	{ "add",   0,-4, 0 },
	{ "sub",   0,-4, 0 },
	{ "divi",  0,-4, 0 },
	{ "divu",  0,-4, 0 },
	{ "modi",  0,-4, 0 },
	{ "modu",  0,-4, 0 },
	{ "muli",  0,-4, 0 },
	{ "mulu",  0,-4, 0 },

	{ "band",  0,-4, 0 },
	{ "bor",   0,-4, 0 },
	{ "bxor",  0,-4, 0 },
	{ "bcom",  0, 0, 0 },

	{ "lsh",   0,-4, 0 },
	{ "rshi",  0,-4, 0 },
	{ "rshu",  0,-4, 0 },

	{ "negf",  0, 0, 0 },
	{ "addf",  0,-4, OPF_CALC | OPF_FLOAT },
	{ "subf",  0,-4, OPF_CALC | OPF_FLOAT },
	{ "divf",  0,-4, OPF_CALC | OPF_FLOAT },
	{ "mulf",  0,-4, OPF_CALC | OPF_FLOAT },

	{ "cvif",  0, 0, 0 },
	{ "cvfi",  0, 0, 0 }
};


typedef struct {
	int   value;    // 32
	byte  op;	 	// 8
	byte  opStack;  // 8
	int jused:1;
	int fp_calc:1;
	int fjump:1;
	int jump:1;
} instruction_t;


static	byte    *code;
static	int     compiledOfs;
static	int     instructionCount;

static  instruction_t *inst = NULL;
static  instruction_t *ci;
static  instruction_t *ni;

#define FTOL_PTR

static int cw0F7F = 0x0F7F; // round towards zero
static int cwCurr = 0;

static	int	ip, pass;
static	int lastArg;
static	int	lastConst;
static	int	pop1;
static	int jlabel;

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

static	ELastCommand	LastCommand;

void ErrJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code outside VM" ); 
	exit(1);
}

void BadJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code at bad location inside VM" ); 
	exit(1);
}

static void (*const errJumpPtr)(void) = ErrJump;
static void (*const badJumpPtr)(void) = BadJump;

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


static int Hex( int c ) 
{
	if ( c >= 'a' && c <= 'f' ) {
		return 10 + c - 'a';
	}
	if ( c >= 'A' && c <= 'F' ) {
		return 10 + c - 'A';
	}
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
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
			EmitString( "83 EF 04" );	// sub edi, 4
			break;

		case LAST_COMMAND_SUB_DI_8:
			EmitString( "83 EF 08" );	// sub edi, 8
			break;

		case LAST_COMMAND_FSTP_EDI:
			EmitString( "D9 1F" );		// fstp dword ptr [edi]
			break;

		default:
			break;
	}
	LastCommand = command;
}


static void EmitAddEDI4( vm_t *vm ) {
	if ( jlabel ) {
		EmitString( "83 C7 04" );	//	add edi,4
		return;
	}
	if ( LastCommand == LAST_COMMAND_SUB_DI_4 ) 
	{	// sub edi, 4
		compiledOfs -= 3;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		LastCommand = LAST_COMMAND_NONE;
		return;
	}
	if ( LastCommand == LAST_COMMAND_SUB_DI_8 ) 
	{	// sub edi, 8
		compiledOfs -= 3;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitCommand(LAST_COMMAND_SUB_DI_4);
		return;
	}
	EmitString( "83 C7 04" );	//	add edi,4
}

static int EmitMovEAXEDI(vm_t *vm) 
{
	if ( jlabel ) {
		if ( !vm )
			return 0;
		EmitString( "8B 07" );		// mov eax, dword ptr [edi]
		return 0;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI )  {
		return 1;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI_CALL ) {
		return 1;
	}
	if (LastCommand == LAST_COMMAND_MOV_EDI_EAX) 
	{	// mov [edi], eax
		if ( !vm )
			return 1;
		compiledOfs -= 2;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		LastCommand = LAST_COMMAND_NONE; 
		return 1;
	}
	if (pop1 == OP_DIVI || pop1 == OP_DIVU || pop1 == OP_MULI || pop1 == OP_MULU ||
		pop1 == OP_STORE4 || pop1 == OP_STORE2 || pop1 == OP_STORE1 ) 
	{	
		return 1;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EDI_CONST ) 
	{	// mov dword ptr [edi], 0x12345678
		if ( !vm )
			return 2;
		compiledOfs -= 6;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		if ( lastConst == 0 ) {
			EmitString( "31 C0" );		// xor eax, eax
		} else {
			EmitString( "B8" );			// mov	eax, 0x12345678
			Emit4( lastConst );
		}
		return 2;
	}
	if ( !vm )
		return 1;
	EmitString( "8B 07" );		    // mov eax, dword ptr [edi]
	return 1;
}


void EmitMovECXEDI( vm_t *vm ) 
{
	if ( jlabel ) {
		EmitString( "8B 0F" );		// mov ecx, dword ptr [edi]
		return;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI_CALL ) {
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI )  {
#if 1
		EmitString( "89 C1" );		// mov ecx, eax // FIXME: mov ecx, dword ptr [edi]
#else
		compiledOfs -= 2;			// mov eax, dword ptr [edi]
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitString( "8B 0F" );		// mov ecx, dword ptr [edi]
#endif
		return;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) // mov [edi], eax
	{
		compiledOfs -= 2;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}
	if (pop1 == OP_DIVI || pop1 == OP_DIVU || pop1 == OP_MULI || pop1 == OP_MULU ||
		pop1 == OP_STORE4 || pop1 == OP_STORE2 || pop1 == OP_STORE1 ) 
	{	
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}
	EmitString( "8B 0F" );		    // mov ecx, dword ptr [edi]
}


static qboolean EmitMovEBXEDI(vm_t *vm, int andit) {
	if ( jlabel ) {
		EmitString( "8B 1F" );		// mov ebx, dword ptr [edi]
		return qfalse;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI_CALL ) {
		EmitString( "89 C3" );		// mov ebx, eax
		return qfalse;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) 
	{	// mov dword ptr [edi], eax
		compiledOfs -= 2;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitString( "89 C3" );		// mov ebx, eax
		return qfalse;
	}
	if ( pop1 == OP_DIVI || pop1 == OP_DIVU || pop1 == OP_MULI || pop1 == OP_MULU ||
		pop1 == OP_STORE4 || pop1 == OP_STORE2 || pop1 == OP_STORE1 ) 
	{	
		EmitString( "89 C3" );		// mov ebx, eax
		return qfalse;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EDI_CONST ) 
	{	// mov dword ptr [edi], 0x12345678
		compiledOfs -= 6;
		vm->instructionPointers[ ip-1 ] = compiledOfs;
		EmitString( "BB" );			// mov	ebx, 0x12345678
		if (andit) {
			Emit4( lastConst & andit );
		} else {
			Emit4( lastConst );
		}
		return qtrue;
	}

	EmitString( "8B 1F" );		    // mov ebx, dword ptr [edi]
	return qfalse;
}


static int EmitFldEDI( vm_t *vm ) {
	if ( jlabel ) {
		EmitString( "D9 07" );		// fld dword ptr [edi]
		return 0;
	}
	if ( LastCommand == LAST_COMMAND_FSTP_EDI ) { 	
		// fstp dword ptr [edi]
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


const char *JumpStr( int op, int *n ) 
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
		case OP_CALL: *n = 1; return "E8";   // call
	};
	return NULL;
}


void EmitJump( vm_t *vm, instruction_t *i, int addr ) 
{
	const char *str;
	int v, jump_size;

	v = vm->instructionPointers[ addr ] - compiledOfs;
	
	str = JumpStr( i->op, &jump_size );	
	v -= ( jump_size + 4 );
	EmitString( str );
	Emit4( v ); 
}


void EmitCall( vm_t *vm ) 
{
	EmitString( "8B 07" );			// mov eax, dword ptr [edi]
	EmitString( "83 EF 04" );		// sub edi, 4
	EmitString( "85 C0" );			// test eax, eax
	EmitString( "7C 1A" );			// jl (SystemCall) +26
	EmitString( "3D" );				// cmp eax, [vm->instructionCount]
	Emit4( vm->instructionCount );
	EmitString( "73 0D" );			// jae badAddr (+13)

	// calling another vm function
	EmitString( "C1 E0 02" );		// shl eax, 2
	EmitString( "05" );				// add eax, [vm->instructionPointers]
	Emit4( (int)vm->instructionPointers );
	EmitString( "FF 10" );			// call dword ptr [eax]
	EmitString( "8B 07" );			// mov eax, dword ptr [edi]
	EmitString( "C3" );				// ret

	// badAddr:
	EmitString( "FF 15" );          // call errJumpPtr
	Emit4( (int)&errJumpPtr );

	// systemCall:
	// convert negative num to system call number
	// and store right before the first arg
	EmitString( "F7 D0" );          // not eax
	EmitString( "55" );				// push ebp
	EmitString( "89 E5" );			// mov ebp, esp
	EmitString( "83 E4 F0" );		// and esp, -16

	EmitString( "56" );				// push esi
	EmitString( "57" );				// push edi
	// save syscallNum
	EmitString( "89 C3" );			// mov ebx, eax

	// currentVM->programStack = programStack - 4;
	EmitString( "8D 46 FC" );		// lea eax, [esi-4]
	EmitString( "A3" );				// mov [currentVM->programStack], eax 
	Emit4( (int)&vm->programStack );

	// params = (int *)((byte *)currentVM->dataBase + programStack + 4);
	EmitString( "B8" );				// mov eax, [currentVM->dataBase] (+ 4)  
	Emit4( (int)vm->dataBase + 4 );
	EmitString( "01 F0" );			// add eax, esi 

	// params[0] = syscallNum
	EmitString( "89 18" );			// mov [eax], ebx
	// cdecl - push params
	EmitString( "50" );				// push eax

	// currentVm->systemCall( param );
#if 0
	EmitString( "FF 51" );		// call	dword ptr [ecx+VM_OFFSET_SYSTEM_CALL] ;
	Emit1( VM_OFFSET_SYSTEM_CALL );
#else
	EmitString( "FF 15" );		// call dword ptr [&currentVM->systemCall]
	Emit4( (int)&vm->systemCall );
#endif

	EmitString( "83 C4 04" );		// add esp, 4
	EmitString( "5F" );				// pop edi
	EmitString( "5E" );				// pop esi

	// we added the return value: *(opstack+1) = eax
	EmitString( "83 C7 04" );		// add edi, 4
	EmitString( "89 07" );			// mov [edi], eax

	EmitString( "89 EC" );			// mov esp, ebp
	EmitString( "5D" );				// pop ebp
	EmitString( "C3" );				// ret
}


void EmitFTOL( vm_t *vm ) 
{
	EmitString( "9B D9 3D" );	// fnstcw word ptr [cwCurr]
	Emit4( (int)&cwCurr );
	//EmitString( "D9 07" );		// fld dword ptr [edi]
	EmitString( "D9 2D" );		// fldcw word ptr [cw0F7F]
	Emit4( (int)&cw0F7F );
	EmitString( "DB 1F" );		// fistp dword ptr [edi]
	EmitString( "D9 2D" );		// fldcw word ptr [cwCurr]
	Emit4( (int)&cwCurr );
	EmitString( "C3" );			// ret
}


#define EMITJMP(S,N) \
	do { \
		v = ci->value; \
		n = vm->instructionPointers[v] - compiledOfs - (N); \
		EmitString( S ); \
		Emit4( n ); \
	} while(0) \


#define JE()   EMITJMP( "0F 84", 6 )
#define JNE()  EMITJMP( "0F 85", 6 )
#define JZ()   EMITJMP( "0F 84", 6 )
#define JNZ()  EMITJMP( "0F 85", 6 )

#define FCOMSF() \
	do { \
		EmitString( "D9 47 04" );	/* fld dword ptr [edi+4] */ \
		EmitString( "D8 5F 08" );	/* fcomp dword ptr [edi+8] */ \
		EmitString( "DF E0" );		/* fnstsw ax */ \
	} while (0)

/*
=================
FloatMerge
=================
*/
static int FloatMerge( instruction_t *curr, instruction_t *next ) 
{
	if ( next->jused || !next->fp_calc ) 
		return 0;

	EmitString( "D9 47 F8" );				// fld dword ptr [edi-8]
	EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
	switch ( curr->op ) {
		case OP_ADDF: EmitString( "D8 07" ); break; // fadd dword ptr [edi]
		case OP_SUBF: EmitString( "D8 27" ); break; // fsub dword ptr [edi]
		case OP_MULF: EmitString( "D8 0F" ); break; // fmul dword ptr [edi]
		case OP_DIVF: EmitString( "D8 37" ); break; // fdiv dword ptr [edi]
	}
	switch ( next->op ) {
		case OP_ADDF: EmitString( "DE C1" ); break; // faddp
		case OP_SUBF: EmitString( "DE E9" ); break; // fsubp
		case OP_MULF: EmitString( "DE C9" ); break; // fmulp
		case OP_DIVF: EmitString( "DE F9" ); break; // fdivp
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
qboolean ConstOptimize( vm_t *vm ) {
	int v, opt;
	int op1;

	// we can safely perform optimizations only in case if 
	// we are 100% sure that next instruction is not a jump label
	if ( !ni->jused )
		op1 = ni->op;
	else
		return qfalse;

	switch ( op1 ) {

	case OP_LOAD4:
		EmitAddEDI4(vm);
		EmitString( "BB" );         // mov ebx, 0x12345678
		Emit4( ( ci->value & vm->dataMask ) + (int)vm->dataBase );
		EmitString( "8B 03" );      // mov eax, dword ptr [ebx]
		EmitCommand(LAST_COMMAND_MOV_EDI_EAX); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_LOAD2:
		EmitAddEDI4(vm);
		EmitString( "BB" );         // mov ebx, 0x12345678
		Emit4( (  ci->value & vm->dataMask ) + (int)vm->dataBase );
		EmitString( "0F B7 03" );   // movzx eax, word ptr [ebx]
		EmitCommand(LAST_COMMAND_MOV_EDI_EAX); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_LOAD1:
		EmitAddEDI4(vm);
		EmitString( "BB" );         // mov ebx, 0x12345678
		Emit4( ( ci->value & vm->dataMask ) + (int)vm->dataBase );
		EmitString( "0F B6 03" );	// movzx eax, byte ptr [ebx]
		EmitCommand(LAST_COMMAND_MOV_EDI_EAX); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_STORE4:
		opt = EmitMovEBXEDI( vm, ( vm->dataMask & ~3 ) );
		EmitString( "B8" );			// mov	eax, 0x12345678
		Emit4( ci->value );
//		if (!opt) {
//			EmitString( "81 E3" );  // and ebx, 0x12345678
//			Emit4( vm->dataMask & ~3 );
//		}
		EmitString( "89 83" );      // mov dword ptr [ebx+0x12345678], eax
		Emit4( (int)vm->dataBase );
		EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
		ip += 1;
		return qtrue;

	case OP_STORE2:
		opt = EmitMovEBXEDI(vm, (vm->dataMask & ~1));
		EmitString( "B8" );			// mov	eax, 0x12345678
		Emit4( ci->value );
//		if (!opt) {
//			EmitString( "81 E3" );  // and ebx, 0x12345678
//			Emit4( vm->dataMask & ~1 );
//		}
		EmitString( "66 89 83" );   // mov word ptr [ebx+0x12345678], eax
		Emit4( (int)vm->dataBase );
		EmitCommand(LAST_COMMAND_SUB_DI_4); // sub edi, 4
		ip += 1;
		return qtrue;

	case OP_STORE1:
		opt = EmitMovEBXEDI(vm, vm->dataMask);
		EmitString( "B8" );			// mov	eax, 0x12345678
		Emit4( ci->value );
//		if (!opt) {
//			EmitString( "81 E3" );	// and ebx, 0x12345678
//			Emit4( vm->dataMask );
//		}
		EmitString( "88 83" );		// mov byte ptr [ebx+0x12345678], eax
		Emit4( (int)vm->dataBase );
		EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
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
		EmitString( "C1 E0" );	// shl dword ptr [edi], 0x12
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
		if ( v >= 0 && v <= 127 ) {
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
		if ( v >= 0 && v <= 127 ) {
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
		if ( v >= 0 && v <= 127 ) {
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
		EmitJump( vm, ni, ci->value );
		ip += 1; // OP_JUMP
		return qtrue;

	case OP_CALL:
		v = ci->value;
		// try to inline some syscalls
		if ( v == ~TRAP_SIN || v == ~TRAP_COS || v == ~TRAP_SQRT ) {
			EmitString( "D9 86" );			// fld dword ptr[esi+database]
			Emit4( lastArg + (int)vm->dataBase );
			switch ( v ) {
				case ~TRAP_SQRT: EmitString( "D9 FA" ); break; // fsqrt
				case ~TRAP_SIN: EmitString( "D9 FE" ); break;  // fsin
				case ~TRAP_COS: EmitString( "D9 FF" ); break;  // fcos
			}
			EmitAddEDI4( vm );						// add edi, 4
			EmitCommand( LAST_COMMAND_FSTP_EDI ); 	// fstp dword ptr[edi]
			EmitCommand( LAST_COMMAND_MOV_EAX_EDI );
			ip += 1;
			return qtrue;
		}
		if ( v < 0 )
			break;
#ifdef VM_LOG_SYSCALLS
		EmitString( "C7 86" );    // mov dword ptr [esi+database],0x12345678
		Emit4( (int)vm->dataBase );
		Emit4( ip );
#endif
		EmitJump( vm, ni, ci->value );
		EmitCommand( LAST_COMMAND_MOV_EAX_EDI );
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
		EmitJump( vm, ni, ni->value );
		ip += 1; 
		return qtrue;

	default:
		break;
	}

	return qfalse;
}


#define LOCALOP(OP) ( ni->op == OP_LOCAL && ci->value == ni->value && inst[ip+1].op == OP_LOAD4 && inst[ip+2].op == OP_CONST && inst[ip+3].op == (OP) && inst[ip+4].op == OP_STORE4 \
	&& !ni->jused && !inst[ip+1].jused && !inst[ip+2].jused && !inst[ip+3].jused )


int VM_LoadInstructions( vm_t *vm, vmHeader_t *header ) 
{
	byte *code, *code_start, *code_end;
	int i, n, v, op0, op1, opStack, pstack;
	instruction_t *ci;
#ifdef VM_DEBUG
	fileHandle_t fh;
	char buf[128];
#endif

	code = (byte *) header + header->codeOffset;
	code_start = code; // for printing
	code_end =  (byte *) header + header->codeOffset + header->codeLength;

	ci = inst;
	opStack = 0;

	// load instructions and perform some initial calculations/checks
	for ( i = 0; i < header->instructionCount; i++ ) {
		op0 = *code;
		if ( op0 < 0 || op0 >= OP_MAX ) 
		{
			VM_FreeBuffers();
			Com_Error( ERR_DROP, "VM_CompileX86: bad opcode %02X at offset %i", 
				op0,  code - code_start );
			return 0;
		}
		n = ops[ op0 ].size;
		if ( code + 1 + n  > code_end ) {
			VM_FreeBuffers();
			Com_Error( ERR_DROP, "VM_CompileX86: code > code_end" );
			return 0;
		}
		code++;
		ci->op = op0;
		if ( n == 1 || n == 4 ) {
			memcpy( &ci->value, code, n ); // FIXME: endianess
			code += n;
		} else {
			ci->value = 0;
		}

		ci->opStack = opStack;
		opStack += ops[ op0 ].stack;

		// check opstack bounds
		if ( opStack < 0 ) {
			VM_FreeBuffers();
			Com_Error( ERR_DROP, "VM_CompileX86: opStack underflow at %i", i ); 
			return 0;
		}
		if ( opStack >= MAX_OPSTACK * 4 ) {
			VM_FreeBuffers();
			Com_Error( ERR_DROP, "VM_CompileX86: opStack overflow at %i", i ); 
			return 0;
		}

		// set some bits for easy access
		if ( ops[ op0 ].flags & OPF_JUMP ) 
			ci->jump = 1;
		else
			ci->jump = 0;

		if ( (ops[ op0 ].flags & (OPF_FLOAT|OPF_JUMP)) == (OPF_FLOAT|OPF_JUMP) )
			ci->fjump = 1;
		else
			ci->fjump = 0;

		if ( (ops[ op0 ].flags & (OPF_FLOAT|OPF_CALC)) == (OPF_FLOAT|OPF_CALC) )
			ci->fp_calc = 1;
		else
			ci->fp_calc = 0;

		ci++;
	}

	ci = inst;

#ifdef VM_DEBUG
	fh = FS_FOpenFileWrite( va( "%s_vm.txt", vm->name ) );

	for ( i = 0; i < header->instructionCount; i++ ) {
		op0 = ci->op;
		if ( op0 == OP_ENTER ) {
			sprintf( buf, "====================\n" );
			FS_Write( buf, strlen( buf ), fh );
			//Com_Printf( "====================\n" );
		}
		if ( ops[op0].size ) {
			sprintf( buf, "%2i %2i %5s %i\n", i, ci->opStack, ops[op0].name, inst[i].value );
		} else {
			sprintf( buf, "%2i %2i %5s\n", i, ci->opStack, ops[op0].name );
		}
		FS_Write( buf, strlen( buf ), fh );

		if ( op0 == OP_LEAVE ) {
			sprintf( buf, "--------------------\n" );
			FS_Write( buf, strlen( buf ), fh );
		}
		ci++;
	}
	FS_FCloseFile( fh );
#endif

	// ensure that the optimisation pass knows about all the jump table targets
	if ( vm->jumpTableTargets ) {
		for( i = 0; i < vm->numJumpTableTargets; i++ ) {
			n = *(int *)(vm->jumpTableTargets + ( i * sizeof( int ) ) );
			if ( (unsigned)n >= header->instructionCount )
				continue;
			if ( inst[n].opStack != 0 ) {
				opStack = inst[n].opStack;
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: jump target set on instruction %i with bad opStack %i", 
					n, opStack ); 
				return 0;
			}
			inst[n].jused = 1;
		}
	} else {
		// instruction with opStack > 0 can't be jump labels so its safe to optimize/merge
		for ( i = 0; i < header->instructionCount; i++ ) {
			if ( inst[i].opStack > 0 )
				inst[i].jused = 0;
			else
				inst[i].jused = 1;
		}
	}

	ci = inst;
	pstack = 0;
	op1 = OP_UNDEF;

	// Additional security checks

	for ( i = 0; i < header->instructionCount; i++ ) {
		op0 = ci->op;

		// function entry
		if ( op0 == OP_ENTER ) {
			// missing block end
			if ( pstack && op1 != OP_LEAVE ) {
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: missing return instruction before %i", i ); 
				return 0;
			}
			if ( ci->opStack != 0 ) {
				v = ci->opStack;
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: bad entry opstack %i at %i", v, i ); 
				return 0;
			}
			pstack = ci->value;
			// mark jump target
			ci->jused = 1;
		}

		// function return
		if ( op0 == OP_LEAVE ) {
			// bad return programStack
			if ( pstack != ci->value ) {
				v = ci->value;
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: bad programStack %i at %i", v, i ); 
				return 0;
			}
			// bad opStack before return
			if ( ci->opStack != 4 ) {
				v = ci->opStack;
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: bad opStack %i at %i", v, i ); 
				return 0;
			}
		}

		// conditional jumps
		if ( ci->jump ) {
			if ( (unsigned)(ci->value) >= vm->instructionCount ) {
				v = ci->value;
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: jump target %i is out of range", v ); 
				return 0;
			}
			if ( inst[ci->value].opStack != 0 ) {
				v = ci->value;
				n = inst[v].opStack;
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: jump target %i has bad opStack %i", v, n ); 
				return 0;
			}
			// mark jump target
			inst[ci->value].jused = 1;
		}

		// unconditional jumps
		if ( op0 == OP_JUMP ) {
			// jumps should have opStack == 4
			if ( ci->opStack != 4 ) {
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: bad jump opStack at %i", i ); 
				return 0;
			}
			if ( op1 == OP_CONST ) {
				v = inst[i-1].value;
				if ( (unsigned)v >= vm->instructionCount ) {
					VM_FreeBuffers();
					Com_Error( ERR_DROP, "VM_CompileX86: jump target %i is out of range", v );
					return 0;
				}
				if ( inst[v].opStack != 0 ) {
					n = inst[v].opStack;
					VM_FreeBuffers();
					Com_Error( ERR_DROP, "VM_CompileX86: jump target %i has bad opStack %i", v, n ); 
					return 0;
				}
				if ( inst[v].op == OP_ENTER ) {
					n = inst[v].op;
					VM_FreeBuffers();
					Com_Error( ERR_DROP, "VM_CompileX86: jump target %i has bad opcode %i", v, n ); 
					return 0;
				}
				// mark jump target
				inst[v].jused = 1;
			}
		}

		if ( op0 == OP_CALL ) {
			if ( ci->opStack < 4 ) {
				VM_FreeBuffers();
				Com_Error( ERR_DROP, "VM_CompileX86: bad call opStack at %i", i ); 
			}
			if ( op1 == OP_CONST ) {
				v = inst[i-1].value;
				// analyse only local function calls
				if ( v >= 0 ) {
					if ( v >= vm->instructionCount ) {
						VM_FreeBuffers();
						Com_Error( ERR_DROP, "VM_CompileX86: call target %i is out of range", v ); 
						return 0;
					}
					if ( inst[v].opStack != 0 ) {
						n = inst[v].opStack;
						VM_FreeBuffers();
						Com_Error( ERR_DROP, "VM_CompileX86: call target %i has bad opStack %i", v, n ); 
						return 0;
					}
					if ( inst[v].op != OP_ENTER ) {
						n = inst[v].op;
						VM_FreeBuffers();
						Com_Error( ERR_DROP, "VM_CompileX86: call target %i has bad opcode %i", v, n ); 
						return 0;
					}
					if ( v == 0 ) {
						VM_FreeBuffers();
						Com_Error( ERR_DROP, "VM_CompileX86: explicit vmMain call inside VM", v ); 
						return 0;
					}
					// mark jump target
					inst[v].jused = 1;
				}
			}
		}

		op1 = op0;
		ci++;
	}

	return 1;
}

enum {
	FUNC_CALL = 0,
	FUNC_FTOL,
	FUNC_LAST
};


/*
=================
VM_Compile
=================
*/
void VM_Compile( vm_t *vm, vmHeader_t *header ) {
	int		op;
	int		v, n;
	int		i;
	int		codeOffset[FUNC_LAST];

	memset( codeOffset, 0, sizeof( codeOffset ) );

	instructionCount = header->instructionCount;

	code = NULL; // we will allocate memory later, after last defined pass

	inst = Z_Malloc( (instructionCount + 8 ) * sizeof( instruction_t ) );
	Com_Memset( inst, 0, (instructionCount + 8 ) * sizeof( instruction_t ) );
	
	VM_LoadInstructions( vm, header );

	for( pass = 0; pass < 3; pass++ ) 
	{
__compile:
	pop1 = OP_UNDEF;
	lastArg = 0;
	lastConst = 0;

	// translate all instructions
	ip = 0;
	compiledOfs = 0;

	LastCommand = LAST_COMMAND_NONE;

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

		op = ci->op;

		if ( ci->fp_calc && FloatMerge( ci, ni) ) {
			pop1 = op;
			continue;
		}

		if ( ci->fjump && CPU_Flags & CPU_FCOM ) {
			EmitFldEDI( vm );							// fld dword ptr [edi]
			EmitCommand( LAST_COMMAND_SUB_DI_8 );		// sub edi, 8

			//EmitString( "D9 47 08" );					// fld dword ptr [edi+8]
			EmitString( "D9 47 04" );					// fld dword ptr [edi+4]
			EmitString( "DF E9" );						// fucomip
			EmitString( "DD D8" );						// fstp st(0)

			EmitJump( vm, ci, ci->value );
			pop1 = OP_UNDEF;
			continue;
		}

		switch ( op ) {

		case OP_UNDEF:
			break;

		case OP_BREAK:
			EmitString( "CC" );			// int 3
			break;

		case OP_ENTER:
			if ( ISS8( ci->value ) ) {
				EmitString( "83 EE" );		// sub	esi, 0x12345678
				Emit1( ci->value );
			} else {
				EmitString( "81 EE" );		// sub	esi, 0x12345678
				Emit4( ci->value );
			}
			break;

		case OP_CONST:
			if ( ConstOptimize( vm ) )
				break;
			EmitAddEDI4(vm);
			EmitString( "C7 07" );		// mov	dword ptr [edi], 0x12345678
			lastConst = ci->value;
			Emit4( lastConst );
			LastCommand = LAST_COMMAND_MOV_EDI_CONST;
			break;

		case OP_LOCAL:

			// [local]++
			if ( LOCALOP( OP_ADD ) ) {
				v = ci->value + (int) vm->dataBase; // local variable address
				n = inst[ip+2].value;
				if ( ISS8( n ) ) {
					EmitString( "83 86" );		// add dword ptr[esi+0x12345678],0x12
					Emit4( v );
					Emit1( n );
				} else {
					EmitString( "81 86" );		// add dword ptr[esi+0x12345678],0x12345678
					Emit4( v );
					Emit4( n );
				}
				ip += 5;
				break;
			}

			// [local]--
			if ( LOCALOP( OP_SUB ) ) {
				v = ci->value + (int) vm->dataBase; // local variable address
				n = inst[ip+2].value;
				if ( ISS8( n ) ){
					EmitString( "83 AE" );		// add dword ptr[esi+0x12345678],0x12
					Emit4( v );
					Emit1( n );
				} else {
					EmitString( "81 AE" );		// add dword ptr[esi+0x12345678],0x12345678
					Emit4( v );
					Emit4( n );
				}
				ip += 5;
				break;
			}

			// merge OP_LOCAL + OP_LOAD4
			if ( ni->op == OP_LOAD4 && !ni->jused ) {
				EmitAddEDI4( vm );
				EmitString( "8B 86" );	// mov eax, dword ptr [esi + LOCAL + vm->dataBase ]
				Emit4( ci->value + (int)vm->dataBase );
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				ip++;
				break;
			}

			// merge OP_LOCAL + OP_LOAD2
			if ( ni->op == OP_LOAD2 && !ni->jused ) {
				EmitAddEDI4( vm );
				EmitString( "0F B7 86" );	// movzx eax, word ptr[esi + LOCAL + vm->dataBase ]
				Emit4( ci->value + (int)vm->dataBase );
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				ip++;
				break;
			}

			// merge OP_LOCAL + OP_LOAD1
			if ( ni->op == OP_LOAD1 && !ni->jused ) {
				EmitAddEDI4( vm );
				EmitString( "0F B6 86" );	// movzx eax, byte ptr[esi + LOCAL + vm->dataBase ]
				Emit4( ci->value + (int)vm->dataBase );
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				ip++;
				break;
			}

			// TODO: i = j + k;
			// TODO: i = j - k;

			EmitAddEDI4(vm);
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "8D 46" );		// lea eax, [0x7F + esi]
				Emit1( v );
			} else {
				EmitString( "8D 86" );		// lea eax, [0x12345678 + esi]
				Emit4( v );
			}
			EmitCommand(LAST_COMMAND_MOV_EDI_EAX);		// mov dword ptr [edi], eax
			break;

		case OP_ARG:
			EmitMovEAXEDI(vm);			// mov	eax,dword ptr [edi]
			lastArg = ci->value;
			// FIXME: range check
			EmitString( "89 86" );		// mov	dword ptr [esi+database],eax
			Emit4( lastArg + (int)vm->dataBase );
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_CALL:
#ifdef VM_LOG_SYSCALLS
			EmitString( "C7 86" );		// mov dword ptr [esi + dataBase + 0 ],0x12345678
			Emit4( (int)vm->dataBase );
			Emit4( ip-1 );
#endif
			n = codeOffset[FUNC_CALL] - compiledOfs;
			EmitString( "E8" );			// call +codeOffset[FUNC_CALL]
			Emit4( n - 5 );
			LastCommand = LAST_COMMAND_MOV_EAX_EDI_CALL;
			break;

		case OP_PUSH:
			EmitAddEDI4(vm);
			break;

		case OP_POP:
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_LEAVE:
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "83 C6" );		// add	esi, 0x12
				Emit1( v );
			} else {
				EmitString( "81 C6" );		// add	esi, 0x12345678
				Emit4( v );
			}
			EmitString( "C3" );			// ret
			break;

		case OP_LOAD4:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				compiledOfs -= 2;
				vm->instructionPointers[ ip-1 ] = compiledOfs;
				EmitString( "8B 80" );						// mov eax, dword ptr [eax + 0x1234567]
				Emit4( (int)vm->dataBase );
				EmitCommand(LAST_COMMAND_MOV_EDI_EAX);		// mov dword ptr [edi], eax
				break;
			}
			EmitMovEBXEDI(vm, vm->dataMask);
			EmitString( "8B 83" );		// mov	eax, dword ptr [ebx + 0x12345678]
			Emit4( (int)vm->dataBase );
			EmitCommand(LAST_COMMAND_MOV_EDI_EAX);			// mov dword ptr [edi], eax
			break;

		case OP_LOAD2:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				compiledOfs -= 2;
				vm->instructionPointers[ ip-1 ] = compiledOfs;
				EmitString( "0F B7 80" );					// movzx eax, word ptr [eax + 0x12345678]
				Emit4( (int)vm->dataBase );
				EmitCommand(LAST_COMMAND_MOV_EDI_EAX);		// mov dword ptr [edi], eax
				break;
			}
			EmitMovEBXEDI(vm, vm->dataMask);
			EmitString( "0F B7 83" );						// movzx	eax, word ptr [ebx + 0x12345678]
			Emit4( (int)vm->dataBase );
			EmitCommand(LAST_COMMAND_MOV_EDI_EAX);			// mov dword ptr [edi], eax
			break;

		case OP_LOAD1:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				compiledOfs -= 2;
				vm->instructionPointers[ ip-1 ] = compiledOfs;
				EmitString( "0F B6 80" );					// movzx eax, byte ptr [eax + 0x12345678]
				Emit4( (int)vm->dataBase );
				EmitCommand(LAST_COMMAND_MOV_EDI_EAX);		// mov dword ptr [edi], eax
				break;
			}
			EmitMovEBXEDI(vm, vm->dataMask);
			EmitString( "0F B6 83" );					// movzx eax, byte ptr [ebx + 0x12345678]
			Emit4( (int)vm->dataBase );
			EmitCommand(LAST_COMMAND_MOV_EDI_EAX);		// mov dword ptr [edi], eax
			break;

		case OP_STORE4:
			EmitMovEAXEDI(vm);	
			EmitString( "8B 5F FC" );	// mov	ebx, dword ptr [edi-4]
//			if (pop1 != OP_CALL) {
//				EmitString( "81 E3" );		// and ebx, 0x12345678
//				Emit4( vm->dataMask & ~3 );
//			}
			EmitString( "89 83" );		// mov dword ptr [ebx+0x12345678], eax
			Emit4( (int)vm->dataBase );
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			break;

		case OP_STORE2:
			EmitMovEAXEDI(vm);	
			EmitString( "8B 5F FC" );	// mov	ebx, dword ptr [edi-4]
//			EmitString( "81 E3" );		// and ebx, 0x12345678
//			Emit4( vm->dataMask & ~1 );
			EmitString( "66 89 83" );	// mov word ptr [ebx+0x12345678], eax
			Emit4( (int)vm->dataBase );
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			break;

		case OP_STORE1:
			EmitMovEAXEDI(vm);	
			EmitString( "8B 5F FC" );	// mov	ebx, dword ptr [edi-4]
//			EmitString( "81 E3" );		// and ebx, 0x12345678
//			Emit4( vm->dataMask );
			EmitString( "88 83" );		// mov byte ptr [ebx+0x12345678], eax
			Emit4( (int)vm->dataBase );
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
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
			EmitJump( vm, ci, ci->value );
			break;

		case OP_EQF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			FCOMSF();
			EmitString( "80 E4 40" );	// and ah,0x40
			JNE();
			break;			

		case OP_NEF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			FCOMSF();
			EmitString( "80 E4 40" );	// and ah,0x40
			JE();
			break;			

		case OP_LTF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			FCOMSF();
			EmitString( "80 E4 01" );	// and ah,0x01
			JNE();
			break;			

		case OP_LEF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			FCOMSF();
			EmitString( "80 E4 41" );	// and ah,0x41
			JNE();
			break;			

		case OP_GTF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			FCOMSF();
			EmitString( "80 E4 41" );	// and ah,0x41
			JE();
			break;			

		case OP_GEF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			FCOMSF();
			EmitString( "80 E4 01" );	// and ah,0x01
			JE();
			break;			

		case OP_NEGI:
			EmitMovEAXEDI( vm );
			EmitString( "F7 D8" );		// neg eax
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
			break;

		case OP_ADD:
			EmitMovEAXEDI(vm);			// mov eax, dword ptr [edi]
			EmitString( "01 47 FC" );	// add dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;

		case OP_SUB:
			EmitMovEAXEDI(vm);			// mov eax, dword ptr [edi]
			EmitString( "29 47 FC" );	// sub dword ptr [edi-4],eax
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
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
			EmitString( "F7 17" );		// not dword ptr [edi]
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
			EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
			EmitString( "D8 07" );					// fadd dword ptr [edi]
			EmitString( "D9 5F FC" );				// fstp dword ptr [edi-4]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_SUBF:
			EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
			EmitString( "D8 27" );					// fsub dword ptr [edi]
			EmitString( "D9 5F FC" );				// fstp dword ptr [edi-4]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_DIVF:
			EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
			EmitString( "D8 37" );					// fdiv dword ptr [edi]
			EmitString( "D9 5F FC" );				// fstp dword ptr [edi-4]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_MULF:
			EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
			EmitString( "D8 0F" );					// fmul dword ptr [edi]
			EmitString( "D9 5F FC" );				// fstp dword ptr [edi-4]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_CVIF:
			EmitString( "DB 07" );					// fild dword ptr [edi]
			EmitCommand( LAST_COMMAND_FSTP_EDI );	// fstp dword ptr [edi]
			break;

		case OP_CVFI:
#ifndef FTOL_PTR
			EmitString( "9B D9 3D" );	// fnstcw word ptr [cwCurr]
			Emit4( (int)&cwCurr );
			EmitString( "D9 07" );		// fld dword ptr [edi]
			EmitString( "D9 2D" );		// fldcw word ptr [cw0F7F]
			Emit4( (int)&cw0F7F );
			EmitString( "DB 1F" );		// fistp dword ptr [edi]
			EmitString( "D9 2D" );		// fldcw word ptr [cwCurr]
			Emit4( (int)&cwCurr );
#else
			EmitFldEDI( vm );			// fld dword ptr [edi]
			if ( CPU_Flags & CPU_SSE3 ) {
				// fast sse3 truncation
				EmitString( "DB 0F" );	// fisttp dword ptr [edi]
			} else {
				// call the library conversion function
				n = codeOffset[FUNC_FTOL] - compiledOfs;
				EmitString( "E8" );		// call +codeOffset[FUNC_FTOL]
				Emit4( n - 5 );
			}
#endif
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
			// FIXME: range check
			EmitString( "56" );			// push esi
			EmitString( "57" );			// push edi
			EmitString( "8B 37" );		// mov esi,[edi] 
			EmitString( "8B 7F FC" );	// mov edi,[edi-4] 
			EmitString( "B9" );			// mov ecx,0x12345678
			Emit4( ci->value >> 2 );
			EmitString( "B8" );			// mov eax, datamask
			Emit4( vm->dataMask );
			EmitString( "BB" );			// mov ebx, database
			Emit4( (int)vm->dataBase );
			EmitString( "23 F0" );		// and esi, eax
			EmitString( "03 F3" );		// add esi, ebx
			EmitString( "23 F8" );		// and edi, eax
			EmitString( "03 FB" );		// add edi, ebx
			EmitString( "F3 A5" );		// rep movsd
			EmitString( "5F" );			// pop edi
			EmitString( "5E" );			// pop esi
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			break;

		case OP_JUMP:
			EmitCommand(LAST_COMMAND_SUB_DI_4); // sub edi, 4
			EmitString( "8B 47 04" );           // mov eax,dword ptr [edi+4]
			EmitString( "3D" );		            // cmp eax, 0x12345678
			Emit4( vm->instructionCount );
			EmitString( "73 07" );              // jae +7
			// FIXME: allow jump withing local function scope only
			EmitString( "FF 24 85" );           // jmp dword ptr [instructionPointers + eax * 4]
			Emit4( (int)vm->instructionPointers );
			EmitString( "FF 15" );              // call errJumpPtr
			Emit4( (int)&errJumpPtr );
			break;
		}

		pop1 = op;
	} // while( ip < header->instructionCount )

		codeOffset[FUNC_CALL] = compiledOfs;
		EmitCall( vm );
		codeOffset[FUNC_FTOL] = compiledOfs;
		EmitFTOL( vm );

	} // for( pass = 0; pass < n; pass++ )

	if ( code == NULL ) 
	{
		code = VM_Alloc_Compiled( vm, compiledOfs );
		goto __compile;
	}

#ifdef VM_X86_MMAP
	if ( mprotect( vm->codeBase, compiledOfs, PROT_READ|PROT_EXEC ) )
		Com_Error( ERR_FATAL, "VM_CompileX86: mprotect failed" );
#elif _WIN32
	{
		DWORD oldProtect = 0;
		
		// remove write permissions.
		if ( !VirtualProtect(vm->codeBase, compiledOfs, PAGE_EXECUTE_READ, &oldProtect ) )
			Com_Error( ERR_FATAL, "VM_CompileX86: VirtualProtect failed" );
	}
#endif
	
	// offset all the instruction pointers for the new location
	for ( i = 0 ; i < header->instructionCount ; i++ ) {
		if ( inst[i].opStack != 0 ) {
			vm->instructionPointers[i] = (int)badJumpPtr;
			continue;
		}
		vm->instructionPointers[i] += (int)vm->codeBase;
	}

	VM_FreeBuffers();

	vm->destroy = VM_Destroy_Compiled;

	Com_Printf( "VM file %s compiled to %i bytes of code\n", vm->name, compiledOfs );
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
	vm->codeBase = mmap( NULL, vm->codeLength, PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0 );
	if ( vm->codeBase == MAP_FAILED )
		Com_Error( ERR_FATAL, "VM_CompileX86: can't mmap memory" );
#elif _WIN32
	// allocate memory with EXECUTE permissions under windows.
	vm->codeBase = VirtualAlloc( NULL, vm->codeLength, MEM_COMMIT, PAGE_EXECUTE_READWRITE );
	if ( !vm->codeBase )
		Com_Error( ERR_FATAL, "VM_CompileX86: VirtualAlloc failed" );
#else
	vm->codeBase = malloc( vm->codeLength );
	if ( !vm->codeBase )
	        Com_Error( ERR_FATAL, "VM_CompileX86: malloc failed" );
#endif
	return vm->codeBase;
}


void VM_Destroy_Compiled( vm_t* vm )
{
#ifdef VM_X86_MMAP
	munmap( vm->codeBase, vm->codeLength );
#elif _WIN32
	VirtualFree( vm->codeBase, 0, MEM_RELEASE );
#else
	free( vm->codeBase );
#endif
}

/*
==============
VM_CallCompiled

This function is called directly by the generated code
==============
*/
int	VM_CallCompiled( vm_t *vm, int *args ) {
	int		stack[MAX_OPSTACK+2];
	size_t	programStack;
	size_t	stackOnEntry;
	byte	*image;
	void	*opStack;
	vm_t	*oldVM;

	oldVM = currentVM;

	currentVM = vm;

	// interpret the code
	//vm->currentlyInterpreting = qtrue;

	// we might be called recursively, so this might not be the very top
	programStack = vm->programStack;
	stackOnEntry = programStack;

	// set up the stack frame 
	image = vm->dataBase;

	programStack -= 48;

	*(int *)&image[ programStack + 44] = args[9];
	*(int *)&image[ programStack + 40] = args[8];
	*(int *)&image[ programStack + 36] = args[7];
	*(int *)&image[ programStack + 32] = args[6];
	*(int *)&image[ programStack + 28] = args[5];
	*(int *)&image[ programStack + 24] = args[4];
	*(int *)&image[ programStack + 20] = args[3];
	*(int *)&image[ programStack + 16] = args[2];
	*(int *)&image[ programStack + 12] = args[1];
	*(int *)&image[ programStack + 8 ] = args[0];
	*(int *)&image[ programStack + 4 ] = 0;	// return stack
	//*(int *)&image[ programStack ] = -1;	// will terminate the loop on return

	// off we go into generated code...
	opStack = &stack[1];

	{
#ifdef _MSC_VER
		void *entryPoint = vm->codeBase;

	__asm  {
		pushad
		mov		esi, programStack
		mov		edi, opStack
		call	entryPoint
		mov		programStack, esi
		mov		opStack, edi
		popad
	}
#else
		__asm__ volatile(
			"call *%2"
			: "+S" (programStack), "+D" (opStack)
			: "mr" (vm->codeBase)
			: "cc", "memory", "%eax", "%ecx", "%edx"
		);
#endif
	}

	if ( opStack != &stack[2] ) {
		Com_Error( ERR_DROP, "opStack corrupted in compiled code" );
	}
	if ( programStack != stackOnEntry - 48 ) {
		Com_Error( ERR_DROP, "programStack corrupted in compiled code" );
	}

	vm->programStack = stackOnEntry;

	// in case we were recursively called by another vm
	currentVM = oldVM;

	return *(int *)opStack;
}
