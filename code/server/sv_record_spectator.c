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
// Definitions
/* ******************************************************************************** */

typedef struct {
	playerState_t ps;
	int frameEntitiesPosition;
	record_visibility_state_t visibility;
} spectator_frame_t;

typedef struct {
	client_t cl;
	int targetClient;	// Client currently being spectated
	spectator_frame_t frames[PACKET_BACKUP];
	int lastSnapshotSvTime;
	int baselineCutoff;
	int targetFiringTime;

	// Client settings
	qboolean weptiming;
	qboolean cycleall;
} spectator_t;

#define FRAME_ENTITY_COUNT (PACKET_BACKUP * 2)

typedef struct {
	record_entityset_t currentBaselines;
	spectator_t *spectators;
	int maxSpectators;
	int frameEntitiesPosition;
	record_entityset_t frameEntities[FRAME_ENTITY_COUNT];
} spectator_system_t;

spectator_system_t *sps;

/* ******************************************************************************** */
// Command / configstring update handling
/* ******************************************************************************** */

/*
==================
Record_Spectator_AddServerCommand

Based on sv_main.c->SV_AddServerCommand
==================
*/
static void Record_Spectator_AddServerCommand( client_t *cl, const char *cmd ) {
	int index;
	++cl->reliableSequence;
	if ( cl->reliableSequence - cl->reliableAcknowledge >= MAX_RELIABLE_COMMANDS + 1 ) {
		Record_Printf( RP_DEBUG, "Record_Spectator_AddServerCommand: command overflow\n" );
		return;
	}
	index = cl->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( cl->reliableCommands[index], cmd, sizeof( cl->reliableCommands[index] ) );
}

static void QDECL Record_Spectator_AddServerCmdFmt(client_t *cl, const char *fmt, ...)
		__attribute__ ((format (printf, 2, 3)));

/*
==================
Record_Spectator_AddServerCmdFmt
==================
*/
static void QDECL Record_Spectator_AddServerCmdFmt( client_t *cl, const char *fmt, ... ) {
	va_list argptr;
	char message[MAX_STRING_CHARS];

	va_start( argptr, fmt );
	Q_vsnprintf( message, sizeof( message ), fmt, argptr );
	va_end( argptr );

	Record_Spectator_AddServerCommand( cl, message );
}

/*
==================
Record_Spectator_SendConfigstring

Based on sv_init.c->SV_SendConfigstring
==================
*/
static void Record_Spectator_SendConfigstring( client_t *cl, int index, const char *value ) {
	int maxChunkSize = MAX_STRING_CHARS - 24;
	int len = strlen( value );

	if ( len >= maxChunkSize ) {
		int sent = 0;
		int remaining = len;
		char *cmd;
		char buf[MAX_STRING_CHARS];

		while ( remaining > 0 ) {
			if ( sent == 0 ) {
				cmd = "bcs0";
			} else if ( remaining < maxChunkSize ) {
				cmd = "bcs2";
			} else {
				cmd = "bcs1";
			}
			Q_strncpyz( buf, &value[sent], maxChunkSize );

			Record_Spectator_AddServerCmdFmt( cl, "%s %i \"%s\"\n", cmd, index, buf );

			sent += ( maxChunkSize - 1 );
			remaining -= ( maxChunkSize - 1 );
		}
	} else {
		// standard cs, just send it
		Record_Spectator_AddServerCmdFmt( cl, "cs %i \"%s\"\n", index, value );
	}
}

/* ******************************************************************************** */
// Target Selection
/* ******************************************************************************** */

/*
==================
Record_TargetClientValid
==================
*/
static qboolean Record_TargetClientValid( int clientnum ) {
	if ( sv.state != SS_GAME || clientnum < 0 || clientnum > sv_maxclients->integer ||
			svs.clients[clientnum].state != CS_ACTIVE ) {
		return qfalse;
	}
	return qtrue;
}

