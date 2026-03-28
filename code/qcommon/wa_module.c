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

This file incorporates work covered by the following copyright and
permission notice:

Copyright (C) Joel Martin <github@martintribe.org>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "vm_local.h"
#include "wa_local.h"

static qboolean wa_string_eq(const wa_string_t *s, const char *str)
{
    return s->len == strlen(str) && !memcmp(s->data, str, s->len);
}

// Static definition of block_types
static const wa_type_t block_types[5] = {
    { .form = BLOCK, .num_results = 0, },
    { .form = BLOCK, .num_results = 1, .results = { I32 } },
    { .form = BLOCK, .num_results = 1, .results = { I64 } },
    { .form = BLOCK, .num_results = 1, .results = { F32 } },
    { .form = BLOCK, .num_results = 1, .results = { F64 } }
};

const wa_type_t *WA_GetBlockType(uint32_t value_type)
{
    switch (value_type) {
    case BLOCK:
        return &block_types[0];
    case I32:
        return &block_types[1];
    case I64:
        return &block_types[2];
    case F32:
        return &block_types[3];
    case F64:
        return &block_types[4];
    default:
        ASSERT(0, "Invalid block value_type: %#x", value_type);
        return NULL;
    }
}

static int get_value_type(int c)
{
    switch (c) {
    case 'i':
        return I32;
    case 'I':
        return I64;
    case 'f':
        return F32;
    case 'F':
        return F64;
    }
    return 0;
}

static qboolean wa_type_eq(const wa_type_t *type, const char *s)
{
    int t;

    if (type->num_results == 1) {
        if (!(t = get_value_type(*s)))
            return qfalse;
        if (s[1] != ' ' || type->results[0] != t)
            return qfalse;
        s += 2;
    }

    for (uint32_t p = 0; p < type->num_params; p++, s++) {
        if (!(t = get_value_type(*s)))
            return qfalse;
        if (type->params[p] != t)
            return qfalse;
    }

    return *s == 0;
}

static qboolean import_function(wa_t *m, const wa_string_t *module, const wa_string_t *name, const wa_type_t *type)
{
    const wa_import_t *import;

    ASSERT(wa_string_eq(module, "env"), "Unknown import module %.*s", module->len, module->data);

    for (import = wa_stdlib; import->name; import++)
        if (wa_string_eq(name, import->name))
            break;

    ASSERT(import->name, "Import %.*s not found", name->len, name->data);

    ASSERT(wa_type_eq(type, import->mask), "Import %.*s type mismatch", name->len, name->data);

    ASSERT(m->num_imports < MAX_FUNCS, "Too many functions");
    wa_block_t *func = &m->funcs[m->num_imports++];
    func->type = type;
    func->thunk = import->thunk;
    return qtrue;
}

static qboolean wa_read_string(sizebuf_t *sz, wa_string_t *s)
{
    s->len = SZ_ReadLeb(sz);
    s->data = SZ_ReadData(sz, s->len);
    ASSERT(s->data, "Read past end of section");
    return qtrue;
}

static qboolean run_init_expr(wa_t *m, wa_value_t *val, sizebuf_t *sz)
{
    int opcode = SZ_ReadByte(sz);
    uint32_t arg;

    switch (opcode) {
    case GlobalGet:
        arg = SZ_ReadLeb(sz);
        ASSERT(arg < m->num_globals, "Bad global index");
        *val = m->globals[arg];
        break;
    case I32_Const:
        val->u32 = SZ_ReadSignedLeb(sz, 32);
        break;
    case I64_Const:
        val->u64 = SZ_ReadSignedLeb(sz, 64);
        break;
    case F32_Const:
        val->u32 = SZ_ReadLong(sz);
        break;
    case F64_Const:
        val->u64 = SZ_ReadLong64(sz);
        break;
    default:
        ASSERT(0, "Init expression not constant (opcode = %#x)", opcode);
    }

    opcode = SZ_ReadByte(sz);
    ASSERT(opcode == End, "End opcode expected after init expression");
    return qtrue;
}

