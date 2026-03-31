/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2011-2026 Quake3e project

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// cross-plaform QVM opStack/register optimization framework

// ---------------- register allocation --------------------

// register allocation preferences

#define RMASK  0x1F		// mask to get register number from preference, must cover all platforms

#define FORCED 0x20		// load function must return specified register
#define TEMP   0x40		// hint: temporary allocation, will not be stored on opStack
#define RCONST 0x80		// hint: register value will be not modified
#define XMASK  0x100	// exclude masked registers
#define SHIFT4 0x200	// load bottom (opstack-1) item

// -------------- virtual opStack management ---------------

// types of items on the opStack
typedef enum {
	TYPE_RAW,		// stored value
	TYPE_CONST,		// constant
	TYPE_LOCAL,		// address of local variable
	TYPE_RX,		// volatile - general-purpose register
	TYPE_SX,		// volatile - FPU scalar register
} opstack_value_t;

typedef struct opstack_s {
	uint32_t value;
	int offset;		// negative value means it is already on the opStack
	opstack_value_t type;
	int safe_arg;	// local/global address validated to be in the sane range
} opstack_t;

typedef struct var_addr_s {
	int32_t addr; // variable address/offset
	uint8_t base; // procBase or dataBase register, ranges should NOT overlap
	uint8_t size; // in bytes: 1, 2, 4
} var_addr_t;

typedef enum {
	RTYPE_UNUSED = 0x0,
	RTYPE_CONST = 0x1,
	RTYPE_VAR = 0x2
} reg_value_t;

typedef enum {
	Z_NONE,
	Z_EXT8,
	S_EXT8,
	Z_EXT16,
	S_EXT16,
} ext_t;

#ifndef REG_MAP_COUNT
#define REG_MAP_COUNT 4
#endif

typedef struct reg_s {
	int type_mask; // mask or enum of reg_value_t
	struct {
		uint32_t value;
	} cnst;
	// register value can be mapped to many memory regions
	struct {
		var_addr_t map[REG_MAP_COUNT];
		unsigned idx; // next allocation slot
	} vars;
	uint32_t ip; // ip of last reference
	int refcnt;  // reference counter
	ext_t ext;	 // zero/sign-extension flags
} reg_t;

static int opstack;
static opstack_t opstackv[PROC_OPSTACK_SIZE + 1];

// masked register can't be allocated or flushed to opStack on register pressure/spilling

static int32_t rx_mask[NUM_RX_REGS];
static int32_t sx_mask[NUM_SX_REGS];

// cached register values/metadata

static reg_t rx_regs[NUM_RX_REGS];
static reg_t sx_regs[NUM_SX_REGS];

// functions that must be implemented on per-platform basis:

// gp.dst = gp.src
static void mov_rx( uint32_t dst, uint32_t src );
// fp.dst = fp.src
static void mov_sx( uint32_t dst, uint32_t src );
// alloc new.gp.reg; new.gp.reg = gp.reg
static uint32_t clone_rx( uint32_t reg );
// alloc new.fp.reg; new.fp.reg = fp.reg
static uint32_t clone_sx( uint32_t reg );
// gp.rx = fp.sx
static void mov_rx_sx( uint32_t rx, uint32_t sx );
// fp.sx = gp.rx
static void mov_sx_rx( uint32_t sx, uint32_t rx );
// gp.reg = imm32
static void mov_rx_imm32( uint32_t reg, uint32_t imm32 );
// fp.reg = imm32
static void mov_sx_imm32( uint32_t reg, uint32_t imm32 );
// gp.reg = programStack + addr
static void mov_rx_local( uint32_t reg, const uint32_t addr );
// fp.reg = programStack + addr
static void mov_sx_local( uint32_t reg, const uint32_t addr );
// gp.reg = opStack[offset]
static void load4_rx( uint32_t reg, uint32_t offset );
// fp.reg = opStack[offset]
static void load4_sx( uint32_t reg, uint32_t offset );
// opStack[offset] = gp.rx
static void store4_rx( uint32_t rx, uint32_t offset );
// opStack[offset] = fp.sx
static void store4_sx( uint32_t sx, uint32_t offset );
// opStack[offset] = const
static void store4_const( uint32_t value, uint32_t offset );
// opStack[offset] = pStack + addr
static void store4_local( uint32_t value, uint32_t offset );

// internal forward declarations:

static void unmask_rx( uint32_t reg );
static void unmask_sx( uint32_t reg );
static uint32_t alloc_rx( uint32_t pref );
static uint32_t alloc_sx( uint32_t pref );
static uint32_t alloc_rx_const( uint32_t pref, uint32_t imm );
static uint32_t alloc_rx_local( uint32_t pref, uint32_t imm );