/*
==================
Record_SelectTargetClient

Returns clientnum if valid client selected, -1 otherwise
==================
*/
static int Record_SelectTargetClient( int startIndex, qboolean cycleall ) {
	int i;
	if ( startIndex < 0 || startIndex >= sv_maxclients->integer ) {
		startIndex = 0;
	}

	for ( i = startIndex; i < startIndex + sv_maxclients->integer; ++i ) {
		int clientnum = i % sv_maxclients->integer;
		if ( !Record_TargetClientValid( clientnum ) ) {
			continue;
		}
		if ( !cycleall ) {
			if ( svs.clients[clientnum].netchan.remoteAddress.type == NA_BOT ) {
				continue;
			}
			if ( Record_PlayerstateIsSpectator( SV_GameClientNum( clientnum ) ) ) {
				continue;
			}
		}
		return clientnum;
	}

	if ( !cycleall ) {
		return Record_SelectTargetClient( startIndex, qtrue );
	}
	return -1;
}

/*
==================
Record_AdvanceTargetClient

Advances to next target client
Sets targetClient to -1 if no valid target available
==================
*/
static void Record_AdvanceTargetClient( spectator_t *spectator ) {
	int original_target = spectator->targetClient;
	spectator->targetClient = Record_SelectTargetClient( spectator->targetClient + 1, spectator->cycleall );
	if ( spectator->targetClient >= 0 && spectator->targetClient != original_target ) {
		const char *suffix = "";
		if ( Record_PlayerstateIsSpectator( SV_GameClientNum( spectator->targetClient ) ) ) {
			suffix = " [SPECT]";
		}
		if ( svs.clients[spectator->targetClient].netchan.remoteAddress.type == NA_BOT ) {
			suffix = " [BOT]";
		}

		Record_Spectator_AddServerCmdFmt( &spectator->cl, "print \"Client(%i) Name(%s^7)%s\n\"",
				spectator->targetClient, svs.clients[spectator->targetClient].name, suffix );
	}
}

/*
==================
Record_ValidateTargetClient

Advances target client if current one is invalid
Sets targetClient to -1 if no valid target available
==================
*/
static void Record_ValidateTargetClient( spectator_t *spectator ) {
	if ( !Record_TargetClientValid( spectator->targetClient ) ) {
		Record_AdvanceTargetClient( spectator );
	}
}

/* ******************************************************************************** */
// Outgoing message (gamestate/snapshot) handling
/* ******************************************************************************** */

/*
==================
Record_InitSpectatorMessage

Initializes a base message common to both gamestate and snapshot
==================
*/
static void Record_InitSpectatorMessage( client_t *cl, msg_t *msg, byte *buffer, int bufferSize ) {
	MSG_Init( msg, buffer, bufferSize );

	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( msg, cl->lastClientCommand );

	// Update server commands to client
	// The standard non-spectator function *should* be safe to use here
	SV_UpdateServerCommandsToClient( cl, msg );
}

/*
==================
Record_SendSpectatorGamestate

Based on sv_client.c->SV_SendClientGameState
==================
*/
static void Record_SendSpectatorGamestate( spectator_t *spectator ) {
	client_t *cl = &spectator->cl;
	msg_t msg;
	byte msgBuf[MAX_MSGLEN_BUF];

	if ( SVC_RateLimit( &cl->gamestate_rate, 4, 1000 ) ) {
		return;
	}

	cl->state = CS_PRIMED;

	// Note the message number to avoid further attempts to send the gamestate
	// until the client acknowledges a higher message number
	cl->gamestateMessageNum = cl->netchan.outgoingSequence;

	// Initialize message
	Record_InitSpectatorMessage( cl, &msg, msgBuf, MAX_MSGLEN );

	// Write gamestate message
	Record_WriteGamestateMessage( &sps->currentBaselines, sv.configstrings, 0, cl->reliableSequence, &msg,
			&spectator->baselineCutoff );

	// Send to client
	SV_SendMessageToClient( &msg, cl );
}

