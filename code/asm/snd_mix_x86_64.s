#if defined(MACOS_X)
#undef ELF
#endif

#ifdef __ELF__
.section .note.GNU-stack,"",@progbits
#endif

#ifdef ELF
#define C(label) label
#else
#define C(label) _##label
#endif

.intel_syntax noprefix

.text

.globl C(S_WriteLinearBlastStereo16_SSE_x64)
C(S_WriteLinearBlastStereo16_SSE_x64):
	push rbx
#ifdef _WIN32
	// RCX RDX R8 R9
	push rsi
	push rdi
	mov rsi, rcx
	mov rdi, rdx
	mov ebx, r8d
#else
	// RDI RSI RDX RCX R8 R9
	xchg rsi, rdi
	mov ebx, edx
#endif
	test ebx,ebx
	jz	LExit
	mov ecx,esi
	and ecx,63
	jz LMain
	and ecx,3
	jnz LTail
	shr ecx,2
	not ecx
	add ecx,17
LClamp1:
	mov eax,[rsi]
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
	mov [rdi],ax
	add rsi,4
	add rdi,2
	dec ebx
	jz	LExit
	dec ecx
	jnz	LClamp1
LMain:
	mov ecx,ebx
	shr ecx,4
	jz  LTail
	and ebx,15
LAgain:
	movq mm0, qword ptr [rsi+ 0]
	movq mm1, qword ptr [rsi+ 8]
	movq mm2, qword ptr [rsi+16]
	movq mm3, qword ptr [rsi+24]
	movq mm4, qword ptr [rsi+32]
	movq mm5, qword ptr [rsi+40]
	movq mm6, qword ptr [rsi+48]
	movq mm7, qword ptr [rsi+56]
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
	movntq qword ptr [rdi+ 0], mm0
	movntq qword ptr [rdi+ 8], mm2
	movntq qword ptr [rdi+16], mm4
	movntq qword ptr [rdi+24], mm6
	add rsi, 64
	add rdi, 32
	dec ecx
	jnz LAgain
LTail:
	test ebx, ebx
	jz	LEnd
LClamp2:
	mov eax,[rsi]
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
	mov [rdi],ax
	add rsi,4
	add rdi,2
	dec ebx
	jnz	LClamp2
LEnd:
	sfence
	emms
LExit:
#ifdef _WIN32
	pop rdi
	pop rsi
#endif
	pop rbx
	ret
