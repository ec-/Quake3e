/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

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
#include "q_shared.h"
#include "qcommon.h"

static int pcount[256];

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/
void MSG_Init( msg_t *buf, byte *data, int length ) {
	Com_Memset (buf, 0, sizeof(*buf));
	buf->data = data;
	buf->maxsize = length;
	buf->maxbits = length * 8;
}


void MSG_InitOOB( msg_t *buf, byte *data, int length ) {
	Com_Memset (buf, 0, sizeof(*buf));
	buf->data = data;
	buf->maxsize = length;
	buf->maxbits = length * 8;
	buf->oob = qtrue;
}


void MSG_Clear( msg_t *buf ) {
	buf->cursize = 0;
	buf->overflowed = qfalse;
	buf->bit = 0;					//<- in bits
}


void MSG_Bitstream( msg_t *buf ) {
	buf->oob = qfalse;
}


void MSG_BeginReading( msg_t *msg ) {
	msg->readcount = 0;
	msg->bit = 0;
	msg->oob = qfalse;
}


void MSG_BeginReadingOOB( msg_t *msg ) {
	msg->readcount = 0;
	msg->bit = 0;
	msg->oob = qtrue;
}


void MSG_Copy(msg_t *buf, byte *data, int length, const msg_t *src)
{
	if (length<src->cursize) {
		Com_Error( ERR_DROP, "MSG_Copy: can't copy into a smaller msg_t buffer");
	}
	Com_Memcpy(buf, src, sizeof(msg_t));
	buf->data = data;
	Com_Memcpy(buf->data, src->data, src->cursize);
}

/*
=============================================================================

bit functions
  
=============================================================================
*/

// negative bit values include signs
void MSG_WriteBits( msg_t *msg, int value, int bits ) {
	int	i;

	if ( bits == 0 || bits < -31 || bits > 32 ) {
		Com_Error( ERR_DROP, "MSG_WriteBits: bad bits %i", bits );
	}

	if ( msg->overflowed != qfalse )
		return;

	if ( bits < 0 ) {
		bits = -bits;
	}
	if (msg->oob) {
		if ( bits == 8 ) {
			msg->data[msg->cursize] = value;
			msg->cursize += 1;
			msg->bit += 8;
		} else if ( bits == 16 ) {
			short temp = value;
			
			CopyLittleShort(&msg->data[msg->cursize], &temp);
			msg->cursize += 2;
			msg->bit += 16;
		} else if ( bits==32 ) {
			CopyLittleLong(&msg->data[msg->cursize], &value);
			msg->cursize += 4;
			msg->bit += 32;
		} else {
			Com_Error(ERR_DROP, "can't write %d bits", bits);
		}
	} else {
		value &= (0xffffffff>>(32-bits));
		if ( bits & 7 ) {
			int nbits;
			nbits = bits&7;
			for ( i = 0; i < nbits ; i++ ) {
				HuffmanPutBit( msg->data, msg->bit, (value & 1) );
				msg->bit++;
				value = (value>>1);
			}
			bits = bits - nbits;
		}
		if ( bits ) {
			for( i = 0 ; i < bits ; i += 8 ) {
				msg->bit += HuffmanPutSymbol( msg->data, msg->bit, (value & 0xFF) );
				value = (value>>8);
			}
		}
		msg->cursize = (msg->bit>>3)+1;
	}

	if ( msg->bit > msg->maxbits ) {
		msg->overflowed = qtrue;
	}
}


int MSG_ReadBits( msg_t *msg, int bits ) {
	int		value;
	qboolean	sgn;
	int		i;
	unsigned int	sym;
	const byte *buffer = msg->data; // dereference optimization

	if ( msg->bit >= msg->maxbits )
		return 0;

	value = 0;

	if ( bits < 0 ) {
		bits = -bits; // always greater than zero
		sgn = qtrue;
	} else {
		sgn = qfalse;
	}

	if ( msg->oob ) {
		if( bits == 8 )
		{
			value = *(buffer + msg->readcount);
			msg->readcount += 1;
			msg->bit += 8;
		}
		else if ( bits == 16 )
		{
			short temp;
			CopyLittleShort( &temp, buffer + msg->readcount );
			value = temp;
			msg->readcount += 2;
			msg->bit += 16;
		}
		else if ( bits == 32 )
		{
			CopyLittleLong( &value, buffer + msg->readcount );
			msg->readcount += 4;
			msg->bit += 32;
		}
		else
			Com_Error( ERR_DROP, "can't read %d bits", bits );
	} else {
		const int nbits = bits & 7;
		int bitIndex = msg->bit; // dereference optimization
		if ( nbits )
		{		
			for ( i = 0; i < nbits; i++ ) {
				value |= HuffmanGetBit( buffer, bitIndex ) << i;
				bitIndex++;
			}
			bits -= nbits;
		}
		if ( bits )
		{
			for ( i = 0; i < bits; i += 8 )
			{
				bitIndex += HuffmanGetSymbol( &sym, buffer, bitIndex );
				value |= ( sym << (i+nbits) );
			}
		}
		msg->bit = bitIndex;
		msg->readcount = (bitIndex >> 3) + 1;
	}

	if ( sgn && bits < 32 ) {
		if ( value & ( 1 << ( bits - 1 ) ) ) {
			value |= -1 ^ ( ( 1U << bits ) - 1 );
		}
	}

	return value;
}



//================================================================================

//
// writing functions
//

void MSG_WriteChar( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < -128 || c > 127)
		Com_Error (ERR_FATAL, "MSG_WriteChar: range error");
#endif

	MSG_WriteBits( sb, c, 8 );
}

void MSG_WriteByte( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < 0 || c > 255)
		Com_Error (ERR_FATAL, "MSG_WriteByte: range error");
#endif

	MSG_WriteBits( sb, c, 8 );
}

void MSG_WriteData( msg_t *buf, const void *data, int length ) {
	int i;
	for(i=0;i<length;i++) {
		MSG_WriteByte(buf, ((byte *)data)[i]);
	}
}

void MSG_WriteShort( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Com_Error (ERR_FATAL, "MSG_WriteShort: range error");
#endif

	MSG_WriteBits( sb, c, 16 );
}

void MSG_WriteLong( msg_t *sb, int c ) {
	MSG_WriteBits( sb, c, 32 );
}

void MSG_WriteFloat( msg_t *sb, float f ) {
	floatint_t dat;
	dat.f = f;
	MSG_WriteBits( sb, dat.i, 32 );
}

void MSG_WriteString( msg_t *sb, const char *s ) {
	int l, i;
	char v;

	l = s ? strlen( s ) : 0;
	if ( l >= MAX_STRING_CHARS ) {
		Com_Printf( "MSG_WriteString: MAX_STRING_CHARS\n" );
		l = 0; 
	}

	for ( i = 0 ; i < l; i++ ) {
		// get rid of 0x80+ and '%' chars, because old clients don't like them
		if ( (byte)s[i] & 0x80 || s[i] == '%' )
			v = '.';
		else
			v = s[i];
		MSG_WriteChar( sb, v );
	}

	MSG_WriteChar( sb, '\0' );
}

void MSG_WriteBigString( msg_t *sb, const char *s ) {
	int l, i;
	char v;

	l = s ? strlen( s ) : 0;
	if ( l >= BIG_INFO_STRING ) {
		Com_Printf( "MSG_WriteBigString: BIG_INFO_STRING\n" );
		l = 0; 
	}

	for ( i = 0 ; i < l ; i++ ) {
		// get rid of 0x80+ and '%' chars, because old clients don't like them
		if ( (byte)s[i] & 0x80 || s[i] == '%' )
			v = '.';
		else
			v = s[i];
		MSG_WriteChar( sb, v );
	}

	MSG_WriteChar( sb, '\0' );
}

