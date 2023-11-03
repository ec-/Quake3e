/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2017-2023 Noah Metzger (chomenor@gmail.com)

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

#include "sv_record_local.h"
#include <time.h>

/* ******************************************************************************** */
// Definitions
/* ******************************************************************************** */

typedef struct {
	qboolean autoStarted;

	record_state_t *rs;
	char activePlayers[RECORD_MAX_CLIENTS];
	int lastSnapflags;

	char *targetDirectory;
	char *targetFilename;

	fileHandle_t recordfile;
	record_data_stream_t stream;
	char streamBuffer[130000];
} record_writer_state_t;

record_writer_state_t *rws;

/* ******************************************************************************** */
// State-Updating Operations
/* ******************************************************************************** */

/*
==================
Record_CompareEntityStates

Returns qtrue on discrepancy, qfalse otherwise
==================
*/
static qboolean Record_CompareEntityStates( record_entityset_t *state1, record_entityset_t *state2, qboolean verbose ) {
	qboolean discrepancy = qfalse;
	int i;
	for ( i = 0; i < MAX_GENTITIES; ++i ) {
		if ( Record_Bit_Get( state1->activeFlags, i ) != Record_Bit_Get( state2->activeFlags, i ) ) {
			if ( verbose ) {
				Record_Printf( RP_ALL, "Entity %i active discrepancy\n", i );
			}
			discrepancy = qtrue;
			continue;
		}
		if ( !Record_Bit_Get( state1->activeFlags, i ) ) {
			continue;
		}
		if ( memcmp( &state1->entities[i], &state2->entities[i], sizeof( *state1->entities ) ) ) {
			if ( verbose ) {
				Record_Printf( RP_ALL, "Entity %i content discrepancy\n", i );
			}
			discrepancy = qtrue;
			continue;
		}
	}
	return discrepancy;
}

/*
==================
Record_UpdateEntityset
==================
*/
static void Record_UpdateEntityset( record_entityset_t *entities ) {
	record_data_stream_t verifyStream;
	record_entityset_t *verifyEntities = 0;

	Record_Stream_WriteValue( RC_STATE_ENTITY_SET, 1, &rws->stream );

	if ( sv_recordVerifyData->integer ) {
		verifyStream = rws->stream;
		verifyEntities = (record_entityset_t *)Z_Malloc( sizeof( *verifyEntities ) );
		*verifyEntities = rws->rs->entities;
	}

	Record_EncodeEntityset( &rws->rs->entities, entities, &rws->stream );

	if ( sv_recordVerifyData->integer ) {
		Record_DecodeEntityset( verifyEntities, &verifyStream );
		if ( verifyStream.position != rws->stream.position ) {
			Record_Printf( RP_ALL, "Record_UpdateEntityset: verify stream in different position\n" );
		} else if ( Record_CompareEntityStates( entities, verifyEntities, qtrue ) ) {
			Record_Printf( RP_ALL, "Record_UpdateEntityset: verify discrepancy\n" );
		}
		Z_Free( verifyEntities );
	}
}

/*
==================
Record_UpdatePlayerstate
==================
*/
static void Record_UpdatePlayerstate( playerState_t *ps, int clientNum ) {
	record_data_stream_t verifyStream;
	playerState_t verifyPs;

	if ( !memcmp( ps, &rws->rs->clients[clientNum].playerstate, sizeof( *ps ) ) ) {
		return;
	}

	Record_Stream_WriteValue( RC_STATE_PLAYERSTATE, 1, &rws->stream );

	// We can't rely on ps->clientNum because it can be wrong due to spectating and such
	Record_Stream_WriteValue( clientNum, 1, &rws->stream );

	if ( sv_recordVerifyData->integer ) {
		verifyStream = rws->stream;
		verifyPs = rws->rs->clients[clientNum].playerstate;
	}

	Record_EncodePlayerstate( &rws->rs->clients[clientNum].playerstate, ps, &rws->stream );

	if ( sv_recordVerifyData->integer ) {
		Record_DecodePlayerstate( &verifyPs, &verifyStream );
		if ( verifyStream.position != rws->stream.position ) {
			Record_Printf( RP_ALL, "Record_UpdatePlayerstate: verify stream in different position\n" );
		} else if ( memcmp( ps, &verifyPs, sizeof( *ps ) ) ) {
			Record_Printf( RP_ALL, "Record_UpdatePlayerstate: verify discrepancy\n" );
		}
	}
}

