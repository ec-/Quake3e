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
// sv_mvconvert.c -- convert multiview demos (dm_71) to standard per-player demos (dm_68)

#include "server.h"

#ifdef USE_MV

#define MVC_CS_PLAYERS	544 // CS_PLAYERS from bg_public.h, used for output file names only

typedef struct {
	int				areabytes;
	byte			areamask[MAX_MAP_AREA_BYTES];
	byte			entMask[MAX_GENTITIES/8];
	playerState_t	ps;
	qboolean		valid;
} mvcPlayer_t;

typedef struct {
	qboolean		valid;
	int				messageNum;
	int				serverTime;
	int				snapFlags;
	int				version;
	int				mergeMask;
	byte			clientMask[MAX_CLIENTS/8];
	mvcPlayer_t		clps[MAX_CLIENTS];
	int				numEntities;
	entityState_t	entities[MAX_GENTITIES];
} mvcSnapshot_t;

typedef struct {
	fileHandle_t	file;
	char			name[MAX_QPATH];
	int				messageNum;		// sequence of the next written message
	int				commandNum;		// last written server command
	qboolean		deltaValid;		// previous input snapshot was written to this output
	int				numSnapshots;
	int				firstServerTime;
	int				lastServerTime;
} mvcOutput_t;

typedef struct {
	fileHandle_t	file;
	char			baseName[MAX_QPATH];
	int				onlyClient;		// -1 = convert all clients

	int				messageNum;		// sequence of the current input message
	int				reliableAcknowledge;
	int				clientNum;
	int				checksumFeed;
	qboolean		haveGamestate;

	char			*configstrings[MAX_CONFIGSTRINGS];
	char			bigConfigString[BIG_INFO_STRING];
	entityState_t	baselines[MAX_GENTITIES];
	byte			baselineUsed[MAX_GENTITIES];

	int				commandSequence;
	char			commands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];

	mvcSnapshot_t	snaps[2];
	int				current;		// index of the last parsed snapshot in snaps[]

	mvcOutput_t		out[MAX_CLIENTS];
} mvConvert_t;

static mvConvert_t *mvc_active; // to release resources after Com_Error()


/*
==================
MVC_Free
==================
*/
static void MVC_Free( mvConvert_t *mvc ) {
	int i;

	if ( mvc->file != FS_INVALID_HANDLE ) {
		FS_FCloseFile( mvc->file );
	}

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( mvc->out[i].file != FS_INVALID_HANDLE ) {
			FS_FCloseFile( mvc->out[i].file );
		}
	}

	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( mvc->configstrings[i] ) {
			Z_Free( mvc->configstrings[i] );
		}
	}

	Z_Free( mvc );

	if ( mvc_active == mvc ) {
		mvc_active = NULL;
	}
}


/*
==================
MVC_SetConfigstring
==================
*/
static void MVC_SetConfigstring( mvConvert_t *mvc, int index, const char *s ) {
	if ( mvc->configstrings[index] ) {
		Z_Free( mvc->configstrings[index] );
		mvc->configstrings[index] = NULL;
	}
	if ( s[0] ) {
		mvc->configstrings[index] = CopyString( s );
	}
}


/*
==================
MVC_ExecuteCommand

Track configstring changes so outputs opened later get an up-to-date gamestate
==================
*/
static void MVC_ExecuteCommand( mvConvert_t *mvc, const char *s ) {
	const char *cmd;
	int index;

	Cmd_TokenizeString( s );
	cmd = Cmd_Argv( 0 );

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( mvc->bigConfigString, BIG_INFO_STRING, "cs %s \"%s", Cmd_Argv( 1 ), Cmd_Argv( 2 ) );
		return;
	}

	if ( !strcmp( cmd, "bcs1" ) || !strcmp( cmd, "bcs2" ) ) {
		if ( strlen( mvc->bigConfigString ) + strlen( Cmd_Argv( 2 ) ) + 1 >= BIG_INFO_STRING ) {
			return;
		}
		Q_strcat( mvc->bigConfigString, BIG_INFO_STRING, Cmd_Argv( 2 ) );
		if ( cmd[3] == '1' ) {
			return;
		}
		Q_strcat( mvc->bigConfigString, BIG_INFO_STRING, "\"" );
		Cmd_TokenizeString( mvc->bigConfigString );
		cmd = Cmd_Argv( 0 );
	}

	if ( !strcmp( cmd, "cs" ) ) {
		index = atoi( Cmd_Argv( 1 ) );
		if ( (unsigned) index < MAX_CONFIGSTRINGS ) {
			MVC_SetConfigstring( mvc, index, Cmd_ArgsFrom( 2 ) );
		}
	}
}