void MSG_WriteAngle( msg_t *sb, float f ) {
	MSG_WriteByte (sb, (int)(f*256/360) & 255);
}

void MSG_WriteAngle16( msg_t *sb, float f ) {
	MSG_WriteShort (sb, ANGLE2SHORT(f));
}


//============================================================

//
// reading functions
//

// returns -1 if no more characters are available
int MSG_ReadChar (msg_t *msg ) {
	int	c;
	
	c = (signed char)MSG_ReadBits( msg, 8 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	
	return c;
}

int MSG_ReadByte( msg_t *msg ) {
	int	c;
	
	c = (unsigned char)MSG_ReadBits( msg, 8 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	return c;
}

int MSG_ReadShort( msg_t *msg ) {
	int	c;
	
	c = (short)MSG_ReadBits( msg, 16 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	

	return c;
}

int MSG_ReadLong( msg_t *msg ) {
	int	c;
	
	c = MSG_ReadBits( msg, 32 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	
	return c;
}

float MSG_ReadFloat( msg_t *msg ) {
	floatint_t dat;
	
	dat.i = MSG_ReadBits( msg, 32 );
	if ( msg->readcount > msg->cursize ) {
		dat.f = -1;
	}	
	
	return dat.f;	
}


const char *MSG_ReadString( msg_t *msg ) {
	static char	string[MAX_STRING_CHARS];
	int	l, c;
	
	l = 0;
	do {
		c = MSG_ReadByte( msg ); // use ReadByte so -1 is out of bounds
		if ( c <= 0 /*c == -1 || c == 0 */ || l >= sizeof(string)-1 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		} else
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		string[ l++ ] = c;
	} while ( qtrue );
	
	string[ l ] = '\0';
	
	return string;
}


const char *MSG_ReadBigString( msg_t *msg ) {
	static char	string[ BIG_INFO_STRING ];
	int	l, c;
	
	l = 0;
	do {
		c = MSG_ReadByte( msg ); // use ReadByte so -1 is out of bounds
		if ( c <= 0 /*c == -1 || c == 0*/ || l >= sizeof(string)-1 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		} else
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		string[ l++ ] = c;
	} while ( qtrue );
	
	string[ l ] = '\0';
	
	return string;
}


const char *MSG_ReadStringLine( msg_t *msg ) {
	static char	string[MAX_STRING_CHARS];
	int	l, c;

	l = 0;
	do {
		c = MSG_ReadByte( msg ); // use ReadByte so -1 is out of bounds
		if ( c <= 0 /*c == -1 || c == 0*/ || c == '\n' || l >= sizeof(string)-1 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		} else
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		string[ l++ ] = c;
	} while ( qtrue );
	
	string[ l ] = '\0';
	
	return string;
}


float MSG_ReadAngle16( msg_t *msg ) {
	return SHORT2ANGLE(MSG_ReadShort(msg));
}


void MSG_ReadData( msg_t *msg, void *data, int len ) {
	int		i;

	for (i=0 ; i<len ; i++) {
		((byte *)data)[i] = MSG_ReadByte (msg);
	}
}


int MSG_ReadEntitynum( msg_t *msg ) {
	const int num = MSG_ReadBits( msg, GENTITYNUM_BITS );
	if ( msg->readcount > msg->cursize ) {
		return -1;
	} else {
		return num;
	}
	
}


// a string hasher which gives the same hash value even if the
// string is later modified via the legacy MSG read/write code
int MSG_HashKey(const char *string, int maxlen) {
	int hash, i;

	hash = 0;
	for (i = 0; i < maxlen && string[i] != '\0'; i++) {
		if (string[i] & 0x80 || string[i] == '%')
			hash += '.' * (119 + i);
		else
			hash += string[i] * (119 + i);
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20));
	return hash;
}

#ifndef DEDICATED
extern cvar_t *cl_shownet;
#define	LOG(x) if( cl_shownet && cl_shownet->integer == 4 ) { Com_Printf("%s ", x ); };
#else
#define	LOG(x)
#endif

/*
=============================================================================

delta functions with keys
  
=============================================================================
*/

static const int kbitmask[32] = {
	0x00000001, 0x00000003, 0x00000007, 0x0000000F,
	0x0000001F,	0x0000003F,	0x0000007F,	0x000000FF,
	0x000001FF,	0x000003FF,	0x000007FF,	0x00000FFF,
	0x00001FFF,	0x00003FFF,	0x00007FFF,	0x0000FFFF,
	0x0001FFFF,	0x0003FFFF,	0x0007FFFF,	0x000FFFFF,
	0x001FFFFf,	0x003FFFFF,	0x007FFFFF,	0x00FFFFFF,
	0x01FFFFFF,	0x03FFFFFF,	0x07FFFFFF,	0x0FFFFFFF,
	0x1FFFFFFF,	0x3FFFFFFF,	0x7FFFFFFF,	0xFFFFFFFF,
};


static void MSG_WriteDeltaKey( msg_t *msg, int key, int oldV, int newV, int bits ) {
	if ( oldV == newV ) {
		MSG_WriteBits( msg, 0, 1 );
		return;
	}
	MSG_WriteBits( msg, 1, 1 );
	MSG_WriteBits( msg, newV ^ key, bits );
}


static int MSG_ReadDeltaKey( msg_t *msg, int key, int oldV, int bits ) {
	if ( MSG_ReadBits( msg, 1 ) ) {
		return MSG_ReadBits( msg, bits ) ^ (key & kbitmask[ bits - 1 ]);
	}
	return oldV;
}


/*
============================================================================

usercmd_t communication

============================================================================
*/

/*
=====================
MSG_WriteDeltaUsercmdKey
=====================
*/
void MSG_WriteDeltaUsercmdKey( msg_t *msg, int key, const usercmd_t *from, const usercmd_t *to ) {
	if ( (unsigned)(to->serverTime - from->serverTime) < 256 ) {
		MSG_WriteBits( msg, 1, 1 );
		MSG_WriteBits( msg, to->serverTime - from->serverTime, 8 );
	} else {
		MSG_WriteBits( msg, 0, 1 );
		MSG_WriteBits( msg, to->serverTime, 32 );
	}
	if (from->angles[0] == to->angles[0] &&
		from->angles[1] == to->angles[1] &&
		from->angles[2] == to->angles[2] &&
		from->forwardmove == to->forwardmove &&
		from->rightmove == to->rightmove &&
		from->upmove == to->upmove &&
		from->buttons == to->buttons &&
		from->weapon == to->weapon) {
			MSG_WriteBits( msg, 0, 1 );				// no change
			return;
	}
	key ^= to->serverTime;
	MSG_WriteBits( msg, 1, 1 );
	MSG_WriteDeltaKey( msg, key, from->angles[0], to->angles[0], 16 );
	MSG_WriteDeltaKey( msg, key, from->angles[1], to->angles[1], 16 );
	MSG_WriteDeltaKey( msg, key, from->angles[2], to->angles[2], 16 );
	MSG_WriteDeltaKey( msg, key, from->forwardmove, to->forwardmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->rightmove, to->rightmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->upmove, to->upmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->buttons, to->buttons, 16 );
	MSG_WriteDeltaKey( msg, key, from->weapon, to->weapon, 8 );
}


/*
=====================
MSG_ReadDeltaUsercmdKey
=====================
*/
void MSG_ReadDeltaUsercmdKey( msg_t *msg, int key, const usercmd_t *from, usercmd_t *to ) {
	if ( MSG_ReadBits( msg, 1 ) ) {
		to->serverTime = from->serverTime + MSG_ReadBits( msg, 8 );
	} else {
		to->serverTime = MSG_ReadBits( msg, 32 );
	}
	if ( MSG_ReadBits( msg, 1 ) ) {
		key ^= to->serverTime;
		to->angles[0] = MSG_ReadDeltaKey( msg, key, from->angles[0], 16);
		to->angles[1] = MSG_ReadDeltaKey( msg, key, from->angles[1], 16);
		to->angles[2] = MSG_ReadDeltaKey( msg, key, from->angles[2], 16);
		to->forwardmove = MSG_ReadDeltaKey( msg, key, from->forwardmove, 8);
		if( to->forwardmove == -128 )
			to->forwardmove = -127;
		to->rightmove = MSG_ReadDeltaKey( msg, key, from->rightmove, 8);
		if( to->rightmove == -128 )
			to->rightmove = -127;
		to->upmove = MSG_ReadDeltaKey( msg, key, from->upmove, 8);
		if( to->upmove == -128 )
			to->upmove = -127;
		to->buttons = MSG_ReadDeltaKey( msg, key, from->buttons, 16);
		to->weapon = MSG_ReadDeltaKey( msg, key, from->weapon, 8);
	} else {
		to->angles[0] = from->angles[0];
		to->angles[1] = from->angles[1];
		to->angles[2] = from->angles[2];
		to->forwardmove = from->forwardmove;
		to->rightmove = from->rightmove;
		to->upmove = from->upmove;
		to->buttons = from->buttons;
		to->weapon = from->weapon;
	}
}

/*
=============================================================================

entityState_t communication
  
=============================================================================
*/

/*
=================
MSG_ReportChangeVectors_f

Prints out a table from the current statistics for copying to code
=================
*/
void MSG_ReportChangeVectors_f( void ) {
	int i;
	for(i=0;i<256;i++) {
		if (pcount[i]) {
			Com_Printf("%d used %d\n", i, pcount[i]);
		}
	}
}

typedef struct {
	const char	*name;
	const int	offset;
	const int	bits;	// 0 = float
	const int	mergeMask;	// skip_mask, used by multiview only
} netField_t;

#ifdef USE_MV
int MSG_entMergeMask = 0;
#endif

// using the stringizing operator to save typing...
#define	NETF(x) #x,(size_t)&((entityState_t*)0)->x

static const netField_t entityStateFields[] =
{
{ NETF(pos.trTime), 32, SM_TRTIME },				// CPMA: SM_TRTIME
{ NETF(pos.trBase[0]), 0, SM_BASE },
{ NETF(pos.trBase[1]), 0, SM_BASE },
{ NETF(pos.trDelta[0]), 0, SM_BASE },				// CPMA: affected by SM_TRDELTA!
{ NETF(pos.trDelta[1]), 0, SM_BASE },				// CPMA: affected by SM_TRDELTA!
{ NETF(pos.trBase[2]), 0, SM_BASE },
{ NETF(apos.trBase[1]), 0, SM_BASE },
{ NETF(pos.trDelta[2]), 0, SM_BASE },				// CPMA: affected by SM_TRDELTA!
{ NETF(apos.trBase[0]), 0, SM_BASE },
{ NETF(event), 10 },
{ NETF(angles2[YAW]), 0, SM_BASE },					// BASE
{ NETF(eType), 8, SM_BASE },
{ NETF(torsoAnim), 8, SM_BASE },					// BASE
{ NETF(eventParm), 8 },
{ NETF(legsAnim), 8, SM_BASE },						// BASE
{ NETF(groundEntityNum), GENTITYNUM_BITS, SM_BASE },// BASE
{ NETF(pos.trType), 8, SM_TRTYPE },					// !CPMA: SM_TRTYPE
{ NETF(eFlags), 19, SM_EFLAGS },					// EFLAGS
{ NETF(otherEntityNum), GENTITYNUM_BITS },
{ NETF(weapon), 8, SM_BASE },						// BASE
{ NETF(clientNum), 8, SM_BASE },					// BASE
{ NETF(angles[1]), 0 },
{ NETF(pos.trDuration), 32 },
{ NETF(apos.trType), 8, SM_BASE },					// BASE
{ NETF(origin[0]), 0 },
{ NETF(origin[1]), 0 },
{ NETF(origin[2]), 0 },
{ NETF(solid), 24 },
{ NETF(powerups), MAX_POWERUPS, SM_BASE },			// BASE
{ NETF(modelindex), 8 },
{ NETF(otherEntityNum2), GENTITYNUM_BITS },
{ NETF(loopSound), 8, SM_BASE },					// BASE
{ NETF(generic1), 8, SM_BASE },						// BASE
{ NETF(origin2[2]), 0 },
{ NETF(origin2[0]), 0 },
{ NETF(origin2[1]), 0 },
{ NETF(modelindex2), 8 },
{ NETF(angles[0]), 0 },
{ NETF(time), 32 },
{ NETF(apos.trTime), 32 },
{ NETF(apos.trDuration), 32 },
{ NETF(apos.trBase[2]), 0, SM_BASE },				// BASE
{ NETF(apos.trDelta[0]), 0, SM_BASE },				// BASE
{ NETF(apos.trDelta[1]), 0, SM_BASE },				// BASE
{ NETF(apos.trDelta[2]), 0, SM_BASE },				// BASE
{ NETF(time2), 32 },
{ NETF(angles[2]), 0 },
{ NETF(angles2[0]), 0 },
{ NETF(angles2[2]), 0 },
{ NETF(constantLight), 32 },
{ NETF(frame), 16 }
};

#ifdef USE_MV

#include "../game/bg_public.h"

int MSG_PlayerStateToEntityStateXMask( const playerState_t *ps, const entityState_t *s, qboolean snap ) {
	int		i;
	int		tmp;
	vec3_t	vec3;
	int		mask;

	mask = 0;

	// SM_TRTIME
	if ( s->pos.trTime != ps->commandTime ) // CPMA
		mask |= SM_TRTIME;

	if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPECTATOR ) {
		if ( s->eType != ET_INVISIBLE ) {
			//Com_DPrintf( S_COLOR_YELLOW "E#0.1\n" );
			mask |= SM_BASE;
		}
	} else if ( ps->stats[STAT_HEALTH] <= GIB_HEALTH ) {
		if ( s->eType != ET_INVISIBLE ) {
			//Com_DPrintf( S_COLOR_YELLOW "E#0.2\n" );
			mask |= SM_BASE;
		}
	} else {
		if ( s->eType != ET_PLAYER ) {
			//Com_DPrintf( S_COLOR_YELLOW "E#0.3\n" );
			mask |= SM_BASE;
		}
	}

	// !CPMA: SM_TRTYPE
	if ( s->pos.trType != TR_INTERPOLATE ) {
		mask |= SM_TRTYPE;
	}

	if ( s->apos.trType != TR_INTERPOLATE ) {
		//Com_DPrintf( S_COLOR_YELLOW "E#2\n" );
		mask |= SM_BASE;
	}

	VectorCopy( ps->origin, vec3 );
	if ( snap )
		SnapVector( vec3 );
	if ( !VectorCompare( vec3, s->pos.trBase ) ) {
		//Com_Printf( S_COLOR_YELLOW "E#3\n" );
		mask |= SM_BASE;
	}

	// set the trDelta for flag direction
	if ( !VectorCompare( ps->velocity, s->pos.trDelta ) ) {
		VectorCopy( ps->velocity, vec3 );
		SnapVector( vec3 );
		if ( !VectorCompare( vec3, s->pos.trDelta ) )
			mask |= SM_BASE;		// reject all
		else
			mask |= SM_TRDELTA; // CPMA
	}

	VectorCopy( ps->viewangles, vec3 );
	if ( snap )
		SnapVector( vec3 );
	if ( !VectorCompare( vec3, s->apos.trBase ) ) {
		//Com_DPrintf( S_COLOR_YELLOW "E#5\n" );
		mask |= SM_BASE;
	}

	if ( s->weapon != ps->weapon || s->groundEntityNum != ps->groundEntityNum ) {
		//Com_DPrintf( S_COLOR_YELLOW "E#6 s.w=%i ps.w=%i, s.en=%i ps.en=%i\n", s->weapon, ps->weapon, s->groundEntityNum, ps->groundEntityNum );
		mask |= SM_BASE;
	}

	if ( s->angles2[YAW] != ps->movementDir ||
		s->legsAnim != ps->legsAnim ||
		s->torsoAnim != ps->torsoAnim ||
		s->clientNum != ps->clientNum ) {
			Com_Printf( S_COLOR_YELLOW "E#7\n" );
			mask |= SM_BASE;
	}

	// EFLAGS
	tmp = ps->eFlags;
	if ( ps->stats[STAT_HEALTH] <= 0 ) {
		tmp |= EF_DEAD;
	} else {
		tmp &= ~EF_DEAD;
	}
	if ( s->eFlags != tmp ) {
		Com_Printf( S_COLOR_YELLOW "E#8: s->eFlags %i != %i health=%i\n", s->eFlags, tmp, ps->stats[ STAT_HEALTH ] );
		mask |= SM_EFLAGS;
	}

	if ( s->loopSound != ps->loopSound || s->generic1 != ps->generic1 ) {
		//Com_DPrintf( S_COLOR_YELLOW "E#9\n" );
		mask |= SM_BASE;
	}

	// POWERUPS
	tmp = 0; //s->powerups = 0;
	for ( i = 0 ; i < MAX_POWERUPS; i++ ) {
		if ( ps->powerups[ i ] ) {
		//	s->powerups |= 1 << i;
			tmp |= 1 << i;
		}
	}
	if ( s->powerups != tmp ) {
		mask |= SM_BASE;
	}

	return mask;
}


void MSG_PlayerStateToEntityState( playerState_t *ps, entityState_t *s, qboolean snap, skip_mask sm ) {
	int		i;

	if ( sm & SM_TRTIME ) 
		s->pos.trTime = ps->commandTime;

	if ( sm & SM_TRTYPE )
		s->pos.trType = TR_INTERPOLATE;

	//if ( sm & SM_TRDELTA )
	//	VectorCopy( ps->velocity, s->pos.trDelta );

	if ( sm & SM_BASE ) 
	{
		if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPECTATOR ) {
			s->eType = ET_INVISIBLE;
		} else if ( ps->stats[STAT_HEALTH] <= GIB_HEALTH ) {
			s->eType = ET_INVISIBLE;
		} else {
			s->eType = ET_PLAYER;
		}

		//s->pos.trType = TR_INTERPOLATE; // -> now set by SM_TRTYPE
		s->apos.trType = TR_INTERPOLATE;

		VectorCopy( ps->origin, s->pos.trBase );
		if ( snap )
			SnapVector( s->pos.trBase );

		// set the trDelta for flag direction
		VectorCopy( ps->velocity, s->pos.trDelta );

		if ( sm & SM_TRDELTA )
			SnapVector( s->pos.trDelta ); // CPMA

		VectorCopy( ps->viewangles, s->apos.trBase );
		if ( snap )
			SnapVector( s->apos.trBase );

		s->weapon = ps->weapon;
		s->groundEntityNum = ps->groundEntityNum; 

		s->angles2[YAW] = ps->movementDir;
		s->legsAnim = ps->legsAnim;
		s->torsoAnim = ps->torsoAnim;
		s->clientNum = ps->clientNum;

		//s->eFlags = ps->eFlags; // -> SM_EFLAGS
		s->loopSound = ps->loopSound;
		s->generic1 = ps->generic1;

		s->powerups = 0;
		for ( i = 0 ; i < MAX_POWERUPS; i++ ) {
			if ( ps->powerups[ i ] ) {
				s->powerups |= 1 << i;
			}
		}
	}

	if ( sm & SM_EFLAGS ) {
		s->eFlags = ps->eFlags;
	}
#if 0
	// ET_PLAYER looks here instead of at number
	// so corpses can also reference the proper config
	//s->eFlags = ps->eFlags;
	tmp = ps->eFlags;
	if ( ps->stats[STAT_HEALTH] <= 0 ) {
		//s->eFlags |= EF_DEAD;
		tmp |= EF_DEAD;
	} else {
		//s->eFlags &= ~EF_DEAD;
		tmp &= ~EF_DEAD;;
	}
	if ( s->eFlags != tmp )
		return SM_3;

	// moved up!
	//if ( s->weapon != ps->weapon || s->groundEntityNum != ps->groundEntityNum )
	//	return SM_4;

	tmp = 0; //s->powerups = 0;
	for ( i = 0 ; i < MAX_POWERUPS; i++ ) {
		if ( ps->powerups[ i ] ) {
		//	s->powerups |= 1 << i;
			tmp |= 1 << i;
		}
	}
	if ( s->powerups != tmp )
		return SM_4;

	if ( s->loopSound != ps->loopSound || s->generic1 != ps->generic1 )
		return SM_4;
#endif
}


#endif


// if (int)f == f and (int)f + ( 1<<(FLOAT_INT_BITS-1) ) < ( 1 << FLOAT_INT_BITS )
// the float will be sent with FLOAT_INT_BITS, otherwise all 32 bits will be sent
#define	FLOAT_INT_BITS	13
#define	FLOAT_INT_BIAS	(1<<(FLOAT_INT_BITS-1))

/*
==================
MSG_WriteDeltaEntity

Writes part of a packetentities message, including the entity number.
Can delta from either a baseline or a previous packet_entity
If to is NULL, a remove entity update will be sent
If force is not set, then nothing at all will be generated if the entity is
identical, under the assumption that the in-order delta code will catch it.
==================
*/
void MSG_WriteDeltaEntity( msg_t *msg, const entityState_t *from, const entityState_t *to, qboolean force ) {
	int			i, lc;
	int			numFields;
	const netField_t *field;
	int			trunc;
	float		fullFloat;
	const int	*fromF, *toF;

	numFields = ARRAY_LEN( entityStateFields );

	// all fields should be 32 bits to avoid any compiler packing issues
	// the "number" field is not part of the field list
	// if this assert fails, someone added a field to the entityState_t
	// struct without updating the message fields
	assert( numFields + 1 == sizeof( *from )/4 );

	// a NULL to is a delta remove message
	if ( to == NULL ) {
		if ( from == NULL ) {
			return;
		}
		MSG_WriteBits( msg, from->number, GENTITYNUM_BITS );
		MSG_WriteBits( msg, 1, 1 );
		return;
	}

	if ( to->number < 0 || to->number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "MSG_WriteDeltaEntity: Bad entity number: %i", to->number );
	}

	lc = 0;
	// build the change vector as bytes so it is endian independent
	for ( i = 0, field = entityStateFields ; i < numFields ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );
#ifdef USE_MV
		if ( ( field->mergeMask & MSG_entMergeMask ) && to->number < MAX_CLIENTS )
			continue;
#endif
		if ( *fromF != *toF ) {
			lc = i+1;
		}
	}

	if ( lc == 0 ) {
		// nothing at all changed
		if ( !force ) {
			return;		// nothing at all
		}
		// write two bits for no change
		MSG_WriteBits( msg, to->number, GENTITYNUM_BITS );
		MSG_WriteBits( msg, 0, 1 );		// not removed
		MSG_WriteBits( msg, 0, 1 );		// no delta
		return;
	}

	MSG_WriteBits( msg, to->number, GENTITYNUM_BITS );
	MSG_WriteBits( msg, 0, 1 );			// not removed
	MSG_WriteBits( msg, 1, 1 );			// we have a delta

	MSG_WriteByte( msg, lc );	// # of changes

	for ( i = 0, field = entityStateFields ; i < lc ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );
