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
// vm.c -- virtual machine

/*


intermix code and data
symbol table

a dll has one imported function: VM_SystemCall
and one exported function: Perform


*/

#include "vm_local.h"

opcode_info_t ops[ OP_MAX ] = 
{
	{ 0, 0, 0, 0 }, // undef
	{ 0, 0, 0, 0 }, // ignore
	{ 0, 0, 0, 0 }, // break

	{ 4, 0, 0, 0 }, // enter
	{ 4,-4, 0, 0 }, // leave
	{ 0, 0, 1, 0 }, // call
	{ 0, 4, 0, 0 }, // push
	{ 0,-4, 1, 0 }, // pop

	{ 4, 4, 0, 0 }, // const
	{ 4, 4, 0, 0 }, // local
	{ 0,-4, 1, 0 }, // jump

	{ 4,-8, 2, JUMP }, // eq
	{ 4,-8, 2, JUMP }, // ne

	{ 4,-8, 2, JUMP }, // lti
	{ 4,-8, 2, JUMP }, // lei
	{ 4,-8, 2, JUMP }, // gti
	{ 4,-8, 2, JUMP }, // gei

	{ 4,-8, 2, JUMP }, // ltu
	{ 4,-8, 2, JUMP }, // leu
	{ 4,-8, 2, JUMP }, // gtu
	{ 4,-8, 2, JUMP }, // geu

	{ 4,-8, 2, JUMP }, // eqf
	{ 4,-8, 2, JUMP }, // nef

	{ 4,-8, 2, JUMP }, // ltf
	{ 4,-8, 2, JUMP }, // lef
	{ 4,-8, 2, JUMP }, // gtf
	{ 4,-8, 2, JUMP }, // gef

	{ 0, 0, 1, 0 }, // load1
	{ 0, 0, 1, 0 }, // load2
	{ 0, 0, 1, 0 }, // load4
	{ 0,-8, 2, 0 }, // store1
	{ 0,-8, 2, 0 }, // store2
	{ 0,-8, 2, 0 }, // store4
	{ 1,-4, 1, 0 }, // arg
	{ 4,-8, 2, 0 }, // bcopy

	{ 0, 0, 1, 0 }, // sex8
	{ 0, 0, 1, 0 }, // sex16

	{ 0, 0, 1, 0 }, // negi
	{ 0,-4, 3, 0 }, // add
	{ 0,-4, 3, 0 }, // sub
	{ 0,-4, 3, 0 }, // divi
	{ 0,-4, 3, 0 }, // divu
	{ 0,-4, 3, 0 }, // modi
	{ 0,-4, 3, 0 }, // modu
	{ 0,-4, 3, 0 }, // muli
	{ 0,-4, 3, 0 }, // mulu

	{ 0,-4, 3, 0 }, // band
	{ 0,-4, 3, 0 }, // bor
	{ 0,-4, 3, 0 }, // bxor
	{ 0, 0, 1, 0 }, // bcom

	{ 0,-4, 3, 0 }, // lsh
	{ 0,-4, 3, 0 }, // rshi
	{ 0,-4, 3, 0 }, // rshu

	{ 0, 0, 1, 0 }, // negf
	{ 0,-4, 3, 0 }, // addf
	{ 0,-4, 3, 0 }, // subf
	{ 0,-4, 3, 0 }, // divf
	{ 0,-4, 3, 0 }, // mulf

	{ 0, 0, 1, 0 }, // cvif
	{ 0, 0, 1, 0 } // cvfi
};

const char *opname[ 256 ] = {
	"OP_UNDEF", 

	"OP_IGNORE", 

	"OP_BREAK",

	"OP_ENTER",
	"OP_LEAVE",
	"OP_CALL",
	"OP_PUSH",
	"OP_POP",

	"OP_CONST",

	"OP_LOCAL",

	"OP_JUMP",

	//-------------------

	"OP_EQ",
	"OP_NE",

	"OP_LTI",
	"OP_LEI",
	"OP_GTI",
	"OP_GEI",

	"OP_LTU",
	"OP_LEU",
	"OP_GTU",
	"OP_GEU",

	"OP_EQF",
	"OP_NEF",

	"OP_LTF",
	"OP_LEF",
	"OP_GTF",
	"OP_GEF",

	//-------------------

	"OP_LOAD1",
	"OP_LOAD2",
	"OP_LOAD4",
	"OP_STORE1",
	"OP_STORE2",
	"OP_STORE4",
	"OP_ARG",

	"OP_BLOCK_COPY",

	//-------------------

	"OP_SEX8",
	"OP_SEX16",

	"OP_NEGI",
	"OP_ADD",
	"OP_SUB",
	"OP_DIVI",
	"OP_DIVU",
	"OP_MODI",
	"OP_MODU",
	"OP_MULI",
	"OP_MULU",

	"OP_BAND",
	"OP_BOR",
	"OP_BXOR",
	"OP_BCOM",

	"OP_LSH",
	"OP_RSHI",
	"OP_RSHU",

	"OP_NEGF",
	"OP_ADDF",
	"OP_SUBF",
	"OP_DIVF",
	"OP_MULF",

	"OP_CVIF",
	"OP_CVFI"
};

cvar_t	*vm_rtChecks;

int		vm_debugLevel;

// used by Com_Error to get rid of running vm's before longjmp
static int forced_unload;

struct vm_s	vmTable[ VM_COUNT ];

static const char *vmName[ VM_COUNT ] = {
	"qagame",
	"cgame",
	"ui"
};

void VM_VmInfo_f( void );
void VM_VmProfile_f( void );


