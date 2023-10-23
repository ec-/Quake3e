; common x86_64 assembler code form MSVC

.code

; params :  RCX - void *jmp_buf[10], RSP[0] = callee addr, RSP[8] - callee stack pointer
Q_setjmp_c PROC
	; store RIP in jmp_buf[0]
	mov rax, [rsp+0]
	mov [rcx+ 0], rax
	; store RSP in jmp_buf[1]
	lea rax, [rsp+8]
	mov [rcx+ 8], rax
	; store all remaining regs
	mov [rcx+16], rbp
	mov [rcx+24], rbx
	mov [rcx+32], rsi
	mov [rcx+40], rdi
	mov [rcx+48], r12
	mov [rcx+56], r13
	mov [rcx+64], r14
	mov [rcx+72], r15
	; set normal return code (0)
	xor eax, eax
	ret
Q_setjmp_c ENDP


; params :  RCX - void *jmp_buf[10], RDX - return code
Q_longjmp_c PROC
	; restore registers
	mov r15, [rcx+72]
	mov r14, [rcx+64]
	mov r13, [rcx+56]
	mov r12, [rcx+48]
	mov rdi, [rcx+40]
	mov rsi, [rcx+32] 
	mov rbx, [rcx+24]
	mov rbp, [rcx+16]
	mov rsp, [rcx+ 8]
	; set return code in EAX
	mov eax, edx
	; jump to RIP
	jmp qword ptr [rcx+ 0]
Q_longjmp_c ENDP


; input: RCX=eax, RDX=ecx, R8=out, R9=unused/scratch
CPUID_EX PROC
	mov r9, rbx ; push rbx
	mov eax, ecx
	mov ecx, edx
	cpuid
	mov dword ptr [r8+0], eax
	mov dword ptr [r8+4], ebx
	mov dword ptr [r8+8], ecx
	mov dword ptr [r8+12], edx
	mov rbx, r9 ; pop rbx
	ret
CPUID_EX ENDP

END