#ifdef USE_MV
		if ( *fromF == *toF || ( ( field->mergeMask & MSG_entMergeMask ) && (to->number < MAX_CLIENTS) ) ) {
			MSG_WriteBits( msg, 0, 1 );	// no change
			continue;
		}
#else
		if ( *fromF == *toF ) {
			MSG_WriteBits( msg, 0, 1 );	// no change
			continue;
		}
#endif
		MSG_WriteBits( msg, 1, 1 );	// changed

		if ( field->bits == 0 ) {
			// float
			fullFloat = *(const float *)toF;
			trunc = (int)fullFloat;

			if (fullFloat == 0.0f) {
				MSG_WriteBits( msg, 0, 1 );
			} else {
				MSG_WriteBits( msg, 1, 1 );
				if ( trunc == fullFloat && trunc + FLOAT_INT_BIAS >= 0 && 
					trunc + FLOAT_INT_BIAS < ( 1 << FLOAT_INT_BITS ) ) {
					// send as small integer
					MSG_WriteBits( msg, 0, 1 );
					MSG_WriteBits( msg, trunc + FLOAT_INT_BIAS, FLOAT_INT_BITS );
				} else {
					// send as full floating point value
					MSG_WriteBits( msg, 1, 1 );
					MSG_WriteBits( msg, *toF, 32 );
				}
			}
		} else {
			if (*toF == 0) {
				MSG_WriteBits( msg, 0, 1 );
			} else {
				MSG_WriteBits( msg, 1, 1 );
				// integer
				MSG_WriteBits( msg, *toF, field->bits );
			}
		}
	}
}