#ifdef LOAD_OPTIMIZE
static void wipe_reg_range( reg_t* reg, const var_addr_t* v ) {
	if ( reg->type_mask & RTYPE_VAR ) {
		uint32_t c, n;
		for ( c = 0, n = 0; n < ARRAY_LEN( reg->vars.map ); n++ ) {
			var_addr_t *var = &reg->vars.map[n];
			if ( var->size != 0 ) {
				c++;
				if ( var->base == v->base ) {
					if ( v->addr < var->addr + var->size && v->addr + v->size > var->addr ) {
						memset( var, 0x0, sizeof( *var ) );
						//var->size = 0;
						c--; continue;
					}
				}
			}
		}
		if ( c == 0 ) {
			reg->type_mask &= ~RTYPE_VAR;
			reg->ext = Z_NONE;
		} else {
			//reg->type_mask |= RTYPE_VAR;
		}
	}
}
#endif


static void wipe_var_range( const var_addr_t *v )
{
#ifdef LOAD_OPTIMIZE
	uint32_t i;
#ifdef DEBUG_VM
	if ( v->size == 0 || v->base == 0 )
		DROP( "incorrect variable setup" );
#endif
	// wipe all types of overlapping variables
	for ( i = 0; i < ARRAY_LEN( rx_regs ); i++ ) {
		wipe_reg_range( &rx_regs[i], v );
	}
	for ( i = 0; i < ARRAY_LEN( sx_regs ); i++ ) {
		wipe_reg_range( &sx_regs[i], v );
	}
#endif
}


#ifdef LOAD_OPTIMIZE
static void set_var_map( reg_t *r, const var_addr_t *v ) {
	uint32_t n;
	for ( n = 0; n < ARRAY_LEN( r->vars.map ); n++ ) {
		if ( r->vars.map[n].size == 0 ) {
			r->vars.map[n] = *v;
			r->vars.idx = ( n + 1 ) % ARRAY_LEN( r->vars.map );
			return;
		}
	}
	r->vars.map[r->vars.idx] = *v;
	r->vars.idx = ( r->vars.idx + 1 ) % ARRAY_LEN( r->vars.map );
}
#endif


void set_rx_var( uint32_t reg, const var_addr_t *v ) {
#ifdef LOAD_OPTIMIZE
	if ( reg < ARRAY_LEN( rx_regs ) ) {
		reg_t *r = rx_regs + reg;
#ifdef REG_TYPE_MASK
		r->type_mask |= RTYPE_VAR;
#else
		r->type_mask = RTYPE_VAR;
#endif
		set_var_map( r, v );
		r->refcnt++; // = 1;
		r->ip = ip;
	}
#endif
}


void set_rx_ext( uint32_t reg, ext_t ext ) {
#ifdef LOAD_OPTIMIZE
	if ( reg >= ARRAY_LEN( rx_regs ) )
		DROP( "register index %i is out of range", reg );
	else
		rx_regs[reg].ext = ext;
#endif
}


void set_sx_var( uint32_t reg, const var_addr_t *v ) {
#ifdef LOAD_OPTIMIZE
	if ( reg < ARRAY_LEN( sx_regs ) ) {
		reg_t *r = sx_regs + reg;
#ifdef REG_TYPE_MASK
		r->type_mask |= RTYPE_VAR;
#else
		r->type_mask = RTYPE_VAR;
#endif
		set_var_map( r, v );
		r->refcnt++; // = 1;
		r->ip = ip;
	}
#endif
}


reg_t *find_rx_var( uint32_t *reg, const var_addr_t *v ) {
#ifdef LOAD_OPTIMIZE
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( rx_regs ); i++ ) {
		reg_t *r = &rx_regs[i];
		if ( r->type_mask & RTYPE_VAR ) {
			uint32_t n;
			for ( n = 0; n < ARRAY_LEN( r->vars.map ); n++ ) {
				if ( r->vars.map[n].size && r->vars.map[n].addr == v->addr && r->vars.map[n].size == v->size && r->vars.map[n].base == v->base ) {
					r->refcnt++;
					r->ip = ip;
					*reg = i;
					return r;
				}
			}
		}
	}
#endif
	return NULL;
}


qboolean find_sx_var( uint32_t *reg, const var_addr_t *v ) {
#ifdef LOAD_OPTIMIZE
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( sx_regs ); i++ ) {
		reg_t *r = &sx_regs[i];
		if ( r->type_mask & RTYPE_VAR ) {
			uint32_t n;
			for ( n = 0; n < ARRAY_LEN( r->vars.map ); n++ ) {
				if ( r->vars.map[n].size && r->vars.map[n].addr == v->addr && r->vars.map[n].size == v->size && r->vars.map[n].base == v->base ) {
					r->refcnt++;
					r->ip = ip;
					*reg = i;
					return qtrue;
				}
			}
		}
	}
#endif // LOAD_OPTIMIZE
	return qfalse;
}


void reduce_map_size( reg_t *reg, uint32_t size ) {
#ifdef Q3_LITTLE_ENDIAN
	int i;
	for ( i = 0; i < ARRAY_LEN( reg->vars.map ); i++ ) {
		if ( reg->vars.map[i].size > size ) {
			reg->vars.map[i].size = size;
		}
	}
#else
	// zero/sign extension shifts memory mappings on BE systems
	reg->type_mask &= ~RTYPE_VAR;
	memset( reg->vars.map, 0x0, sizeof( reg->vars.map ) );
#endif
	// modify constant
	if ( size == 1 ) {
		reg->cnst.value &= 0xFF;
		reg->ext = Z_EXT8;
	} else {
		reg->cnst.value &= 0xFFFF;
		reg->ext = Z_EXT16;
	}
}


 reg_t *rx_on_top( void ) {
	opstack_t *it = &opstackv[ opstack ];
	if ( it->type == TYPE_RX ) {
		return &rx_regs[ it->value ];
	} else {
		return NULL;
	}
}


