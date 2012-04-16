.386
.MODEL FLAT,C


.DATA

cwCurr DWORD 0
cw037F DWORD 037Fh ; round towards nearest

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


END