/*
==================
MSG_ReadDeltaEntity

The entity number has already been read from the message, which
is how the from state is identified.

If the delta removes the entity, entityState_t->number will be set to MAX_GENTITIES-1

Can go from either a baseline or a previous packet_entity
==================
*/
void MSG_ReadDeltaEntity( msg_t *msg, const entityState_t *from, entityState_t *to, int number ) {
	int			i, lc;
	int			numFields;
	const netField_t *field;
	const int	*fromF;
	int			*toF;
	int			print;
	int			trunc;
	int			startBit, endBit;

	if ( number < 0 || number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "Bad delta entity number: %i", number );
	}

	if ( msg->bit == 0 ) {
		startBit = msg->readcount * 8 - GENTITYNUM_BITS;
	} else {
		startBit = ( msg->readcount - 1 ) * 8 + msg->bit - GENTITYNUM_BITS;
	}

	// check for a remove
	if ( MSG_ReadBits( msg, 1 ) == 1 ) {
		Com_Memset( to, 0, sizeof( *to ) );	
		to->number = MAX_GENTITIES - 1;
#ifndef DEDICATED
		if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -1 ) ) {
			Com_Printf( "%3i: #%-3i remove\n", msg->readcount, number );
		}
#endif
		return;
	}

	// check for no delta
	if ( MSG_ReadBits( msg, 1 ) == 0 ) {
		*to = *from;
		to->number = number;
		return;
	}

	numFields = ARRAY_LEN( entityStateFields );
	lc = MSG_ReadByte(msg);

	if ( lc > numFields || lc < 0 ) {
		Com_Error( ERR_DROP, "invalid entityState field count" );
	}

	to->number = number;

