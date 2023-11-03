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

/* ******************************************************************************** */
// Record Demo Writer
/* ******************************************************************************** */

typedef struct {
	fileHandle_t demofile;
	record_entityset_t baselines;

	qboolean haveDelta;
	record_entityset_t deltaEntities;
	record_visibility_state_t deltaVisibility;
	playerState_t deltaPlayerstate;

	char pendingCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];
	int pendingCommandCount;

	int baselineCutoff;
	int messageSequence;
	int serverCommandSequence;
	int snapflags;
} record_demo_writer_t;

/*
==================
Record_InitializeDemoWriter

Returns qtrue on success, qfalse otherwise
In the event of qtrue, stream needs to be freed by Record_CloseDemoWriter
==================
*/
static qboolean Record_InitializeDemoWriter( record_demo_writer_t *rdw, const char *path ) {
	Com_Memset( rdw, 0, sizeof( *rdw ) );

	rdw->demofile = FS_FOpenFileWrite( path );
	if ( !rdw->demofile ) {
		Record_Printf( RP_ALL, "Record_InitializeDemoWriter: failed to open file\n" );
		return qfalse;
	}

	rdw->messageSequence = 1;
	return qtrue;
}

/*
==================
Record_CloseDemoWriter
==================
*/
static void Record_CloseDemoWriter( record_demo_writer_t *rdw ) {
	FS_FCloseFile( rdw->demofile );
}

/*
==================
Record_FinishDemoMessage

From sv_net_chan->SV_Netchan_Transmit
==================
*/
static void Record_FinishDemoMessage( msg_t *msg, record_demo_writer_t *rdw ) {
	MSG_WriteByte( msg, svc_EOF );

	FS_Write( &rdw->messageSequence, 4, rdw->demofile );
	++rdw->messageSequence;

	FS_Write( &msg->cursize, 4, rdw->demofile );
	FS_Write( msg->data, msg->cursize, rdw->demofile );
}

/*
==================
Record_WriteDemoGamestate

Based on cl_main.c->CL_Record_f
==================
*/
static void Record_WriteDemoGamestate( record_entityset_t *baselines, char **configstrings,
		int clientNum, record_demo_writer_t *rdw ) {
	byte buffer[MAX_MSGLEN];
	msg_t msg;

	// Delta from baselines for next snapshot
	rdw->haveDelta = qfalse;
	rdw->baselines = *baselines;

	MSG_Init( &msg, buffer, sizeof( buffer ) );

	MSG_WriteLong( &msg, 0 );

	Record_WriteGamestateMessage( baselines, configstrings, clientNum, rdw->serverCommandSequence,
			&msg, &rdw->baselineCutoff );

	Record_FinishDemoMessage( &msg, rdw );
}

/*
==================
Record_WriteDemoSvcmd
==================
*/
static void Record_WriteDemoSvcmd( char *command, record_demo_writer_t *rdw ) {
	if ( rdw->pendingCommandCount >= ARRAY_LEN( rdw->pendingCommands ) ) {
		Record_Printf( RP_ALL, "Record_WriteDemoSvcmd: pending command overflow\n" );
		return;
	}

	Q_strncpyz( rdw->pendingCommands[rdw->pendingCommandCount], command, sizeof( *rdw->pendingCommands ) );
	++rdw->pendingCommandCount;
}

/*
==================
Record_WriteDemoSnapshot

Based on sv.snapshot.c->SV_SendClientSnapshot
==================
*/
static void Record_WriteDemoSnapshot( record_entityset_t *entities, record_visibility_state_t *visibility,
		playerState_t *ps, int svTime, record_demo_writer_t *rdw ) {
	int i;
	byte buffer[MAX_MSGLEN];
	msg_t msg;

	MSG_Init( &msg, buffer, sizeof( buffer ) );

	MSG_WriteLong( &msg, 0 );

	// send any reliable server commands
	for ( i = 0; i < rdw->pendingCommandCount; ++i ) {
		MSG_WriteByte( &msg, svc_serverCommand );
		MSG_WriteLong( &msg, ++rdw->serverCommandSequence );
		MSG_WriteString( &msg, rdw->pendingCommands[i] );
	}
	rdw->pendingCommandCount = 0;

	// Write the snapshot
	if ( rdw->haveDelta ) {
		Record_WriteSnapshotMessage( entities, visibility, ps, &rdw->deltaEntities, &rdw->deltaVisibility,
				&rdw->deltaPlayerstate, &rdw->baselines, rdw->baselineCutoff, 0, 1, rdw->snapflags, svTime, &msg );
	} else {
		Record_WriteSnapshotMessage( entities, visibility, ps, 0, 0, 0, &rdw->baselines, rdw->baselineCutoff, 0, 0,
				rdw->snapflags, svTime, &msg );
	}

	// Store delta for next frame
	rdw->deltaEntities = *entities;
	rdw->deltaVisibility = *visibility;
	rdw->deltaPlayerstate = *ps;
	rdw->haveDelta = qtrue;

	Record_FinishDemoMessage( &msg, rdw );
}