static void wipe_vars( void )
{
#ifdef LOAD_OPTIMIZE
	uint32_t i;
	reg_t *r;

	for ( i = 0; i < ARRAY_LEN( rx_regs ); i++ ) {
		r = &rx_regs[i];
		memset( &r->vars, 0, sizeof( r->vars ) );
		r->type_mask &= ~RTYPE_VAR;
		r->ext = Z_NONE;
	}
	for ( i = 0; i < ARRAY_LEN( sx_regs ); i++ ) {
		r = &sx_regs[i];
		memset( &r->vars, 0, sizeof( r->vars ) );
		r->type_mask &= ~RTYPE_VAR;
		r->ext = Z_NONE;
	}
#endif
}


static qboolean search_opstack( opstack_value_t type, uint32_t value ) {
	int i;
	for ( i = 1; i <= opstack; i++ ) {
		if ( opstackv[ i ].type == type && opstackv[ i ].value == value ) {
			return qtrue;
		}
	}
	return qfalse;
}


static void wipe_rx_meta( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( reg >= ARRAY_LEN( rx_regs ) )
		DROP( "incorrect register index %i", reg );
#endif
	memset( &rx_regs[reg], 0x0, sizeof( rx_regs[0] ) );
	//rx_regs[reg].type_mask = RTYPE_UNUSED;
}


static void wipe_sx_meta( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( reg >= ARRAY_LEN( sx_regs ) )
		DROP( "incorrect register index %i", reg );
#endif
	memset( &sx_regs[reg], 0x0, sizeof( sx_regs[0] ) );
	//sx_regs[reg].type_mask = RTYPE_UNUSED;
}


static void mask_rx( uint32_t reg )
{
	rx_mask[reg]++;
}


static void mask_sx( uint32_t reg )
{
	sx_mask[reg]++;
}


static void unmask_rx( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( rx_mask[reg] <= 0 ) {
		DROP( "register R%i is already unmasked", reg );
	}
#endif
	rx_mask[reg]--;
}


static void unmask_sx( uint32_t reg )
{
#ifdef DEBUG_VM
	if ( sx_mask[reg] <= 0 ) {
		DROP( "register S%i is already unmasked", reg );
	}
#endif
	sx_mask[reg]--;
}


static void flush_item( opstack_t *it )
{
	switch ( it->type ) {

	case TYPE_RX:
		if ( it->offset >= 0 )
			store4_rx( it->value, it->offset );
		// else syscall return, already on the opStack
		break;

	case TYPE_SX:
		store4_sx( it->value, it->offset );
		break;

	case TYPE_CONST:
		store4_const( it->value, it->offset );
		break;

	case TYPE_LOCAL:
		store4_local( it->value, it->offset );
		break;

	default:
		break;
	}

	it->type = TYPE_RAW;
	it->safe_arg = 0;
}


static void flush_items( opstack_value_t type, uint32_t value ) {
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == type && it->value == value ) {
			flush_item( it );
		}
	}
}


static void init_opstack( void )
{
	opstack = 0;

	Com_Memset( &opstackv[0], 0, sizeof( opstackv ) );

	Com_Memset( &rx_mask[0], 0, sizeof( rx_mask ) );
	Com_Memset( &sx_mask[0], 0, sizeof( sx_mask ) );

	Com_Memset( &rx_regs[0], 0, sizeof( rx_regs ) );
	Com_Memset( &sx_regs[0], 0, sizeof( sx_regs ) );
}


qboolean scalar_on_top( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
#ifdef FPU_OPTIMIZE
	if ( opstackv[ opstack ].type == TYPE_SX )
		return qtrue;
#endif
	return qfalse;
}


qboolean addr_on_top( var_addr_t *addr, uint32_t dataBase, uint32_t procBase )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
#ifdef ADDR_OPTIMIZE
	if ( opstackv[ opstack ].type == TYPE_CONST ) {
		addr->addr = opstackv[ opstack ].value;
		addr->base = dataBase;
		addr->size = 0;
		return qtrue;
	}
	if ( opstackv[ opstack ].type == TYPE_LOCAL ) {
		addr->addr = opstackv[ opstack ].value;
		addr->base = procBase;
		addr->size = 0;
		return qtrue;
	}
#endif
	return qfalse;
}


void discard_top( void )
{
	opstack_t *it = &opstackv[ opstack ];
	it->type = TYPE_RAW;
	it->safe_arg = 0;
}


int is_safe_arg( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
	return opstackv[ opstack ].safe_arg;
}


static void inc_opstack( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE )
		DROP( "opstack overflow - %i", opstack * 4 );