/*
==================
Record_SendSpectatorSnapshot

Based on sv_snapshot.c->SV_SendClientSnapshot
==================
*/
static void Record_SendSpectatorSnapshot( spectator_t *spectator ) {
	client_t *cl = &spectator->cl;
	msg_t msg;
	byte msg_buf[MAX_MSGLEN_BUF];
	spectator_frame_t *current_frame = &spectator->frames[cl->netchan.outgoingSequence % PACKET_BACKUP];
	spectator_frame_t *delta_frame = 0;
	int delta_frame_offset = 0;
	int snapFlags = svs.snapFlagServerBit;

	// Advance target client if current one is invalid
	Record_ValidateTargetClient( spectator );
	if ( spectator->targetClient < 0 ) {
		return;
	}

	// Store snapshot time in case it is needed to set oldServerTime on a map change
	spectator->lastSnapshotSvTime = sv.time + cl->oldServerTime;

	// Determine snapFlags
	if ( cl->state != CS_ACTIVE ) {
		snapFlags |= SNAPFLAG_NOT_ACTIVE;
	}

	// Set up current frame
	current_frame->frameEntitiesPosition = sps->frameEntitiesPosition;
	current_frame->ps = *SV_GameClientNum( spectator->targetClient );
	Record_GetCurrentVisibility( spectator->targetClient, &current_frame->visibility );

	// Tweak playerstate to indicate spectator mode
	Record_SetPlayerstateFollowFlag( &current_frame->ps );

	// Determine delta frame
	if ( cl->state == CS_ACTIVE && cl->deltaMessage > 0 ) {
		delta_frame_offset = cl->netchan.outgoingSequence - cl->deltaMessage;
		if ( delta_frame_offset > 0 && delta_frame_offset < PACKET_BACKUP - 3 ) {
			delta_frame = &spectator->frames[cl->deltaMessage % PACKET_BACKUP];
			// Make sure delta frame references valid frame entities
			// If this client skipped enough frames, the frame entities could have been overwritten
			if ( sps->frameEntitiesPosition - delta_frame->frameEntitiesPosition >= FRAME_ENTITY_COUNT ) {
				delta_frame = 0;
			}
		}
	}

	// Initialize message
	Record_InitSpectatorMessage( cl, &msg, msg_buf, MAX_MSGLEN );

	// Write snapshot message
	Record_WriteSnapshotMessage( &sps->frameEntities[current_frame->frameEntitiesPosition % FRAME_ENTITY_COUNT],
			&current_frame->visibility, &current_frame->ps,
			delta_frame ? &sps->frameEntities[delta_frame->frameEntitiesPosition % FRAME_ENTITY_COUNT] : 0,
			delta_frame ? &delta_frame->visibility : 0, delta_frame ? &delta_frame->ps : 0,
			&sps->currentBaselines, spectator->baselineCutoff, cl->lastClientCommand,
			delta_frame ? delta_frame_offset : 0, snapFlags, spectator->lastSnapshotSvTime, &msg );

	// Send to client
	SV_SendMessageToClient( &msg, cl );
}

/* ******************************************************************************** */
// Spectator client functions
/* ******************************************************************************** */

/*
==================
Record_Spectator_DropClient
==================
*/
static void Record_Spectator_DropClient( spectator_t *spectator, const char *message ) {
	client_t *cl = &spectator->cl;
	if ( cl->state == CS_FREE ) {
		return;
	}

	if ( message ) {
		Record_Spectator_AddServerCmdFmt( cl, "disconnect \"%s\"", message );

		Record_SendSpectatorSnapshot( spectator );
		while ( cl->netchan.unsentFragments || cl->netchan_start_queue ) {
			SV_Netchan_TransmitNextFragment( cl );
		}
	}

	SV_Netchan_FreeQueue( cl );
	cl->state = CS_FREE;
}

/*
==================
Record_Spectator_ProcessUserinfo

Based on sv_client.c->SV_UserinfoChanged
Currently just sets rate
==================
*/
static void Record_Spectator_ProcessUserinfo( spectator_t *spectator, const char *userinfo ) {
	spectator->cl.rate = atoi( Info_ValueForKey( userinfo, "rate" ) );
	if ( spectator->cl.rate <= 0 ) {
		spectator->cl.rate = 90000;
	} else if ( spectator->cl.rate < 5000 ) {
		spectator->cl.rate = 5000;
	} else if ( spectator->cl.rate > 90000 ) {
		spectator->cl.rate = 90000;
	}
}

