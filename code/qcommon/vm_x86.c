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

static void VM_Destroy_Compiled(vm_t* self);

/*

  eax	scratch
  ebx	scratch 
  ecx	scratch (required for shifts) | currentVM
  edx	scratch (required for divisions)
  esi	program stack
  edi	opstack

*/

#define VMFREE_BUFFERS() do {Z_Free(code); Z_Free(buf); Z_Free(jused);} while(0)
//#define VM_LOG_SYSCALLS

static	byte	*buf = NULL;
static	byte	*jused = NULL;
static	int		compiledOfs = 0;
static	byte	*code = NULL;
static	int		pc = 0;
static	int     jusedSize = 0;

int		*instructionPointers = NULL;
int		instructionCount = 0;

#define FTOL_PTR

#if defined( FTOL_PTR )

int qftol0F7F( void );
static	int		ftolPtr = (int)qftol0F7F;

#else

int cw0F7F = 0x0F7F;
int cwCurr = 0;

#endif

static	int	instruction, pass;
static	int	lastConst = 0;
static	int	pop1;
static	int jlabel;

typedef enum 
{
	LAST_COMMAND_NONE	= 0,
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

static void (*const errJumpPtr)(void) = ErrJump;

void AsmCall( void ); // see corresponding asm files

static void (*const AsmCallPtr)(void) = AsmCall;

static int	Constant4( void ) {
	int		v;

	v = code[pc] | (code[pc+1]<<8) | (code[pc+2]<<16) | (code[pc+3]<<24);
	pc += 4;
	return v;
}

static int	NextConstant4( void ) {
	return (code[pc] | (code[pc+1]<<8) | (code[pc+2]<<16) | (code[pc+3]<<24));
}


static int	Constant1( void ) {
	int		v;

	v = code[pc];
	pc += 1;
	return v;
}

static void Emit1( int v ) 
{
	buf[ compiledOfs ] = v;
	compiledOfs++;

	LastCommand = LAST_COMMAND_NONE;
}

#if 0
static void Emit2( int v ) {
	Emit1( v & 255 );
	Emit1( ( v >> 8 ) & 255 );
}
#endif

static void Emit4( int v ) {
	Emit1( v & 255 );
	Emit1( ( v >> 8 ) & 255 );
	Emit1( ( v >> 16 ) & 255 );
	Emit1( ( v >> 24 ) & 255 );
}

static int Hex( int c ) {
	if ( c >= 'a' && c <= 'f' ) {
		return 10 + c - 'a';
	}
	if ( c >= 'A' && c <= 'F' ) {
		return 10 + c - 'A';
	}
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}

	VMFREE_BUFFERS();
	Com_Error( ERR_DROP, "Hex: bad char '%c'", c );

	return 0;
}
static void EmitString( const char *string ) {
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



static void EmitCommand(ELastCommand command)
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
		vm->instructionPointers[ instruction-1 ] = compiledOfs;
		LastCommand = LAST_COMMAND_NONE;
		return;
	}
	if ( LastCommand == LAST_COMMAND_SUB_DI_8 ) 
	{	// sub edi, 8
		compiledOfs -= 3;
		vm->instructionPointers[ instruction-1 ] = compiledOfs;
		EmitCommand(LAST_COMMAND_SUB_DI_4);
		return;
	}
	EmitString( "83 C7 04" );	//	add edi,4
}

static int EmitMovEAXEDI(vm_t *vm) {
	if ( jlabel ) {
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
		vm->instructionPointers[ instruction-1 ] = compiledOfs;
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
		vm->instructionPointers[ instruction-1 ] = compiledOfs;
		EmitString( "B8" );			// mov	eax, 0x12345678
		Emit4( lastConst );
		return 2;
	}
	EmitString( "8B 07" );		    // mov eax, dword ptr [edi]
	return 1;
}

void EmitMovECXEDI( vm_t *vm ) {
	if ( jlabel ) {
		EmitString( "8B 0F" );		// mov ecx, dword ptr [edi]
		return;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI_CALL ) {
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI )  {
		EmitString( "89 C1" );		// mov ecx, eax // FIXME: mov ecx, dword ptr [edi]
		return;
	}
	if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) // mov [edi], eax
	{
		compiledOfs -= 2;
		vm->instructionPointers[ instruction-1 ] = compiledOfs;
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
		vm->instructionPointers[ instruction-1 ] = compiledOfs;
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
		vm->instructionPointers[ instruction-1 ] = compiledOfs;
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
		vm->instructionPointers[ instruction-1 ] = compiledOfs;
		LastCommand = LAST_COMMAND_NONE;
		return 1;
	}
	EmitString( "D9 07" );			// fld dword ptr [edi]
	return 0;
}


