.DATA

cwCurr DWORD 0
cw037F DWORD 037Fh ; round towards nearest

.CODE

; [RCX] - vector
PUBLIC Sys_SnapVector
Sys_SnapVector proc

	fnstcw word ptr cwCurr
	fldcw word ptr cw037F

	fld   dword ptr[rcx+8]
	fistp dword ptr[rcx+8]
	fild  dword ptr[rcx+8]
	fstp  dword ptr[rcx+8]

	fld   dword ptr[rcx+4]
	fistp dword ptr[rcx+4]
	fild  dword ptr[rcx+4]
	fstp  dword ptr[rcx+4]

	fld   dword ptr[rcx+0]
	fistp dword ptr[rcx+0]
	fild  dword ptr[rcx+0]
	fstp  dword ptr[rcx+0]

	fldcw word ptr cwCurr

	ret
Sys_SnapVector endp

END