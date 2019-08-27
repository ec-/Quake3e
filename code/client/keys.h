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
#include "keycodes.h"

typedef struct {
	qboolean	down;
	int			repeats;		// if > 1, it is autorepeating
	char		*binding;
} qkey_t;

extern	qboolean	key_overstrikeMode;
extern	qkey_t		keys[MAX_KEYS];

extern  int         anykeydown;

// NOTE TTimo the declaration of field_t and Field_Clear is now in qcommon/qcommon.h

void Key_WriteBindings( fileHandle_t f );
void Key_SetBinding( int keynum, const char *binding );
const char *Key_GetBinding( int keynum );
void Key_ParseBinding( int key, qboolean down, unsigned time );

int Key_GetKey( const char *binding );
const char *Key_KeynumToString( int keynum );
int Key_StringToKeynum( const char *str );

qboolean Key_IsDown( int keynum );
void Key_ClearStates( void );

qboolean Key_GetOverstrikeMode( void );
void Key_SetOverstrikeMode( qboolean state );

void Com_InitKeyCommands( void );