/*
==================
MVC_ParseCommandString
==================
*/
static void MVC_ParseCommandString( mvConvert_t *mvc, msg_t *msg ) {
	const char *s;
	int seq;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );

	// see if we have already received it
	if ( mvc->commandSequence - seq >= 0 ) {
		return;
	}

	mvc->commandSequence = seq;
	Q_strncpyz( mvc->commands[ seq & (MAX_RELIABLE_COMMANDS-1) ], s, sizeof( mvc->commands[0] ) );

	MVC_ExecuteCommand( mvc, s );
}


/*
==================
MVC_ParseGamestate
==================
*/
static qboolean MVC_ParseGamestate( mvConvert_t *mvc, msg_t *msg ) {
	entityState_t nullstate;
	int i, cmd, newnum;

	Com_Memset( &nullstate, 0, sizeof( nullstate ) );

	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		MVC_SetConfigstring( mvc, i, "" );
	}
	Com_Memset( mvc->baselines, 0, sizeof( mvc->baselines ) );
	Com_Memset( mvc->baselineUsed, 0, sizeof( mvc->baselineUsed ) );

	mvc->commandSequence = MSG_ReadLong( msg );

	while ( 1 ) {
		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF ) {
			break;
		}

		if ( cmd == svc_configstring ) {
			i = MSG_ReadShort( msg );
			if ( i < 0 || i >= MAX_CONFIGSTRINGS ) {
				Com_Printf( S_COLOR_YELLOW "mvconvert: bad configstring index %i\n", i );
				return qfalse;
			}
			MVC_SetConfigstring( mvc, i, MSG_ReadBigString( msg ) );
		} else if ( cmd == svc_baseline ) {
			newnum = MSG_ReadEntitynum( msg );
			if ( newnum < 0 || newnum >= MAX_GENTITIES ) {
				Com_Printf( S_COLOR_YELLOW "mvconvert: bad baseline number %i\n", newnum );
				return qfalse;
			}
			MSG_ReadDeltaEntity( msg, &nullstate, &mvc->baselines[ newnum ], newnum );
			mvc->baselineUsed[ newnum ] = 1;
		} else {
			Com_Printf( S_COLOR_YELLOW "mvconvert: bad gamestate command byte %i\n", cmd );
			return qfalse;
		}
	}

	mvc->clientNum = MSG_ReadLong( msg );
	mvc->checksumFeed = MSG_ReadLong( msg );
	mvc->haveGamestate = qtrue;

	// no delta from snapshots of the previous gamestate
	mvc->snaps[0].valid = qfalse;
	mvc->snaps[1].valid = qfalse;

	return qtrue;
}


/*
==================
MVC_DeltaEntity
==================
*/
static qboolean MVC_DeltaEntity( msg_t *msg, mvcSnapshot_t *frame, int newnum, const entityState_t *old, qboolean unchanged ) {
	entityState_t *state;

	if ( frame->numEntities >= MAX_GENTITIES ) {
		Com_Printf( S_COLOR_YELLOW "mvconvert: too many entities in snapshot\n" );
		return qfalse;
	}

	state = &frame->entities[ frame->numEntities ];

	if ( unchanged ) {
		*state = *old;
	} else {
		MSG_ReadDeltaEntity( msg, old, state, newnum );
	}

	if ( state->number != (MAX_GENTITIES-1) ) {
		frame->numEntities++;
	}

	return qtrue;
}