void VM_Debug( int level ) {
	vm_debugLevel = level;
}


/*
==============
VM_CheckBounds
==============
*/
void VM_CheckBounds( const vm_t *vm, unsigned int address, unsigned int length )
{
	//if ( !vm->entryPoint )
	{
		if ( (address | length) > vm->dataMask || (address + length) > vm->dataMask )
		{
			Com_Error( ERR_DROP, "program tried to bypass data segment bounds" );
		}
	}
}


/*
==============
VM_CheckBounds2
==============
*/
void VM_CheckBounds2( const vm_t *vm, unsigned int addr1, unsigned int addr2, unsigned int length )
{
	//if ( !vm->entryPoint )
	{
		if ( (addr1 | addr2 | length) > vm->dataMask || (addr1 + length) > vm->dataMask || (addr2+length) > vm->dataMask )
		{
			Com_Error( ERR_DROP, "program tried to bypass data segment bounds" );
		}
	}
}


/*
==============
VM_Init
==============
*/
void VM_Init( void ) {
	Cvar_Get( "vm_cgame", "2", CVAR_ARCHIVE | CVAR_PROTECTED );	// !@# SHIP WITH SET TO 2
	Cvar_Get( "vm_game", "2", CVAR_ARCHIVE | CVAR_PROTECTED );	// !@# SHIP WITH SET TO 2
	Cvar_Get( "vm_ui", "2", CVAR_ARCHIVE | CVAR_PROTECTED );	// !@# SHIP WITH SET TO 2

	Cmd_AddCommand( "vmprofile", VM_VmProfile_f );
	Cmd_AddCommand( "vminfo", VM_VmInfo_f );

	Com_Memset( vmTable, 0, sizeof( vmTable ) );
}


/*
===============
VM_ValueToSymbol

Assumes a program counter value
===============
*/
const char *VM_ValueToSymbol( vm_t *vm, int value ) {
	vmSymbol_t	*sym;
	static char		text[MAX_TOKEN_CHARS];

	sym = vm->symbols;
	if ( !sym ) {
		return "NO SYMBOLS";
	}

	// find the symbol
	while ( sym->next && sym->next->symValue <= value ) {
		sym = sym->next;
	}

	if ( value == sym->symValue ) {
		return sym->symName;
	}

	Com_sprintf( text, sizeof( text ), "%s+%i", sym->symName, value - sym->symValue );

	return text;
}


/*
===============
VM_ValueToFunctionSymbol

For profiling, find the symbol behind this value
===============
*/
vmSymbol_t *VM_ValueToFunctionSymbol( vm_t *vm, int value ) {
	vmSymbol_t	*sym;
	static vmSymbol_t	nullSym;

	sym = vm->symbols;
	if ( !sym ) {
		return &nullSym;
	}

	while ( sym->next && sym->next->symValue <= value ) {
		sym = sym->next;
	}

	return sym;
}


/*
===============
VM_SymbolToValue
===============
*/
int VM_SymbolToValue( vm_t *vm, const char *symbol ) {
	vmSymbol_t	*sym;

	for ( sym = vm->symbols ; sym ; sym = sym->next ) {
		if ( !strcmp( symbol, sym->symName ) ) {
			return sym->symValue;
		}
	}
	return 0;
}


/*
=====================
VM_SymbolForCompiledPointer
=====================
*/
#if 0 // 64bit!
const char *VM_SymbolForCompiledPointer( vm_t *vm, void *code ) {
	int			i;

	if ( code < (void *)vm->codeBase.ptr ) {
		return "Before code block";
	}
	if ( code >= (void *)(vm->codeBase.ptr + vm->codeLength) ) {
		return "After code block";
	}

	// find which original instruction it is after
	for ( i = 0 ; i < vm->codeLength ; i++ ) {
		if ( (void *)vm->instructionPointers[i] > code ) {
			break;
		}
	}
	i--;

	// now look up the bytecode instruction pointer
	return VM_ValueToSymbol( vm, i );
}
#endif



/*
===============
ParseHex
===============
*/
int	ParseHex( const char *text ) {
	int		value;
	int		c;

	value = 0;
	while ( ( c = *text++ ) != 0 ) {
		if ( c >= '0' && c <= '9' ) {
			value = value * 16 + c - '0';
			continue;
		}
		if ( c >= 'a' && c <= 'f' ) {
			value = value * 16 + 10 + c - 'a';
			continue;
		}
		if ( c >= 'A' && c <= 'F' ) {
			value = value * 16 + 10 + c - 'A';
			continue;
		}
	}

	return value;
}