static qboolean parse_types(wa_t *m, sizebuf_t *sz)
{
    m->num_types = SZ_ReadLeb(sz);
    ASSERT(m->num_types <= MAX_TYPES, "Too many types");
    m->types = WA_Malloc(m->num_types * sizeof(m->types[0]));

    for (uint32_t c = 0; c < m->num_types; c++) {
        wa_type_t *type = &m->types[c];
        type->form = SZ_ReadLeb(sz);
        ASSERT(type->form == FUNC, "Must be function type");

        type->num_params = SZ_ReadLeb(sz);
        ASSERT(type->num_params <= MAX_LOCALS, "Too many parameters");
        type->params = WA_Malloc(type->num_params * sizeof(type->params[0]));
        for (uint32_t p = 0; p < type->num_params; p++)
            type->params[p] = SZ_ReadLeb(sz);

        type->num_results = SZ_ReadLeb(sz);
        ASSERT(type->num_results <= MAX_RESULTS, "Too many results");
        for (uint32_t r = 0; r < type->num_results; r++)
            type->results[r] = SZ_ReadLeb(sz);
    }

    return qtrue;
}

static qboolean parse_imports(wa_t *m, sizebuf_t *sz)
{
    // Allocate functions AND imports now
    uint32_t num_imports = SZ_ReadLeb(sz);
    ASSERT(num_imports <= MAX_FUNCS - m->num_funcs, "Too many imports");
    m->funcs = WA_Malloc((m->num_funcs + num_imports) * sizeof(m->funcs[0]));

    for (uint32_t gidx = 0; gidx < num_imports; gidx++) {
        wa_string_t module, name;
        if (!wa_read_string(sz, &module))
            return qfalse;
        if (!wa_read_string(sz, &name))
            return qfalse;

        uint32_t kind = SZ_ReadByte(sz);
        ASSERT(kind == KIND_FUNCTION, "Import of kind %d not supported", kind);

        uint32_t tidx = SZ_ReadLeb(sz);
        ASSERT(tidx < m->num_types, "Bad type index");
        if (!import_function(m, &module, &name, &m->types[tidx]))
            return qfalse;
    }

    return qtrue;
}

static qboolean parse_functions_pre(wa_t *m, sizebuf_t *sz)
{
    uint32_t count = SZ_ReadLeb(sz);
    ASSERT(count <= MAX_FUNCS, "Bad number of functions");
    m->num_funcs = count;
    return qtrue;
}

static qboolean parse_functions(wa_t *m, sizebuf_t *sz)
{
    // We have already parsed num_funcs
    uint32_t count = SZ_ReadLeb(sz);
    ASSERT(count == m->num_funcs, "Bad number of functions");
    m->num_funcs += m->num_imports;

    for (uint32_t f = m->num_imports; f < m->num_funcs; f++) {
        uint32_t tidx = SZ_ReadLeb(sz);
        ASSERT(tidx < m->num_types, "Bad type index");
        m->funcs[f].type = &m->types[tidx];
    }

    return qtrue;
}

static qboolean parse_tables(wa_t *m, sizebuf_t *sz)
{
    uint32_t table_count = SZ_ReadLeb(sz);
    ASSERT(table_count == 1, "Only 1 default table supported");

    uint32_t type = SZ_ReadLeb(sz);
    ASSERT(type == FUNCREF, "Must be funcref");

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t tsize = SZ_ReadLeb(sz); // Initial size
    m->table.initial = tsize;
    m->table.size = tsize;
    // Limit the maximum to 64K elements
    if (flags & 0x1) {
        tsize = SZ_ReadLeb(sz); // Max size
        m->table.maximum = MIN(0x10000, tsize);
    } else {
        m->table.maximum = 0x10000;
    }
    ASSERT(m->table.size <= m->table.maximum, "Bad table size");

    // Allocate the table
    m->table.entries = WA_Malloc(m->table.size * sizeof(m->table.entries[0]));
    return qtrue;
}

static qboolean parse_memory(wa_t *m, sizebuf_t *sz)
{
    uint32_t memory_count = SZ_ReadLeb(sz);
    ASSERT(memory_count == 1, "Only 1 default memory supported");

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t pages = SZ_ReadLeb(sz); // Initial size
    m->memory.initial = pages;
    m->memory.pages = pages;
    // Limit the maximum to 1.5K pages (100MB)
    if (flags & 0x1) {
        pages = SZ_ReadLeb(sz); // Max size
        m->memory.maximum = MIN(0x600, pages);
    } else {
        m->memory.maximum = 0x600;
    }
    if (flags & 0x8) {
        SZ_ReadLeb(sz); // Page size
    }
    ASSERT(m->memory.pages <= m->memory.maximum, "Bad memory size");

    // Allocate memory
    m->memory.bytesize = log2pad(m->memory.pages * WA_PAGE_SIZE, 1);
    m->memory.bytes = WA_Malloc(m->memory.bytesize + VM_DATA_GUARD_SIZE);
    return qtrue;
}

