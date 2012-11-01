IFDEF id386
.386
.MODEL FLAT,C
ENDIF

.DATA

cwCurr DWORD 0
cw037F DWORD 037Fh ; round towards nearest

.CODE

IFDEF id386

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

ELSE

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

ENDIF

END