#define JUSED(x) \
	do { \
		if (x < 0 || x >= jusedSize) { \
		        VMFREE_BUFFERS(); \
			Com_Error( ERR_DROP, \
					"VM_CompileX86: jump target out of range at offset %d", pc ); \
		} \
		jused[x] = 1; \
	} while(0)

#define EMITJMP(S,N) \
	do { \
		v = Constant4(); \
		JUSED(v); \
		n = vm->instructionPointers[v] - compiledOfs - (N); \
		EmitString( S ); \
		Emit4( n ); \
	} while(0) \

#define JE()   EMITJMP( "0F 84", 6 )
#define JNE()  EMITJMP( "0F 85", 6 )
#define JL()   EMITJMP( "0F 8C", 6 )
#define JLE()  EMITJMP( "0F 8E", 6 )
#define JG()   EMITJMP( "0F 8F", 6 )
#define JGE()  EMITJMP( "0F 8D", 6 )
#define JB()   EMITJMP( "0F 82", 6 )
#define JBE()  EMITJMP( "0F 86", 6 )
#define JA()   EMITJMP( "0F 87", 6 )
#define JAE()  EMITJMP( "0F 83", 6 )

#define JZ()   EMITJMP( "0F 84", 6 )
#define JNZ()  EMITJMP( "0F 85", 6 )

#define JMP()  EMITJMP( "E9", 5 )

#define CMPI() \
	do { \
		EmitMovEAXEDI( vm ); \
		EmitCommand(LAST_COMMAND_SUB_DI_8); /* sub edi, 8 */ \
		EmitString( "39 47 04" );	/* cmp dword ptr [edi+4], eax */ \
	} while(0)

#define CMPCI() \
	do { \
		v = Constant4(); \
		if ( abs(v) <= 127 ) { \
			EmitString( "83 F8" ); \
			Emit1( v ); \
		} else { \
			EmitString( "3D" ); \
			Emit4( v ); \
		} \
	} while(0)

#define FCOMFF() \
	do { \
		EmitString( "D9 47 08" );	/* fld dword ptr [edi+8] */ \
		EmitString( "D9 47 04" );	/* fld dword ptr [edi+4] */ \
		EmitString( "DF E9" );		/* fucomip */ \
		EmitString( "DD D8" );		/* fstp st(0) */ \
	} while (0)

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
static int FloatMerge( void ) 
{
	int nextOp = code[pc];
	int currOp = code[pc-1];
	if ( nextOp != OP_ADDF && nextOp != OP_SUBF && nextOp != OP_MULF && nextOp != OP_DIVF )
		return 0;
	if ( jused[ instruction ] )
		return 0;

	EmitString( "D9 47 F8" );				// fld dword ptr [edi-8]
	EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
	switch ( currOp ) {
		case OP_ADDF: EmitString( "D8 07" ); break; // fadd dword ptr [edi]
		case OP_SUBF: EmitString( "D8 27" ); break; // fsub dword ptr [edi]
		case OP_MULF: EmitString( "D8 0F" ); break; // fmul dword ptr [edi]
		case OP_DIVF: EmitString( "D8 37" ); break; // fdiv dword ptr [edi]
	}
	switch ( nextOp ) {
		case OP_ADDF: EmitString( "DE C1" ); break; // faddp
		case OP_SUBF: EmitString( "DE E9" ); break; // fsubp
		case OP_MULF: EmitString( "DE C9" ); break; // fmulp
		case OP_DIVF: EmitString( "DE F9" ); break; // fdivp
	}
	EmitString( "D9 5F F8" );				// fstp dword ptr [edi-8]
	EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
	instruction += 1;
	pc += 1;
	return 1;
}