static qboolean parse_globals(wa_t *m, sizebuf_t *sz)
{
    uint32_t num_globals = SZ_ReadLeb(sz);
    ASSERT(num_globals <= MAX_GLOBALS, "Too many globals");
    m->globals = WA_Malloc(num_globals * sizeof(m->globals[0]));
    m->num_globals = num_globals;

    for (uint32_t g = 0; g < num_globals; g++) {
        uint32_t type = SZ_ReadLeb(sz);
        SZ_ReadByte(sz); // mutability
        (void)type;

        // Run the init_expr to get global value
        if (!run_init_expr(m, &m->globals[g], sz))
            return qfalse;
    }
    return qtrue;
}

static qboolean parse_exports(wa_t *m, sizebuf_t *sz)
{
    uint32_t num_exports = SZ_ReadLeb(sz);
    ASSERT(num_exports <= SZ_Remaining(sz) / 3, "Too many exports");
    m->exports = WA_Malloc(num_exports * sizeof(m->exports[0]));
    m->num_exports = num_exports;

    for (uint32_t e = 0; e < num_exports; e++) {
        wa_export_t *export = &m->exports[e];
        if (!wa_read_string(sz, &export->name))
            return qfalse;
        uint32_t kind = SZ_ReadByte(sz);
        uint32_t index = SZ_ReadLeb(sz);
        export->kind = kind;

        switch (kind) {
        case KIND_FUNCTION:
            ASSERT(index < m->num_funcs, "Bad function index");
            export->value = &m->funcs[index];
            break;
        case KIND_TABLE:
            ASSERT(index == 0, "Only 1 default table supported");
            export->value = &m->table;
            break;
        case KIND_MEMORY:
            ASSERT(index == 0, "Only 1 default memory supported");
            export->value = &m->memory;
            break;
        case KIND_GLOBAL:
            ASSERT(index < m->num_globals, "Bad global index");
            export->value = &m->globals[index];
            break;
        default:
            ASSERT(0, "Export of kind %d not supported", kind);
        }
    }

    return qtrue;
}

static qboolean parse_elements(wa_t *m, sizebuf_t *sz)
{
    uint32_t element_count = SZ_ReadLeb(sz);
    for (uint32_t c = 0; c < element_count; c++) {
        uint32_t flags = SZ_ReadLeb(sz);
        ASSERT(flags == 0, "Element flags %#x not supported", flags);

        // Run the init_expr to get offset
        wa_value_t init = { 0 };
        if (!run_init_expr(m, &init, sz))
            return qfalse;

        uint32_t offset = init.u32;
        uint32_t num_elem = SZ_ReadLeb(sz);
        ASSERT((uint64_t)offset + num_elem <= m->table.size, "Table init out of bounds");
        for (uint32_t n = 0; n < num_elem; n++)
            m->table.entries[offset + n] = SZ_ReadLeb(sz);
    }

    return qtrue;
}

static qboolean parse_data(wa_t *m, sizebuf_t *sz)
{
    uint32_t seg_count = SZ_ReadLeb(sz);
    for (uint32_t s = 0; s < seg_count; s++) {
        uint32_t flags = SZ_ReadLeb(sz);
        ASSERT(flags == 0, "Segment flags %#x not supported", flags);

        // Run the init_expr to get the offset
        wa_value_t init = { 0 };
        if (!run_init_expr(m, &init, sz))
            return qfalse;

        // Copy the data to the memory offset
        uint32_t offset = init.u32;
        uint32_t size = SZ_ReadLeb(sz);
        ASSERT((uint64_t)offset + size <= m->memory.bytesize, "Memory init out of bounds");
        void *data = SZ_ReadData(sz, size);
        ASSERT(data, "Read past end of section");
        memcpy(m->memory.bytes + offset, data, size);
    }

    return qtrue;
}