/*
==================
Record_WriteDemoMapRestart
==================
*/
static void Record_WriteDemoMapRestart( record_demo_writer_t *rdw ) {
	rdw->snapflags ^= SNAPFLAG_SERVERCOUNT;
}

/* ******************************************************************************** */
// Record Stream Reader
/* ******************************************************************************** */

typedef struct {
	record_data_stream_t stream;
	record_state_t *rs;

	record_command_t command;
	int time;
	int clientNum;
} record_stream_reader_t;

/*
==================
Record_StreamReader_LoadFile

Returns qtrue on success, qfalse otherwise
If qtrue returned, call free on stream->data
==================
*/
static qboolean Record_StreamReader_LoadFile( fileHandle_t fp, record_data_stream_t *stream ) {
	FS_Seek( fp, 0, FS_SEEK_END );
	stream->size = FS_FTell( fp );
	if ( !stream->size ) {
		return qfalse;
	}
	stream->data = (char *)Record_Calloc( stream->size );

	FS_Seek( fp, 0, FS_SEEK_SET );
	FS_Read( stream->data, stream->size, fp );
	stream->position = 0;
	return qtrue;
}

/*
==================
Record_StreamReader_Init

Returns qtrue on success, qfalse otherwise
If qtrue returned, stream needs to be freed by Record_StreamReader_Close
==================
*/
static qboolean Record_StreamReader_Init( record_stream_reader_t *rsr, const char *path ) {
	fileHandle_t fp = 0;
	int size;
	char *protocol;
	int maxClients;

	Com_Memset( rsr, 0, sizeof( *rsr ) );

	FS_SV_FOpenFileRead( path, &fp );
	if ( !fp ) {
		Record_Printf( RP_ALL, "Record_StreamReader_Init: failed to open source file\n" );
		return qfalse;
	}

	if ( !Record_StreamReader_LoadFile( fp, &rsr->stream ) ) {
		Record_Printf( RP_ALL, "Record_StreamReader_Init: failed to read source file\n" );
		FS_FCloseFile( fp );
		return qfalse;
	}
	FS_FCloseFile( fp );

	if ( rsr->stream.size < 8 ) {
		Record_Printf( RP_ALL, "Record_StreamReader_Init: invalid source file length\n" );
		Record_Free( rsr->stream.data );
		return qfalse;
	}

	// verify protocol version
	size = *(int *)Record_Stream_ReadStatic( 4, &rsr->stream );
	if ( size != sizeof( RECORD_PROTOCOL ) - 1 ) {
		Record_Printf( RP_ALL, "Record_StreamReader_Init: record stream has wrong protocol length\n" );
		Record_Free( rsr->stream.data );
		return qfalse;
	}
	protocol = Record_Stream_ReadStatic( size, &rsr->stream );
	if ( memcmp( protocol, RECORD_PROTOCOL, size ) ) {
		Record_Printf( RP_ALL, "Record_StreamReader_Init: record stream has wrong protocol string\n" );
		Record_Free( rsr->stream.data );
		return qfalse;
	}
	// read past optional auxiliary field
	size = *(int *)Record_Stream_ReadStatic( 4, &rsr->stream );
	Record_Stream_ReadStatic( size, &rsr->stream );

	maxClients = *(int *)Record_Stream_ReadStatic( 4, &rsr->stream );
	if ( maxClients < 1 || maxClients > RECORD_MAX_CLIENTS ) {
		Record_Printf( RP_ALL, "Record_StreamReader_Init: bad maxClients\n" );
		Record_Free( rsr->stream.data );
		return qfalse;
	}

	rsr->rs = Record_AllocateState( maxClients );
	Record_Printf( RP_DEBUG, "stream reader initialized with %i maxClients\n", maxClients );
	return qtrue;
}

