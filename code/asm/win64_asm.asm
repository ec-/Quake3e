.data

  ALIGN 16
  ssemask DWORD 0FFFFFFFFh, 0FFFFFFFFh, 0FFFFFFFFh, 00000000h
  ssecw DWORD 00001F80h

.code

PUBLIC Sys_SnapVector

Sys_SnapVector PROC
	sub rsp, 8
	movaps xmm1, ssemask		; initialize the mask register
	movups xmm0, [rcx]			; here is stored our vector. Read 4 values in one go
	movaps xmm2, xmm0			; keep a copy of the original data
	andps xmm0, xmm1			; set the fourth value to zero in xmm0
	andnps xmm1, xmm2			; copy fourth value to xmm1 and set rest to zero
	cvtps2dq xmm0, xmm0			; convert 4 single fp to int
	cvtdq2ps xmm0, xmm0			; convert 4 int to single fp
	orps xmm0, xmm1				; combine all 4 values again
	movups [rcx], xmm0			; write 3 rounded and 1 unchanged values back to memory
	ret
Sys_SnapVector ENDP

END