#ifndef DEDICATED
	// shownet 2/3 will interleave with other printed info, -1 will
	// just print the delta records
	if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -1 ) ) {
		print = 1;
		Com_Printf( "%3i: #%-3i ", msg->readcount, to->number );
	} else {
		print = 0;
	}
#else
		print = 0;
#endif

	for ( i = 0, field = entityStateFields ; i < lc ; i++, field++ ) {
		fromF = (const int *)( (const byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );

		if ( ! MSG_ReadBits( msg, 1 ) ) {
			// no change
			*toF = *fromF;
		} else {
			if ( field->bits == 0 ) {
				// float
				if ( MSG_ReadBits( msg, 1 ) == 0 ) {
						*(float *)toF = 0.0f; 
				} else {
					if ( MSG_ReadBits( msg, 1 ) == 0 ) {
						// integral float
						trunc = MSG_ReadBits( msg, FLOAT_INT_BITS );
						// bias to allow equal parts positive and negative
						trunc -= FLOAT_INT_BIAS;
						*(float *)toF = trunc; 
						if ( print ) {
							Com_Printf( "%s:%i ", field->name, trunc );
						}
					} else {
						// full floating point value
						*toF = MSG_ReadBits( msg, 32 );
						if ( print ) {
							Com_Printf( "%s:%f ", field->name, *(float *)toF );
						}
					}
				}
			} else {
				if ( MSG_ReadBits( msg, 1 ) == 0 ) {
					*toF = 0;
				} else {
					// integer
					*toF = MSG_ReadBits( msg, field->bits );
					if ( print ) {
						Com_Printf( "%s:%i ", field->name, *toF );
					}
				}
			}
//			pcount[i]++;
		}
	}
	for ( i = lc, field = &entityStateFields[lc] ; i < numFields ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );
		// no change
		*toF = *fromF;
	}

	if ( print ) {
		if ( msg->bit == 0 ) {
			endBit = msg->readcount * 8 - GENTITYNUM_BITS;
		} else {
			endBit = ( msg->readcount - 1 ) * 8 + msg->bit - GENTITYNUM_BITS;
		}
		Com_Printf( " (%i bits)\n", endBit - startBit  );
	}
}


/*
============================================================================

plyer_state_t communication

============================================================================
*/

// using the stringizing operator to save typing...
#define	PSF(x) #x,(size_t)&((playerState_t*)0)->x