static qboolean parse_code(wa_t *m, sizebuf_t *sz)
{
    uint32_t body_count = SZ_ReadLeb(sz);
    ASSERT(body_count <= m->num_funcs - m->num_imports, "Too many functions");

    for (uint32_t b = 0; b < body_count; b++) {
        wa_block_t *func = &m->funcs[m->num_imports + b];
        uint32_t body_size = SZ_ReadLeb(sz);
        ASSERT(body_size > 0, "Empty function");
        ASSERT(body_size <= SZ_Remaining(sz), "Function out of bounds");
        uint32_t payload_start = sz->readcount;
        uint32_t num_locals = SZ_ReadLeb(sz);
        uint32_t save_pos, tidx, lidx, lecount;

        // Get number of locals for alloc
        save_pos = sz->readcount;
        func->num_locals = 0;
        for (uint32_t l = 0; l < num_locals; l++) {
            lecount = SZ_ReadLeb(sz);
            ASSERT(lecount <= MAX_LOCALS - func->num_locals, "Too many locals");
            func->num_locals += lecount;
            tidx = SZ_ReadLeb(sz);
            (void)tidx;
        }
        func->locals = WA_Malloc(func->num_locals * sizeof(func->locals[0]));

        // Restore position and read the locals
        sz->readcount = save_pos;
        lidx = 0;
        for (uint32_t l = 0; l < num_locals; l++) {
            lecount = SZ_ReadLeb(sz);
            tidx = SZ_ReadLeb(sz);
            for (uint32_t l = 0; l < lecount; l++)
                func->locals[lidx++] = tidx;
        }

        func->start_addr = sz->readcount;
        func->end_addr = payload_start + body_size - 1;
        ASSERT(sz->data[func->end_addr] == End, "Function block doesn't end with End opcode");
        sz->readcount = func->end_addr + 1;
    }

    return qtrue;
}

typedef struct {
    uint32_t pos, len;
} wa_section_t;

typedef struct {
    wa_section_id_t id;
    qboolean (*parse)(wa_t *m, sizebuf_t *sz);
} wa_parsefunc_t;

static const wa_parsefunc_t parsefuncs[] = {
    { SectTypes, parse_types },
    { SectFunctions, parse_functions_pre },
    { SectImports, parse_imports },
    { SectFunctions, parse_functions },
    { SectTables, parse_tables },
    { SectMemory, parse_memory },
    { SectGlobals, parse_globals },
    { SectExports, parse_exports },
    { SectElements, parse_elements },
    { SectCode, parse_code },
    { SectData, parse_data },
};

static qboolean parse_sections(wa_t *m, sizebuf_t *sz)
{
    // Read the sections
    wa_section_t sections[NumSections] = { 0 };
    while (sz->readcount < sz->cursize) {
        uint32_t id = SZ_ReadByte(sz);
        uint32_t len = SZ_ReadLeb(sz);
        ASSERT(id < NumSections, "Unknown section %u", id);
        ASSERT(len <= SZ_Remaining(sz), "Section %u out of bounds", id);
        sections[id].pos = sz->readcount;
        sections[id].len = len;
        sz->readcount += len;
    }

    uint32_t cursize = sz->cursize;
    for (uint32_t i = 0; i < ARRAY_LEN(parsefuncs); i++) {
        const wa_parsefunc_t *p = &parsefuncs[i];
        if (!sections[p->id].len)
            continue;
        sz->readcount = sections[p->id].pos;
        sz->cursize = sections[p->id].pos + sections[p->id].len;
        if (!p->parse(m, sz))
            return qfalse;
        ASSERT(sz->readcount <= sz->cursize, "Read past end of section");
    }

    sz->readcount = 0;
    sz->cursize = cursize;
    return qtrue;
}

static const wa_export_t *find_export(wa_t *m, uint32_t kind, const char *name)
{
    for (uint32_t e = 0; e < m->num_exports; e++) {
        const wa_export_t *export = &m->exports[e];
        if (export->kind == kind && wa_string_eq(&export->name, name))
            return export;
    }
    return NULL;
}

static qboolean find_exports(wa_t *m)
{
    const wa_export_t *export;

    // Find vmMain
    export = find_export(m, KIND_FUNCTION, "vmMain");
    ASSERT(export, "vmMain export not found");
    m->vmMain = export->value;
    ASSERT(m->vmMain->type->num_results, "vmMain must return a value");

    // Find LLVM stack pointer
    export = find_export(m, KIND_GLOBAL, "__stack_pointer");
    if (export)
        m->llvm_stack_pointer = export->value;
    else
        Com_Printf(S_COLOR_YELLOW "WARNING: __stack_pointer export not found\n");

    // Prevent dangling pointers after file is freed
    for (uint32_t e = 0; e < m->num_exports; e++)
        m->exports[e].name = (wa_string_t){ 0 };

    return qtrue;
}