/*
==================
Record_StreamReader_Close
==================
*/
static void Record_StreamReader_Close( record_stream_reader_t *rsr ) {
	Record_Free( rsr->stream.data );
	Record_FreeState( rsr->rs );
}

/*
==================
Record_StreamReader_SetClientnum
==================
*/
static void Record_StreamReader_SetClientnum( record_stream_reader_t *rsr, int clientNum ) {
	if ( clientNum < 0 || clientNum >= rsr->rs->maxClients ) {
		Record_Stream_Error( &rsr->stream, "Record_StreamReader_SetClientnum: invalid clientnum" );
	}
	rsr->clientNum = clientNum;
}

/*
==================
Record_StreamReader_Advance

Returns qtrue on success, qfalse on error or end of stream
==================
*/
static qboolean Record_StreamReader_Advance( record_stream_reader_t *rsr ) {
	if ( rsr->stream.position >= rsr->stream.size ) {
		return qfalse;
	}
	rsr->command = (record_command_t)*(unsigned char *)Record_Stream_ReadStatic( 1, &rsr->stream );

	switch ( rsr->command ) {
		case RC_MISC_COMMAND: {
			int size = *(unsigned char *)Record_Stream_ReadStatic( 1, &rsr->stream );
			if ( size == 255 ) {
				size = *(unsigned short *)Record_Stream_ReadStatic( 2, &rsr->stream );
			}
			Record_Stream_ReadStatic( size, &rsr->stream );
			break;
		}

		case RC_STATE_ENTITY_SET:
			Record_DecodeEntityset( &rsr->rs->entities, &rsr->stream );
			break;
		case RC_STATE_PLAYERSTATE:
			Record_StreamReader_SetClientnum( rsr, *(unsigned char *)Record_Stream_ReadStatic( 1, &rsr->stream ) );
			Record_DecodePlayerstate( &rsr->rs->clients[rsr->clientNum].playerstate, &rsr->stream );
			break;
		case RC_STATE_VISIBILITY:
			Record_StreamReader_SetClientnum( rsr, *(unsigned char *)Record_Stream_ReadStatic( 1, &rsr->stream ) );
			Record_DecodeVisibilityState( &rsr->rs->clients[rsr->clientNum].visibility, &rsr->stream );
			break;
		case RC_STATE_USERCMD:
			Record_StreamReader_SetClientnum( rsr, *(unsigned char *)Record_Stream_ReadStatic( 1, &rsr->stream ) );
			Record_DecodeUsercmd( &rsr->rs->clients[rsr->clientNum].usercmd, &rsr->stream );
			break;
		case RC_STATE_CONFIGSTRING: {
			int index = *(unsigned short *)Record_Stream_ReadStatic( 2, &rsr->stream );
			char *string = Record_DecodeString( &rsr->stream );
			Z_Free( rsr->rs->configstrings[index] );
			rsr->rs->configstrings[index] = CopyString( string );
			break;
		}
		case RC_STATE_CURRENT_SERVERCMD: {
			char *string = Record_DecodeString( &rsr->stream );
			Z_Free( rsr->rs->currentServercmd );
			rsr->rs->currentServercmd = CopyString( string );
			break;
		}

		case RC_EVENT_SNAPSHOT:
			rsr->time = *(int *)Record_Stream_ReadStatic( 4, &rsr->stream );
			break;
		case RC_EVENT_SERVERCMD:
		case RC_EVENT_CLIENT_ENTER_WORLD:
		case RC_EVENT_CLIENT_DISCONNECT:
			Record_StreamReader_SetClientnum( rsr, *(unsigned char *)Record_Stream_ReadStatic( 1, &rsr->stream ) );
			break;
		case RC_EVENT_BASELINES:
		case RC_EVENT_MAP_RESTART:
			break;

		default:
			Record_Printf( RP_ALL, "Record_StreamReader_Advance: unknown command %i\n", rsr->command );
			return qfalse;
	}

	return qtrue;
}

