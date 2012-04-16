#include "qasm.h"

#if id386

.data

.arch i586
.intel_syntax noprefix

cw037F: .long 0x037F // rounding towards nearest (even)
cw0F7F: .long 0x0F7F // rounding towards zero (chop mode)
cwCurr: .long 0x0000

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

#endif