/*
==================
Record_UpdateVisibilityState
==================
*/
static void Record_UpdateVisibilityState( record_visibility_state_t *vs, int clientNum ) {
	record_data_stream_t verifyStream;
	record_visibility_state_t verifyVs;

	if ( !memcmp( vs, &rws->rs->clients[clientNum].visibility, sizeof( *vs ) ) ) {
		return;
	}

	Record_Stream_WriteValue( RC_STATE_VISIBILITY, 1, &rws->stream );
	Record_Stream_WriteValue( clientNum, 1, &rws->stream );

	if ( sv_recordVerifyData->integer ) {
		verifyStream = rws->stream;
		verifyVs = rws->rs->clients[clientNum].visibility;
	}

	Record_EncodeVisibilityState( &rws->rs->clients[clientNum].visibility, vs, &rws->stream );

	if ( sv_recordVerifyData->integer ) {
		Record_DecodeVisibilityState( &verifyVs, &verifyStream );
		if ( verifyStream.position != rws->stream.position ) {
			Record_Printf( RP_ALL, "Record_UpdateVisibilityState: verify stream in different position\n" );
		} else if ( memcmp( vs, &verifyVs, sizeof( *vs ) ) ) {
			Record_Printf( RP_ALL, "Record_UpdateVisibilityState: verify discrepancy\n" );
		}
	}
}

/*
==================
Record_UpdateVisibilityStateClient
==================
*/
static void Record_UpdateVisibilityStateClient( int clientNum ) {
	record_visibility_state_t vs;
	record_visibility_state_t vsOptimized;
	Record_GetCurrentVisibility( clientNum, &vs );
	Record_OptimizeInactiveVisibility( &rws->rs->entities, &rws->rs->clients[clientNum].visibility, &vs, &vsOptimized );
	Record_UpdateVisibilityState( &vsOptimized, clientNum );
}

/*
==================
Record_UpdateUsercmd
==================
*/
static void Record_UpdateUsercmd( usercmd_t *usercmd, int clientNum ) {
	record_data_stream_t verifyStream;
	usercmd_t verifyUsercmd;

	Record_Stream_WriteValue( RC_STATE_USERCMD, 1, &rws->stream );
	Record_Stream_WriteValue( clientNum, 1, &rws->stream );

	if ( sv_recordVerifyData->integer ) {
		verifyStream = rws->stream;
		verifyUsercmd = rws->rs->clients[clientNum].usercmd;
	}

	Record_EncodeUsercmd( &rws->rs->clients[clientNum].usercmd, usercmd, &rws->stream );

	if ( sv_recordVerifyData->integer ) {
		Record_DecodeUsercmd( &verifyUsercmd, &verifyStream );
		if ( verifyStream.position != rws->stream.position ) {
			Record_Printf( RP_ALL, "Record_UpdateUsercmd: verify stream in different position\n" );
		} else if ( memcmp( usercmd, &verifyUsercmd, sizeof( *usercmd ) ) ) {
			Record_Printf( RP_ALL, "Record_UpdateUsercmd: verify discrepancy\n" );
		}
	}
}

/*
==================
Record_UpdateConfigstring
==================
*/
static void Record_UpdateConfigstring( int index, char *value ) {
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Record_Printf( RP_ALL, "Record_UpdateConfigstring: invalid configstring index\n" );
		return;
	}

	if ( !strcmp( rws->rs->configstrings[index], value ) ) {
		return;
	}

	Record_Stream_WriteValue( RC_STATE_CONFIGSTRING, 1, &rws->stream );
	Record_Stream_WriteValue( index, 2, &rws->stream );
	Record_EncodeString( value, &rws->stream );

	Z_Free( rws->rs->configstrings[index] );
	rws->rs->configstrings[index] = CopyString( value );
}

/*
==================
Record_UpdateCurrentServercmd
==================
*/
static void Record_UpdateCurrentServercmd( char *value ) {
	if ( !strcmp( rws->rs->currentServercmd, value ) ) {
		return;
	}

	Record_Stream_WriteValue( RC_STATE_CURRENT_SERVERCMD, 1, &rws->stream );
	Record_EncodeString( value, &rws->stream );

	Z_Free( rws->rs->currentServercmd );
	rws->rs->currentServercmd = CopyString( value );
}