/* ******************************************************************************** */
// Record Conversion
/* ******************************************************************************** */

typedef enum {
	CSTATE_NOT_STARTED,		// Gamestate not written yet
	CSTATE_CONVERTING,		// Gamestate written, write snapshots
	CSTATE_FINISHED			// Finished, don't write anything more
} record_conversion_state_t;

typedef struct {
	int clientNum;
	int instanceWait;
	int firingTime;	// For weapon timing
	record_conversion_state_t state;
	record_entityset_t baselines;
	record_stream_reader_t rsr;
	record_demo_writer_t rdw;
	int frameCount;
} record_conversion_handler_t;

/*
==================
Record_Convert_Process
==================
*/
static void Record_Convert_Process( record_conversion_handler_t *rch ) {
	rch->rsr.stream.abortSet = qtrue;
	if ( setjmp( rch->rsr.stream.abort ) ) {
		return;
	}

	while ( Record_StreamReader_Advance( &rch->rsr ) ) {
		switch ( rch->rsr.command ) {
			case RC_EVENT_BASELINES:
				rch->baselines = rch->rsr.rs->entities;
				break;

			case RC_EVENT_SNAPSHOT:
				if ( rch->state == CSTATE_CONVERTING ) {
					playerState_t ps = rch->rsr.rs->clients[rch->clientNum].playerstate;
					if ( sv_recordConvertSimulateFollow->integer ) {
						Record_SetPlayerstateFollowFlag( &ps );
					}
					Record_WriteDemoSnapshot( &rch->rsr.rs->entities, &rch->rsr.rs->clients[rch->clientNum].visibility,
							&ps, rch->rsr.time, &rch->rdw );
					++rch->frameCount;
				}
				break;

			case RC_EVENT_SERVERCMD:
				if ( rch->state == CSTATE_CONVERTING && rch->rsr.clientNum == rch->clientNum ) {
					Record_WriteDemoSvcmd( rch->rsr.rs->currentServercmd, &rch->rdw );
				}
				break;

			case RC_STATE_USERCMD:
				if ( rch->state == CSTATE_CONVERTING && rch->rsr.clientNum == rch->clientNum &&
						sv_recordConvertWeptiming->integer ) {
					usercmd_t *usercmd = &rch->rsr.rs->clients[rch->clientNum].usercmd;
					if ( Record_UsercmdIsFiringWeapon( usercmd ) ) {
						if ( !rch->firingTime ) {
							Record_WriteDemoSvcmd( "print \"Firing\n\"", &rch->rdw );
							rch->firingTime = usercmd->serverTime;
						}
					} else {
						if ( rch->firingTime ) {
							char buffer[128];
							Com_sprintf( buffer, sizeof( buffer ), "print \"Ceased %i\n\"",
									usercmd->serverTime - rch->firingTime );
							Record_WriteDemoSvcmd( buffer, &rch->rdw );
							rch->firingTime = 0;
						}
					}
				}
				break;

			case RC_EVENT_MAP_RESTART:
				if ( rch->state == CSTATE_CONVERTING ) {
					Record_WriteDemoMapRestart( &rch->rdw );
				}
				break;

			case RC_EVENT_CLIENT_ENTER_WORLD:
				if ( rch->state == CSTATE_NOT_STARTED && rch->rsr.clientNum == rch->clientNum ) {
					if ( rch->instanceWait ) {
						--rch->instanceWait;
					} else {
						// Start encoding
						Record_WriteDemoGamestate( &rch->baselines, rch->rsr.rs->configstrings, rch->clientNum, &rch->rdw );
						rch->state = CSTATE_CONVERTING;
					}
				}
				break;

			case RC_EVENT_CLIENT_DISCONNECT:
				if ( rch->state == CSTATE_CONVERTING && rch->rsr.clientNum == rch->clientNum ) {
					// Stop encoding
					rch->state = CSTATE_FINISHED;
				}
				break;

			default:
				break;
		}
	}

	rch->rsr.stream.abortSet = qfalse;
}