static const netField_t playerStateFields[] = 
{
{ PSF(commandTime), 32 },
{ PSF(origin[0]), 0 },
{ PSF(origin[1]), 0 },
{ PSF(bobCycle), 8 },
{ PSF(velocity[0]), 0 },
{ PSF(velocity[1]), 0 },
{ PSF(viewangles[1]), 0 },
{ PSF(viewangles[0]), 0 },
{ PSF(weaponTime), -16 },
{ PSF(origin[2]), 0 },
{ PSF(velocity[2]), 0 },
{ PSF(legsTimer), 8 },
{ PSF(pm_time), -16 },
{ PSF(eventSequence), 16 },
{ PSF(torsoAnim), 8 },
{ PSF(movementDir), 4 },
{ PSF(events[0]), 8 },
{ PSF(legsAnim), 8 },
{ PSF(events[1]), 8 },
{ PSF(pm_flags), 16 },
{ PSF(groundEntityNum), GENTITYNUM_BITS },
{ PSF(weaponstate), 4 },
{ PSF(eFlags), 16 },
{ PSF(externalEvent), 10 },
{ PSF(gravity), 16 },
{ PSF(speed), 16 },
{ PSF(delta_angles[1]), 16 },
{ PSF(externalEventParm), 8 },
{ PSF(viewheight), -8 },
{ PSF(damageEvent), 8 },
{ PSF(damageYaw), 8 },
{ PSF(damagePitch), 8 },
{ PSF(damageCount), 8 },
{ PSF(generic1), 8 },
{ PSF(pm_type), 8 },					
{ PSF(delta_angles[0]), 16 },
{ PSF(delta_angles[2]), 16 },
{ PSF(torsoTimer), 12 },
{ PSF(eventParms[0]), 8 },
{ PSF(eventParms[1]), 8 },
{ PSF(clientNum), 8 },
{ PSF(weapon), 5 },
{ PSF(viewangles[2]), 0 },
{ PSF(grapplePoint[0]), 0 },
{ PSF(grapplePoint[1]), 0 },
{ PSF(grapplePoint[2]), 0 },
{ PSF(jumppad_ent), GENTITYNUM_BITS },
{ PSF(loopSound), 16 }
};

/*
=============
MSG_WriteDeltaPlayerstate

=============
*/
void MSG_WriteDeltaPlayerstate( msg_t *msg, const playerState_t *from, const playerState_t *to ) {
	static const playerState_t dummy = { 0 };
	int				i;
	int				statsbits;
	int				persistantbits;
	int				ammobits;
	int				powerupbits;
	int				numFields;
	const netField_t *field;
	const int		*fromF, *toF;
	float			fullFloat;
	int				trunc, lc;

	if ( !from ) {
		from = &dummy;
	}

	numFields = ARRAY_LEN( playerStateFields );

	lc = 0;
	for ( i = 0, field = playerStateFields ; i < numFields ; i++, field++ ) {
		fromF = (const int *)( (byte *)from + field->offset );
		toF = (const int *)( (byte *)to + field->offset );
		if ( *fromF != *toF ) {
			lc = i+1;
		}
	}

	MSG_WriteByte( msg, lc );	// # of changes

	for ( i = 0, field = playerStateFields ; i < lc ; i++, field++ ) {
		fromF = (const int *)( (byte *)from + field->offset );
		toF = (const int *)( (byte *)to + field->offset );

		if ( *fromF == *toF ) {
			MSG_WriteBits( msg, 0, 1 );	// no change
			continue;
		}

		MSG_WriteBits( msg, 1, 1 );	// changed
//		pcount[i]++;

		if ( field->bits == 0 ) {
			// float
			fullFloat = *(const float *)toF;
			trunc = (int)fullFloat;

			if ( trunc == fullFloat && trunc + FLOAT_INT_BIAS >= 0 && 
				trunc + FLOAT_INT_BIAS < ( 1 << FLOAT_INT_BITS ) ) {
				// send as small integer
				MSG_WriteBits( msg, 0, 1 );
				MSG_WriteBits( msg, trunc + FLOAT_INT_BIAS, FLOAT_INT_BITS );
			} else {
				// send as full floating point value
				MSG_WriteBits( msg, 1, 1 );
				MSG_WriteBits( msg, *toF, 32 );
			}
		} else {
			// integer
			MSG_WriteBits( msg, *toF, field->bits );
		}
	}


	//
	// send the arrays
	//
	statsbits = 0;
	for (i=0 ; i<MAX_STATS ; i++) {
		if (to->stats[i] != from->stats[i]) {
			statsbits |= 1<<i;
		}
	}
	persistantbits = 0;
	for (i=0 ; i<MAX_PERSISTANT ; i++) {
		if (to->persistant[i] != from->persistant[i]) {
			persistantbits |= 1<<i;
		}
	}
	ammobits = 0;
	for (i=0 ; i<MAX_WEAPONS ; i++) {
		if (to->ammo[i] != from->ammo[i]) {
			ammobits |= 1<<i;
		}
	}
	powerupbits = 0;
	for (i=0 ; i<MAX_POWERUPS ; i++) {
		if (to->powerups[i] != from->powerups[i]) {
			powerupbits |= 1<<i;
		}
	}

	if (!statsbits && !persistantbits && !ammobits && !powerupbits) {
		MSG_WriteBits( msg, 0, 1 );	// no change
		return;
	}
	MSG_WriteBits( msg, 1, 1 );	// changed

	if ( statsbits ) {
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteBits( msg, statsbits, MAX_STATS );
		for (i=0 ; i<MAX_STATS ; i++)
			if (statsbits & (1<<i) )
				MSG_WriteShort (msg, to->stats[i]);
	} else {
		MSG_WriteBits( msg, 0, 1 );	// no change
	}


	if ( persistantbits ) {
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteBits( msg, persistantbits, MAX_PERSISTANT );
		for (i=0 ; i<MAX_PERSISTANT ; i++)
			if (persistantbits & (1<<i) )
				MSG_WriteShort (msg, to->persistant[i]);
	} else {
		MSG_WriteBits( msg, 0, 1 );	// no change
	}


	if ( ammobits ) {
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteBits( msg, ammobits, MAX_WEAPONS );
		for (i=0 ; i<MAX_WEAPONS ; i++)
			if (ammobits & (1<<i) )
				MSG_WriteShort (msg, to->ammo[i]);
	} else {
		MSG_WriteBits( msg, 0, 1 );	// no change
	}


	if ( powerupbits ) {
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteBits( msg, powerupbits, MAX_POWERUPS );
		for (i=0 ; i<MAX_POWERUPS ; i++)
			if (powerupbits & (1<<i) )
				MSG_WriteLong( msg, to->powerups[i] );
	} else {
		MSG_WriteBits( msg, 0, 1 );	// no change
	}
}


/*
===================
MSG_ReadDeltaPlayerstate
===================
*/
void MSG_ReadDeltaPlayerstate( msg_t *msg, const playerState_t *from, playerState_t *to ) {
	int			i, lc;
	int			bits;
	const netField_t *field;
	int			numFields;
	int			startBit, endBit;
	int			print;
	const int	*fromF;
	int			*toF;
	int			trunc;
	playerState_t	dummy;

	if ( !from ) {
		from = &dummy;
		Com_Memset( &dummy, 0, sizeof( dummy ) );
	}
	*to = *from;

	if ( msg->bit == 0 ) {
		startBit = msg->readcount * 8 - GENTITYNUM_BITS;
	} else {
		startBit = ( msg->readcount - 1 ) * 8 + msg->bit - GENTITYNUM_BITS;
	}

#ifndef DEDICATED	
	// shownet 2/3 will interleave with other printed info, -2 will
	// just print the delta records
	if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -2 ) ) {
		print = 1;
		Com_Printf( "%3i: playerstate ", msg->readcount );
	} else {
		print = 0;
	}
#else
		print = 0;