static qboolean WA_Restart(wa_t *m, sizebuf_t *sz)
{
    ASSERT(m, "VM format changed after restart");
    WA_Reset(m);

    // Clear memory and stacks
    memset(m->memory.bytes, 0, m->memory.bytesize);
    memset(m->stack, 0, sizeof(m->stack[0]) * STACK_SIZE);
    memset(m->callstack, 0, sizeof(m->callstack[0]) * CALLSTACK_SIZE);

    // Init memory
    while (sz->readcount < sz->cursize) {
        uint32_t id = SZ_ReadByte(sz);
        uint32_t len = SZ_ReadLeb(sz);
        ASSERT(id < NumSections, "Unknown section %u", id);
        ASSERT(len <= SZ_Remaining(sz), "Section %u out of bounds", id);
        if (id == SectData) {
            sz->cursize = sz->readcount + len;
            if (!parse_data(m, sz))
                return qfalse;
            break;
        }
        sz->readcount += len;
    }

    // TODO: reset globals too? There should be only one global though,
    // and that's __stack_pointer, so typically this is not needed...

    return qtrue;
}

qboolean WA_LoadModule(vm_t *vm, void *data, uint32_t size, qboolean alloc)
{
    wa_t        *m;
    sizebuf_t   sz;

    SZ_InitRead(&sz, data, size);

    ASSERT(SZ_ReadLong(&sz) == WA_MAGIC, "Bad WASM header magic");
    ASSERT(SZ_ReadLong(&sz) == WA_VERSION, "Bad WASM header version");

    if (!alloc)
        return WA_Restart(vm->wasm, &sz);

    // Allocate the module
    m = WA_Malloc(sizeof(*m));

    // Empty stacks
    m->sp  = -1;
    m->fp  = -1;
    m->csp = -1;

    if (!parse_sections(m, &sz))
        return qfalse;

    if (!find_exports(m))
        return qfalse;

    if (!WA_PrepareInterpreter(m, &sz))
        return qfalse;

    // Save LLVM stack start
    if (m->llvm_stack_pointer)
        m->llvm_stack_start = *m->llvm_stack_pointer;

    // Alloc stacks
    m->vm = vm;
    m->stack = WA_Malloc(sizeof(m->stack[0]) * STACK_SIZE);
    m->callstack = WA_Malloc(sizeof(m->callstack[0]) * CALLSTACK_SIZE);

    // Fill in some stuff
    vm->wasm = m;
    vm->exactDataLength = m->memory.pages * WA_PAGE_SIZE;
    vm->dataLength = m->memory.bytesize;
    vm->dataAlloc = m->memory.bytesize + VM_DATA_GUARD_SIZE;
    vm->dataMask = m->memory.bytesize - 1;
    vm->dataBase = m->memory.bytes;

    Com_DPrintf("Loaded %s: %d KB of code, %d MB of memory\n", vm->name,
                m->num_bytes / 1000, m->memory.bytesize / 1000000);

    return qtrue;
}

// Push n values to stack
static wa_value_t *WA_Push(wa_t *m, int n)
{
    WA_ASSERT(m->sp < STACK_SIZE - n, "Stack overflow");
    m->sp += n;
    return &m->stack[m->sp - n + 1];
}

// Pop one value from stack
static wa_value_t *WA_Pop(wa_t *m)
{
    WA_ASSERT(m->sp >= 0, "Stack underflow");
    return &m->stack[m->sp--];
}

int32_t WA_CallInterpreted(wa_t *m, int nargs, int32_t *args)
{
    const wa_block_t *func = m->vmMain;
    WA_ASSERT(nargs <= func->type->num_params, "Too many arguments");

    // Push ALL vmMain parameters to stack.
    // Only first nargs will contain meaningful values.
    wa_value_t *stack = WA_Push(m, func->type->num_params);
    for (int i = 0; i < nargs; i++)
        stack[i].i32 = args[i];

    WA_SetupCall(m, func - m->funcs);
    WA_Interpret(m);
    return WA_Pop(m)->i32;
}

// This needs to be called after longjmp()'ing from VM
void WA_Reset(wa_t *m)
{
    if (!m)
        return;

    // Empty stacks
    m->sp  = -1;
    m->fp  = -1;
    m->csp = -1;

    // Reset LLVM stack pointer
    if (m->llvm_stack_pointer)
        *m->llvm_stack_pointer = m->llvm_stack_start;
}
