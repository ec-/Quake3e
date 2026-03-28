/*
Copyright (C) 2026 skullernet

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "vm_local.h"
#include "wa_local.h"

WA_THUNK(sin) {
    WA_F64(0) = sin(WA_F64(0));
}

WA_THUNK(cos) {
    WA_F64(0) = cos(WA_F64(0));
}

WA_THUNK(tan) {
    WA_F64(0) = tan(WA_F64(0));
}

WA_THUNK(asin) {
    WA_F64(0) = asin(WA_F64(0));
}

WA_THUNK(acos) {
    WA_F64(0) = acos(WA_F64(0));
}

WA_THUNK(atan) {
    WA_F64(0) = atan(WA_F64(0));
}

WA_THUNK(atan2) {
    WA_F64(0) = atan2(WA_F64(0), WA_F64(1));
}

WA_THUNK(memcmp) {
    uint32_t p1   = WA_U32(0);
    uint32_t p2   = WA_U32(1);
    uint32_t size = WA_U32(2);

    WA_ASSERT((uint64_t)p1 + size <= m->memory.bytesize &&
              (uint64_t)p2 + size <= m->memory.bytesize, "Memory compare out of bounds");

    WA_I32(0) = memcmp(m->memory.bytes + p1, m->memory.bytes + p2, size);
}

WA_THUNK(strncpy) {
    Q_strncpy(WA_PTR_CNT(0, char, WA_U32(2)), WA_STR(1), WA_U32(2));
}

WA_THUNK(syscall) {
    // Get pointer to varargs chunk in WASM memory.
    // We don't know its length. Hope for the best :)
    // It will be NULL if there are no varargs.
    const uint32_t *arr = WA_PTR_NULL(1, uint32_t);
    intptr_t args[16] = { 0 };

    args[0] = WA_I32(0);
    if (arr) {
        // This code only works if all arguments are 32-bit.
        for (int i = 0; i < 15; i++)
            args[i + 1] = arr[i];
    }
    WA_I32(0) = m->vm->systemCall(args);
}

const wa_import_t wa_stdlib[] = {
    WA_IMPORT(sin, "F F"),
    WA_IMPORT(cos, "F F"),
    WA_IMPORT(tan, "F F"),
    WA_IMPORT(asin, "F F"),
    WA_IMPORT(acos, "F F"),
    WA_IMPORT(atan, "F F"),
    WA_IMPORT(atan2, "F FF"),
    WA_IMPORT(memcmp, "i iii"),
    WA_IMPORT(strncpy, "i iii"),
    WA_IMPORT(syscall, "i ii"),

    { 0 }
};