/*
==================
MVC_ParsePacketEntities
==================
*/
static qboolean MVC_ParsePacketEntities( mvConvert_t *mvc, msg_t *msg, const mvcSnapshot_t *oldframe, mvcSnapshot_t *newframe ) {
	const entityState_t *oldstate;
	int oldindex, oldnum, newnum;

	newframe->numEntities = 0;

	oldindex = 0;
	oldstate = NULL;
	if ( !oldframe || oldframe->numEntities == 0 ) {
		oldnum = MAX_GENTITIES+1;
	} else {
		oldstate = &oldframe->entities[ 0 ];
		oldnum = oldstate->number;
	}

#define MVC_NEXT_OLD() \
	oldindex++; \
	if ( oldindex >= oldframe->numEntities ) { \
		oldnum = MAX_GENTITIES+1; \
	} else { \
		oldstate = &oldframe->entities[ oldindex ]; \
		oldnum = oldstate->number; \
	}

	while ( 1 ) {
		newnum = MSG_ReadEntitynum( msg );
		if ( newnum < 0 ) {
			Com_Printf( S_COLOR_YELLOW "mvconvert: end of message in packet entities\n" );
			return qfalse;
		}

		if ( newnum == (MAX_GENTITIES-1) ) {
			break;
		}

		while ( oldnum < newnum ) {
			// one or more entities from the old packet are unchanged
			if ( !MVC_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue ) )
				return qfalse;
			MVC_NEXT_OLD();
		}

		if ( oldnum == newnum ) {
			// delta from previous state
			if ( !MVC_DeltaEntity( msg, newframe, newnum, oldstate, qfalse ) )
				return qfalse;
			MVC_NEXT_OLD();
			continue;
		}

		// delta from baseline
		if ( !MVC_DeltaEntity( msg, newframe, newnum, &mvc->baselines[ newnum ], qfalse ) )
			return qfalse;
	}

	// any remaining entities in the old frame are copied over
	while ( oldnum != MAX_GENTITIES+1 ) {
		if ( !MVC_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue ) )
			return qfalse;
		MVC_NEXT_OLD();
	}

#undef MVC_NEXT_OLD

	return qtrue;
}


/*
==================
MVC_ParseMultiviewSnapshot

Mirrors CL_ParseSnapshot() for svc_multiview
==================
*/
static qboolean MVC_ParseMultiviewSnapshot( mvConvert_t *mvc, msg_t *msg ) {
	mvcSnapshot_t *snap, *old;
	const playerState_t *oldPs;
	entityState_t *es;
	int deltaNum, firstIndex, lastIndex;
	int clientNum, i;

	snap = &mvc->snaps[ mvc->current ^ 1 ];
	Com_Memset( snap, 0, sizeof( *snap ) );

	snap->messageNum = mvc->messageNum;
	snap->serverTime = MSG_ReadLong( msg );
	deltaNum = MSG_ReadByte( msg );
	snap->snapFlags = MSG_ReadByte( msg );

	if ( deltaNum == 0 ) {
		old = NULL;
	} else {
		old = &mvc->snaps[ mvc->current ];
		if ( !old->valid || old->messageNum != snap->messageNum - deltaNum ) {
			// recorder always deltas from the previous message, so this means a damaged file
			Com_Printf( S_COLOR_YELLOW "mvconvert: delta from unknown frame at message %i\n", snap->messageNum );
			return qfalse;
		}
		Com_Memcpy( snap->clientMask, old->clientMask, sizeof( snap->clientMask ) );
		snap->mergeMask = old->mergeMask;
		snap->version = old->version;
	}

	if ( MSG_ReadBits( msg, 1 ) ) {
		snap->version = MSG_ReadByte( msg );
	}

	if ( snap->version != MV_PROTOCOL_VERSION ) {
		Com_Printf( S_COLOR_YELLOW "mvconvert: unknown multiview protocol version %i\n", snap->version );
		return qfalse;
	}

	if ( MSG_ReadBits( msg, 1 ) ) {
		snap->mergeMask = MSG_ReadBits( msg, SM_BITS );
	}

	// playerstate mask
	while ( MSG_ReadBits( msg, 1 ) ) {
		firstIndex = MSG_ReadBits( msg, 3 );
		lastIndex = MSG_ReadBits( msg, 3 );
		for ( ; firstIndex < lastIndex + 1; firstIndex++ ) {
			snap->clientMask[ firstIndex ] ^= MSG_ReadByte( msg ); // delta-xor mask
		}
	}

	for ( clientNum = 0; clientNum < MAX_CLIENTS; clientNum++ ) {
		mvcPlayer_t *pl = &snap->clps[ clientNum ];

		if ( !GET_ABIT( snap->clientMask, clientNum ) )
			continue;

		pl->areabytes = MSG_ReadBits( msg, 6 );
		if ( pl->areabytes > sizeof( pl->areamask ) ) {
			Com_Printf( S_COLOR_YELLOW "mvconvert: invalid areamask size %i for client %i\n", pl->areabytes, clientNum );
			return qfalse;
		}
		MSG_ReadData( msg, pl->areamask, pl->areabytes );

		if ( old && old->clps[ clientNum ].valid ) {
			Com_Memcpy( pl->entMask, old->clps[ clientNum ].entMask, sizeof( pl->entMask ) );
			oldPs = &old->clps[ clientNum ].ps;
		} else {
			oldPs = NULL;
		}

		MSG_ReadDeltaPlayerstate( msg, oldPs, &pl->ps );

		// entity mask
		while ( MSG_ReadBits( msg, 1 ) ) {
			firstIndex = MSG_ReadBits( msg, 7 );
			lastIndex = MSG_ReadBits( msg, 7 );
			for ( i = firstIndex; i < lastIndex + 1; i++ ) {
				pl->entMask[ i ] ^= MSG_ReadByte( msg ); // delta-xor mask
			}
		}

		pl->valid = qtrue;
	}

	if ( !MVC_ParsePacketEntities( mvc, msg, old, snap ) ) {
		return qfalse;
	}

	// restore player entity fields that were skipped in favor of playerstate
	if ( snap->mergeMask ) {
		for ( i = 0; i < snap->numEntities; i++ ) {
			es = &snap->entities[ i ];
			if ( es->number >= MAX_CLIENTS )
				break;
			if ( snap->clps[ es->number ].valid )
				MSG_PlayerStateToEntityState( &snap->clps[ es->number ].ps, es, qtrue, snap->mergeMask );
		}
	}

	snap->valid = qtrue;
	mvc->current ^= 1;

	return qtrue;
}