/*
===============
VM_LoadSymbols
===============
*/
void VM_LoadSymbols( vm_t *vm ) {
	union {
		char	*c;
		void	*v;
	} mapfile;
	const char *text_p, *token;
	char	name[MAX_QPATH];
	char	symbols[MAX_QPATH];
	vmSymbol_t	**prev, *sym;
	int		count;
	int		value;
	int		chars;
	int		segment;
	int		numInstructions;

	// don't load symbols if not developer
	if ( !com_developer->integer ) {
		return;
	}

	COM_StripExtension(vm->name, name, sizeof(name));
	Com_sprintf( symbols, sizeof( symbols ), "vm/%s.map", name );
	FS_ReadFile( symbols, &mapfile.v );
	if ( !mapfile.c ) {
		Com_Printf( "Couldn't load symbol file: %s\n", symbols );
		return;
	}

	numInstructions = vm->instructionCount;

	// parse the symbols
	text_p = mapfile.c;
	prev = &vm->symbols;
	count = 0;

	while ( 1 ) {
		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			break;
		}
		segment = ParseHex( token );
		if ( segment ) {
			COM_Parse( &text_p );
			COM_Parse( &text_p );
			continue;		// only load code segment values
		}

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			Com_Printf( "WARNING: incomplete line at end of file\n" );
			break;
		}
		value = ParseHex( token );

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			Com_Printf( "WARNING: incomplete line at end of file\n" );
			break;
		}
		chars = strlen( token );
		sym = Hunk_Alloc( sizeof( *sym ) + chars, h_high );
		*prev = sym;
		prev = &sym->next;
		sym->next = NULL;

		// convert value from an instruction number to a code offset
		if ( vm->instructionPointers && value >= 0 && value < numInstructions ) {
			value = vm->instructionPointers[value];
		}

		sym->symValue = value;
		Q_strncpyz( sym->symName, token, chars + 1 );

		count++;
	}

	vm->numSymbols = count;
	Com_Printf( "%i symbols parsed from %s\n", count, symbols );
	FS_FreeFile( mapfile.v );
}


/*
============
VM_DllSyscall

Dlls will call this directly

 rcg010206 The horror; the horror.

  The syscall mechanism relies on stack manipulation to get its args.
   This is likely due to C's inability to pass "..." parameters to
   a function in one clean chunk. On PowerPC Linux, these parameters
   are not necessarily passed on the stack, so while (&arg[0] == arg)
   is true, (&arg[1] == 2nd function parameter) is not necessarily
   accurate, as arg's value might have been stored to the stack or
   other piece of scratch memory to give it a valid address, but the
   next parameter might still be sitting in a register.

  Quake's syscall system also assumes that the stack grows downward,
   and that any needed types can be squeezed, safely, into a signed int.

  This hack below copies all needed values for an argument to a
   array in memory, so that Quake can get the correct values. This can
   also be used on systems where the stack grows upwards, as the
   presumably standard and safe stdargs.h macros are used.

  As for having enough space in a signed int for your datatypes, well,
   it might be better to wait for DOOM 3 before you start porting.  :)

  The original code, while probably still inherently dangerous, seems
   to work well enough for the platforms it already works on. Rather
   than add the performance hit for those platforms, the original code
   is still in use there.

  For speed, we just grab 15 arguments, and don't worry about exactly
   how many the syscall actually needs; the extra is thrown away.
 
============
*/
#if 0 // - disabled because now is different for each module
intptr_t QDECL VM_DllSyscall( intptr_t arg, ... ) {
#if !id386 || defined __clang__
  // rcg010206 - see commentary above
  intptr_t	args[16];
  va_list	ap;
  int i;
  
  args[0] = arg;
  
  va_start( ap, arg );
  for (i = 1; i < ARRAY_LEN( args ); i++ )
    args[ i ] = va_arg( ap, intptr_t );
  va_end( ap );
  
  return currentVM->systemCall( args );
#else // original id code
	return currentVM->systemCall( &arg );
#endif
}
#endif


static int Load_JTS( vm_t *vm, unsigned int crc32, void *data, int vmPakIndex )  {
	char		filename[MAX_QPATH];
	int			header[2];
	int			length, i;
	fileHandle_t fh;

	// load the image
	Com_sprintf( filename, sizeof(filename), "vm/%s.jts", vm->name );
	if ( data )
		Com_Printf( "Loading jts file %s...\n", filename );

	length = FS_FOpenFileRead( filename, &fh, qtrue );
	
	if ( fh == FS_INVALID_HANDLE ) {
		if ( data )
			Com_Printf( " not found.\n" );
		return -1;
	}

	if ( fs_lastPakIndex != vmPakIndex ) {
		Com_DPrintf( " invalid pak index %i (expecting %i) for %s.\n", fs_lastPakIndex, vmPakIndex, filename );
		FS_FCloseFile( fh );
		return -1;
	}

	if ( length < sizeof( header ) ) {
		if ( data )
			Com_Printf( " bad filesize %i for %s.\n", length, filename );
		FS_FCloseFile( fh );
		return -1;
	}

	if ( FS_Read( header, sizeof( header ), fh ) != sizeof( header ) ) {
		if ( data )
			Com_Printf( " error reading header of %s.\n", filename );
		FS_FCloseFile( fh );
		return -1;
	}

	// byte swap the header
	for ( i = 0 ; i < sizeof( header  ) / sizeof( int ) ; i++ ) {
		((int *)header)[i] = LittleLong( ((int *)header)[i] );
	}

	if ( (unsigned int)header[0] != crc32 ) {
		if ( data )
			Com_Printf( " crc32 mismatch: %08X <-> %08X.\n", header[0], crc32 );
		FS_FCloseFile( fh );
		return -1;
	}

	if ( header[1] < 0 || header[1] != (length - (int)sizeof( header ) ) ) {
		if ( data )
			Com_Printf( " bad file header.\n" );
		FS_FCloseFile( fh );
		return -1;
	}

	length -= sizeof( header ); // skip header and filesize

	// we need just filesize
	if ( !data ) { 
		FS_FCloseFile( fh );
		return length;
	}

	FS_Read( data, length, fh );
	FS_FCloseFile( fh );

	// byte swap the data
	for ( i = 0 ; i < length / sizeof( int ); i++ ) {
		((int *)data)[i] = LittleLong( ((int *)data)[i] );
	}

	return length;
}