#endif

	opstack += 1;

#ifdef DEBUG_VM
	if ( opstackv[ opstack ].type != TYPE_RAW )
		DROP( "bad item type %i at opstack %i", opstackv[ opstack ].type, opstack * 4 );
#endif
}


static void dec_opstack( void )
{
#ifdef DEBUG_VM
	opstack_t *it;

	if ( opstack <= 0 )
		DROP( "opstack underflow - %i", opstack * 4 );

	it = &opstackv[ opstack ];
	if ( it->type != TYPE_RAW )
		DROP( "opstack[%i]: item type %i is not consumed", opstack * 4, it->type );
#endif
	opstack -= 1;
}


static void dec_opstack_discard( void )
{
	opstack_t *it;

	it = &opstackv[ opstack ];
#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "opstack underflow - %i", opstack * 4 );

	if ( it->type != TYPE_RAW && ( it->type != TYPE_RX || it->offset >= 0 ) )
		DROP( "opstack[%i]: item type %i is not consumed", opstack * 4, it->type );
#endif

	it->type = TYPE_RAW; // discard value
	it->safe_arg = 0;

	opstack -= 1;
}


// returns bitmask of registers present on opstack
#if defined(DYN_ALLOC_RX) || defined(DYN_ALLOC_SX)
static uint32_t build_opstack_mask( opstack_value_t reg_type )
{
	uint32_t mask = 0;
	int i;
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == reg_type ) {
			mask |= ( 1 << it->value );
		}
	}
	return mask;
}
#endif


#ifdef DYN_ALLOC_RX
static uint32_t build_rx_mask( void )
{
	uint32_t i, mask = 0;
	for ( i = 0; i < ARRAY_LEN( rx_mask ); i++ ) {
		if ( rx_mask[i] ) {
			mask |= 1 << i;
		}
	}
	return mask;
}
#endif


#ifdef DYN_ALLOC_SX
static uint32_t build_sx_mask( void )
{
	uint32_t i, mask = 0;
	for ( i = 0; i < ARRAY_LEN( sx_mask ); i++ ) {
		if ( sx_mask[i] ) {
			mask |= 1 << i;
		}
	}
	return mask;
}
#endif

// allocate register with local address value
static uint32_t alloc_rx_local( uint32_t pref, uint32_t imm )
{
	uint32_t rx = alloc_rx( pref );
	mov_rx_local( rx, imm );
	return rx;
}


// returns qtrue if a register with specified constant found or a free register available to store it
qboolean find_rx_const( uint32_t imm32 )
{
#ifdef CONST_CACHE_RX
	uint32_t mask = build_rx_mask() | build_opstack_mask( TYPE_RX );
	int i;

	for ( i = 0; i < ARRAY_LEN( rx_list_cache ); i++ ) {
		reg_t *r;
		uint32_t n = rx_list_cache[ i ];
		if ( mask & ( 1 << n ) ) {
			// target register must be unmasked
			continue;
		}
		r = &rx_regs[n];
		if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm32 ) {
			return qtrue;
		}
		if ( r->type_mask == RTYPE_UNUSED ) {
			return qtrue;
		}
	}
#endif  // CONST_CACHE_RX
	return qfalse;
}


// returns qtrue if register with specified masked constant value found
qboolean find_rx_const_mask( uint32_t imm, uint32_t mask )
{
#ifdef CONST_CACHE_RX
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( rx_list_cache ); i++ ) {
		reg_t *r = &rx_regs[ rx_list_cache[ i ] ];
		if ( ( r->type_mask & RTYPE_CONST ) && ( r->cnst.value & mask ) == imm ) {
			return qtrue;
		}
	}
#endif // CONST_CACHE_RX
	return qfalse;
}


// allocate integer register with constant value
static uint32_t alloc_rx_const( uint32_t pref, uint32_t imm )
{
#ifdef CONST_CACHE_RX
	reg_t *r;
#endif
	uint32_t rx;

#ifdef CONST_CACHE_RX
#ifdef DYN_ALLOC_RX
	if ( ( pref & FORCED ) == 0 ) {
		// support only dynamic allocation mode
		const uint32_t mask = build_rx_mask() | build_opstack_mask( TYPE_RX );
		int min_ref = MAX_QINT;
		int min_ip = MAX_QINT;
		int idx = -1;
		int i, n;

		if ( ( pref & XMASK ) == 0 ) {
			// we can select from already masked registers
			for ( n = 0; n < ARRAY_LEN( rx_regs ); n++ ) {
				r = &rx_regs[n];
				if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
					r->refcnt++;
					r->ip = ip;
					mask_rx( n );
					return n;
				}
			}
		}

		for ( i = 0; i < ARRAY_LEN( rx_list_cache ); i++ ) {
			n = rx_list_cache[i];
			if ( mask & ( 1 << n ) ) {
				// target register must be unmasked and not present on the opStack
				continue;
			}
			r = &rx_regs[n];
			if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
				// exact match, re-use this register
				r->refcnt++; // increase reference count
				r->ip = ip;  // update address too
				mask_rx( n );
				return n;
			}
			if ( r->type_mask == RTYPE_UNUSED ) {
				idx = n;
				break;
			}
			if ( ( r->refcnt < min_ref ) || ( r->refcnt == min_ref && r->ip < min_ip ) ) {
				// update least referenced item index
				min_ref = r->refcnt;
				min_ip = r->ip;
				idx = n;
				continue;
			}
		}
		if ( idx != -1 ) {
			r = &rx_regs[ idx ];
			memset( &r->vars, 0, sizeof( r->vars ) );
			r->type_mask = RTYPE_CONST;
			r->cnst.value = imm;
			r->refcnt = 1;
			r->ip = ip;
			r->ext = Z_NONE;
			mov_rx_imm32( idx, imm );
			mask_rx( idx );
			return idx;
		}
		// else go to usual allocation to handle register spilling
	}