/*
==================
MVC_WriteMessage
==================
*/
static void MVC_WriteMessage( mvcOutput_t *out, const msg_t *msg ) {
	int len;

	len = LittleLong( out->messageNum );
	FS_Write( &len, 4, out->file );
	len = LittleLong( msg->cursize );
	FS_Write( &len, 4, out->file );
	FS_Write( msg->data, msg->cursize, out->file );

	out->messageNum++;
}


/*
==================
MVC_WriteGamestate
==================
*/
static void MVC_WriteGamestate( const mvConvert_t *mvc, mvcOutput_t *out, int clientNum ) {
	byte			bufData[ MAX_MSGLEN_BUF ];
	msg_t			msg;
	entityState_t	nullstate;
	int				i;

	MSG_Init( &msg, bufData, MAX_MSGLEN );
	MSG_Bitstream( &msg );

	MSG_WriteLong( &msg, mvc->reliableAcknowledge );

	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, mvc->commandSequence );

	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( !mvc->configstrings[i] )
			continue;
		MSG_WriteByte( &msg, svc_configstring );
		MSG_WriteShort( &msg, i );
		MSG_WriteBigString( &msg, mvc->configstrings[i] );
	}

	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		if ( !mvc->baselineUsed[i] )
			continue;
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, &mvc->baselines[i], qtrue );
	}

	MSG_WriteByte( &msg, svc_EOF );

	MSG_WriteLong( &msg, clientNum );
	MSG_WriteLong( &msg, mvc->checksumFeed );

	MSG_WriteByte( &msg, svc_EOF );

	MVC_WriteMessage( out, &msg );

	out->commandNum = mvc->commandSequence;
	out->deltaValid = qfalse;
}


/*
==================
MVC_BuildEntityList

Select entities visible to the given client, like CL_GetSnapshot() does for multiview
==================
*/
static int MVC_BuildEntityList( const mvcSnapshot_t *snap, int clientNum, const entityState_t **list ) {
	const mvcPlayer_t *pl;
	const entityState_t *es;
	int i, count;

	pl = &snap->clps[ clientNum ];
	count = 0;

	for ( i = 0; i < snap->numEntities; i++ ) {
		es = &snap->entities[ i ];
		if ( !GET_ABIT( pl->entMask, es->number ) )
			continue;
		// skip own and spectated entity
		if ( es->number == clientNum || es->number == pl->ps.clientNum )
			continue;
		if ( count >= MAX_SNAPSHOT_ENTITIES )
			break;
		list[ count++ ] = es;
	}

	return count;
}