#endif

	numFields = ARRAY_LEN( playerStateFields );
	lc = MSG_ReadByte(msg);

	if ( lc > numFields || lc < 0 ) {
		Com_Error( ERR_DROP, "invalid playerState field count" );
	}

	for ( i = 0, field = playerStateFields ; i < lc ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );

		if ( ! MSG_ReadBits( msg, 1 ) ) {
			// no change
			*toF = *fromF;
		} else {
			if ( field->bits == 0 ) {
				// float
				if ( MSG_ReadBits( msg, 1 ) == 0 ) {
					// integral float
					trunc = MSG_ReadBits( msg, FLOAT_INT_BITS );
					// bias to allow equal parts positive and negative
					trunc -= FLOAT_INT_BIAS;
					*(float *)toF = trunc; 
					if ( print ) {
						Com_Printf( "%s:%i ", field->name, trunc );
					}
				} else {
					// full floating point value
					*toF = MSG_ReadBits( msg, 32 );
					if ( print ) {
						Com_Printf( "%s:%f ", field->name, *(float *)toF );
					}
				}
			} else {
				// integer
				*toF = MSG_ReadBits( msg, field->bits );
				if ( print ) {
					Com_Printf( "%s:%i ", field->name, *toF );
				}
			}
		}
	}
	for ( i=lc,field = &playerStateFields[lc];i<numFields; i++, field++) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );
		// no change
		*toF = *fromF;
	}


	// read the arrays
	if (MSG_ReadBits( msg, 1 ) ) {
		// parse stats
		if ( MSG_ReadBits( msg, 1 ) ) {
			LOG("PS_STATS");
			bits = MSG_ReadBits (msg, MAX_STATS);
			for (i=0 ; i<MAX_STATS ; i++) {
				if (bits & (1<<i) ) {
					to->stats[i] = MSG_ReadShort(msg);
				}
			}
		}

		// parse persistant stats
		if ( MSG_ReadBits( msg, 1 ) ) {
			LOG("PS_PERSISTANT");
			bits = MSG_ReadBits (msg, MAX_PERSISTANT);
			for (i=0 ; i<MAX_PERSISTANT ; i++) {
				if (bits & (1<<i) ) {
					to->persistant[i] = MSG_ReadShort(msg);
				}
			}
		}

		// parse ammo
		if ( MSG_ReadBits( msg, 1 ) ) {
			LOG("PS_AMMO");
			bits = MSG_ReadBits (msg, MAX_WEAPONS);
			for (i=0 ; i<MAX_WEAPONS ; i++) {
				if (bits & (1<<i) ) {
					to->ammo[i] = MSG_ReadShort(msg);
				}
			}
		}

		// parse powerups
		if ( MSG_ReadBits( msg, 1 ) ) {
			LOG("PS_POWERUPS");
			bits = MSG_ReadBits (msg, MAX_POWERUPS);
			for (i=0 ; i<MAX_POWERUPS ; i++) {
				if (bits & (1<<i) ) {
					to->powerups[i] = MSG_ReadLong(msg);
				}
			}
		}
	}

	if ( print ) {
		if ( msg->bit == 0 ) {
			endBit = msg->readcount * 8 - GENTITYNUM_BITS;
		} else {
			endBit = ( msg->readcount - 1 ) * 8 + msg->bit - GENTITYNUM_BITS;
		}
		Com_Printf( " (%i bits)\n", endBit - startBit  );
	}
}


#if defined( USE_MV ) && defined( USE_MV_ZCMD )

// command compression/decompression

#define LZ_MOD(a)  ( (a) & (LZ_WINDOW_SIZE - 1) )
#define DEF_POS 0
#define NUM_PASSES LZ_WINDOW_SIZE
#define HASH_BLK LZ_MIN_MATCH

static unsigned int hash_func( const byte *window, int pos )
{
	unsigned int h, i;
	for ( i = 0, h = 0; i < HASH_BLK; i++ ) 
		h = h * 101 + window[ pos + i ]; // MODULO( pos + i )
	return h & (HTAB_SIZE-1);
}


static void hash_update( lzctx_t *ctx, int pos )
{
	int hash;

	hash = hash_func( ctx->window, pos );

	ctx->hvals[ pos ] = hash;

	if ( ctx->htable[ hash ] < 0 )
		ctx->htable[ hash ] = pos;
	else 
		ctx->hlist[ ctx->htlast[ hash ] ] = pos;

	 // save last inserted
	ctx->htlast[ hash ] = pos;

	 // zero last
	ctx->hlist[ pos ] = -1;
}


static void hash_delete( lzctx_t *ctx, int pos )
{
	int hash;
	//pos = MODULO( pos ); // no need?

	if ( ( hash = ctx->hvals[ pos ] ) < 0 ) // nothing inserted at this position?
		return;

	if ( ctx->htable[ hash ] == pos )
	{
		ctx->htable[ hash ] = ctx->hlist[ pos ]; // point to next bucket or -1
		ctx->hlist[ pos ] = -1; // now unused
      	ctx->hvals[ pos ] = -1; // now unused
	}
/*
	else 
	{
		printf( "ERROR4 - must never happen!\n" );
		fflush( NULL );
		exit( 1 );
	}
*/
}

#define HASH_SEARCH_OPTIMIZE

static int hash_search( const lzctx_t *ctx, int current_pos, int look_ahead, int *match_pos, int steps )
{
	int hash;
	int start;
	int n, match_len, last_match, s, v;
	const byte *wcp;
	const byte *window;

	//if ( look_ahead < HASH_BLK ) 
	//	return 1;

	window = ctx->window;

	wcp = ctx->window + current_pos; // small optimization

	hash = hash_func( ctx->window, current_pos );

	start = ctx->htable[ hash ];

	match_len = HASH_BLK-1; // 2
	last_match = 0;

	s = HASH_BLK-1;

#ifdef HASH_SEARCH_OPTIMIZE
	v = wcp[ s ]; //lz_window[ current_pos + s ];
#else
	v = window[ LZ_MOD( current_pos + s) ];
#endif

	while ( start >= 0 ) // chain >= 0
	{
		// first valid match of last symbol
#ifdef HASH_SEARCH_OPTIMIZE
		if ( window[ start + s ] == v ) // lazy match of last symbol
#else
		if ( window[ LZ_MOD(start + s) ] == v ) // lazy match of last symbol
#endif
		{
			for ( n = 0; n < look_ahead; n++ ) 
			{
#ifdef HASH_SEARCH_OPTIMIZE
				if ( window[ start + n ] != wcp[ n ] ) // != lz_window[ LZ_MOD( current_pos + n ) ]
#else
				if ( window[ LZ_MOD(start + n) ] != window[ LZ_MOD( current_pos + n ) ] )
#endif
					break;
			}
			if ( n > match_len ) 
			{
				match_len = n;
				last_match = start;
				if ( n >= look_ahead )
					break;
				// save last match
				s = n;
#ifdef HASH_SEARCH_OPTIMIZE
				v = wcp[ n ]; // window[ LZ_MOD(current_pos + s) ];
				if ( ctx->htable[ hash_func( window, current_pos + n - (HASH_BLK-1) ) ] < 0 ) // quick reject
#else
				v = window[ LZ_MOD(current_pos + s) ];
				if ( ctx->htable[ hash_func( window, LZ_MOD(current_pos + n - (HASH_BLK-1)) ) ] < 0 ) // quick reject
#endif
					break;

			}
		}
		start = ctx->hlist[ start ]; // chain = chain->next; // switch to next item
	}

	*match_pos = last_match;

	return match_len;
}