#endif // DYN_ALLOC_RX
#endif // CONST_CACHE_RX

	rx = alloc_rx( pref );

	mov_rx_imm32( rx, imm );

#ifdef CONST_CACHE_RX
	r = &rx_regs[ rx ];
	//memset( &r->vars, 0, sizeof( r->vars ) );
	r->type_mask = RTYPE_CONST;
	r->cnst.value = imm;
	r->refcnt = 1;
	r->ip = ip;
	//r->ext = Z_NONE;
#endif

	return rx;
}


// allocate scalar register with constant value
static uint32_t alloc_sx_const( uint32_t pref, uint32_t imm )
{
#ifdef CONST_CACHE_SX
	reg_t *r;
#endif
	uint32_t sx;

#ifdef CONST_CACHE_SX
#ifdef DYN_ALLOC_SX
	if ( ( pref & FORCED ) == 0 ) {
		// support only dynamic allocation mode
		const uint32_t mask = build_sx_mask() | build_opstack_mask( TYPE_SX );
		int min_ref = MAX_QINT;
		int min_ip = MAX_QINT;
		int idx = -1;
		int i, n;

		if ( ( pref & XMASK ) == 0 ) {
			// we can select from already masked registers
			for ( n = 0; n < ARRAY_LEN( sx_regs ); n++ ) {
				r = &sx_regs[n];
				if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
					r->refcnt++;
					r->ip = ip;
					mask_sx( n );
					return n;
				}
			}
		}

		for ( i = 0; i < ARRAY_LEN( sx_list_cache ); i++ ) {
			n = sx_list_cache[i];
			if ( mask & ( 1 << n ) ) {
				// target register must be unmasked and not present on the opStack
				continue;
			}
			r = &sx_regs[n];
			if ( r->type_mask & RTYPE_CONST && r->cnst.value == imm ) {
				// exact match, re-use this register
				r->refcnt++; // increase reference count
				r->ip = ip;  // update address too
				mask_sx( n );
				return n;
			}
			if ( r->type_mask == RTYPE_UNUSED ) {
				idx = n;
				break;
			}
			if ( ( r->refcnt < min_ref ) || ( r->refcnt == min_ref && r->ip < min_ip ) ) {
				// update least referenced item index
				min_ref = r->refcnt;
				min_ip = r->ip;
				idx = n;
				continue;
			}
		}
		if ( idx != -1 ) {
			r = &sx_regs[ idx ];
			memset( &r->vars, 0, sizeof( r->vars ) );
			r->type_mask = RTYPE_CONST;
			r->cnst.value = imm;
			r->refcnt = 1;
			r->ip = ip;
			r->ext = Z_NONE;
			mov_sx_imm32( idx, imm );
			mask_sx( idx );
			return idx;
		}
		// else go to usual allocation to handle register spilling
	}
#endif // DYN_ALLOC_SX
#endif // CONST_CACHE_SX

	sx = alloc_sx( pref );

	mov_sx_imm32( sx, imm );

#ifdef CONST_CACHE_SX
	r = &sx_regs[sx];
	//memset( &r->vars, 0, sizeof( r->vars ) );
	r->type_mask = RTYPE_CONST;
	r->cnst.value = imm;
	r->refcnt = 1;
	r->ip = ip;
	//r->ext = Z_NONE;
#endif

	return sx;
}

#ifdef DYN_ALLOC_SX
static uint32_t dyn_alloc_rx( void )
{
	const uint32_t _rx_mask = build_rx_mask();
	const uint32_t mask = _rx_mask | build_opstack_mask( TYPE_RX );
	const reg_t *reg, *used = NULL;
	uint32_t i, n;

	// try to bypass registers with metadata
	for ( i = 0; i < ARRAY_LEN( rx_list_alloc ); i++ ) {
		n = rx_list_alloc[i];
		if ( mask & ( 1 << n ) ) {
			continue;
		}
		reg = &rx_regs[n];
		if ( reg->type_mask != RTYPE_UNUSED ) {
			// mark least used item
			if ( !used || reg->refcnt < used->refcnt || ( reg->refcnt == used->refcnt && reg->ip < used->ip ) ) {
				used = reg;
			}
			continue;
		}
		wipe_rx_meta( n );
		mask_rx( n );
		return n;
	}

	if ( used ) {
		// no free slots but something occupied by metadata
		uint32_t idx = used - rx_regs;
		wipe_rx_meta( idx );
		mask_rx( idx );
		return idx;
	}

	// no free registers, flush bottom of the opStack
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_RX ) {
			n = it->value;
			// skip masked registers
			if ( _rx_mask & ( 1 << n ) ) {
				continue;
			}
			flush_item( it );
			flush_items( TYPE_RX, n ); // flush cloned registers too
			wipe_rx_meta( n );
			mask_rx( n );
			return n;
		}
	}

	return ~0U;
}
#endif // DYN_ALLOC_SX


