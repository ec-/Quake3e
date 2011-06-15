#include "qasm.h"

#if id386

.data

.arch i586
.intel_syntax noprefix

cw037F: .long 0x037F // rounding towards nearest (even)
cw0F7F: .long 0x0F7F // rounding towards zero (chop mode)
cwCurr: .long 0x0000

// !!! This should be synced with vm_local.h !!!

.equ VM_OFFSET_PROGRAM_STACK,         0
.equ VM_OFFSET_SYSTEM_CALL,           4
.equ VM_OFFSET_DATA_BASE,             8
.equ VM_OFFSET_INSTRUCTION_COUNT,    12
.equ VM_OFFSET_INSTRUCTION_POINTERS, 16

.text


.globl C(Sys_SnapVector)
C(Sys_SnapVector):
    fnstcw word ptr cwCurr
	mov   eax, dword ptr[esp+4]
    fldcw word ptr cw037F
	fld   dword ptr[eax+8]
	fistp dword ptr[eax+8]
	fild  dword ptr[eax+8]
	fstp  dword ptr[eax+8]
	fld   dword ptr[eax+4]
	fistp dword ptr[eax+4]
	fild  dword ptr[eax+4]
	fstp  dword ptr[eax+4]
	fld   dword ptr[eax+0]
	fistp dword ptr[eax+0]
	fild  dword ptr[eax+0]
	fstp  dword ptr[eax+0]
	fldcw word ptr cwCurr
	ret


.globl C(qftol0F7F)
C(qftol0F7F):
	fstcw  word ptr cwCurr
    fldcw  word ptr cw0F7F
    fistp dword ptr [edi]
    fldcw  word ptr cwCurr
    ret


.globl C(AsmCall)
C(AsmCall):

	mov		eax, dword ptr [edi]
	sub		edi, 4
	test	eax, eax
	jl		systemCall
	cmp		eax, [ecx+VM_OFFSET_INSTRUCTION_COUNT]
	jae		badAddr
	// calling another vm function
	shl		eax,2
	add		eax, dword ptr [ecx+VM_OFFSET_INSTRUCTION_POINTERS]
	call	dword ptr [eax]
	mov		eax, dword ptr [edi]
	ret
badAddr:
	call	ErrJump

systemCall:

	// convert negative num to system call number
	// and store right before the first arg
	not		eax

	push    ebp
	mov     ebp, esp
	and		esp, -16 					// align stack pointer on 16-byte boundary

	push	esi							// we may call recursively, so the
	push	edi							// statics aren't guaranteed to be around

	mov		ebx, eax					// save syscallNum

	// currentVM->programStack = programStack - 4;
	lea		eax, [esi-4]
	mov		[ecx+VM_OFFSET_PROGRAM_STACK], eax

	// params = (int *)((byte *)currentVM->dataBase + programStack + 4);
	mov		eax, [ecx+VM_OFFSET_DATA_BASE]
	lea		eax, [eax+esi+4]

	mov		[eax], ebx					// *params = syscallNum

	push	eax							// cdecl - push params
	call	dword ptr [ecx+VM_OFFSET_SYSTEM_CALL] // currentVm->systemCall( param );
	add		esp, 4

	pop		edi
	pop		esi

	add		edi, 4						// we added the return value
	mov		[edi], eax					// *(opstack+1) = eax

	mov     esp, ebp
	pop     ebp

	ret

#endif