/*
==================
Record_Spectator_EnterWorld

Based on sv_client.c->SV_ClientEnterWorld
Spectators don't really enter the world, but they do need some configuration
to go to CS_ACTIVE after loading the map
==================
*/
static void Record_Spectator_EnterWorld( spectator_t *spectator ) {
	client_t *cl = &spectator->cl;
	int i;

	cl->state = CS_ACTIVE;

	// Based on sv_init.c->SV_UpdateConfigstrings
	for ( i = 0; i < MAX_CONFIGSTRINGS; ++i ) {
		if ( cl->csUpdated[i] ) {
			Record_Spectator_SendConfigstring( cl, i, sv.configstrings[i] );
			cl->csUpdated[i] = qfalse;
		}
	}

	cl->deltaMessage = -1;
	cl->lastSnapshotTime = 0;
}

/*
==================
Record_Spectator_Think
==================
*/
static void Record_Spectator_Think( spectator_t *spectator, usercmd_t *cmd ) {
	client_t *cl = &spectator->cl;
	if ( Record_UsercmdIsFiringWeapon( cmd ) && !Record_UsercmdIsFiringWeapon( &cl->lastUsercmd ) ) {
		Record_AdvanceTargetClient( spectator );
	}
}

/*
==================
Record_Spectator_Move

Based on sv_client.c->SV_UserMove
==================
*/
static void Record_Spectator_Move( spectator_t *spectator, msg_t *msg, qboolean delta ) {
	client_t *cl = &spectator->cl;
	int i;
	int key;
	int cmdCount;
	usercmd_t nullcmd;
	usercmd_t cmds[MAX_PACKET_USERCMDS];
	usercmd_t *cmd, *oldcmd;

	if ( delta ) {
		cl->deltaMessage = cl->messageAcknowledge;
	} else {
		cl->deltaMessage = -1;
	}

	cmdCount = MSG_ReadByte( msg );
	if ( cmdCount < 1 || cmdCount > MAX_PACKET_USERCMDS ) {
		Record_Printf( RP_DEBUG, "Record_Spectator_Move: invalid spectator cmdCount\n" );
		return;
	}

	// use the checksum feed in the key
	key = 0;
	// also use the message acknowledge
	key ^= cl->messageAcknowledge;
	// also use the last acknowledged server command in the key
	key ^= MSG_HashKey(cl->reliableCommands[ cl->reliableAcknowledge & (MAX_RELIABLE_COMMANDS-1) ], 32);

	Com_Memset( &nullcmd, 0, sizeof( nullcmd ) );
	oldcmd = &nullcmd;
	for ( i = 0; i < cmdCount; i++ ) {
		cmd = &cmds[i];
		MSG_ReadDeltaUsercmdKey( msg, key, oldcmd, cmd );
		oldcmd = cmd;
	}

	if ( cl->state == CS_PRIMED ) {
		Record_Spectator_EnterWorld( spectator );
	}

	// Handle sv.time reset on map restart etc.
	if ( cl->lastUsercmd.serverTime > sv.time ) {
		cl->lastUsercmd.serverTime = 0;
	}

	for ( i = 0; i < cmdCount; ++i ) {
		if ( cmds[i].serverTime > cmds[cmdCount - 1].serverTime ) {
			continue;
		}
		if ( cmds[i].serverTime <= cl->lastUsercmd.serverTime ) {
			continue;
		}
		Record_Spectator_Think( spectator, &cmds[i] );
		cl->lastUsercmd = cmds[i];
	}
}

/*
==================
Record_Spectator_ProcessBooleanSetting
==================
*/
static void Record_Spectator_ProcessBooleanSetting( spectator_t *spectator, const char *setting_name, qboolean *target ) {
	if ( !Q_stricmp( Cmd_Argv( 1 ), "0" ) ) {
		Record_Spectator_AddServerCmdFmt( &spectator->cl, "print \"%s disabled\n\"", setting_name );
		*target = qfalse;
	} else if ( !Q_stricmp( Cmd_Argv( 1 ), "1" ) ) {
		Record_Spectator_AddServerCmdFmt( &spectator->cl, "print \"%s enabled\n\"", setting_name );
		*target = qtrue;
	} else
		Record_Spectator_AddServerCmdFmt( &spectator->cl, "print \"Usage: '%s 0' or '%s 1'\n\"",
				setting_name, setting_name );
}

