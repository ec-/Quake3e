.586 ; for cpuid instruction
.mmx ; for sound mixing code
.xmm ; for sound mixing code
.MODEL FLAT,C


.DATA

cwCurr DWORD 0
cw037F DWORD 037Fh ; round towards nearest
cw0F7F DWORD 0F7Fh ; round towards zero


.CODE

; assuming __cdecl calling convention
; [esp+4] - vector
PUBLIC Sys_SnapVector
Sys_SnapVector proc

	fnstcw word ptr cwCurr
	mov   eax, [esp+4]
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
Sys_SnapVector endp


; assuming __cdecl calling convention
; st(0) - float

PUBLIC qftol0F7F
qftol0F7F proc
	fstcw  word ptr cwCurr
    fldcw  word ptr cw0F7F
    fistp dword ptr [edi]
    fldcw  word ptr cwCurr
    ret
qftol0F7F endp


; ================
; Virtual Machine
; ================

; !!! This should be synced with vm_local.h !!!
VM_OFFSET_PROGRAM_STACK			equ	0
VM_OFFSET_SYSTEM_CALL			equ	4
VM_OFFSET_DATA_BASE				equ	8

VM_OFFSET_INSTRUCTION_COUNT		equ	12
VM_OFFSET_INSTRUCTION_POINTERS	equ 16

extern ErrJump:			dword

; ecx - current VM pointer
PUBLIC AsmCall
AsmCall proc

	mov		eax, dword ptr [edi]
	sub		edi, 4
	test	eax, eax
	jl		systemCall
	cmp		eax, [ecx+VM_OFFSET_INSTRUCTION_COUNT]
	jae		badAddr
	; calling another vm function
	shl		eax,2
	add		eax, dword ptr [ecx+VM_OFFSET_INSTRUCTION_POINTERS]
	call	dword ptr [eax]
	mov		eax, dword ptr [edi]
	ret
badAddr:
	call	ErrJump

systemCall:

	; convert negative num to system call number
	; and store right before the first arg
	not		eax

	push    ebp
	mov     ebp, esp
	and		esp, -16 					; align stack pointer on 16-byte boundary

	push	esi							; we may call recursively, so the
	push	edi							; statics aren't guaranteed to be around

	mov		ebx, eax					; save syscallNum

	; currentVM->programStack = programStack - 4;
	lea		eax, [esi-4]
	mov		[ecx+VM_OFFSET_PROGRAM_STACK], eax

	; params = (int *)((byte *)currentVM->dataBase + programStack + 4);
	mov		eax, [ecx+VM_OFFSET_DATA_BASE]
	lea		eax, [eax+esi+4]

	mov		[eax], ebx					; *params = syscallNum

	push	eax							; cdecl - push params
	call	dword ptr [ecx+VM_OFFSET_SYSTEM_CALL] ; currentVm->systemCall( param );
	add		esp, 4

	pop		edi
	pop		esi

	add		edi, 4						; we added the return value
	mov		[edi], eax					; *(opstack+1) = eax

	mov     esp, ebp
	pop     ebp

	ret

AsmCall endp


; assuming __cdecl calling convention
PUBLIC IsPentium
IsPentium proc
	pushfd						; save eflags
	pop		eax
	test	eax, 000200000h		; check ID bit
	jz		set21				; bit 21 is not set, so jump to set_21
	and		eax, 0ffdfffffh		; clear bit 21
	push	eax					; save new value in register
	popfd						; store new value in flags
	pushfd
	pop		eax
	test	eax, 000200000h		; check ID bit
	jz		good
	jmp		err					; cpuid not supported
set21:
	or		eax, 000200000h		; set ID bit
	push	eax					; store new value
	popfd						; store new value in EFLAGS
	pushfd
	pop		eax
	test	eax, 000200000h		; if bit 21 is on
	jnz		good
	jmp		err
err:
	xor eax, eax
	ret
good:
	mov eax, 0ffffffffh
	ret
IsPentium endp


; ================
; Sound Mixing
; ================

extern snd_p:			dword
extern snd_out:			dword
extern snd_linear_count:dword


S_WriteLinearBlastStereo16_MMX proc
	push ebx
	push esi
	push edi
	mov esi,snd_p
	mov edi,snd_out
	mov ebx,snd_linear_count
	mov eax,esi
	and eax,63
	jz LMain
	shr eax,2
	jc LTail
	mov ecx,16
	sub ecx,eax
LClamp1:
	mov eax,[esi]
	sar eax,8
	cmp eax,32767
	jg	LClampHigh1
	cmp eax,-32768
	jnl LClampDone1
	mov eax,-32768
	jmp LClampDone1