// integer register allocation
static uint32_t alloc_rx( uint32_t pref )
{
	uint32_t reg;

#ifdef DYN_ALLOC_RX
	if ( ( pref & FORCED ) == 0 ) {
		uint32_t v = dyn_alloc_rx();
		if ( v == ~0U ) {
			DROP( "no free registers at ip %i, pref %x, opStack %i, mask %04x", ip, pref, opstack * 4, build_rx_mask() );
		}
		return v;
	}
#endif

	reg = pref & RMASK;

#ifdef DEBUG_VM
	if ( reg >= ARRAY_LEN( rx_mask ) )
		DROP( "forced register R%i index overflowed!", reg );
	else if ( rx_mask[reg] )
		DROP( "forced register R%i is already masked!", reg );
#endif

	// FORCED option: find and flush target register
	flush_items( TYPE_RX, reg );

	wipe_rx_meta( reg );
	mask_rx( reg );
	return reg;
}


#ifdef DYN_ALLOC_SX
static uint32_t dyn_alloc_sx( void )
{
	const uint32_t _sx_mask = build_sx_mask();
	const uint32_t mask = _sx_mask | build_opstack_mask( TYPE_SX );
	const reg_t *reg, *used = NULL;
	uint32_t i, n;

	// try to bypass registers with metadata
	for ( i = 0; i < ARRAY_LEN( sx_list_alloc ); i++ ) {
		n = sx_list_alloc[i];
		if ( mask & ( 1 << n ) ) {
			continue;
		}
		reg = &sx_regs[n];
		if ( reg->type_mask != RTYPE_UNUSED ) {
			// mark least used item
			if ( !used || reg->refcnt < used->refcnt || ( reg->refcnt == used->refcnt && reg->ip < used->ip ) ) {
				used = reg;
			}
			continue;
		}
		wipe_sx_meta( n );
		mask_sx( n );
		return n;
	}

	if ( used ) {
		// no free slots but something occupied by metadata
		uint32_t idx = used - sx_regs;
		wipe_sx_meta( idx );
		mask_sx( idx );
		return idx;
	}

	// no free registers, flush bottom of the opStack
	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_SX ) {
			n = it->value;
			// skip masked registers
			if ( _sx_mask & ( 1 << n ) ) {
				continue;
			}
			flush_item( it );
			flush_items( TYPE_SX, n ); // flush cloned registers too
			wipe_sx_meta( n );
			mask_sx( n );
			return n;
		}
	}

	return ~0U;
}
#endif


// scalar register allocation
static uint32_t alloc_sx( uint32_t pref )
{
	uint32_t reg;

#ifdef DYN_ALLOC_SX
	if ( ( pref & FORCED ) == 0 ) {
		uint32_t v = dyn_alloc_sx();
		if ( v == ~0U ) {
			DROP( "no free registers at ip %i, pref %x, opStack %i, mask %04x", ip, pref, opstack * 4, build_sx_mask() );
		}
		return v;
	}
#endif

	reg = pref & RMASK;

#ifdef DEBUG_VM
	if ( reg >= ARRAY_LEN( sx_mask ) )
		DROP( "forced register S%i index overflowed!", reg );
	else if ( sx_mask[reg] )
		DROP( "forced register S%i is already masked!", reg );
#endif

	// FORCED option: find and flush target register
	flush_items( TYPE_SX, reg );

	wipe_sx_meta( reg );
	mask_sx( reg );
	return reg;
}


/*
==============
flush_volatile

flush any cached register/address/constant to opstack and reset meta (constants mapping)
this MUST be called before any unconditional jump, return or function call
==============
*/
static void flush_volatile( void )
{
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		if ( it->type == TYPE_RX || it->type == TYPE_SX ) {
			flush_item( it );
		}
	}

	// wipe all constants metadata
	Com_Memset( &rx_regs[0], 0, sizeof( rx_regs ) );
	Com_Memset( &sx_regs[0], 0, sizeof( sx_regs ) );
}


static void flush_opstack( void )
{
	int i;

	for ( i = 0; i <= opstack; i++ ) {
		opstack_t *it = opstackv + i;
		flush_item( it );
	}

	// wipe all constants metadata
	Com_Memset( &rx_regs[0], 0, sizeof( rx_regs ) );
	Com_Memset( &sx_regs[0], 0, sizeof( sx_regs ) );
}