/*
==================
Record_Spectator_ProcessCommand

Based on sv_client.c->SV_ClientCommand
==================
*/
static void Record_Spectator_ProcessCommand( spectator_t *spectator, msg_t *msg ) {
	client_t *cl = &spectator->cl;
	int seq = MSG_ReadLong( msg );
	const char *cmd = MSG_ReadString( msg );

	if ( cl->lastClientCommand >= seq ) {
		// Command already executed
		return;
	}

	if ( seq > cl->lastClientCommand + 1 ) {
		// Command lost error
		Record_Printf( RP_ALL, "Spectator %i lost client commands\n", (int)( spectator - sps->spectators ) );
		Record_Spectator_DropClient( spectator, "Lost reliable commands" );
		return;
	}

	Record_Printf( RP_DEBUG, "Have spectator command: %s\n", cmd );
	cl->lastClientCommand = seq;
	Q_strncpyz( cl->lastClientCommandString, cmd, sizeof( cl->lastClientCommandString ) );

	Cmd_TokenizeString( cmd );
	if ( !Q_stricmp( Cmd_Argv( 0 ), "disconnect" ) ) {
		Record_Printf( RP_ALL, "Spectator %i disconnected\n", (int)( spectator - sps->spectators ) );
		Record_Spectator_DropClient( spectator, "disconnected" );
		return;
	} else if ( !Q_stricmp( Cmd_Argv( 0 ), "weptiming" ) ) {
		Record_Spectator_ProcessBooleanSetting( spectator, "weptiming", &spectator->weptiming );
	} else if ( !Q_stricmp( Cmd_Argv( 0 ), "cycleall" ) ) {
		Record_Spectator_ProcessBooleanSetting( spectator, "cycleall", &spectator->cycleall );
	} else if ( !Q_stricmp( Cmd_Argv( 0 ), "help" ) ) {
		Record_Spectator_AddServerCommand( cl, "print \"Commands:\nweptiming - Enables or disables"
				" weapon firing prints\ncycleall - Enables or disables selecting bot and spectator"
				" target clients\n\"" );
	} else if ( !Q_stricmp( Cmd_Argv( 0 ), "userinfo" ) ) {
		Record_Spectator_ProcessUserinfo( spectator, Cmd_Argv( 1 ) );
	}
}

/*
==================
Record_Spectator_ProcessMessage

Based on sv_client.c->SV_ExecuteClientMessage
==================
*/
static void Record_Spectator_ProcessMessage( spectator_t *spectator, msg_t *msg ) {
	client_t *cl = &spectator->cl;
	int serverId;
	int cmd;

	MSG_Bitstream( msg );

	serverId = MSG_ReadLong( msg );
	cl->messageAcknowledge = MSG_ReadLong( msg );
	if ( cl->netchan.outgoingSequence - cl->messageAcknowledge <= 0 ) {
		Record_Printf( RP_DEBUG, "Invalid messageAcknowledge" );
		return;
	}

	cl->reliableAcknowledge = MSG_ReadLong( msg );
	if ( cl->reliableSequence - cl->reliableAcknowledge < 0 ||
			cl->reliableSequence - cl->reliableAcknowledge > MAX_RELIABLE_COMMANDS ) {
		Record_Printf( RP_DEBUG, "Invalid reliableAcknowledge" );
		cl->reliableAcknowledge = cl->reliableSequence;
	}

	if ( serverId < sv.restartedServerId || serverId > sv.serverId ) {
		// Pre map change serverID, or invalid high serverID
		if ( cl->messageAcknowledge > cl->gamestateMessageNum ) {
			// No previous gamestate waiting to be acknowledged - send new one
			Record_SendSpectatorGamestate( spectator );
		}
		return;
	}

	// No need to send old servertime once an up-to-date gamestate is acknowledged
	cl->oldServerTime = 0;

	// Read optional client command strings
	while ( 1 ) {
		cmd = MSG_ReadByte( msg );

		if ( cmd == clc_EOF ) {
			return;
		}
		if ( cmd != clc_clientCommand ) {
			break;
		}
		Record_Spectator_ProcessCommand( spectator, msg );

		// In case command resulted in error/disconnection
		if ( cl->state < CS_CONNECTED ) {
			return;
		}
	}

	// Process move commands
	if ( cmd == clc_move ) {
		Record_Spectator_Move( spectator, msg, qtrue );
	} else if ( cmd == clc_moveNoDelta ) {
		Record_Spectator_Move( spectator, msg, qfalse );
	} else {
		Record_Printf( RP_DEBUG, "Record_Spectator_ProcessMessage: invalid spectator command byte\n" );
	}
}