LClampHigh1:
	mov eax,32767
LClampDone1:
	mov [edi],ax
	add esi,4
	add edi,2
	dec ebx
	jz	LExit
	dec ecx
	jnz	LClamp1
LMain:
    mov ecx,ebx
    shr ecx,4
    test ecx,ecx
	jz  LTail
	and ebx, 15
LAgain:
	movq mm0, qword ptr [esi+ 0]
	movq mm1, qword ptr [esi+ 8]
	movq mm2, qword ptr [esi+16]
	movq mm3, qword ptr [esi+24]
	movq mm4, qword ptr [esi+32]
	movq mm5, qword ptr [esi+40]
	movq mm6, qword ptr [esi+48]
	movq mm7, qword ptr [esi+56]
	psrad mm0,8
	psrad mm1,8
	psrad mm2,8
	psrad mm3,8
	psrad mm4,8
	psrad mm5,8
	psrad mm6,8
	psrad mm7,8
	packssdw mm0, mm1
	packssdw mm2, mm3
	packssdw mm4, mm5
	packssdw mm6, mm7
	movq qword ptr [edi+ 0], mm0
	movq qword ptr [edi+ 8], mm2
	movq qword ptr [edi+16], mm4
	movq qword ptr [edi+24], mm6
	add esi, 64
	add edi, 32
	dec ecx
	jnz LAgain
LTail:
	test ebx, ebx
	jz	LEnd
LClamp2:
	mov eax,[esi]
	sar eax,8
	cmp eax,32767
	jg	LClampHigh2
	cmp eax,-32768
	jnl LClampDone2
	mov eax,-32768
LClampHigh2:
	mov eax,32767
LClampDone2:
	mov [edi],ax
	add esi,4
	add edi,2
	dec ebx
	jnz	LClamp2
LEnd:
    emms
LExit:
	pop edi
	pop esi
	pop ebx
	ret
S_WriteLinearBlastStereo16_MMX endp



S_WriteLinearBlastStereo16_SSE proc
	push ebx
	push esi
	push edi
	mov esi,snd_p
	mov edi,snd_out
	mov ebx,snd_linear_count
	mov eax,esi
	and eax,63
	jz LMain
	shr eax,2
	jc LTail
	mov ecx,16
	sub ecx,eax
LClamp1:
	mov eax,[esi]
	sar eax,8
	cmp eax,32767
	jg	LClampHigh1
	cmp eax,-32768
	jnl LClampDone1
	mov eax,-32768
	jmp LClampDone1
LClampHigh1:
	mov eax,32767
LClampDone1:
	mov [edi], ax
	add esi,4
	add edi,2
	dec ebx
	jz	LExit
	dec ecx
	jnz	LClamp1
LMain:
	mov ecx,ebx
	shr ecx,4
	test ecx,ecx
	jz  LTail
	and ebx, 15
LAgain:
	movq mm0, qword ptr [esi+ 0]
	movq mm1, qword ptr [esi+ 8]
	movq mm2, qword ptr [esi+16]
	movq mm3, qword ptr [esi+24]
	movq mm4, qword ptr [esi+32]
	movq mm5, qword ptr [esi+40]
	movq mm6, qword ptr [esi+48]
	movq mm7, qword ptr [esi+56]
	psrad mm0,8
	psrad mm1,8
	psrad mm2,8
	psrad mm3,8
	psrad mm4,8
	psrad mm5,8
	psrad mm6,8
	psrad mm7,8
	packssdw mm0, mm1
	packssdw mm2, mm3
	packssdw mm4, mm5
	packssdw mm6, mm7
	movntq qword ptr [edi+ 0], mm0
	movntq qword ptr [edi+ 8], mm2
	movntq qword ptr [edi+16], mm4
	movntq qword ptr [edi+24], mm6
	add esi, 64
	add edi, 32
	dec ecx
	jnz LAgain
LTail:
	test ebx, ebx
	jz	LEnd
LClamp2:
	mov eax,[esi]
	sar eax,8
	cmp eax,32767
	jg	LClampHigh2
	cmp eax,-32768
	jnl LClampDone2
	mov eax,-32768
	jmp LClampDone2
LClampHigh2:
	mov eax,32767
LClampDone2:
	mov [edi],ax
	add esi,4
	add edi,2
	dec ebx
	jnz	LClamp2
LEnd:
	sfence
	emms
LExit:
	pop edi
	pop esi
	pop ebx
	ret
S_WriteLinearBlastStereo16_SSE endp


END