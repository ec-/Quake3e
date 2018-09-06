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
#include "vm_local.h"


char *VM_Indent( vm_t *vm ) {
	static char	*string = "                                        ";
	if ( vm->callLevel > 20 ) {
		return string;
	}
	return string + 2 * ( 20 - vm->callLevel );
}


void VM_StackTrace( vm_t *vm, int programCounter, int programStack ) {
	int		count;

	count = 0;
	do {
		Com_Printf( "%s\n", VM_ValueToSymbol( vm, programCounter ) );
		programStack =  *(int *)&vm->dataBase[programStack+4];
		programCounter = *(int *)&vm->dataBase[programStack];
	} while ( programCounter != -1 && ++count < 32 );

}

// macro opcode sequences
typedef enum {
	MOP_LLOAD4 = OP_MAX
} macro_op_t;


/*
=================
VM_FindMOps

Search for known macro-op sequences
=================
*/
static void VM_FindMOps( instruction_t *buf, int instructionCount )
{
	int i, op0;
	instruction_t *ci;
	
	ci = buf;
	i = 0;

	while ( i < instructionCount )
	{
		op0 = ci->op;
		if ( op0 == OP_LOCAL && (ci+1)->op == OP_LOAD4 ) {
			ci->op = MOP_LLOAD4;
			ci += 2; i += 2;
			continue;
		}
		ci++;
		i++;
	}
}


/*
====================
VM_PrepareInterpreter2
====================
*/
qboolean VM_PrepareInterpreter2( vm_t *vm, vmHeader_t *header ) 
{
	const char *errMsg;
	instruction_t *buf;
	buf = ( instruction_t *) Hunk_Alloc( (vm->instructionCount + 8) * sizeof( instruction_t ), h_high );

	errMsg = VM_LoadInstructions( (byte *) header + header->codeOffset, header->codeLength, header->instructionCount, buf );
	if ( !errMsg ) {
		errMsg = VM_CheckInstructions( buf, vm->instructionCount, vm->jumpTableTargets, vm->numJumpTableTargets, vm->exactDataLength );
	}
	if ( errMsg ) {
		Com_Printf( "VM_PrepareInterpreter2 error: %s\n", errMsg );
		return qfalse;
	}

	VM_ReplaceInstructions( vm, buf );

	VM_FindMOps( buf, vm->instructionCount );

	vm->codeBase.ptr = (void*)buf;
	return qtrue;
}