/* ******************************************************************************** */
// Recording Start/Stop Functions
/* ******************************************************************************** */

/*
==================
Record_DeallocateRecordWriter
==================
*/
static void Record_DeallocateRecordWriter( void ) {
	if ( !rws ) {
		return;
	}
	if ( rws->rs ) {
		Record_FreeState( rws->rs );
	}
	if ( rws->targetDirectory ) {
		Z_Free( rws->targetDirectory );
	}
	if ( rws->targetFilename ) {
		Z_Free( rws->targetFilename );
	}
	Record_Free( rws );
	rws = 0;
}

/*
==================
Record_CloseRecordWriter
==================
*/
static void Record_CloseRecordWriter( void ) {
	if ( !rws ) {
		// Not supposed to happen
		Record_Printf( RP_ALL, "Record_CloseRecordWriter called with record writer not initialized\n" );
		return;
	}

	// Flush stream to file and close temp file
	Record_Stream_DumpToFile( &rws->stream, rws->recordfile );
	FS_FCloseFile( rws->recordfile );

	// Attempt to move the temp file to final destination
	FS_SV_Rename( "records/current.rec", va( "records/%s/%s.rec", rws->targetDirectory, rws->targetFilename ) );

	Record_DeallocateRecordWriter();
}

/*
==================
Record_InitializeRecordWriter
==================
*/
static void Record_InitializeRecordWriter( int maxClients, qboolean autoStarted ) {
	if ( rws ) {
		// Not supposed to happen
		Record_Printf( RP_ALL, "Record_InitializeRecordWriter called with record writer already initialized\n" );
		return;
	}

	// Allocate the structure
	rws = (record_writer_state_t *)Record_Calloc( sizeof( *rws ) );

	// Make sure records folder exists
	Sys_Mkdir( va( "%s/records", Cvar_VariableString( "fs_homepath" ) ) );

	// Rename any existing output file that might have been left over from a crash
	FS_SV_Rename( "records/current.rec", va( "records/orphan_%u.rec", rand() ) );

	// Determine move location (targetDirectory and targetFilename) for when recording is complete
	{
		time_t rawtime;
		struct tm *timeinfo;

		time( &rawtime );
		timeinfo = localtime( &rawtime );
		if ( !timeinfo ) {
			Record_Printf( RP_ALL, "Record_InitializeRecordWriter: failed to get timeinfo\n" );
			Record_DeallocateRecordWriter();
			return;
		}

		rws->targetDirectory = CopyString( va( "%i-%02i-%02i", timeinfo->tm_year + 1900,
				timeinfo->tm_mon + 1, timeinfo->tm_mday ) );
		if ( sv_recordFilenameIncludeMap->integer ) {
			rws->targetFilename = CopyString( va( "%02i-%02i-%02i-%s", timeinfo->tm_hour,
					timeinfo->tm_min, timeinfo->tm_sec, Cvar_VariableString( "mapname" ) ) );
		} else {
			rws->targetFilename = CopyString( va( "%02i-%02i-%02i", timeinfo->tm_hour,
					timeinfo->tm_min, timeinfo->tm_sec ) );
		}
	}

	// Open the temp output file
	rws->recordfile = FS_SV_FOpenFileWrite( "records/current.rec" );
	if ( !rws->recordfile ) {
		Record_Printf( RP_ALL, "Record_InitializeRecordWriter: failed to open output file\n" );
		Record_DeallocateRecordWriter();
		return;
	}

	// Set up the stream
	rws->stream.data = rws->streamBuffer;
	rws->stream.size = sizeof( rws->streamBuffer );

	// Set up the record state
	rws->rs = Record_AllocateState( maxClients );
	rws->autoStarted = autoStarted;
	rws->lastSnapflags = svs.snapFlagServerBit;
}

/*
==================
Record_WriteClientEnterWorld
==================
*/
static void Record_WriteClientEnterWorld( int clientNum ) {
	if ( !rws ) {
		return;
	}
	rws->activePlayers[clientNum] = 1;
	Record_Stream_WriteValue( RC_EVENT_CLIENT_ENTER_WORLD, 1, &rws->stream );
	Record_Stream_WriteValue( clientNum, 1, &rws->stream );
}