// clear dictionary and hash search structures
void LZSS_InitContext( lzctx_t *ctx )
{
	int i;

	for ( i = 0; i < DICT_SIZE; i++ )
	{
		ctx->hlist[ i ] = -1;
		ctx->hvals[ i ] = -1;
	}

	for ( i = 0; i < HTAB_SIZE; i++ )
	{
		ctx->htable[ i ] = -1;
		ctx->htlast[ i ] = -1;
	}
	ctx->current_pos = DEF_POS;

	memset( ctx->window, '\0', sizeof( ctx->window ) );
}


void LZSS_SeekEOS( msg_t *msg, int charbits ) {
	int c;
	for ( ;; ) {
		if ( MSG_ReadBits( msg, 1 ) ) {
			c = MSG_ReadBits( msg, charbits );
			if ( c == '\0' ) // FIXME: <= 0 ?
				break;
		} else {
			MSG_ReadBits( msg, INDEX_BITS );
			MSG_ReadBits( msg, LENGTH_BITS );
		}
	}
}


int LZSS_Expand( lzctx_t *ctx, msg_t *msg, byte *out, int maxsize, int charbits )
{
	int i;
	int c;
	int current_pos;
	int match_len;
	int match_pos;
	byte *window;
	const byte *base;
	const byte *max;

	window = ctx->window;
	current_pos = ctx->current_pos; // DEF_POS

	base = out;
	max = out + maxsize - 1;
		
	for ( ;; ) {
		if ( MSG_ReadBits( msg, 1 ) ) { // literal
			c = MSG_ReadBits( msg, charbits );
			if ( c == '\0' ) // c <= 0 ?
				break;
			window[ current_pos ] = (byte) c;
			current_pos = LZ_MOD( current_pos + 1 );
			if ( out < max )
				*out++ = c;
		} else { // match pair
			match_pos = MSG_ReadBits( msg, INDEX_BITS );
			match_len = MSG_ReadBits( msg, LENGTH_BITS );
			match_pos = LZ_MOD( current_pos - match_pos );
			for ( i = 0; i < match_len + LZ_MIN_MATCH; i++ ) {
				c = window[ LZ_MOD( match_pos + i ) ];
				window[ current_pos ] = (byte) c;
				current_pos = LZ_MOD( current_pos + 1 );
				if ( out < max )
					*out++ = c;
			}
		}
	}

	*out = '\0'; // terminate string

	ctx->current_pos = current_pos;

	return (out - base);
}


int LZSS_CompressToStream( lzctx_t *ctx, lzstream_t *stream, const byte *in, int length )
{
	int i, j, c;
	int look_ahead_bytes;
	int current_pos;
	int replace_count;
	int match_len;
	int match_pos;
	const byte *eos;
	int	count;
	byte *window;
	byte *output;

	current_pos = ctx->current_pos; // DEF_POS
	window = ctx->window;

	eos = in + length;

	for ( i = 0; i < LOOK_AHEAD_SIZE; i++ ) // i < 18
	{
		if ( in >= eos ) //if ( (c = getc (input)) == EOF )
			break;
		c = *in++;
		j = LZ_MOD( current_pos + i );
		window[ j ] = c;
		// string search optimization
#ifdef SEARCH_OPTIMIZE 
		if ( current_pos + i >= LZ_WINDOW_SIZE ) 
			window[ current_pos + i ] = c;
#endif
		// remove inserted characters from lookup
		hash_delete( ctx, j );
	}

	look_ahead_bytes = i;

#if 1
	Com_Memset( stream->type, 0, ((length + 7)/8) + 1 );
#else
	Com_Memset( stream->type, 0, sizeof( stream->type ) );
	Com_Memset( stream->cmd, 0, sizeof( stream->cmd ) );
#endif

	output = stream->cmd;
	count = 0;

	if ( stream->zdelta == 0 ) {
		// initial state
		match_len = 1;
		match_pos = current_pos;
	} else {
		// dictionary is not empty so we can search for matches
		match_len = hash_search( ctx, current_pos, look_ahead_bytes, &match_pos, NUM_PASSES );
	}

	while ( look_ahead_bytes > 0 )
	{
		if ( match_len < LZ_MIN_MATCH )
		{
			replace_count = 1;
			//stream->type[ count / 8 ] |=  1 << ( count & 7 );
			SET_ABIT( stream->type, count );
			*output++ = window[ current_pos ];
		}
		else
		{
			i = LZ_MOD( current_pos - match_pos );
			j = match_len - LZ_MIN_MATCH;
			//stream->type[ count / 8 ] |= 0 << ( count & 7 );
			*output++ = i;
			*output++ = ( ( i >> (8 - LENGTH_BITS)) & LENGTH_MASK1 ) | j;
			replace_count = match_len;
		}

		count++;

		for ( i = 0; i < replace_count; i++ )
		{
			hash_delete( ctx, LZ_MOD( current_pos + LOOK_AHEAD_SIZE ) );
			if ( in >= eos ) 	// if ( (c = getc (input)) == EOF )
			{
				look_ahead_bytes--;
			}
			else  
			{
				c = *in++;
				window[ LZ_MOD( current_pos + LOOK_AHEAD_SIZE ) ] = c;
				// string search optimization
#ifdef SEARCH_OPTIMIZE
				if ( current_pos + LOOK_AHEAD_SIZE >= LZ_WINDOW_SIZE ) 
					window[ current_pos + LOOK_AHEAD_SIZE ] = c;
#endif
			}

			if ( look_ahead_bytes >= HASH_BLK ) // > 0 
				hash_update( ctx, current_pos );

			current_pos = LZ_MOD( current_pos + 1 );
		}

		match_len = hash_search( ctx, current_pos, look_ahead_bytes, &match_pos, NUM_PASSES );
	}

	SET_ABIT( stream->type, count );
	*output++ = '\0';
	count++;

	ctx->current_pos = current_pos;
	stream->count = count;

	//Com_Printf( "zcmd: [%3i.%i] compressed %i -> %i bits\n", 
	//	stream->zcommandNum, stream->zdelta, length*8, (output - stream->cmd)*8 + count );

	return count;
}


void MSG_WriteLZStream( msg_t *msg, lzstream_t *stream ) 
{
	int pos;
	int len;
	int i;
	byte *cmd;

	MSG_WriteByte( msg, svc_zcmd );
	MSG_WriteBits( msg, stream->zdelta, 3 );
	MSG_WriteBits( msg, stream->zcharbits - 7, 1 ); // 7..8 -> 0..1
	MSG_WriteBits( msg, stream->zcommandSize - 1, 2 );
	MSG_WriteBits( msg, stream->zcommandNum, stream->zcommandSize * 8 );
	MSG_WriteBits( msg, 0, 1 ); // future extension, reserved

	//Com_DPrintf( "\n >>> delta: %i, charbits: %i, size: %i, seq <<< \n", 
	//	stream->zdelta, stream->zcharbits, stream->zcommandSize, stream->zcommandNum );

	cmd = stream->cmd;
	for ( i = 0; i < stream->count; i++ ) {
		if ( GET_ABIT( stream->type, i ) ) {
			// literal
			MSG_WriteBits( msg, 1, 1 );
			MSG_WriteBits( msg, *cmd++, stream->zcharbits );
		} else {
			// match pair
			pos = *cmd++;
			len = *cmd++;
			pos |= ((len & LENGTH_MASK1) << (8 - LENGTH_BITS));
			len &= LENGTH_MASK;
			MSG_WriteBits( msg, 0, 1 );
			MSG_WriteBits( msg, pos, INDEX_BITS );
			MSG_WriteBits( msg, len, LENGTH_BITS );
		}
	}
}
#endif // USE_MV