/*
==============
VM_CallInterpreted2


Upon a system call, the stack will look like:

sp+32	parm1
sp+28	parm0
sp+24	return stack
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
int	VM_CallInterpreted2( vm_t *vm, int nargs, int *args ) {
	int		stack[MAX_OPSTACK_SIZE];
	int		*opStack, *opStackTop;
	unsigned int programStack;
	unsigned int stackOnEntry;
	byte	*image;
	int		v1, v0;
	int		dataMask;
	instruction_t *inst, *ci;
	floatint_t	r0, r1;
	int		opcode;
	int		*img;
	int		i;

	// interpret the code
	//vm->currentlyInterpreting = qtrue;

	// we might be called recursively, so this might not be the very top
	programStack = stackOnEntry = vm->programStack;

	// set up the stack frame 
	image = vm->dataBase;
	inst = (instruction_t *)vm->codeBase.ptr;
	dataMask = vm->dataMask;
	
	// leave a free spot at start of stack so
	// that as long as opStack is valid, opStack-1 will
	// not corrupt anything
	opStack = &stack[1];
	opStackTop = stack + ARRAY_LEN( stack ) - 1;

	programStack -= (MAX_VMMAIN_CALL_ARGS+2)*4;
	img = (int*)&image[ programStack ];
	for ( i = 0; i < nargs; i++ ) {
		img[ i + 2 ] = args[ i ];
	}
	img[ 1 ] = 0; 	// return stack
	img[ 0 ] = -1;	// will terminate the loop on return

	ci = inst;

	// main interpreter loop, will exit when a LEAVE instruction
	// grabs the -1 program counter

	while ( 1 ) {

		r0.i = opStack[0];
		r1.i = opStack[-1];

nextInstruction2:

		v0 = ci->value;
		opcode = ci->op; 
		ci++;

		switch ( opcode ) {

		case OP_BREAK:
			vm->breakCount++;
			goto nextInstruction2;

		case OP_ENTER:
			// get size of stack frame
			programStack -= v0;
			if ( programStack <= vm->stackBottom ) {
				Com_Error( ERR_DROP, "VM programStack overflow" );
			}
			if ( opStack + ((ci-1)->opStack/4) >= opStackTop ) {
				Com_Error( ERR_DROP, "VM opStack overflow" );
			}
			break;

		case OP_LEAVE:
			// remove our stack frame
			programStack += v0;

			// grab the saved program counter
			v1 = *(int *)&image[ programStack ];
			// check for leaving the VM
			if ( v1 == -1 ) {
				goto done;
			} else if ( (unsigned)v1 >= vm->instructionCount ) {
				Com_Error( ERR_DROP, "VM program counter out of range in OP_LEAVE" );
			}
			ci = inst + v1;
			break;

		case OP_CALL:
			// save current program counter
			*(int *)&image[ programStack ] = ci - inst;
			
			// jump to the location on the stack
			if ( r0.i < 0 ) {
				// system call
				// save the stack to allow recursive VM entry
				//vm->programStack = programStack - 4;
				vm->programStack = programStack - 8;
				*(int *)&image[ programStack + 4 ] = ~r0.i;
				{
#if idx64 //__WORDSIZE == 64
					// the vm has ints on the stack, we expect
					// longs so we have to convert it
					intptr_t argarr[16];
					int argn;
					for ( argn = 0; argn < ARRAY_LEN( argarr ); ++argn ) {
						argarr[ argn ] = *(int*)&image[ programStack + 4 + 4*argn ];
					}
					v0 = vm->systemCall( &argarr[0] );
#else
					v0 = vm->systemCall( (intptr_t *)&image[ programStack + 4 ] );
#endif
				}

				// save return value
				//opStack++;
				ci = inst + *(int *)&image[ programStack ];
				*opStack = v0;
			} else if ( r0.u < vm->instructionCount ) {
				// vm call
				ci = inst + r0.i;
				opStack--;
			} else {
				Com_Error( ERR_DROP, "VM program counter out of range in OP_CALL" );
			}
			break;

		// push and pop are only needed for discarded or bad function return values
		case OP_PUSH:
			opStack++;
			break;

		case OP_POP:
			opStack--;
			break;

		case OP_CONST:
			opStack++;
			r1.i = r0.i;
			r0.i = *opStack = v0;
			goto nextInstruction2;

		case OP_LOCAL:
			opStack++;
			r1.i = r0.i;
			r0.i = *opStack = v0 + programStack;
			goto nextInstruction2;

		case OP_JUMP:
			if ( r0.u >= vm->instructionCount ) {
				Com_Error( ERR_DROP, "VM program counter out of range in OP_JUMP" );
			}
			ci = inst + r0.i;
			opStack--;
			break;

		/*
		===================================================================
		BRANCHES
		===================================================================
		*/

		case OP_EQ:
			opStack -= 2;
			if ( r1.i == r0.i )
				ci = inst + v0;
			break;

		case OP_NE:
			opStack -= 2;
			if ( r1.i != r0.i )
				ci = inst + v0;
			break;

		case OP_LTI:
			opStack -= 2;
			if ( r1.i < r0.i )
				ci = inst + v0;
			break;

		case OP_LEI:
			opStack -= 2;
			if ( r1.i <= r0.i )
				ci = inst + v0;
			break;

		case OP_GTI:
			opStack -= 2;
			if ( r1.i > r0.i )
				ci = inst + v0;
			break;

		case OP_GEI:
			opStack -= 2;
			if ( r1.i >= r0.i )
				ci = inst + v0;
			break;

		case OP_LTU:
			opStack -= 2;
			if ( r1.u < r0.u )
				ci = inst + v0;
			break;

		case OP_LEU:
			opStack -= 2;
			if ( r1.u <= r0.u )
				ci = inst + v0;
			break;

		case OP_GTU:
			opStack -= 2;
			if ( r1.u > r0.u )
				ci = inst + v0;
			break;

		case OP_GEU:
			opStack -= 2;
			if ( r1.u >= r0.u )
				ci = inst + v0;
			break;

		case OP_EQF:
			opStack -= 2;
			if ( r1.f == r0.f )
				ci = inst + v0;
			break;

		case OP_NEF:
			opStack -= 2;
			if ( r1.f != r0.f )
				ci = inst + v0;
			break;

		case OP_LTF:
			opStack -= 2;
			if ( r1.f < r0.f )
				ci = inst + v0;
			break;

		case OP_LEF:
			opStack -= 2;
			if ( r1.f <= r0.f )
				ci = inst + v0;
			break;

		case OP_GTF:
			opStack -= 2;
			if ( r1.f > r0.f )
				ci = inst + v0;
			break;

		case OP_GEF:
			opStack -= 2;
			if ( r1.f >= r0.f )
				ci = inst + v0;
			break;

		//===================================================================

		case OP_LOAD1:
			r0.i = *opStack = image[ r0.i & dataMask ];
			goto nextInstruction2;

		case OP_LOAD2:
			r0.i = *opStack = *(unsigned short *)&image[ r0.i & dataMask ];
			goto nextInstruction2;

		case OP_LOAD4:
			r0.i = *opStack = *(int *)&image[ r0.i & dataMask ];
			goto nextInstruction2;

		case OP_STORE1:
			image[ r1.i & dataMask ] = r0.i;
			opStack -= 2;
			break;

		case OP_STORE2:
			*(short *)&image[ r1.i & dataMask ] = r0.i;
			opStack -= 2;
			break;

		case OP_STORE4:
			*(int *)&image[ r1.i & dataMask ] = r0.i;
			opStack -= 2;
			break;

		case OP_ARG:
			// single byte offset from programStack
			*(int *)&image[ ( v0 + programStack ) /*& ( dataMask & ~3 ) */ ] = r0.i;
			opStack--;
			break;

		case OP_BLOCK_COPY:
			{
				int		*src, *dest;
				int		count, srci, desti;

				count = v0;
				// MrE: copy range check
				srci = r0.i & dataMask;
				desti = r1.i & dataMask;
				count = ((srci + count) & dataMask) - srci;
				count = ((desti + count) & dataMask) - desti;

				src = (int *)&image[ srci ];
				dest = (int *)&image[ desti ];
				
				memcpy( dest, src, count );
				opStack -= 2;
			}
			break;

		case OP_SEX8:
			*opStack = (signed char)*opStack;
			break;

		case OP_SEX16:
			*opStack = (short)*opStack;
			break;

		case OP_NEGI:
			*opStack = -r0.i;
			break;

		case OP_ADD:
			opStack[-1] = r1.i + r0.i;
			opStack--;
			break;

		case OP_SUB:
			opStack[-1] = r1.i - r0.i;
			opStack--;
			break;

		case OP_DIVI:
			opStack[-1] = r1.i / r0.i;
			opStack--;
			break;

		case OP_DIVU:
			opStack[-1] = r1.u / r0.u;
			opStack--;
			break;

		case OP_MODI:
			opStack[-1] = r1.i % r0.i;
			opStack--;
			break;

		case OP_MODU:
			opStack[-1] = r1.u % r0.u;
			opStack--;
			break;

		case OP_MULI:
			opStack[-1] = r1.i * r0.i;
			opStack--;
			break;

		case OP_MULU:
			opStack[-1] = r1.u * r0.u;
			opStack--;
			break;

		case OP_BAND:
			opStack[-1] = r1.u & r0.u;
			opStack--;
			break;

		case OP_BOR:
			opStack[-1] = r1.u | r0.u;
			opStack--;
			break;

		case OP_BXOR:
			opStack[-1] = r1.u ^ r0.u;
			opStack--;
			break;

		case OP_BCOM:
			*opStack = ~ r0.u;
			break;

		case OP_LSH:
			opStack[-1] = r1.i << r0.i;
			opStack--;
			break;

		case OP_RSHI:
			opStack[-1] = r1.i >> r0.i;
			opStack--;
			break;

		case OP_RSHU:
			opStack[-1] = r1.u >> r0.i;
			opStack--;
			break;

		case OP_NEGF:
			*(float *)opStack =  - r0.f;
			break;

		case OP_ADDF:
			*(float *)(opStack-1) = r1.f + r0.f;
			opStack--;
			break;

		case OP_SUBF:
			*(float *)(opStack-1) = r1.f - r0.f;
			opStack--;
			break;

		case OP_DIVF:
			*(float *)(opStack-1) = r1.f / r0.f;
			opStack--;
			break;

		case OP_MULF:
			*(float *)(opStack-1) = r1.f * r0.f;
			opStack--;
			break;

		case OP_CVIF:
			*(float *)opStack = (float) r0.i;
			break;

		case OP_CVFI:
			*opStack = (int) r0.f;
			break;

		case MOP_LLOAD4: // combined OP_LOCAL + OP_LOAD4
			ci++;
			opStack++;
			r1.i = r0.i;
			r0.i = *opStack = *(int *)&image[ v0 + programStack ];
			goto nextInstruction2;
		}
	}

done:
	//vm->currentlyInterpreting = qfalse;

	if ( opStack != &stack[2] ) {
		Com_Error( ERR_DROP, "Interpreter error: opStack = %ld", (long int) (opStack - stack) );
	}

	vm->programStack = stackOnEntry;

	// return the result
	return *opStack;
}