/*
==================
Record_Convert_Run
==================
*/
static void Record_Convert_Run( const char *path, int clientNum, int instance ) {
	const char *output_path = "demos/output.dm_" XSTRING( OLD_PROTOCOL_VERSION );
	record_conversion_handler_t *rch;

	rch = (record_conversion_handler_t *)Record_Calloc( sizeof( *rch ) );
	rch->clientNum = clientNum;
	rch->instanceWait = instance;

	if ( !Record_StreamReader_Init( &rch->rsr, path ) ) {
		Record_Free( rch );
		return;
	}

	if ( !Record_InitializeDemoWriter( &rch->rdw, output_path ) ) {
		Record_StreamReader_Close( &rch->rsr );
		Record_Free( rch );
		return;
	}

	Record_Convert_Process( rch );

	if ( rch->state == CSTATE_NOT_STARTED ) {
		Record_Printf( RP_ALL, "failed to locate session; check client and instance parameters\n"
				"use record_scan command to show available client and instance options\n" );
	} else {
		if ( rch->state == CSTATE_CONVERTING ) {
			Record_Printf( RP_ALL, "failed to reach disconnect marker; demo may be incomplete\n" );
		}
		Record_Printf( RP_ALL, "%i frames written to %s\n", rch->frameCount, output_path );
	}

	Record_CloseDemoWriter( &rch->rdw );
	Record_StreamReader_Close( &rch->rsr );
	Record_Free( rch );
}

/*
==================
Record_Convert_Cmd
==================
*/
void Record_Convert_Cmd( void ) {
	char path[128];

	if ( Cmd_Argc() < 2 ) {
		Record_Printf( RP_ALL, "Usage: record_convert <path within 'records' directory> <client> <instance>\n"
				"Example: record_convert source.rec 0 0\n" );
		return;
	}

	Com_sprintf( path, sizeof( path ), "records/%s", Cmd_Argv( 1 ) );
	COM_DefaultExtension( path, sizeof( path ), ".rec" );
	if ( strstr( path, ".." ) ) {
		Record_Printf( RP_ALL, "Invalid path\n" );
		return;
	}

	Record_Convert_Run( path, atoi( Cmd_Argv( 2 ) ), atoi( Cmd_Argv( 3 ) ) );
}

/* ******************************************************************************** */
// Record Scanning
/* ******************************************************************************** */

/*
==================
Record_Scan_ProcessStream
==================
*/
static void Record_Scan_ProcessStream( record_stream_reader_t *rsr ) {
	int instance_counts[RECORD_MAX_CLIENTS];

	rsr->stream.abortSet = qtrue;
	if ( setjmp( rsr->stream.abort ) ) {
		return;
	}

	Com_Memset( instance_counts, 0, sizeof( instance_counts ) );

	while ( Record_StreamReader_Advance( rsr ) ) {
		switch ( rsr->command ) {
			case RC_EVENT_CLIENT_ENTER_WORLD:
				Record_Printf( RP_ALL, "client(%i) instance(%i)\n", rsr->clientNum,
						instance_counts[rsr->clientNum] );
				++instance_counts[rsr->clientNum];
				break;
			default:
				break;
		}
	}

	rsr->stream.abortSet = qfalse;
}

/*
==================
Record_Scan_Run
==================
*/
static void Record_Scan_Run( const char *path ) {
	record_stream_reader_t *rsr = (record_stream_reader_t *)Record_Calloc( sizeof( *rsr ) );

	if ( !Record_StreamReader_Init( rsr, path ) ) {
		Record_Free( rsr );
		return;
	}

	Record_Scan_ProcessStream( rsr );

	Record_StreamReader_Close( rsr );
	Record_Free( rsr );
}

/*
==================
Record_Scan_Cmd
==================
*/
void Record_Scan_Cmd( void ) {
	char path[128];

	if ( Cmd_Argc() < 2 ) {
		Record_Printf( RP_ALL, "Usage: record_scan <path within 'records' directory>\n"
				"Example: record_scan source.rec\n" );
		return;
	}

	Com_sprintf( path, sizeof( path ), "records/%s", Cmd_Argv( 1 ) );
	COM_DefaultExtension( path, sizeof( path ), ".rec" );
	if ( strstr( path, ".." ) ) {
		Record_Printf( RP_ALL, "Invalid path\n" );
		return;
	}

	Record_Scan_Run( path );
}