/* ******************************************************************************** */
// Spectator system initialization/allocation
/* ******************************************************************************** */

/*
==================
Record_Spectator_Init
==================
*/
static void Record_Spectator_Init( int maxSpectators ) {
	sps = (spectator_system_t *)Record_Calloc( sizeof( *sps ) );
	sps->spectators = (spectator_t *)Record_Calloc( sizeof( *sps->spectators ) * maxSpectators );
	sps->maxSpectators = maxSpectators;
	Record_GetCurrentBaselines( &sps->currentBaselines );
}

/*
==================
Record_Spectator_Shutdown
==================
*/
static void Record_Spectator_Shutdown( void ) {
	Record_Free( sps->spectators );
	Record_Free( sps );
	sps = 0;
}

/*
==================
Record_Spectator_AllocateClient

Returns either reused or new spectator on success, or null if all slots in use
Allocated structure will not have zeroed memory
==================
*/
static spectator_t *Record_Spectator_AllocateClient( const netadr_t *address, int qport ) {
	int i;
	spectator_t *avail = 0;
	if ( !sps ) {
		Record_Spectator_Init( sv_adminSpectatorSlots->integer );
	}
	for ( i = 0; i < sps->maxSpectators; ++i ) {
		if ( sps->spectators[i].cl.state == CS_FREE ) {
			if ( !avail ) {
				avail = &sps->spectators[i];
			}
		}
		else if ( NET_CompareBaseAdr( address, &sps->spectators[i].cl.netchan.remoteAddress )
				&& ( sps->spectators[i].cl.netchan.qport == qport ||
				address->port == sps->spectators[i].cl.netchan.remoteAddress.port ) ) {
			Record_Spectator_DropClient( &sps->spectators[i], 0 );
			return &sps->spectators[i];
		}
	}
	return avail;
}

/* ******************************************************************************** */
// Exported functions
/* ******************************************************************************** */

/*
==================
Record_Spectator_PrintStatus
==================
*/
void Record_Spectator_PrintStatus( void ) {
	int i;
	if ( !sps ) {
		Record_Printf( RP_ALL, "No spectators; spectator system not running\n" );
		return;
	}

	for ( i = 0; i < sps->maxSpectators; ++i ) {
		client_t *cl = &sps->spectators[i].cl;
		const char *state = "unknown";
		if ( cl->state == CS_FREE ) {
			continue;
		}

		if ( cl->state == CS_CONNECTED ) {
			state = "connected";
		} else if ( cl->state == CS_PRIMED ) {
			state = "primed";
		} else if ( cl->state == CS_ACTIVE ) {
			state = "active";
		}

		Record_Printf( RP_ALL, "num(%i) address(%s) state(%s) lastmsg(%i) rate(%i)\n", i,
				NET_AdrToString( &cl->netchan.remoteAddress ), state, svs.time - cl->lastPacketTime, cl->rate );
	}
}