/*
==================
Record_WriteClientDisconnect
==================
*/
static void Record_WriteClientDisconnect( int clientNum ) {
	if ( !rws || !rws->activePlayers[clientNum] ) {
		return;
	}
	rws->activePlayers[clientNum] = 0;
	Record_Stream_WriteValue( RC_EVENT_CLIENT_DISCONNECT, 1, &rws->stream );
	Record_Stream_WriteValue( clientNum, 1, &rws->stream );
}

/*
==================
Record_CheckConnections

Handles connecting / disconnecting clients from record state
==================
*/
static void Record_CheckConnections( void ) {
	int i;
	for ( i = 0; i < rws->rs->maxClients; ++i ) {
		if ( sv.state == SS_GAME && i < sv_maxclients->integer && svs.clients[i].state == CS_ACTIVE &&
				( svs.clients[i].netchan.remoteAddress.type != NA_BOT || sv_recordFullBotData->integer ) ) {
			if ( !rws->activePlayers[i] ) {
				Record_WriteClientEnterWorld( i );
			}
		} else {
			if ( rws->activePlayers[i] ) {
				Record_WriteClientDisconnect( i );
			}
		}
	}
}

/*
==================
Record_StartWriter
==================
*/
static void Record_StartWriter( int maxClients, qboolean autoStarted ) {
	int i;
	if ( rws ) {
		return;
	}

	if ( maxClients < 1 || maxClients > RECORD_MAX_CLIENTS ) {
		Record_Printf( RP_ALL, "Record_StartWriter: invalid maxClients" );
		maxClients = RECORD_MAX_CLIENTS;
	}

	Record_InitializeRecordWriter( maxClients, autoStarted );
	if ( !rws ) {
		return;
	}

	// Write the protocol
	Record_Stream_WriteValue( sizeof( RECORD_PROTOCOL ) - 1, 4, &rws->stream ); // version length
	Record_Stream_Write( RECORD_PROTOCOL, sizeof( RECORD_PROTOCOL ) - 1, &rws->stream ); // version value
	Record_Stream_WriteValue( 0, 4, &rws->stream ); // aux info length (zero)

	// Write max clients
	Record_Stream_WriteValue( maxClients, 4, &rws->stream );

	// Write the configstrings
	for ( i = 0; i < MAX_CONFIGSTRINGS; ++i ) {
		if ( !sv.configstrings[i] ) {
			Record_Printf( RP_ALL, "Record_StartWriter: null configstring\n" );
			continue;
		}
		if ( !*sv.configstrings[i] ) {
			continue;
		}
		Record_UpdateConfigstring( i, sv.configstrings[i] );
	}

	// Write the baselines
	{
		record_entityset_t baselines;
		Record_GetCurrentBaselines( &baselines );
		Record_UpdateEntityset( &baselines );
	}
	Record_Stream_WriteValue( RC_EVENT_BASELINES, 1, &rws->stream );

	Record_Stream_DumpToFile( &rws->stream, rws->recordfile );

	Record_Printf( RP_ALL, "Recording to %s/%s.rec\n", rws->targetDirectory, rws->targetFilename );
}

/*
==================
Record_StopWriter
==================
*/
void Record_StopWriter( void ) {
	if ( !rws ) {
		return;
	}
	Record_CloseRecordWriter();
	Record_Printf( RP_ALL, "Recording stopped.\n" );
}