/*
==================
MVC_EmitPacketEntities
==================
*/
static void MVC_EmitPacketEntities( const mvConvert_t *mvc, msg_t *msg, const entityState_t **from, int numFrom, const entityState_t **to, int numTo ) {
	int oldindex, newindex;
	int oldnum, newnum;

	oldindex = 0;
	newindex = 0;
	while ( newindex < numTo || oldindex < numFrom ) {
		newnum = ( newindex >= numTo ) ? MAX_GENTITIES+1 : to[ newindex ]->number;
		oldnum = ( oldindex >= numFrom ) ? MAX_GENTITIES+1 : from[ oldindex ]->number;

		if ( newnum == oldnum ) {
			// delta update from old position
			MSG_WriteDeltaEntity( msg, from[ oldindex ], to[ newindex ], qfalse );
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity( msg, &mvc->baselines[ newnum ], to[ newindex ], qtrue );
			newindex++;
			continue;
		}

		// the old entity isn't present in the new message
		MSG_WriteDeltaEntity( msg, from[ oldindex ], NULL, qtrue );
		oldindex++;
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS ); // end of packetentities
}


/*
==================
MVC_WriteSnapshot
==================
*/
static qboolean MVC_WriteSnapshot( const mvConvert_t *mvc, mvcOutput_t *out, int clientNum, const mvcSnapshot_t *snap, const mvcSnapshot_t *prev ) {
	const entityState_t *toList[ MAX_SNAPSHOT_ENTITIES ];
	const entityState_t *fromList[ MAX_SNAPSHOT_ENTITIES ];
	const mvcPlayer_t *pl;
	byte		bufData[ MAX_MSGLEN_BUF ];
	msg_t		msg;
	qboolean	delta;
	int			numTo, numFrom, i;

	pl = &snap->clps[ clientNum ];
	delta = out->deltaValid && prev->valid && prev->clps[ clientNum ].valid;

	MSG_Init( &msg, bufData, MAX_MSGLEN );
	MSG_Bitstream( &msg );

	MSG_WriteLong( &msg, mvc->reliableAcknowledge );

	// pending server commands
	if ( mvc->commandSequence - out->commandNum > MAX_RELIABLE_COMMANDS ) {
		out->commandNum = mvc->commandSequence - MAX_RELIABLE_COMMANDS;
	}
	for ( i = out->commandNum + 1; i <= mvc->commandSequence; i++ ) {
		MSG_WriteByte( &msg, svc_serverCommand );
		MSG_WriteLong( &msg, i );
		MSG_WriteString( &msg, mvc->commands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}
	out->commandNum = mvc->commandSequence;

	MSG_WriteByte( &msg, svc_snapshot );
	MSG_WriteLong( &msg, snap->serverTime );
	MSG_WriteByte( &msg, delta ? 1 : 0 );
	MSG_WriteByte( &msg, snap->snapFlags & ~SNAPFLAG_MULTIVIEW );
	MSG_WriteByte( &msg, pl->areabytes );
	MSG_WriteData( &msg, pl->areamask, pl->areabytes );

	numTo = MVC_BuildEntityList( snap, clientNum, toList );

	if ( delta ) {
		MSG_WriteDeltaPlayerstate( &msg, &prev->clps[ clientNum ].ps, &pl->ps );
		numFrom = MVC_BuildEntityList( prev, clientNum, fromList );
	} else {
		MSG_WriteDeltaPlayerstate( &msg, NULL, &pl->ps );
		numFrom = 0;
	}

	MVC_EmitPacketEntities( mvc, &msg, fromList, numFrom, toList, numTo );

	MSG_WriteByte( &msg, svc_EOF );

	if ( msg.overflowed ) {
		Com_Printf( S_COLOR_YELLOW "mvconvert: message overflow for client %i\n", clientNum );
		return qfalse;
	}

	MVC_WriteMessage( out, &msg );

	if ( out->numSnapshots == 0 )
		out->firstServerTime = snap->serverTime;
	out->lastServerTime = snap->serverTime;
	out->numSnapshots++;
	out->deltaValid = qtrue;

	return qtrue;
}


/*
==================
MVC_OpenOutput
==================
*/
static qboolean MVC_OpenOutput( mvConvert_t *mvc, int clientNum ) {
	mvcOutput_t *out;
	// the raw name still has its colour codes, which can make it longer than
	// MAX_NAME_LENGTH; cutting it before Q_CleanStr would lose letters
	char playerName[ MAX_INFO_VALUE ];
	char cleanName[ MAX_NAME_LENGTH ];
	const char *s;
	int i, n;

	out = &mvc->out[ clientNum ];

	// player name from the userinfo configstring, reduced to safe filename characters
	playerName[0] = '\0';
	s = mvc->configstrings[ MVC_CS_PLAYERS + clientNum ];
	if ( s ) {
		Q_strncpyz( playerName, Info_ValueForKey( s, "n" ), sizeof( playerName ) );
		Q_CleanStr( playerName );
	}
	for ( i = 0, n = 0; playerName[i] && n < sizeof( cleanName ) - 1; i++ ) {
		const char c = playerName[i];
		if ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_' ) {
			cleanName[ n++ ] = c;
		}
	}
	cleanName[ n ] = '\0';
	if ( !cleanName[0] ) {
		strcpy( cleanName, "player" );
	}

	Com_sprintf( out->name, sizeof( out->name ), "demos/%s-%02i-%s.%s%d",
		mvc->baseName, clientNum, cleanName, DEMOEXT, OLD_PROTOCOL_VERSION );

	out->file = FS_FOpenFileWrite( out->name );
	if ( out->file == FS_INVALID_HANDLE ) {
		Com_Printf( S_COLOR_YELLOW "mvconvert: couldn't open %s\n", out->name );
		return qfalse;
	}

	out->messageNum = 0;
	MVC_WriteGamestate( mvc, out, clientNum );

	return qtrue;
}