/*
==================
Record_Spectator_ProcessSnapshot
==================
*/
void Record_Spectator_ProcessSnapshot( void ) {
	int i;
	qboolean active = qfalse;
	if ( !sps ) {
		return;
	}

	// Add current entities to entity buffer
	Record_GetCurrentEntities( &sps->frameEntities[++sps->frameEntitiesPosition % FRAME_ENTITY_COUNT] );

	// Based on sv_snapshot.c->SV_SendClientMessages
	for ( i = 0; i < sps->maxSpectators; ++i ) {
		client_t *cl = &sps->spectators[i].cl;
		if ( cl->state == CS_FREE ) {
			continue;
		}
		active = qtrue;

		if ( cl->lastPacketTime > svs.time ) {
			cl->lastPacketTime = svs.time;
		}
		if ( svs.time - cl->lastPacketTime > 60000 ) {
			Record_Printf( RP_ALL, "Spectator %i timed out\n", i );
			Record_Spectator_DropClient( &sps->spectators[i], "timed out" );
			continue;
		}

		if ( cl->netchan.unsentFragments || cl->netchan_start_queue ) {
			SV_Netchan_TransmitNextFragment( cl );
			cl->rateDelayed = qtrue;
			continue;
		}

		// SV_RateMsec appears safe to call
		if ( SV_RateMsec( cl ) > 0 ) {
			cl->rateDelayed = qtrue;
			continue;
		}

		Record_SendSpectatorSnapshot( &sps->spectators[i] );
		cl->lastSnapshotTime = svs.time;
		cl->rateDelayed = qfalse;
	}

	if ( !active ) {
		// No active spectators; free spectator system to save memory
		Record_Spectator_Shutdown();
	}
}

/*
==================
Record_Spectator_ProcessConnection

Returns qtrue to suppress normal handling of connection, qfalse otherwise
==================
*/
qboolean Record_Spectator_ProcessConnection( const netadr_t *address, const char *userinfo, int challenge,
		int qport, qboolean compat ) {
	spectator_t *spectator;
	const char *password = Info_ValueForKey( userinfo, "password" );
	if ( Q_stricmpn( password, "spect_", 6 ) ) {
		return qfalse;
	}

	if ( !*sv_adminSpectatorPassword->string ) {
		NET_OutOfBandPrint( NS_SERVER, address, "print\nSpectator mode not enabled on this server.\n" );
		return qtrue;
	}

	if ( strcmp( password + 6, sv_adminSpectatorPassword->string ) ) {
		NET_OutOfBandPrint( NS_SERVER, address, "print\nIncorrect spectator password.\n" );
		return qtrue;
	}

	spectator = Record_Spectator_AllocateClient( address, qport );
	if ( !spectator ) {
		Record_Printf( RP_ALL, "Failed to allocate spectator slot.\n" );
		NET_OutOfBandPrint( NS_SERVER, address, "print\nSpectator slots full.\n" );
		return qtrue;
	}

	// Perform initializations from sv_client.c->SV_DirectConnect
	Com_Memset( spectator, 0, sizeof( *spectator ) );
	spectator->targetClient = -1;
	spectator->cl.challenge = challenge;
	spectator->cl.compat = compat;
	Netchan_Setup( NS_SERVER, &spectator->cl.netchan, address, qport, spectator->cl.challenge, compat );
	spectator->cl.netchan_end_queue = &spectator->cl.netchan_start_queue;
	NET_OutOfBandPrint( NS_SERVER, address, "connectResponse %d", spectator->cl.challenge );
	spectator->cl.lastPacketTime = svs.time;
	spectator->cl.gamestateMessageNum = -1;
	spectator->cl.state = CS_CONNECTED;
	Record_Spectator_ProcessUserinfo( spectator, userinfo );

	Record_Spectator_AddServerCommand( &spectator->cl, "print \"Spectator mode enabled - type /help for options\n\"" );
	Record_Printf( RP_ALL, "Spectator %i connected from %s\n", (int)( spectator - sps->spectators ), NET_AdrToString( address ) );

	return qtrue;
}

/*
==================
Record_Spectator_ProcessPacketEvent

Returns qtrue to suppress normal handling of packet, qfalse otherwise
Based on sv_main.c->SV_PacketEvent
==================
*/
qboolean Record_Spectator_ProcessPacketEvent( const netadr_t *address, msg_t *msg, int qport ) {
	int i;
	if ( !sps ) {
		return qfalse;
	}

	for ( i = 0; i < sps->maxSpectators; ++i ) {
		client_t *cl = &sps->spectators[i].cl;
		if ( cl->state == CS_FREE ) {
			continue;
		}
		if ( !NET_CompareBaseAdr( address, &cl->netchan.remoteAddress ) || cl->netchan.qport != qport ) {
			continue;
		}

		cl->netchan.remoteAddress.port = address->port;
		if ( SV_Netchan_Process( cl, msg ) ) {
			if ( cl->state != CS_ZOMBIE ) {
				cl->lastPacketTime = svs.time; // don't timeout
				Record_Spectator_ProcessMessage( &sps->spectators[i], msg );
			}
		}
		return qtrue;
	}

	return qfalse;
}