/*
=================
VM_ValidateHeader
=================
*/
static char *VM_ValidateHeader( vmHeader_t *header, int fileSize ) 
{
	static char errMsg[128];
	int i, n;

	// truncated
	if ( fileSize < ( sizeof( vmHeader_t ) - sizeof( int ) ) ) {
		sprintf( errMsg, "truncated image header (%i bytes long)", fileSize );
		return errMsg;
	}

	// bad magic
	if ( LittleLong( header->vmMagic ) != VM_MAGIC && LittleLong( header->vmMagic ) != VM_MAGIC_VER2 ) {
		sprintf( errMsg, "bad file magic %08x", LittleLong( header->vmMagic ) );
		return errMsg;
	}
	
	// truncated
	if ( fileSize < sizeof( vmHeader_t ) && LittleLong( header->vmMagic ) != VM_MAGIC_VER2 ) {
		sprintf( errMsg, "truncated image header (%i bytes long)", fileSize );
		return errMsg;
	}

	if ( LittleLong( header->vmMagic ) == VM_MAGIC_VER2 )
		n = sizeof( vmHeader_t ) / sizeof( int );
	else
		n = ( sizeof( vmHeader_t ) - sizeof( int ) ) / sizeof( int );

	// byte swap the header
	for ( i = 0 ; i < n ; i++ ) {
		((int *)header)[i] = LittleLong( ((int *)header)[i] );
	}

	// bad code offset
	if ( header->codeOffset >= fileSize ) {
		sprintf( errMsg, "bad code segment offset %i", header->codeOffset );
		return errMsg;
	}

	// bad code length
	if ( header->codeLength <= 0 || header->codeOffset + header->codeLength > fileSize ) {
		sprintf( errMsg, "bad code segment length %i", header->codeLength );
		return errMsg;
	}

	// bad data offset
	if ( header->dataOffset >= fileSize || header->dataOffset != header->codeOffset + header->codeLength ) {
		sprintf( errMsg, "bad data segment offset %i", header->dataOffset );
		return errMsg;
	}

	// bad data length
	if ( header->dataOffset + header->dataLength > fileSize )  {
		sprintf( errMsg, "bad data segment length %i", header->dataLength );
		return errMsg;
	}

	if ( header->vmMagic == VM_MAGIC_VER2 ) 
	{
		// bad lit/jtrg length
		if ( header->dataOffset + header->dataLength + header->litLength + header->jtrgLength != fileSize ) {
			sprintf( errMsg, "bad lit/jtrg segment length" );
			return errMsg;
		}
	} 
	// bad lit length
	else if ( header->dataOffset + header->dataLength + header->litLength != fileSize ) 
	{
		sprintf( errMsg, "bad lit segment length %i", header->litLength );
		return errMsg;
	}

	return NULL;	
}


/*
=================
VM_LoadQVM

Load a .qvm file

if ( alloc )
 - Validate header, swap data
 - Alloc memory for data/instructions
 - Alloc memory for instructionPointers - NOT NEEDED
 - Load instructions
 - Clear/load data
else
 - Check for header changes
 - Clear/load data

=================
*/
static vmHeader_t *VM_LoadQVM( vm_t *vm, qboolean alloc ) {
	int					length;
	unsigned int		dataLength;
	unsigned int		dataAlloc;
	int					i;
	char				filename[MAX_QPATH], *errorMsg;
	unsigned int		crc32sum;
	qboolean			tryjts;
	vmHeader_t			*header;
	int					vmPakIndex;

	// load the image
	Com_sprintf( filename, sizeof(filename), "vm/%s.qvm", vm->name );
	Com_Printf( "Loading vm file %s...\n", filename );
	length = FS_ReadFile( filename, (void **)&header );
	if ( !header ) {
		Com_Printf( "Failed.\n" );
		VM_Free( vm );
		return NULL;
	}

	vmPakIndex = fs_lastPakIndex;

	crc32sum = crc32_buffer( (const byte*) header, length );

	// will also swap header
	errorMsg = VM_ValidateHeader( header, length );
	if ( errorMsg ) {
		VM_Free( vm );
		FS_FreeFile( header );
		Com_Error( ERR_FATAL, "%s", errorMsg );
		return NULL;
	}

	vm->crc32sum = crc32sum;
	tryjts = qfalse;

	if( header->vmMagic == VM_MAGIC_VER2 ) {
		Com_Printf( "...which has vmMagic VM_MAGIC_VER2\n" );
	} else {
		tryjts = qtrue;
	}

	
	vm->exactDataLength = header->dataLength + header->litLength + header->bssLength;
	dataLength = vm->exactDataLength + PROGRAM_STACK_EXTRA;
	vm->dataLength = dataLength;

	// round up to next power of 2 so all data operations can
	// be mask protected
	for ( i = 0 ; dataLength > ( 1 << i ) ; i++ ) 
		;
	dataLength = 1 << i;

	// reserve some space for effective LOCAL+LOAD* checks
	dataAlloc = dataLength + 1024;

	if ( dataLength >= (1U<<31) || dataAlloc >= (1U<<31) ) {
		VM_Free( vm );
		FS_FreeFile( header );
		Com_Error( ERR_FATAL, "%s: data segment is too large", __func__ );
		return NULL;
	}

	if ( alloc ) {
		// allocate zero filled space for initialized and uninitialized data
		vm->dataBase = Hunk_Alloc( dataAlloc, h_high );
		vm->dataMask = dataLength - 1;
		vm->dataAlloc = dataAlloc;
	} else {
		// clear the data, but make sure we're not clearing more than allocated
		if ( vm->dataAlloc != dataAlloc ) {
			VM_Free( vm );
			FS_FreeFile( header );
			Com_Printf( S_COLOR_YELLOW "Warning: Data region size of %s not matching after"
					"VM_Restart()\n", filename );
			return NULL;
		}
		Com_Memset( vm->dataBase, 0, vm->dataAlloc );
	}

	// copy the intialized data
	Com_Memcpy( vm->dataBase, (byte *)header + header->dataOffset, header->dataLength + header->litLength );

	// byte swap the longs
	for ( i = 0 ; i < header->dataLength ; i += 4 ) {
		*(int *)(vm->dataBase + i) = LittleLong( *(int *)(vm->dataBase + i ) );
	}

	if( header->vmMagic == VM_MAGIC_VER2 ) {
		int previousNumJumpTableTargets = vm->numJumpTableTargets;

		header->jtrgLength &= ~0x03;

		vm->numJumpTableTargets = header->jtrgLength >> 2;
		Com_Printf( "Loading %d jump table targets\n", vm->numJumpTableTargets );

		if ( alloc ) {
			vm->jumpTableTargets = Hunk_Alloc( header->jtrgLength, h_high );
		} else {
			if ( vm->numJumpTableTargets != previousNumJumpTableTargets ) {
				VM_Free( vm );
				FS_FreeFile( header );

				Com_Printf( S_COLOR_YELLOW "Warning: Jump table size of %s not matching after "
						"VM_Restart()\n", filename );
				return NULL;
			}

			Com_Memset( vm->jumpTableTargets, 0, header->jtrgLength );
		}

		Com_Memcpy( vm->jumpTableTargets, (byte *)header + header->dataOffset +
				header->dataLength + header->litLength, header->jtrgLength );

		// byte swap the longs
		for ( i = 0 ; i < header->jtrgLength ; i += 4 ) {
			*(int *)(vm->jumpTableTargets + i) = LittleLong( *(int *)(vm->jumpTableTargets + i ) );
		}
	}

	if ( tryjts == qtrue && (length = Load_JTS( vm, crc32sum, NULL, vmPakIndex )) >= 0 ) {
		// we are trying to load newer file?
		if ( vm->jumpTableTargets && vm->numJumpTableTargets != length >> 2 ) {
			Com_Printf( S_COLOR_YELLOW "Reload jts file\n" );
			vm->jumpTableTargets = NULL;
			alloc = qtrue;
		}
		vm->numJumpTableTargets = length >> 2;
		Com_Printf( "Loading %d external jump table targets\n", vm->numJumpTableTargets );
		if ( alloc == qtrue ) {
			vm->jumpTableTargets = Hunk_Alloc( length, h_high );
		} else {
			Com_Memset( vm->jumpTableTargets, 0, length );
		}
		Load_JTS( vm, crc32sum, vm->jumpTableTargets, vmPakIndex );
	}

	return header;
}