/*
=================
ConstOptimize
=================
*/
qboolean ConstOptimize( vm_t *vm ) {
	int n, v, opt;
	int op1;

	// we can safely perform optimizations only in case if 
	// we are 100% sure that next instruction is not a jump label
	if ( !jused[instruction] )
		op1 = code[pc+4];
	else
		return qfalse;

	switch ( op1 ) {

	case OP_LOAD4:
		EmitAddEDI4(vm);
		EmitString( "BB" );         // mov ebx, 0x12345678
		Emit4( (Constant4()&vm->dataMask) + (int)vm->dataBase);
		EmitString( "8B 03" );      // mov eax, dword ptr [ebx]
		EmitCommand(LAST_COMMAND_MOV_EDI_EAX); // mov dword ptr [edi], eax
		pc++;						// OP_LOAD4
		instruction += 1;
		return qtrue;

	case OP_LOAD2:
		EmitAddEDI4(vm);
		EmitString( "BB" );         // mov ebx, 0x12345678
		Emit4( (Constant4()&vm->dataMask) + (int)vm->dataBase);
		EmitString( "0F B7 03" );   // movzx eax, word ptr [ebx]
		EmitCommand(LAST_COMMAND_MOV_EDI_EAX); // mov dword ptr [edi], eax
		pc++;						// OP_LOAD2
		instruction += 1;
		return qtrue;

	case OP_LOAD1:
		EmitAddEDI4(vm);
		EmitString( "BB" );         // mov ebx, 0x12345678
		Emit4( (Constant4()&vm->dataMask) + (int)vm->dataBase);
		EmitString( "0F B6 03" );	// movzx eax, byte ptr [ebx]
		EmitCommand(LAST_COMMAND_MOV_EDI_EAX); // mov dword ptr [edi], eax
		pc++;						// OP_LOAD1
		instruction += 1;
		return qtrue;

	case OP_STORE4:
		opt = EmitMovEBXEDI(vm, (vm->dataMask & ~3));
		EmitString( "B8" );			// mov	eax, 0x12345678
		Emit4( Constant4() );
//		if (!opt) {
//			EmitString( "81 E3" );  // and ebx, 0x12345678
//			Emit4( vm->dataMask & ~3 );
//		}
		EmitString( "89 83" );      // mov dword ptr [ebx+0x12345678], eax
		Emit4( (int)vm->dataBase );
		EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
		pc++;						// OP_STORE4
		instruction += 1;
		return qtrue;

	case OP_STORE2:
		opt = EmitMovEBXEDI(vm, (vm->dataMask & ~1));
		EmitString( "B8" );			// mov	eax, 0x12345678
		Emit4( Constant4() );
//		if (!opt) {
//			EmitString( "81 E3" );  // and ebx, 0x12345678
//			Emit4( vm->dataMask & ~1 );
//		}
		EmitString( "66 89 83" );   // mov word ptr [ebx+0x12345678], eax
		Emit4( (int)vm->dataBase );
		EmitCommand(LAST_COMMAND_SUB_DI_4); // sub edi, 4
		pc++;                       // OP_STORE2
		instruction += 1;
		return qtrue;

	case OP_STORE1:
		opt = EmitMovEBXEDI(vm, vm->dataMask);
		EmitString( "B8" );			// mov	eax, 0x12345678
		Emit4( Constant4() );
//		if (!opt) {
//			EmitString( "81 E3" );	// and ebx, 0x12345678
//			Emit4( vm->dataMask );
//		}
		EmitString( "88 83" );		// mov byte ptr [ebx+0x12345678], eax
		Emit4( (int)vm->dataBase );
		EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
		pc++;						// OP_STORE4
		instruction += 1;
		return qtrue;

	case OP_ADD:
		v = Constant4();
		EmitMovEAXEDI( vm ); 
		if ( abs(v) <= 127 ) {
			EmitString( "83 C0" );	// add eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "05" );	    // add eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc++;						// OP_ADD
		instruction += 1;
		return qtrue;

	case OP_SUB:
		v = Constant4();
		EmitMovEAXEDI( vm );
		if ( abs(v) <= 127 ) {
			EmitString( "83 E8" );	// sub eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "2D" );		// sub eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc++;						// OP_SUB
		instruction += 1;
		return qtrue;

	case OP_MULI:
		v = Constant4();
		EmitMovEAXEDI( vm );
		if ( abs(v) <= 127 ) {
			EmitString( "6B C0" );	// imul eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "69 C0" );	// imul eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc++;						// OP_MULI
		instruction += 1;
		return qtrue;

	case OP_LSH:
		v = NextConstant4();
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 E0" );	// shl dword ptr [edi], 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc += 5;				// CONST + OP_LSH
		instruction += 1;
		return qtrue;

	case OP_RSHI:
		v = NextConstant4();
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 F8" );	// sar eax, 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc += 5;				// CONST + OP_RSHI
		instruction += 1;
		return qtrue;

	case OP_RSHU:
		v = NextConstant4();
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 E8" );	// shr eax, 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc += 5;				// CONST + OP_RSHU
		instruction += 1;
		return qtrue;
	
	case OP_BAND:
		v = Constant4();
		EmitMovEAXEDI( vm );
		if ( v >= 0 && v <= 127 ) {
			EmitString( "83 E0" ); // and eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "25" ); // and eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc += 1;				   // OP_BAND
		instruction += 1;
		return qtrue;

	case OP_BOR:
		v = Constant4();
		EmitMovEAXEDI( vm );
		if ( v >= 0 && v <= 127 ) {
			EmitString( "83 C8" ); // or eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "0D" );    // or eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc += 1;				   // OP_BOR
		instruction += 1;
		return qtrue;

	case OP_BXOR:
		v = Constant4();
		EmitMovEAXEDI( vm );
		if ( v >= 0 && v <= 127 ) {
			EmitString( "83 F0" ); // xor eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "35" );    // xor eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		pc += 1;				   // OP_BXOR
		instruction += 1;
		return qtrue;

	case OP_EQF:
	case OP_NEF:
		if ( NextConstant4() != 0 )
			break;
		pc += 5;				   // CONST + OP_EQF|OP_NEF
		EmitMovEAXEDI( vm );
		EmitCommand(LAST_COMMAND_SUB_DI_4);
		// floating point hack :)
		EmitString( "25" );        // and eax, 0x7FFFFFFF
		Emit4( 0x7FFFFFFF );
		if ( op1 == OP_EQF )
			JZ();
		else
			JNZ();
		instruction += 1;
		return qtrue;

	case OP_EQ:
	case OP_NE:
		EmitMovEAXEDI( vm );
		EmitCommand(LAST_COMMAND_SUB_DI_4);
		if ( NextConstant4() == 0 ) {
			EmitString( "85 C0" ); // test eax, eax
			pc += 4;
		} else {
			CMPCI();
		}
		pc += 1;				   // OP_EQ/OP_NE
		if ( op1 == OP_EQ )
			JE();
		else
			JNE();
		instruction += 1;
		return qtrue;

	case OP_GEI:
	case OP_GTI:
		EmitMovEAXEDI( vm );
		EmitCommand( LAST_COMMAND_SUB_DI_4 );
		CMPCI();
		pc += 1;			       // OP_GEI|OP_GTI
		if ( op1 == OP_GEI )
			JGE();
		else
			JG();
		instruction += 1;
		return qtrue;

	case OP_LEI:
	case OP_LTI:
		EmitMovEAXEDI( vm );
		EmitCommand( LAST_COMMAND_SUB_DI_4 );
		CMPCI();
		pc += 1;			       // OP_GEI|OP_GTI
		if ( op1 == OP_LEI )
			JLE();
		else
			JL();
		instruction += 1;
		return qtrue;

	case OP_GTU:
	case OP_GEU:
		EmitMovEAXEDI( vm );
		EmitCommand( LAST_COMMAND_SUB_DI_4 );
		CMPCI();
		pc += 1;			       // OP_GTU|OP_GEU
		if ( op1 == OP_GTU )
			JA();
		else
			JAE();
		instruction += 1;
		return qtrue;

	case OP_LTU:
	case OP_LEU:
		EmitMovEAXEDI( vm );
		EmitCommand( LAST_COMMAND_SUB_DI_4 );
		CMPCI();
		pc += 1;			       // OP_LTU|OP_LEU
		if ( op1 == OP_LTU )
			JB();
		else
			JBE();
		instruction += 1;
		return qtrue;

	case OP_JUMP:
		JMP();
		pc += 1;                  // OP_JUMP
		instruction += 1;
		return qtrue;

	case OP_CALL:
		if ( NextConstant4() < 0 )
			break;
		v = Constant4();
		JUSED(v);
#ifdef VM_LOG_SYSCALLS
		EmitString( "C7 86" );    // mov dword ptr [esi+database],0x12345678
		Emit4( (int)vm->dataBase );
		Emit4( pc );
#endif
		EmitString( "E8" );			// call +offset
		Emit4( vm->instructionPointers[v] - compiledOfs - 6 );
		EmitCommand( LAST_COMMAND_MOV_EAX_EDI );
		pc += 1;                  // OP_CALL
		instruction += 1;
		return qtrue;

	default:
		break;
	}

	return qfalse;
}

#define LOCALOP(OP) ( code[pc+4] == OP_LOCAL && !memcmp( code+pc, code+pc+5, 4 ) && \
	code[pc+9] == OP_LOAD4 && code[pc+10] == OP_CONST && \
	code[pc+15] == (OP) && code[pc+16] == OP_STORE4 && \
	!jused[instruction+0] && !jused[instruction+1] && !jused[instruction+2] && \
	!jused[instruction+3] && !jused[instruction+4] )

/*
=================
VM_Compile
=================
*/
void VM_Compile( vm_t *vm, vmHeader_t *header ) {
	int		op;
	int		maxLength;
	int		v, n;
	int		i;
	//qboolean opt;
	jusedSize = header->instructionCount;

	// allocate a very large temp buffer, we will shrink it later
	maxLength = header->codeLength * 8;
	jused = Z_Malloc(jusedSize);
	buf = Z_Malloc(maxLength);
	code = Z_Malloc(header->codeLength+32);
	
	Com_Memset(jused, 0, jusedSize);
	Com_Memset(buf, 0, maxLength);

	// copy code in larger buffer and put some zeros at the end
	// so we can safely look ahead for a few instructions in it
	// without a chance to get false-positive because of some garbage bytes
	Com_Memset(code, 0, header->codeLength+32);
	Com_Memcpy(code, (byte *)header + header->codeOffset, header->codeLength );

	// ensure that the optimisation pass knows about all the jump
	// table targets
	for( i = 0; i < vm->numJumpTableTargets; i++ ) {
		n = *(int *)(vm->jumpTableTargets + ( i * sizeof( int ) ) );
		if ( n >= jusedSize )
			continue;
		jused[ n ] = 1;
	}
	if ( !vm->jumpTableTargets ) {
		Com_Memset( jused, 1, jusedSize );
	}

	for( pass = 0; pass < 3; pass++ ) {

	pop1 = OP_UNDEF;

	// translate all instructions
	pc = 0;
	instruction = 0;
	compiledOfs = 0;

	LastCommand = LAST_COMMAND_NONE;

	while(instruction < header->instructionCount)
	{
		if(compiledOfs > maxLength - 16)
		{
	        	VMFREE_BUFFERS();
			Com_Error(ERR_DROP, "VM_CompileX86: maxLength exceeded");
		}

		vm->instructionPointers[ instruction ] = compiledOfs;

		jlabel = jused[ instruction ];

		if ( jlabel )
			pop1 = OP_UNDEF;

		instruction++;

		if(pc > header->codeLength)
		{
		        VMFREE_BUFFERS();
			Com_Error(ERR_DROP, "VM_CompileX86: pc > header->codeLength");
		}

		op = code[ pc ];
		pc++;
		switch ( op ) {
		case 0:
			break;
		case OP_BREAK:
			EmitString( "CC" );			// int 3
			break;
		case OP_ENTER:
			v = Constant4();
			if ( abs(v) <= 127 ) {
				EmitString( "83 EE" );		// sub	esi, 0x12345678
				Emit1( v );
			} else {
				EmitString( "81 EE" );		// sub	esi, 0x12345678
				Emit4( v );
			}
			break;
		case OP_CONST:
			if ( ConstOptimize( vm ) )
				break;
			EmitAddEDI4(vm);
			EmitString( "C7 07" );		// mov	dword ptr [edi], 0x12345678
			lastConst = Constant4();
			Emit4( lastConst );
			if (code[pc] == OP_JUMP) {
				JUSED(lastConst);
			}
			LastCommand = LAST_COMMAND_MOV_EDI_CONST;
			break;
		case OP_LOCAL:
#if 0
			if ( code[pc+4] == OP_CONST && code[pc+9] == OP_STORE4 && !jlabel && !jused[instruction] && !jused[instruction+1] ) {
#if 0
				EmitString( "8D 86" );		// lea eax, [esi+0x12345678]
				Emit4( Constant4() + (int)vm->dataBase );
				pc++;						// OP_CONST
				EmitString( "C7 00" );		// mov dword ptr [eax], 0x12345678
				Emit4( Constant4() );
				pc++;						// OP_STORE4
				instruction += 2;
#else
				EmitString( "C7 86" );		// mov dword[esi+0x12345678], 0x12345678
				Emit4( Constant4() + (int)vm->dataBase );
				pc++;						// OP_CONST
				Emit4( Constant4() );
				pc++;						// OP_STORE4
				instruction += 2;
#endif
				break;
			}
#endif
			if ( LOCALOP( OP_ADD ) ) {
				v = Constant4() + (int) vm->dataBase; 
				pc += 1 + 4 + 1 + 1;           // OP_LOCAL + CONST + OP_LOAD4 + OP_CONST
				if ( abs( NextConstant4() ) <= 127 ){
					EmitString( "83 86" );		// add dword ptr[esi+0x12345678],0x12
					Emit4( v );
					Emit1( Constant4() );
				} else {
					EmitString( "81 86" );		// add dword ptr[esi+0x12345678],0x12345678
					Emit4( v );
					Emit4( Constant4() );
				}
				pc++;						    // OP_ADD
				pc++;						    // OP_STORE4
				instruction	+= 5;
				break;
			}

			if ( LOCALOP( OP_SUB ) ) {
				v = Constant4() + (int) vm->dataBase; 
				pc += 1 + 4 + 1 + 1;		// OP_LOCAL + CONST + OP_LOAD4 + OP_CONST
				if ( abs( NextConstant4() ) <= 127 ) {
					EmitString( "83 AE" );		// sub dword ptr[esi+0x12345678],0x12
					Emit4( v );
					Emit1( Constant4() );
				} else {
					EmitString( "81 AE" );		// sub dword ptr[esi+0x12345678],0x12345678
					Emit4( v );
					Emit4( Constant4() );
				}
				pc++;						// OP_SUB
				pc++;						// OP_STORE4
				instruction	+= 5;
				break;
			}

			// TODO: i = j + k;
			// TODO: i = j - k;

			// merge OP_LOCAL + OP_LOAD4
			if ( code[pc+4] == OP_LOAD4 && !jused[instruction] ) {
				EmitAddEDI4( vm );
				EmitString( "8B 86" );	// mov eax, dword ptr [esi + LOCAL + vm->dataBase ]
				v = Constant4() + (int)vm->dataBase;
				pc++;					// OP_LOAD4
				Emit4( v );
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				instruction++;
				break;
			}

			// merge OP_LOCAL + OP_LOAD2
			if ( code[pc+4] == OP_LOAD2 && !jused[instruction] ) {
				EmitAddEDI4( vm );
				EmitString( "0F B7 86" );	// movzx eax, word ptr[esi + LOCAL + vm->dataBase ]
				v = Constant4() + (int)vm->dataBase;
				pc++;						// OP_LOAD2
				Emit4( v );
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				instruction++;
				break;
			}

			// merge OP_LOCAL + OP_LOAD1
			if ( code[pc+4] == OP_LOAD1 && !jused[instruction] ) {
				EmitAddEDI4( vm );
				EmitString( "0F B6 86" );	// movzx eax, byte ptr[esi + LOCAL + vm->dataBase ]
				v = Constant4() + (int)vm->dataBase;
				pc++;						// OP_LOAD1
				Emit4( v );
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
				instruction++;
				break;
			}

			EmitAddEDI4(vm);
			v = Constant4();
			if ( abs(v) <= 127 ) {
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
			EmitString( "89 86" );		// mov	dword ptr [esi+database],eax
			// FIXME: range check
			Emit4( Constant1() + (int)vm->dataBase );
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;
		case OP_CALL:
#ifdef VM_LOG_SYSCALLS
			EmitString( "C7 86" );		// mov dword ptr [esi+database],0x12345678
			Emit4( (int)vm->dataBase );
			Emit4( pc );
#endif
			EmitString( "B9" );			// mov ecx, currentVM
			Emit4( (int)vm );
			EmitString( "FF 15" );		// call asmCallPtr
			Emit4( (int)&AsmCallPtr );
			LastCommand = LAST_COMMAND_MOV_EAX_EDI_CALL;
			break;
		case OP_PUSH:
			EmitAddEDI4(vm);
			break;
		case OP_POP:
			EmitCommand(LAST_COMMAND_SUB_DI_4);		// sub edi, 4
			break;
		case OP_LEAVE:
			v = Constant4();
			if ( abs(v) < 64 ) {
				EmitString( "83 C6" );		// add	esi, 0x12
				Emit1( v );
			} else {
				EmitString( "81 C6" );		// add	esi, 0x12345678
				Emit4( v );
			}
			EmitString( "C3" );			// ret
			break;
		case OP_LOAD4:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX && !jlabel ) {
				compiledOfs -= 2;
				vm->instructionPointers[ instruction-1 ] = compiledOfs;
				EmitString( "8B 80");						// mov eax, dword ptr [eax + 0x1234567]
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
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX && !jlabel ) {
				compiledOfs -= 2;
				vm->instructionPointers[ instruction-1 ] = compiledOfs;
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
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX && !jlabel ) {
				compiledOfs -= 2;
				vm->instructionPointers[ instruction-1 ] = compiledOfs;
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
			CMPI();
			JE();
			break;
		case OP_NE:
			CMPI();
			JNE();
			break;
		case OP_LTI:
			CMPI();
			JL();
			break;
		case OP_LEI:
			CMPI();
			JLE();
			break;
		case OP_GTI:
			CMPI();
			JG();
			break;
		case OP_GEI:
			CMPI();
			JGE();
			break;
		case OP_LTU:
			CMPI();
			JB();
			break;
		case OP_LEU:
			CMPI();
			JBE();
			break;
		case OP_GTU:
			CMPI();
			JA();
			break;
		case OP_GEU:
			CMPI();
			JAE();
			break;
		case OP_EQF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			if ( CPU_Flags & CPU_FCOM ) {
				FCOMFF();
				JE();
			} else  {
				FCOMSF();
				EmitString( "80 E4 40" );	// and ah,0x40
				JNE();
			}
			break;			
		case OP_NEF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			if ( CPU_Flags & CPU_FCOM ) {
				FCOMFF();
				JNE();
			} else  {
				FCOMSF();
				EmitString( "80 E4 40" );	// and ah,0x40
				JE();
			}
			break;			
		case OP_LTF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			if ( CPU_Flags & CPU_FCOM ) {
				FCOMFF();
				JB();
			} else  {
				FCOMSF();
				EmitString( "80 E4 01" );	// and ah,0x01
				JNE();
			}
			break;			
		case OP_LEF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			if ( CPU_Flags & CPU_FCOM ) {
				FCOMFF();
				JBE();
			} else  {
				FCOMSF();
				EmitString( "80 E4 41" );	// and ah,0x41
				JNE();
			}
			break;			
		case OP_GTF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			if ( CPU_Flags & CPU_FCOM ) {
				FCOMFF();
				JA();
			} else  {
				FCOMSF();
				EmitString( "80 E4 41" );	// and ah,0x41
				JE();
			}
			break;			
		case OP_GEF:
			EmitCommand(LAST_COMMAND_SUB_DI_8);		// sub edi, 8
			if ( CPU_Flags & CPU_FCOM ) {
				FCOMFF();
				JAE();
			} else  {
				FCOMSF();
				EmitString( "80 E4 01" );	// and ah,0x01
				JE();
			}
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
			EmitString( "8B 47 FC" );	// mov eax,dword ptr [edi-4]
			EmitString( "F7 2F" );		// imul dword ptr [edi]
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
			if ( FloatMerge() )
				break;
			EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
			EmitString( "D8 07" );					// fadd dword ptr [edi]
			EmitString( "D9 5F FC" );				// fstp dword ptr [edi-4]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;
		case OP_SUBF:
			if ( FloatMerge() )
				break;
			EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
			EmitString( "D8 27" );					// fsub dword ptr [edi]
			EmitString( "D9 5F FC" );				// fstp dword ptr [edi-4]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;
		case OP_DIVF:
			if ( FloatMerge() )
				break;
			EmitString( "D9 47 FC" );				// fld dword ptr [edi-4]
			EmitString( "D8 37" );					// fdiv dword ptr [edi]
			EmitString( "D9 5F FC" );				// fstp dword ptr [edi-4]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;
		case OP_MULF:
			if ( FloatMerge() )
				break;
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
			// call the library conversion function
			EmitString( "D9 07" );		// fld dword ptr [edi]
			EmitString( "FF 15" );		// call dword ptr [ftolPtr]
			Emit4( (int)&ftolPtr );
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
			Emit4( Constant4() >> 2 );
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
			EmitString( "FF 24 85" );           // jmp dword ptr [instructionPointers + eax * 4]
			Emit4( (int)vm->instructionPointers );
			EmitString( "FF 15" );              // call errJumpPtr
			Emit4( (int)&errJumpPtr );
			break;
		default:
		    VMFREE_BUFFERS();
			Com_Error(ERR_DROP, "VM_CompileX86: bad opcode %i at offset %i", op, pc);
		}
		pop1 = op;
	}
	}

	// copy to an exact size buffer on the hunk
	vm->codeLength = compiledOfs;
#ifdef VM_X86_MMAP
	vm->codeBase = mmap(NULL, compiledOfs, PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if(vm->codeBase == MAP_FAILED)
		Com_Error(ERR_FATAL, "VM_CompileX86: can't mmap memory");
#elif _WIN32
	// allocate memory with EXECUTE permissions under windows.
	vm->codeBase = VirtualAlloc(NULL, compiledOfs, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if(!vm->codeBase)
		Com_Error(ERR_FATAL, "VM_CompileX86: VirtualAlloc failed");
#else
	vm->codeBase = malloc(compiledOfs);
	if(!vm->codeBase)
	        Com_Error(ERR_FATAL, "VM_CompileX86: malloc failed");
#endif

	Com_Memcpy( vm->codeBase, buf, compiledOfs );

#ifdef VM_X86_MMAP
	if(mprotect(vm->codeBase, compiledOfs, PROT_READ|PROT_EXEC))
		Com_Error(ERR_FATAL, "VM_CompileX86: mprotect failed");
#elif _WIN32
	{
		DWORD oldProtect = 0;
		
		// remove write permissions.
		if(!VirtualProtect(vm->codeBase, compiledOfs, PAGE_EXECUTE_READ, &oldProtect))
			Com_Error(ERR_FATAL, "VM_CompileX86: VirtualProtect failed");
	}
#endif

	Z_Free( code );
	Z_Free( buf );
	Z_Free( jused );
	Com_Printf( "VM file %s compiled to %i bytes of code\n", vm->name, compiledOfs );

	vm->destroy = VM_Destroy_Compiled;

	// offset all the instruction pointers for the new location
	for ( i = 0 ; i < header->instructionCount ; i++ ) {
		vm->instructionPointers[i] += (int)vm->codeBase;
	}
}

void VM_Destroy_Compiled(vm_t* self)
{
#ifdef VM_X86_MMAP
	munmap(self->codeBase, self->codeLength);
#elif _WIN32
	VirtualFree(self->codeBase, 0, MEM_RELEASE);
#else
	free(self->codeBase);
#endif
}

/*
==============
VM_CallCompiled

This function is called directly by the generated code
==============
*/
int	VM_CallCompiled( vm_t *vm, int *args ) {
	int		stack[1024];
	size_t	programStack;
	size_t	stackOnEntry;
	byte	*image;
	void	*opStack;
	int		*oldInstructionPointers;
	int		oldInstructionCount;
	vm_t	*oldVM;

	oldVM = currentVM;
	oldInstructionPointers = instructionPointers;
	oldInstructionCount	= instructionCount;

	currentVM = vm;
	instructionPointers = vm->instructionPointers;
	instructionCount = vm->instructionCount;

	// interpret the code
	vm->currentlyInterpreting = qtrue;

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
	*(int *)&image[ programStack ] = -1;	// will terminate the loop on return

	// off we go into generated code...
	opStack = &stack[256];

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

	if ( opStack != &stack[257] ) {
		Com_Error( ERR_DROP, "opStack corrupted in compiled code" );
	}
	if ( programStack != stackOnEntry - 48 ) {
		Com_Error( ERR_DROP, "programStack corrupted in compiled code" );
	}

	vm->programStack = stackOnEntry;

	// in case we were recursively called by another vm
	currentVM = oldVM;
	instructionPointers = oldInstructionPointers;
	instructionCount = oldInstructionCount;

	return *(int *)opStack;
}
