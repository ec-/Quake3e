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

#include "server.h"
#include <setjmp.h>

/* ******************************************************************************** */
// Definitions
/* ******************************************************************************** */

#define RECORD_PROTOCOL "quake3-v1"

#define RECORD_MAX_CLIENTS 256

typedef struct {
	char *data;
	unsigned int position;
	unsigned int size;

	// Overflow abort
	qboolean abortSet;
	jmp_buf abort;
} record_data_stream_t;

typedef enum {
	RP_ALL,
	RP_DEBUG
} record_print_mode_t;

typedef struct {
	int activeFlags[(MAX_GENTITIES+31)/32];
	entityState_t entities[MAX_GENTITIES];
} record_entityset_t;

typedef struct {
	int entVisibility[(MAX_GENTITIES+31)/32];
	int areaVisibility[8];
	int areaVisibilitySize;
} record_visibility_state_t;

typedef struct {
	playerState_t playerstate;
	record_visibility_state_t visibility;
	usercmd_t usercmd;
} record_state_client_t;

typedef struct {
	// Holds current data state of the record stream for both recording and playback
	record_entityset_t entities;
	record_state_client_t *clients;
	int maxClients;
	char *configstrings[MAX_CONFIGSTRINGS];
	char *currentServercmd;
} record_state_t;

typedef enum {
	// Optional command which includes the message size, to allow adding data
	// in the future without breaking compatibility
	RC_MISC_COMMAND = 32,

	// State
	RC_STATE_ENTITY_SET,
	RC_STATE_PLAYERSTATE,
	RC_STATE_VISIBILITY,
	RC_STATE_USERCMD,
	RC_STATE_CONFIGSTRING,
	RC_STATE_CURRENT_SERVERCMD,

	// Events
	RC_EVENT_BASELINES,
	RC_EVENT_SNAPSHOT,
	RC_EVENT_SERVERCMD,
	RC_EVENT_CLIENT_ENTER_WORLD,
	RC_EVENT_CLIENT_DISCONNECT,
	RC_EVENT_MAP_RESTART,
} record_command_t;

/* ******************************************************************************** */
// Main
/* ******************************************************************************** */

extern cvar_t *sv_adminSpectatorPassword;
extern cvar_t *sv_adminSpectatorSlots;

extern cvar_t *sv_recordAutoRecording;
extern cvar_t *sv_recordFilenameIncludeMap;
extern cvar_t *sv_recordFullBotData;
extern cvar_t *sv_recordFullUsercmdData;

extern cvar_t *sv_recordConvertWeptiming;
extern cvar_t *sv_recordConvertSimulateFollow;

extern cvar_t *sv_recordVerifyData;
extern cvar_t *sv_recordDebug;

/* ******************************************************************************** */
// Writer
/* ******************************************************************************** */

void Record_Writer_ProcessUsercmd( usercmd_t *usercmd, int clientNum );
void Record_Writer_ProcessConfigstring( int index, const char *value );
void Record_Writer_ProcessServercmd( int clientNum, const char *value );
void Record_Writer_ProcessSnapshot( void );
void Record_StopWriter( void );
void Record_StartCmd( void );
void Record_StopCmd( void );

/* ******************************************************************************** */
// Convert
/* ******************************************************************************** */

void Record_Convert_Cmd( void );
void Record_Scan_Cmd( void );

/* ******************************************************************************** */
// Spectator
/* ******************************************************************************** */

void Record_Spectator_PrintStatus( void );
void Record_Spectator_ProcessSnapshot( void );
qboolean Record_Spectator_ProcessConnection( const netadr_t *address, const char *userinfo, int challenge,
		int qport, qboolean compat );
qboolean Record_Spectator_ProcessPacketEvent( const netadr_t *address, msg_t *msg, int qport );
void Record_Spectator_ProcessMapLoaded( void );
void Record_Spectator_ProcessConfigstring( int index, const char *value );
void Record_Spectator_ProcessServercmd( int clientNum, const char *value );
void Record_Spectator_ProcessUsercmd( int clientNum, usercmd_t *usercmd );