/*
=================
VM_LoadInstructions

loads instructions in structured format
=================
*/
const char *VM_LoadInstructions( const vmHeader_t *header, instruction_t *buf ) 
{
	static char errBuf[ 128 ];
	byte *code_pos, *code_start, *code_end;
	int i, n, op0, op1, opStack;
	instruction_t *ci;
	
	code_pos = (byte *) header + header->codeOffset;
	code_start = code_pos; // for printing
	code_end =  (byte *) header + header->codeOffset + header->codeLength;

	ci = buf;
	opStack = 0;
	op1 = OP_UNDEF;

	// load instructions and perform some initial calculations/checks
	for ( i = 0; i < header->instructionCount; i++, ci++, op1 = op0 ) {
		op0 = *code_pos;
		if ( op0 < 0 || op0 >= OP_MAX ) {
			sprintf( errBuf, "bad opcode %02X at offset %d", op0, (int)(code_pos - code_start) );
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
			ci->value = *((unsigned char*)code_pos);
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
	}

	return NULL;
}


/*
===============================
VM_CheckInstructions

performs additional consistency and security checks
===============================
*/
const char *VM_CheckInstructions( instruction_t *buf, 
								 int instructionCount, 
								 const byte *jumpTableTargets, 
								 int numJumpTableTargets, 
								 int dataLength ) 
{
	static char errBuf[ 128 ];
	int i, n, v, op0, op1, opStack, pstack;
	instruction_t *ci, *proc;
	int startp, endp;

	ci = buf;
	opStack = 0;

	// opstack checks
	for ( i = 0; i < instructionCount; i++, ci++ ) {
		opStack += ops[ ci->op ].stack;
		if ( opStack < 0 ) {
			sprintf( errBuf, "opStack underflow at %i", i ); 
			return errBuf;
		}
		if ( opStack >= PROC_OPSTACK_SIZE * 4 ) {
			sprintf( errBuf, "opStack overflow at %i", i ); 
			return errBuf;
		}
	}

	ci = buf;
	pstack = 0;
	op1 = OP_UNDEF;
	proc = NULL;

	startp = 0;
	endp = instructionCount - 1;

	// Additional security checks

	for ( i = 0; i < instructionCount; i++, ci++, op1 = op0 ) {
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
			for ( endp = 0, n = i+1 ; n < instructionCount; n++ ) {
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

		// proc opstack will carry max.possible opstack value
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
				endp = instructionCount - 1; // end of the image
			}
			continue;
		}

		// conditional jumps
		if ( ops[ ci->op ].flags & JUMP ) {
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
					if ( v >= instructionCount ) {
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
			if ( (ci+1)->op == OP_LOAD1 || (ci+1)->op == OP_LOAD2 || (ci+1)->op == OP_LOAD4 || (ci+1)->op == OP_ARG ) {
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

		if ( ci->op == OP_BLOCK_COPY ) {
			v = ci->value;
			if ( v >= dataLength ) {
				sprintf( errBuf, "bad count %i for block copy at %i", v, i - 1 );
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
		// first pass - validate
		for( i = 0; i < numJumpTableTargets; i++ ) {
			n = *(int *)(jumpTableTargets + ( i * sizeof( int ) ) );
			if ( n < 0 || n >= instructionCount ) {
				Com_Printf( S_COLOR_YELLOW "jump target %i set on instruction %i that is out of range [0..%i]",
					i, n, instructionCount - 1 ); 
				break;
			}
			if ( buf[n].opStack != 0 ) {
				Com_Printf( S_COLOR_YELLOW "jump target %i set on instruction %i (%s) with bad opStack %i\n",
					i, n, opname[ buf[n].op ], buf[n].opStack ); 
				break;
			}
		}
		if ( i != numJumpTableTargets ) {
			// we may trap this on buggy VM_MAGIC_VER2 images
			// but we can safely optimize code even without JTRGSEG
			// so just switch to VM_MAGIC path here
			goto __noJTS;
		}
		// second pass - apply
		for( i = 0; i < numJumpTableTargets; i++ ) {
			n = *(int *)(jumpTableTargets + ( i * sizeof( int ) ) );
			buf[n].jused = 1;
		}
	} else {
__noJTS:
		v = 0;
		// instructions with opStack > 0 can't be jump labels so its safe to optimize/merge
		for ( i = 0, ci = buf; i < instructionCount; i++, ci++ ) {
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


/*
=================
VM_ReplaceInstructions
=================
*/
void VM_ReplaceInstructions( vm_t *vm, instruction_t *buf ) {
	instruction_t *ip;

	//Com_Printf( S_COLOR_GREEN "[%s] crc: %08x, ic: %i, dl: %i\n", vm->name, vm->crc32sum, vm->instructionCount, vm->dataLength );
	if ( vm->index == VM_CGAME ) {
		if ( vm->crc32sum == 0x3E93FC1A && vm->instructionCount == 123596 && vm->exactDataLength == 2007536 ) {
			ip = buf + 110190;
			if ( ip->op == OP_ENTER && (ip+183)->op == OP_LEAVE && ip->value == (ip+183)->value ) {
				ip++;
				ip->op = OP_CONST;	ip->value = 110372; ip++;
				ip->op = OP_JUMP;	ip->value = 0; ip++;
				ip->op = OP_IGNORE; ip->value = 0; ip++;
			}
			if ( buf[4358].op == OP_LOCAL && buf[4358].value == 308 && buf[4359].op == OP_CONST && !buf[4359].value ) {
				buf[4359].value++;
			}
		} 
		else
		if ( vm->crc32sum == 0xF0F1AE90 && vm->instructionCount == 123552 && vm->exactDataLength == 2007520 ) {
			ip = buf + 110177;
			if ( ip->op == OP_ENTER && (ip+183)->op == OP_LEAVE && ip->value == (ip+183)->value ) {
				ip++;
				ip->op = OP_CONST;	ip->value = 110359; ip++;
				ip->op = OP_JUMP;	ip->value = 0; ip++;
				ip->op = OP_IGNORE; ip->value = 0; ip++;
			}
			if ( buf[4358].op == OP_LOCAL && buf[4358].value == 308 && buf[4359].op == OP_CONST && !buf[4359].value ) {
				buf[4359].value++;
			}
		}
		if ( vm->crc32sum == 0x051D4668 && vm->instructionCount == 267812 && vm->exactDataLength == 38064376 ) {
			int i;
			ip = buf + 235;
			if ( ip->value == 70943 ) {
				for ( i = 0; i < 8; i++ ) {
					ip[i].op = OP_IGNORE;
				}
			}
		}
	}

	if ( vm->index == VM_GAME ) {
		if ( vm->crc32sum == 0x5AAE0ACC && vm->instructionCount == 251521 && vm->exactDataLength == 1872464 ) {
			vm->forceDataMask = qtrue; // OSP server doing some bad things with memory
		} else {
			vm->forceDataMask = qfalse;
		}
	}
}


/*
=================
VM_Restart

Reload the data, but leave everything else in place
This allows a server to do a map_restart without changing memory allocation
=================
*/
vm_t *VM_Restart( vm_t *vm ) {
	vmHeader_t	*header;

	// DLL's can't be restarted in place
	if ( vm->dllHandle ) {
		syscall_t		systemCall;
		dllSyscall_t	dllSyscall;
		const int*		vmMainArgs;
		vmIndex_t		index;
				
		index = vm->index;
		systemCall = vm->systemCall;
		dllSyscall = vm->dllSyscall;
		vmMainArgs = vm->vmMainArgs;

		VM_Free( vm );

		vm = VM_Create( index, systemCall, dllSyscall, vmMainArgs, VMI_NATIVE );
		return vm;
	}

	// load the image
	Com_Printf( "VM_Restart()\n" );

	if( ( header = VM_LoadQVM( vm, qfalse ) ) == NULL ) {
		Com_Error( ERR_DROP, "VM_Restart failed" );
		return NULL;
	}

	// free the original file
	FS_FreeFile( header );

	return vm;
}


/*
================
VM_Create

If image ends in .qvm it will be interpreted, otherwise
it will attempt to load as a system dll
================
*/
vm_t *VM_Create( vmIndex_t index, syscall_t systemCalls, dllSyscall_t dllSyscalls, const int *vmMainArgs, vmInterpret_t interpret ) {
	int			remaining;
	const char	*name;
	vmHeader_t	*header;
	vm_t		*vm;

	if ( !systemCalls ) {
		Com_Error( ERR_FATAL, "VM_Create: bad parms" );
	}

	if ( (unsigned)index >= VM_COUNT ) {
		Com_Error( ERR_FATAL, "VM_Create: bad vm index %i", index );	
	}

	remaining = Hunk_MemoryRemaining();

	vm = &vmTable[ index ];

	// see if we already have the VM
	if ( vm->name ) {
		if ( vm->index != index ) {
			Com_Error( ERR_FATAL, "VM_Create: bad allocated vm index %i", vm->index );
			return NULL;
		}
		return vm;
	}

	name = vmName[ index ];

	vm->name = name;
	vm->index = index;
	vm->systemCall = systemCalls;
	vm->dllSyscall = dllSyscalls;
	vm->vmMainArgs = vmMainArgs;
	vm->privateFlag = CVAR_PRIVATE;

	// never allow dll loading with a demo
	if ( interpret == VMI_NATIVE ) {
		if ( Cvar_VariableIntegerValue( "fs_restrict" ) ) {
			interpret = VMI_COMPILED;
		}
	}

	if ( interpret == VMI_NATIVE ) {
		// try to load as a system dll
		Com_Printf( "Loading dll file %s.\n", name );
		vm->dllHandle = Sys_LoadDll( name, &vm->entryPoint, dllSyscalls );
		if ( vm->dllHandle ) {
			vm->privateFlag = 0; // allow reading private cvars
			vm->dataAlloc = ~0U;
			vm->dataMask = ~0U;
			vm->dataBase = 0;
			return vm;
		}

		Com_Printf( "Failed to load dll, looking for qvm.\n" );
		interpret = VMI_COMPILED;
	}

	// load the image
	if( ( header = VM_LoadQVM( vm, qtrue ) ) == NULL ) {
		return NULL;
	}

	// allocate space for the jump targets, which will be filled in by the compile/prep functions
	vm->instructionCount = header->instructionCount;
	//vm->instructionPointers = Hunk_Alloc(vm->instructionCount * sizeof(*vm->instructionPointers), h_high);
	vm->instructionPointers = NULL;

	// copy or compile the instructions
	vm->codeLength = header->codeLength;

	// the stack is implicitly at the end of the image
	vm->programStack = vm->dataMask + 1;
	vm->stackBottom = vm->programStack - PROGRAM_STACK_SIZE - PROGRAM_STACK_EXTRA;

	vm->compiled = qfalse;

#ifdef NO_VM_COMPILED
	if(interpret >= VMI_COMPILED) {
		Com_Printf("Architecture doesn't have a bytecode compiler, using interpreter\n");
		interpret = VMI_BYTECODE;
	}
#else
	if ( interpret >= VMI_COMPILED ) {
		vm->compiled = qtrue;
		if ( !VM_Compile( vm, header ) ) {
			FS_FreeFile( header );	// free the original file
			VM_Free( vm );
			return NULL;
		}
	}
#endif
	// VM_Compile may have reset vm->compiled if compilation failed
	if ( !vm->compiled ) {
		if ( !VM_PrepareInterpreter2( vm, header ) ) {
			FS_FreeFile( header );	// free the original file
			VM_Free( vm );
			return NULL;
		}
	}

	// free the original file
	FS_FreeFile( header );

	// load the map file
	VM_LoadSymbols( vm );

	Com_Printf( "%s loaded in %d bytes on the hunk\n", vm->name, remaining - Hunk_MemoryRemaining() );

	return vm;
}


/*
==============
VM_Free
==============
*/
void VM_Free( vm_t *vm ) {

	if( !vm ) {
		return;
	}

	if ( vm->callLevel ) {
		if ( !forced_unload ) {
			Com_Error( ERR_FATAL, "VM_Free(%s) on running vm", vm->name );
			return;
		} else {
			Com_Printf( "forcefully unloading %s vm\n", vm->name );
		}
	}

	if ( vm->destroy )
		vm->destroy( vm );

	if ( vm->dllHandle )
		Sys_UnloadDll( vm->dllHandle );

#if 0	// now automatically freed by hunk
	if ( vm->codeBase.ptr ) {
		Z_Free( vm->codeBase.ptr );
	}
	if ( vm->dataBase ) {
		Z_Free( vm->dataBase );
	}
	if ( vm->instructionPointers ) {
		Z_Free( vm->instructionPointers );
	}
#endif
	Com_Memset( vm, 0, sizeof( *vm ) );
}


void VM_Clear( void ) {
	int i;
	for ( i = 0; i < VM_COUNT; i++ ) {
		VM_Free( &vmTable[ i ] );
	}
}


void VM_Forced_Unload_Start(void) {
	forced_unload = 1;
}


void VM_Forced_Unload_Done(void) {
	forced_unload = 0;
}


/*
==============
VM_Call


Upon a system call, the stack will look like:

sp+32	parm1
sp+28	parm0
sp+24	return value
sp+20	return address
sp+16	local1
sp+14	local0
sp+12	arg1
sp+8	arg0
sp+4	return stack
sp		return address

An interpreted function will immediately execute
an OP_ENTER instruction, which will subtract space for
locals from sp
==============
*/

intptr_t QDECL VM_Call( vm_t *vm, int callnum, ... )
{
	//vm_t	*oldVM;
	intptr_t r;
	int	nargs;
	int i;

	if ( !vm ) {
		Com_Error( ERR_FATAL, "VM_Call with NULL vm" );
	}

	if ( vm_debugLevel ) {
	  Com_Printf( "VM_Call( %d )\n", callnum );
	}

	nargs = vm->vmMainArgs[ callnum ]; // counting callnum

	++vm->callLevel;
	// if we have a dll loaded, call it directly
	if ( vm->entryPoint ) 
	{
		//rcg010207 -  see dissertation at top of VM_DllSyscall() in this file.
		int args[MAX_VMMAIN_CALL_ARGS-1];
		va_list ap;
		va_start( ap, callnum );
		for ( i = 0; i < nargs-1; i++ ) {
			args[i] = va_arg( ap, int );
		}
		va_end(ap);

		// add more agruments if you're changed MAX_VMMAIN_CALL_ARGS:
		r = vm->entryPoint( callnum, args[0], args[1], args[2] );
	} else {
#if id386 && !defined __clang__ // calling convention doesn't need conversion in some cases
#ifndef NO_VM_COMPILED
		if ( vm->compiled )
			r = VM_CallCompiled( vm, nargs, (int*)&callnum );
		else
#endif
			r = VM_CallInterpreted2( vm, nargs, (int*)&callnum );
#else
		int args[MAX_VMMAIN_CALL_ARGS];
		va_list ap;

		args[0] = callnum;
		va_start( ap, callnum );
		for ( i = 1; i < nargs; i++ ) {
			args[i] = va_arg( ap, int );
		}
		va_end(ap);
#ifndef NO_VM_COMPILED
		if ( vm->compiled )
			r = VM_CallCompiled( vm, nargs, &args[0] );
		else
#endif
			r = VM_CallInterpreted2( vm, nargs, &args[0] );
#endif
	}
	--vm->callLevel;

	return r;
}


//=================================================================

static int QDECL VM_ProfileSort( const void *a, const void *b ) {
	vmSymbol_t	*sa, *sb;

	sa = *(vmSymbol_t **)a;
	sb = *(vmSymbol_t **)b;

	if ( sa->profileCount < sb->profileCount ) {
		return -1;
	}
	if ( sa->profileCount > sb->profileCount ) {
		return 1;
	}
	return 0;
}


/*
==============
VM_NameToVM
==============
*/
vm_t *VM_NameToVM( const char *name ) 
{
	vmIndex_t index;

	if ( !Q_stricmp( name, "game" ) )
		index = VM_GAME;
	else if ( !Q_stricmp( name, "cgame" ) )
		index = VM_CGAME;
	else if ( !Q_stricmp( name, "ui" ) )
		index = VM_UI;
	else {
		Com_Printf( " unknown VM name '%s'\n", name );
		return NULL;
	}

	if ( !vmTable[ index ].name ) {
		Com_Printf( " %s is not running.\n", name );
		return NULL;
	}

	return &vmTable[ index ];
}


/*
==============
VM_VmProfile_f

==============
*/
void VM_VmProfile_f( void ) {
	vm_t		*vm;
	vmSymbol_t	**sorted, *sym;
	int			i;
	double		total;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: %s <game|cgame|ui>\n", Cmd_Argv( 0 ) );
		return;
	}

	vm = VM_NameToVM( Cmd_Argv( 1 ) );
	if ( vm == NULL ) {
		return;
	}

	if ( !vm->numSymbols ) {
		return;
	}

	sorted = Z_Malloc( vm->numSymbols * sizeof( *sorted ) );
	sorted[0] = vm->symbols;
	total = sorted[0]->profileCount;
	for ( i = 1 ; i < vm->numSymbols ; i++ ) {
		sorted[i] = sorted[i-1]->next;
		total += sorted[i]->profileCount;
	}

	qsort( sorted, vm->numSymbols, sizeof( *sorted ), VM_ProfileSort );

	for ( i = 0 ; i < vm->numSymbols ; i++ ) {
		int		perc;

		sym = sorted[i];

		perc = 100 * (float) sym->profileCount / total;
		Com_Printf( "%2i%% %9i %s\n", perc, sym->profileCount, sym->symName );
		sym->profileCount = 0;
	}

	Com_Printf("    %9.0f total\n", total );

	Z_Free( sorted );
}


/*
==============
VM_VmInfo_f
==============
*/
void VM_VmInfo_f( void ) {
	vm_t	*vm;
	int		i;

	Com_Printf( "Registered virtual machines:\n" );
	for ( i = 0 ; i < VM_COUNT ; i++ ) {
		vm = &vmTable[i];
		if ( !vm->name ) {
			continue;
		}
		Com_Printf( "%s : ", vm->name );
		if ( vm->dllHandle ) {
			Com_Printf( "native\n" );
			continue;
		}
		if ( vm->compiled ) {
			Com_Printf( "compiled on load\n" );
		} else {
			Com_Printf( "interpreted\n" );
		}
		Com_Printf( "    code length : %7i\n", vm->codeLength );
		Com_Printf( "    table length: %7i\n", vm->instructionCount*4 );
		Com_Printf( "    data length : %7i\n", vm->dataMask + 1 );
	}
}


/*
===============
VM_LogSyscalls

Insert calls to this while debugging the vm compiler
===============
*/
void VM_LogSyscalls( int *args ) {
#if 0
	static	int		callnum;
	static	FILE	*f;

	if ( !f ) {
		f = Sys_FOpen( "syscalls.log", "w" );
		if ( !f ) {
			return;
		}
	}
	callnum++;
	fprintf( f, "%i: %p (%i) = %i %i %i %i\n", callnum, (void*)(args - (int *)currentVM->dataBase),
		args[0], args[1], args[2], args[3], args[4] );
#endif
}