/*
==================
MVC_EmitSnapshot

Write the last parsed multiview snapshot to all selected outputs
==================
*/
static qboolean MVC_EmitSnapshot( mvConvert_t *mvc ) {
	const mvcSnapshot_t *snap, *prev;
	mvcOutput_t *out;
	int clientNum;

	snap = &mvc->snaps[ mvc->current ];
	prev = &mvc->snaps[ mvc->current ^ 1 ];

	for ( clientNum = 0; clientNum < MAX_CLIENTS; clientNum++ ) {
		out = &mvc->out[ clientNum ];

		if ( mvc->onlyClient >= 0 && clientNum != mvc->onlyClient )
			continue;

		if ( !snap->clps[ clientNum ].valid ) {
			out->deltaValid = qfalse;
			continue;
		}

		if ( out->file == FS_INVALID_HANDLE ) {
			if ( !MVC_OpenOutput( mvc, clientNum ) )
				return qfalse;
		}

		if ( !MVC_WriteSnapshot( mvc, out, clientNum, snap, prev ) )
			return qfalse;
	}

	return qtrue;
}


/*
==================
MVC_ParseMessage
==================
*/
static qboolean MVC_ParseMessage( mvConvert_t *mvc, msg_t *msg ) {
	qboolean gotSnapshot;
	int cmd, i;

	MSG_Bitstream( msg );

	mvc->reliableAcknowledge = MSG_ReadLong( msg );

	gotSnapshot = qfalse;

	while ( 1 ) {
		if ( msg->readcount > msg->cursize ) {
			Com_Printf( S_COLOR_YELLOW "mvconvert: read past end of message %i\n", mvc->messageNum );
			return qfalse;
		}

		cmd = MSG_ReadByte( msg );
		if ( cmd == svc_EOF ) {
			break;
		}

		switch ( cmd ) {
		case svc_nop:
			break;
		case svc_serverCommand:
			MVC_ParseCommandString( mvc, msg );
			break;
		case svc_gamestate:
			if ( !MVC_ParseGamestate( mvc, msg ) )
				return qfalse;
			// new map: restart all opened outputs with the new gamestate
			for ( i = 0; i < MAX_CLIENTS; i++ ) {
				if ( mvc->out[i].file != FS_INVALID_HANDLE ) {
					MVC_WriteGamestate( mvc, &mvc->out[i], i );
				}
			}
			break;
		case svc_multiview:
			if ( !mvc->haveGamestate ) {
				Com_Printf( S_COLOR_YELLOW "mvconvert: snapshot before gamestate\n" );
				return qfalse;
			}
			if ( !MVC_ParseMultiviewSnapshot( mvc, msg ) )
				return qfalse;
			gotSnapshot = qtrue;
			break;
		default:
			Com_Printf( S_COLOR_YELLOW "mvconvert: unsupported server message %i\n", cmd );
			return qfalse;
		}
	}

	if ( gotSnapshot ) {
		return MVC_EmitSnapshot( mvc );
	}

	return qtrue;
}