/* ******************************************************************************** */
// Common
/* ******************************************************************************** */

// ***** Data Stream *****

void Record_Stream_Error( record_data_stream_t *stream, const char *message );
char *Record_Stream_Allocate( int size, record_data_stream_t *stream );
void Record_Stream_Write( void *data, int size, record_data_stream_t *stream );
void Record_Stream_WriteValue( int value, int size, record_data_stream_t *stream );
char *Record_Stream_ReadStatic( int size, record_data_stream_t *stream );
void Record_Stream_ReadBuffer( void *output, int size, record_data_stream_t *stream );
void Record_Stream_DumpToFile( record_data_stream_t *stream, fileHandle_t file );

// ***** Memory Allocation *****

void *Record_Calloc( unsigned int size );
void Record_Free( void *ptr );

// ***** Bit Operations *****

void Record_Bit_Set( int *target, int position );
void Record_Bit_Unset( int *target, int position );
int Record_Bit_Get( int *source, int position );

// ***** Flag Operations *****

qboolean Record_UsercmdIsFiringWeapon( const usercmd_t *cmd );
qboolean Record_PlayerstateIsSpectator( const playerState_t *ps );
void Record_SetPlayerstateFollowFlag( playerState_t *ps );

// ***** Message Printing *****

void QDECL Record_Printf( record_print_mode_t mode, const char *fmt, ... ) __attribute__( ( format( printf, 2, 3 ) ) );

// ***** Record State *****

record_state_t *Record_AllocateState( int maxClients );
void Record_FreeState( record_state_t *rs );

// ***** Structure Encoding/Decoding Functions *****

void Record_EncodeString( char *string, record_data_stream_t *stream );
char *Record_DecodeString( record_data_stream_t *stream );
void Record_EncodePlayerstate( playerState_t *state, playerState_t *source, record_data_stream_t *stream );
void Record_DecodePlayerstate( playerState_t *state, record_data_stream_t *stream );
void Record_EncodeEntitystate( entityState_t *state, entityState_t *source, record_data_stream_t *stream );
void Record_DecodeEntitystate( entityState_t *state, record_data_stream_t *stream );
void Record_EncodeEntityset( record_entityset_t *state, record_entityset_t *source, record_data_stream_t *stream );
void Record_DecodeEntityset( record_entityset_t *state, record_data_stream_t *stream );
void Record_EncodeVisibilityState( record_visibility_state_t *state, record_visibility_state_t *source,
		record_data_stream_t *stream );
void Record_DecodeVisibilityState( record_visibility_state_t *state, record_data_stream_t *stream );
void Record_EncodeUsercmd( usercmd_t *state, usercmd_t *source, record_data_stream_t *stream );
void Record_DecodeUsercmd( usercmd_t *state, record_data_stream_t *stream );

// ***** Entity Set Building *****

void Record_GetCurrentEntities( record_entityset_t *target );
void Record_GetCurrentBaselines( record_entityset_t *target );

// ***** Visibility Building *****

void Record_GetCurrentVisibility( int clientNum, record_visibility_state_t *target );
void Record_OptimizeInactiveVisibility( record_entityset_t *entityset, record_visibility_state_t *delta,
			record_visibility_state_t *source, record_visibility_state_t *target );

// ***** Message Building *****

void Record_WriteGamestateMessage( record_entityset_t *baselines, char **configstrings, int clientNum,
		int serverCommandSequence, msg_t *msg, int *baselineCutoffOut );
void Record_WriteSnapshotMessage( record_entityset_t *entities, record_visibility_state_t *visibility, playerState_t *ps,
		record_entityset_t *deltaEntities, record_visibility_state_t *deltaVisibility, playerState_t *deltaPs,
		record_entityset_t *baselines, int baselineCutoff, int lastClientCommand, int deltaFrame, int snapFlags,
		int svTime, msg_t *msg );
