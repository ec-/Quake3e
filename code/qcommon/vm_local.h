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
#ifndef VM_LOCAL_H
#define VM_LOCAL_H

#include "q_shared.h"
#include "qcommon.h"

#define	MAX_OPSTACK_SIZE	512
#define	PROC_OPSTACK_SIZE	30

// we don't need more than 4 arguments (counting callnum) for vmMain, at least in Quake3
#define MAX_VMMAIN_CALL_ARGS 4

// don't change
// Hardcoded in q3asm an reserved at end of bss
#define	PROGRAM_STACK_SIZE	0x10000

// for some buggy mods
#define	PROGRAM_STACK_EXTRA	(32*1024)

typedef enum {
	OP_UNDEF,

	OP_IGNORE,

	OP_BREAK,

	OP_ENTER,
	OP_LEAVE,
	OP_CALL,
	OP_PUSH,
	OP_POP,

	OP_CONST,
	OP_LOCAL,

	OP_JUMP,

	//-------------------

	OP_EQ,
	OP_NE,

	OP_LTI,
	OP_LEI,
	OP_GTI,
	OP_GEI,

	OP_LTU,
	OP_LEU,
	OP_GTU,
	OP_GEU,

	OP_EQF,
	OP_NEF,

	OP_LTF,
	OP_LEF,
	OP_GTF,
	OP_GEF,

	//-------------------

	OP_LOAD1,
	OP_LOAD2,
	OP_LOAD4,
	OP_STORE1,
	OP_STORE2,
	OP_STORE4,				// *(stack[top-1]) = stack[top]
	OP_ARG,

	OP_BLOCK_COPY,

	//-------------------

	OP_SEX8,
	OP_SEX16,

	OP_NEGI,
	OP_ADD,
	OP_SUB,
	OP_DIVI,
	OP_DIVU,
	OP_MODI,
	OP_MODU,
	OP_MULI,
	OP_MULU,

	OP_BAND,
	OP_BOR,
	OP_BXOR,
	OP_BCOM,

	OP_LSH,
	OP_RSHI,
	OP_RSHU,

	OP_NEGF,
	OP_ADDF,
	OP_SUBF,
	OP_DIVF,
	OP_MULF,

	OP_CVIF,
	OP_CVFI,

	OP_MAX
} opcode_t;

typedef struct {
	int		value;	// 32
	byte	op;		// 8
	byte	opStack;	// 8
	unsigned jused:1;
	unsigned swtch:1;
} instruction_t;

typedef struct vmSymbol_s {
	struct vmSymbol_s	*next;
	int		symValue;
	int		profileCount;
	char	symName[1];		// variable sized
} vmSymbol_t;

//typedef void(*vmfunc_t)(void);

typedef union vmFunc_u {
	byte		*ptr;
	void (*func)(void);
} vmFunc_t;

struct vm_s {

	unsigned int programStack;		// the vm may be recursively entered
	syscall_t	systemCall;
	byte		*dataBase;
	int			*opStack;			// pointer to local function stack

	int			instructionCount;
	intptr_t	*instructionPointers;

	//------------------------------------
   
	const char	*name;
	vmIndex_t	index;

	const int	*vmMainArgs;

	// for dynamic linked modules
	void		*dllHandle;
	dllSyscall_t entryPoint;
	dllSyscall_t dllSyscall;
	void (*destroy)(vm_t* self);

	// for interpreted modules
	//qboolean	currentlyInterpreting;

	qboolean	compiled;

	vmFunc_t	codeBase;
	unsigned int codeSize;			// code + jump targets, needed for proper munmap()
	unsigned int codeLength;		// just for information

	unsigned int dataMask;
	unsigned int dataLength;			// data segment length
	unsigned int exactDataLength;	// from qvm header
	unsigned int dataAlloc;			// actually allocated

	unsigned int stackBottom;		// if programStack < stackBottom, error
	int			*opStackTop;

	int			numSymbols;
	vmSymbol_t	*symbols;

	int			callLevel;			// counts recursive VM_Call
	int			breakFunction;		// increment breakCount on function entry to this
	int			breakCount;

	byte		*jumpTableTargets;
	int			numJumpTableTargets;

	uint32_t	crc32sum;

	qboolean	forceDataMask;

	int			privateFlag;
};

extern	int		vm_debugLevel;

qboolean VM_Compile( vm_t *vm, vmHeader_t *header );
int	VM_CallCompiled( vm_t *vm, int nargs, int *args );

qboolean VM_PrepareInterpreter2( vm_t *vm, vmHeader_t *header );
int	VM_CallInterpreted2( vm_t *vm, int nargs, int *args );

vmSymbol_t *VM_ValueToFunctionSymbol( vm_t *vm, int value );
int VM_SymbolToValue( vm_t *vm, const char *symbol );
const char *VM_ValueToSymbol( vm_t *vm, int value );
void VM_LogSyscalls( int *args );

const char *VM_LoadInstructions( const byte *code_pos, int codeLength, int instructionCount, instruction_t *buf );
const char *VM_CheckInstructions( instruction_t *buf, int instructionCount, 
								 const byte *jumpTableTargets, 
								 int numJumpTableTargets, 
								 int dataLength );

void VM_ReplaceInstructions( vm_t *vm, instruction_t *buf );

#define JUMP	(1<<0)

typedef struct opcode_info_s 
{
	int   size; 
	int	  stack;
	int   nargs;
	int   flags;
} opcode_info_t ;

opcode_info_t ops[ OP_MAX ];

#endif // VM_LOCAL_H