/*
==================
SV_MultiViewConvert_f

mvconvert <demo> [clientNum]
==================
*/
void SV_MultiViewConvert_f( void ) {
	char		demoName[ MAX_QPATH ];
	char		ext[ 16 ];
	byte		bufData[ MAX_MSGLEN_BUF ];
	msg_t		msg;
	mvConvert_t	*mvc;
	mvcOutput_t	*out;
	int			i, r, len, seq, count;
	qboolean	ok;

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 ) {
		Com_Printf( "usage: mvconvert <demoname> [clientNum]\n" );
		return;
	}

	// release resources left by an aborted conversion
	if ( mvc_active ) {
		MVC_Free( mvc_active );
	}

	mvc = Z_Malloc( sizeof( *mvc ) );
	mvc_active = mvc;

	mvc->file = FS_INVALID_HANDLE;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		mvc->out[i].file = FS_INVALID_HANDLE;
	}

	mvc->onlyClient = -1;
	if ( Cmd_Argc() == 3 ) {
		mvc->onlyClient = atoi( Cmd_Argv( 2 ) );
		if ( (unsigned) mvc->onlyClient >= MAX_CLIENTS ) {
			Com_Printf( "mvconvert: bad client number %s\n", Cmd_Argv( 2 ) );
			MVC_Free( mvc );
			return;
		}
	}

	// accept name with or without extension and demos/ prefix
	Q_strncpyz( demoName, Cmd_Argv( 1 ), sizeof( demoName ) );
	if ( !Q_stricmpn( demoName, "demos/", 6 ) ) {
		memmove( demoName, demoName + 6, strlen( demoName + 6 ) + 1 );
	}
	Com_sprintf( ext, sizeof( ext ), ".%s%d", DEMOEXT, NEW_PROTOCOL_VERSION );
	len = (int)strlen( demoName ) - (int)strlen( ext );
	if ( len > 0 && !Q_stricmp( demoName + len, ext ) ) {
		demoName[ len ] = '\0';
	}
	Q_strncpyz( mvc->baseName, demoName, sizeof( mvc->baseName ) );

	Com_sprintf( demoName, sizeof( demoName ), "demos/%s%s", mvc->baseName, ext );

	FS_BypassPure();
	FS_FOpenFileRead( demoName, &mvc->file, qtrue );
	FS_RestorePure();

	if ( mvc->file == FS_INVALID_HANDLE ) {
		Com_Printf( "mvconvert: couldn't open %s\n", demoName );
		MVC_Free( mvc );
		return;
	}

	Com_Printf( "converting %s...\n", demoName );

	ok = qtrue;

	while ( 1 ) {
		r = FS_Read( &seq, 4, mvc->file );
		if ( r != 4 )
			break;
		r = FS_Read( &len, 4, mvc->file );
		if ( r != 4 )
			break;

		len = LittleLong( len );
		if ( len < 0 )
			break; // end of demo marker

		if ( len > MAX_MSGLEN ) {
			Com_Printf( S_COLOR_YELLOW "mvconvert: message too long: %i\n", len );
			ok = qfalse;
			break;
		}

		MSG_Init( &msg, bufData, MAX_MSGLEN );
		r = FS_Read( msg.data, len, mvc->file );
		if ( r != len ) {
			Com_Printf( S_COLOR_YELLOW "mvconvert: demo file was truncated\n" );
			break;
		}
		msg.cursize = len;

		mvc->messageNum = LittleLong( seq );

		if ( !MVC_ParseMessage( mvc, &msg ) ) {
			ok = qfalse;
			break;
		}
	}

	// finish output demos
	count = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		out = &mvc->out[i];
		if ( out->file == FS_INVALID_HANDLE )
			continue;
		len = -1;
		FS_Write( &len, 4, out->file );
		FS_Write( &len, 4, out->file );
		FS_FCloseFile( out->file );
		out->file = FS_INVALID_HANDLE;
		Com_Printf( "  %s: %i snapshots, %i seconds\n", out->name, out->numSnapshots,
			( out->lastServerTime - out->firstServerTime ) / 1000 );
		count++;
	}

	if ( ok ) {
		Com_Printf( "mvconvert: wrote %i demo(s)\n", count );
	} else {
		Com_Printf( S_COLOR_YELLOW "mvconvert: conversion aborted, %i incomplete demo(s) written\n", count );
	}

	MVC_Free( mvc );
}

#endif // USE_MV