static void store_rx_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack * 4 );
#endif

	it->type = TYPE_RX;
	it->offset = opstack * sizeof( int32_t );
	it->value = reg;
	it->safe_arg = 0;

	unmask_rx( reg ); // so it can be flushed on demand
}


void store_syscall_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack * 4 );
#endif

	it->type = TYPE_RX;
	it->offset = -(opstack + 1) * sizeof( int32_t ); // < 0 means it is already on the opStack, no need to flush
	it->value = reg;
	it->safe_arg = 0;

	wipe_rx_meta( it->value );

	unmask_rx( it->value ); // so it can be flushed on demand
}


static void store_sx_opstack( uint32_t reg )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );

	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack * 4 );
#endif

	it->type = TYPE_SX;
	it->offset = opstack * sizeof( int32_t );
	it->value = reg;
	it->safe_arg = 0;

	unmask_sx( reg ); // so it can be flushed on demand
}


static void store_item_opstack( instruction_t* ins )
{
	opstack_t *it = opstackv + opstack;

#ifdef DEBUG_VM
	if ( it->type != TYPE_RAW )
		DROP( "bad type %i at opstack %i", it->type, opstack * 4 );
#endif
	switch ( ins->op ) {
		case OP_CONST: it->type = TYPE_CONST; break;
		case OP_LOCAL: it->type = TYPE_LOCAL; break;
		default: DROP( "incorrect opcode %i", ins->op );
	}

	it->offset = opstack * sizeof( int32_t );
	it->value = ins->value;
	it->safe_arg = ins->safe;
}


static uint32_t finish_rx( uint32_t pref, uint32_t reg ) {

	if ( pref & RCONST ) {
		// non-destructive operation
		return reg;
	}

	if ( search_opstack( TYPE_RX, reg ) ) {
		// another instance is present on opStack
		if ( pref & FORCED ) {
			// nothing should left for a FORCED register
			flush_items( TYPE_RX, reg );
		} else {
			// duplicate
			return clone_rx( reg );
		}
	}

	wipe_rx_meta( reg );
	return reg;
}

/*
===========
load_rx_opstack

loads current opstack value into specified register
returns masked register number, must be unmasked manually if not stored on the opstack
output register is very likely to be modified unless CONST preference is specified
===========
*/
static uint32_t load_rx_opstack( uint32_t pref )
{
	opstack_t *it;
	uint32_t opsv;
	uint32_t reg;

	if ( pref & SHIFT4 ) {
		opsv = opstack - 1;
	} else {
		opsv = opstack;
	}

#ifdef DEBUG_VM
	if ( opsv <= 0 )
		DROP( "bad opstack %i", opsv * 4 );
#endif

	it = &opstackv[opsv];
	reg = pref & RMASK;

	if ( it->type == TYPE_RX ) {
#ifdef DYN_ALLOC_RX
		if ( ( pref & FORCED ) == 0 ) {
			mask_rx( it->value );
			it->type = TYPE_RAW;
			return finish_rx( pref, it->value ); // return current register
		}
#endif
		// FORCED flag: return exact target register
		if ( it->value == reg ) {
			mask_rx( it->value );
			it->type = TYPE_RAW;
			return finish_rx( pref, reg );
		} else {
			// allocate target register
			reg = alloc_rx( pref );

			// copy source to target
			mov_rx( reg, it->value );

			it->type = TYPE_RAW;
			return reg;
		}
	} // it->type == TYPE_RX

	// scalar register on the stack
	if ( it->type == TYPE_SX ) {
		// move from scalar to general-purpose register
		reg = alloc_rx( pref );

		mov_rx_sx( reg, it->value );

		it->type = TYPE_RAW;
		return reg;
	}

	if ( ( pref & RCONST ) == 0 ) {
		// exclude masked registers in use
		pref |= XMASK;
	} // else we can search for constants in masked registers

	if ( it->type == TYPE_CONST ) {
		// move constant to general-purpose register
		reg = alloc_rx_const( pref, it->value );
		it->type = TYPE_RAW;
		return finish_rx( pref, reg );
	}

	if ( it->type == TYPE_LOCAL ) {
		reg = alloc_rx_local( pref, it->value );
		it->type = TYPE_RAW;
		return finish_rx( pref, reg );
	}

	// default raw type, explicit load from opStack[opsv]
	reg = alloc_rx( pref );
	load4_rx( reg, opsv * sizeof( int32_t ) );
	it->type = TYPE_RAW;
	return reg;
}


static uint32_t finish_sx( uint32_t pref, uint32_t reg ) {

	if ( pref & RCONST ) {
		// non-destructive operation
		return reg;
	}

	if ( search_opstack( TYPE_SX, reg ) ) {
		// another instance is present on opStack
		if ( pref & FORCED ) {
			// nothing should left for a FORCED register
			flush_items( TYPE_SX, reg );
		} else {
			// duplicate
			return clone_sx( reg );
		}
	}

	wipe_sx_meta( reg );
	return reg;
}


