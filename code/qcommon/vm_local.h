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

// don't change
// Hardcoded in q3asm an reserved at end of bss
#define	PROGRAM_STACK_SIZE	0x10000

// for some buggy mods
#define	PROGRAM_STACK_EXTRA	(32*1024)

// reserved space for effective LOCAL+LOAD* checks
// also to avoid runtime range checks for many small arguments/structs in systemcalls
#define	VM_DATA_GUARD_SIZE	1024

// guard size must cover at least function arguments area
#if VM_DATA_GUARD_SIZE < 256
#undef VM_DATA_GUARD_SIZE
#define VM_DATA_GUARD_SIZE 256
#endif

// flags for vm_rtChecks cvar
#define VM_RTCHECK_PSTACK  1
#define VM_RTCHECK_OPSTACK 2
#define VM_RTCHECK_JUMP    4
#define VM_RTCHECK_DATA    8

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
	int32_t	value;     // 32
	byte	op;        // 8
	byte	opStack;   // 8
	unsigned jused:1;  // this instruction is a jump target
	unsigned swtch:1;  // indirect jump
	unsigned safe:1;   // non-masked OP_STORE*
	unsigned endp:1;   // for last OP_LEAVE instruction
	unsigned fpu:1;    // load into FPU register
	unsigned njump:1;  // near jump
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

	syscall_t	systemCall;
	byte		*dataBase;
	int32_t		*opStack;			// pointer to local function stack
	int32_t		*opStackTop;

	int32_t		programStack;		// the vm may be recursively entered
	int32_t		stackBottom;		// if programStack < stackBottom, error

	//------------------------------------

	const char	*name;				// module should be bare: "cgame", not "cgame.dll" or "vm/cgame.qvm"
	vmIndex_t	index;

	// for dynamic linked modules
	void		*dllHandle;
	vmMainFunc_t entryPoint;
	dllSyscall_t dllSyscall;
	void (*destroy)(vm_t* self);

	// for interpreted modules
	//qboolean	currentlyInterpreting;

	qboolean	compiled;

	vmFunc_t	codeBase;
	unsigned int codeSize;			// code + jump targets, needed for proper munmap()
	unsigned int codeLength;		// just for information

	int32_t		instructionCount;
	intptr_t	*instructionPointers;

	uint32_t	dataMask;
	uint32_t	dataLength;			// data segment length
	uint32_t	exactDataLength;	// from qvm header
	uint32_t	dataAlloc;			// actually allocated, for mmap()/munmap()
	uint32_t	programStackExtra;

	int			numSymbols;
	vmSymbol_t	*symbols;

	int			callLevel;			// counts recursive VM_Call
	int			breakFunction;		// increment breakCount on function entry to this
	int			breakCount;

	int32_t		*jumpTableTargets;
	int32_t		numJumpTableTargets;

	uint32_t	crc32sum;

	qboolean	forceDataMask;

	int			privateFlag;
};

qboolean VM_Compile( vm_t *vm, vmHeader_t *header );
int32_t VM_CallCompiled( vm_t *vm, int nargs, int32_t *args );

qboolean VM_PrepareInterpreter2( vm_t *vm, vmHeader_t *header );
int32_t VM_CallInterpreted2( vm_t *vm, int nargs, int32_t *args );

vmSymbol_t *VM_ValueToFunctionSymbol( vm_t *vm, int value );
int VM_SymbolToValue( vm_t *vm, const char *symbol );
const char *VM_ValueToSymbol( vm_t *vm, int value );
void VM_LogSyscalls( int *args );

const char *VM_LoadInstructions( const byte *code_pos, int codeLength, int instructionCount, instruction_t *buf );
const char *VM_CheckInstructions( instruction_t *buf, int instructionCount,
								 const int32_t *jumpTableTargets,
								 int numJumpTableTargets,
								 int dataLength );

void VM_ReplaceInstructions( vm_t *vm, instruction_t *buf );

#define JUMP	(1<<0)
#define FPU		(1<<1)

typedef struct opcode_info_s
{
	int	size;
	int	stack;
	int	nargs;
	int	flags;
} opcode_info_t;

extern opcode_info_t ops[ OP_MAX ];

#endif // VM_LOCAL_H