/*
==================
Record_HaveRecordablePlayers
==================
*/
static qboolean Record_HaveRecordablePlayers( qboolean include_bots ) {
	int i;
	if ( sv.state != SS_GAME ) {
		return qfalse;
	}
	for ( i = 0; i < sv_maxclients->integer; ++i ) {
		if ( svs.clients[i].state == CS_ACTIVE && ( ( include_bots && sv_recordFullBotData->integer ) ||
				svs.clients[i].netchan.remoteAddress.type != NA_BOT ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
==================
Record_StartCmd
==================
*/
void Record_StartCmd( void ) {
	if ( rws ) {
		Record_Printf( RP_ALL, "Already recording.\n" );
		return;
	}
	if ( !Record_HaveRecordablePlayers( sv_recordFullBotData->integer ? qtrue : qfalse ) ) {
		Record_Printf( RP_ALL, "No players to record.\n" );
		return;
	}
	Record_StartWriter( sv_maxclients->integer, qfalse );
}

/*
==================
Record_StopCmd
==================
*/
void Record_StopCmd( void ) {
	if ( !rws ) {
		Record_Printf( RP_ALL, "Not currently recording.\n" );
		return;
	}
	if ( sv_recordAutoRecording->integer ) {
		Record_Printf( RP_ALL, "NOTE: To permanently stop recording, set sv_recordAutoRecording to 0.\n" );
	}
	Record_StopWriter();
}

/* ******************************************************************************** */
// Event Handling Functions
/* ******************************************************************************** */

/*
==================
Record_Writer_ProcessUsercmd
==================
*/
void Record_Writer_ProcessUsercmd( usercmd_t *usercmd, int clientNum ) {
	if ( !rws || !rws->activePlayers[clientNum] ) {
		return;
	}

	if ( !sv_recordFullUsercmdData->integer ) {
		// Don't write a new usercmd if most of the fields are the same
		usercmd_t *oldUsercmd = &rws->rs->clients[clientNum].usercmd;
		if ( usercmd->buttons == oldUsercmd->buttons && usercmd->weapon == oldUsercmd->weapon &&
				usercmd->forwardmove == oldUsercmd->forwardmove &&
				usercmd->rightmove == oldUsercmd->rightmove && usercmd->upmove == oldUsercmd->upmove ) {
			return;
		}
		}

	Record_UpdateUsercmd( usercmd, clientNum );
}

/*
==================
Record_Writer_ProcessConfigstring
==================
*/
void Record_Writer_ProcessConfigstring( int index, const char *value ) {
	if ( !rws ) {
		return;
	}
	Record_UpdateConfigstring( index, (char *)value );
}

/*
==================
Record_Writer_ProcessServercmd
==================
*/
void Record_Writer_ProcessServercmd( int clientNum, const char *value ) {
	if ( !rws || !rws->activePlayers[clientNum] ) {
		return;
	}
	Record_UpdateCurrentServercmd( (char *)value );
	Record_Stream_WriteValue( RC_EVENT_SERVERCMD, 1, &rws->stream );
	Record_Stream_WriteValue( clientNum, 1, &rws->stream );
}

/*
==================
Record_Writer_ProcessSnapshot

Check record connections; auto start and stop recording if needed
==================
*/
void Record_Writer_ProcessSnapshot( void ) {
	if ( !rws && sv_recordAutoRecording->integer && Record_HaveRecordablePlayers( qfalse ) ) {
		Record_StartWriter( sv_maxclients->integer, qtrue );
	}
	if ( rws ) {
		Record_CheckConnections();
	}
	if ( rws && !Record_HaveRecordablePlayers( sv_recordFullBotData->integer && !rws->autoStarted ? qtrue : qfalse ) ) {
		Record_StopWriter();
	}
	if ( !rws ) {
		return;
	}

	// Check for map restart
	if ( ( rws->lastSnapflags & SNAPFLAG_SERVERCOUNT ) != ( svs.snapFlagServerBit & SNAPFLAG_SERVERCOUNT ) ) {
		Record_Printf( RP_DEBUG, "Record_Writer_ProcessSnapshot: recording map restart\n" );
		Record_Stream_WriteValue( RC_EVENT_MAP_RESTART, 1, &rws->stream );
	}
	rws->lastSnapflags = svs.snapFlagServerBit;

	{
		record_entityset_t entities;
		Record_GetCurrentEntities( &entities );
		Record_UpdateEntityset( &entities );
	}

	{
		int i;
		for ( i = 0; i < sv_maxclients->integer && i < rws->rs->maxClients; ++i ) {
			if ( svs.clients[i].state < CS_ACTIVE ) {
				continue;
			}
			if ( !rws->activePlayers[i] ) {
				continue;
			}
			Record_UpdatePlayerstate( SV_GameClientNum( i ), i );
			Record_UpdateVisibilityStateClient( i );
		}
	}

	Record_Stream_WriteValue( RC_EVENT_SNAPSHOT, 1, &rws->stream );
	Record_Stream_WriteValue( sv.time, 4, &rws->stream );

	Record_Stream_DumpToFile( &rws->stream, rws->recordfile );
}