// we must unmask register manually after allocation/loading
static uint32_t load_sx_opstack( uint32_t pref )
{
	opstack_t *it;
	uint32_t reg;
	uint32_t opsv;

	if ( pref & SHIFT4 ) {
		opsv = opstack - 1;
	} else {
		opsv = opstack;
	}

#ifdef DEBUG_VM
	if ( opsv <= 0 )
		DROP( "bad opstack %i", opsv * 4 );
#endif

	it = &opstackv[ opsv ];
	reg = pref & RMASK;

	// scalar register on the stack
	if ( it->type == TYPE_SX ) {
#ifdef DYN_ALLOC_SX
		if ( ( pref & FORCED ) == 0 ) {
			mask_sx( it->value );
			it->type = TYPE_RAW;
			return finish_sx( pref, it->value );
		}
#endif
		// FORCED flag: return exact target register
		if ( it->value == reg ) {
			mask_sx( it->value );
			it->type = TYPE_RAW;
			return finish_sx( pref, reg );
		} else {
			// allocate target register
			reg = alloc_sx( pref );
			// copy source to target
			mov_sx( reg, it->value );
			it->type = TYPE_RAW;
			return reg;
		}
	}

	// integer register on the stack
	if ( it->type == TYPE_RX ) {
		// move from general-purpose to scalar register
		// should never happen with FPU type promotion, except syscalls
		reg = alloc_sx( pref );
		if ( it->offset < 0 ) {
			// syscall return
			it->offset = -it->offset + sizeof( int32_t );
			load4_sx( reg, it->offset );
		} else {
			mov_sx_rx( reg, it->value );
		}
		it->type = TYPE_RAW;
		return reg;
	}

	if ( ( pref & RCONST ) == 0 ) {
		// exclude masked registers in use
		pref |= XMASK;
	} // else we can search for constants in masked registers

	if ( it->type == TYPE_CONST ) {
		// move constant to scalar register
		reg = alloc_sx_const( pref, it->value );
		it->type = TYPE_RAW;
		return finish_sx( pref, reg );
	}

	if ( it->type == TYPE_LOCAL ) {
		// bogus case: local address casted to float
		reg = alloc_sx( pref );
		mov_sx_local( reg, it->value );
		it->type = TYPE_RAW;
		return reg;
	}

	// default raw type, explicit load from opStack[opsv]
	reg = alloc_sx( pref );
	load4_sx( reg, opsv * sizeof( int32_t ) );
	it->type = TYPE_RAW;
	return reg;
}


qboolean find_free_rx( void ) {
#ifdef DYN_ALLOC_RX
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( rx_list_alloc ); i++ ) {
		uint32_t n = rx_list_alloc[i];
		if ( rx_regs[n].type_mask == RTYPE_UNUSED ) {
			return qtrue;
		}
	}
#endif // DYN_ALLOC_RX
	return qfalse;
}


qboolean find_free_sx( void ) {
#ifdef DYN_ALLOC_SX
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( sx_list_alloc ); i++ ) {
		uint32_t n = sx_list_alloc[i];
		if ( sx_regs[n].type_mask == RTYPE_UNUSED ) {
			return qtrue;
		}
	}
#endif // SYN_ALLOC_SX
	return qfalse;
}


void load_rx_opstack2( uint32_t *dst, uint32_t dst_pref, uint32_t *src, uint32_t src_pref )
{
#if 0
	*dst = *src = load_rx_opstack( src_pref & ~RCONST ); // source, target = *opstack
#else
	*dst = *src = load_rx_opstack( src_pref | RCONST ); // source, target = *opstack
	if ( search_opstack( TYPE_RX, *src ) || find_free_rx() ) {
		// *src is duplicated on opStack or there is a free register
		*dst = alloc_rx( dst_pref & ~RCONST ); // allocate new register for the target
	} else {
		// will be overwritten, wipe metadata
		wipe_rx_meta( *dst );
	}
#endif
}


void load_sx_opstack2( uint32_t *dst, uint32_t dst_pref, uint32_t *src, uint32_t src_pref )
{
#if 0
	*dst = *src = load_sx_opstack( src_pref & ~RCONST ); // source, target = *opstack
#else
	*dst = *src = load_sx_opstack( src_pref | RCONST ); // source, target = *opstack
	if ( search_opstack( TYPE_SX, *src ) || find_free_sx() ) {
		// *src is duplicated on opStack or there is a free register
		*dst = alloc_sx( dst_pref & ~RCONST ); // allocate new register for the target
	} else {
		// will be overwritten, wipe metadata
		wipe_sx_meta( *dst );
	}
#endif
}


void flush_opstack_top( void )
{
#ifdef DEBUG_VM
	if ( opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
	flush_item( &opstackv[ opstack ] );
}


qboolean const_on_top( void )
{
#ifdef DEBUG_VM
	if ( opstack >= PROC_OPSTACK_SIZE || opstack <= 0 )
		DROP( "bad opstack %i", opstack * 4 );
#endif
#ifdef ADDR_OPTIMIZE
	if ( opstackv[opstack].type == TYPE_CONST )
		return qtrue;
#endif
	return qfalse;
}


int32_t top_value( void )
{
	opstack_t *it = &opstackv[ opstack ];
	return it->value;
}