/*
==================
Record_Spectator_ProcessMapLoaded
==================
*/
void Record_Spectator_ProcessMapLoaded( void ) {
	int i;
	if ( !sps ) {
		return;
	}

	// Update current baselines
	Record_GetCurrentBaselines( &sps->currentBaselines );

	for ( i = 0; i < sps->maxSpectators; ++i ) {
		client_t *cl = &sps->spectators[i].cl;
		if ( cl->state >= CS_CONNECTED ) {
			cl->state = CS_CONNECTED;
			cl->oldServerTime = sps->spectators[i].lastSnapshotSvTime;
		}
	}
}

/*
==================
Record_Spectator_ProcessConfigstring
==================
*/
void Record_Spectator_ProcessConfigstring( int index, const char *value ) {
	int i;
	if ( !sps ) {
		return;
	}

	// Based on sv_init.c->SV_SetConfigstring
	if ( sv.state == SS_GAME || sv.restarting ) {
		for ( i = 0; i < sps->maxSpectators; ++i ) {
			client_t *cl = &sps->spectators[i].cl;
			if ( cl->state == CS_ACTIVE ) {
				Record_Spectator_SendConfigstring( cl, index, value );
			} else {
				cl->csUpdated[index] = qtrue;
			}
		}
	}
}

/*
==================
Record_Spectator_ProcessServercmd
==================
*/
void Record_Spectator_ProcessServercmd( int clientNum, const char *value ) {
	int i;
	if ( !sps ) {
		return;
	}

	if ( !Q_stricmpn( value, "cs ", 3 ) || !Q_stricmpn( value, "bcs0 ", 5 ) || !Q_stricmpn( value, "bcs1 ", 5 ) ||
			!Q_stricmpn( value, "bcs2 ", 5 ) || !Q_stricmpn( value, "disconnect ", 11 ) ) {
		// Skip configstring updates because they are handled separately
		// Also don't cause the spectator to disconnect when the followed client gets a disconnect command
		return;
	}

	for ( i = 0; i < sps->maxSpectators; ++i ) {
		client_t *cl = &sps->spectators[i].cl;
		if ( cl->state != CS_ACTIVE ) {
			continue;
		}
		if ( sps->spectators[i].targetClient == clientNum ) {
			Record_Spectator_AddServerCommand( cl, value );
		}
	}
}

/*
==================
Record_Spectator_ProcessUsercmd
==================
*/
void Record_Spectator_ProcessUsercmd( int clientNum, usercmd_t *usercmd ) {
	int i;
	if ( !sps ) {
		return;
	}

	for ( i = 0; i < sps->maxSpectators; ++i ) {
		// Send firing/ceased messages to spectators following this client with weptiming enabled
		client_t *cl = &sps->spectators[i].cl;
		if ( cl->state != CS_ACTIVE ) {
			continue;
		}
		if ( sps->spectators[i].targetClient != clientNum ) {
			continue;
		}

		if ( Record_UsercmdIsFiringWeapon( usercmd ) ) {
			if ( !sps->spectators[i].targetFiringTime ) {
				if ( sps->spectators[i].weptiming ) {
					Record_Spectator_AddServerCommand( cl, "print \"Firing\n\"" );
				}
				sps->spectators[i].targetFiringTime = usercmd->serverTime;
			}
		} else {
			if ( sps->spectators[i].targetFiringTime ) {
				if ( sps->spectators[i].weptiming ) {
					Record_Spectator_AddServerCmdFmt( cl, "print \"Ceased %i\n\"",
							usercmd->serverTime - sps->spectators[i].targetFiringTime );
				}
				sps->spectators[i].targetFiringTime = 0;
			}
		}
	}
}
