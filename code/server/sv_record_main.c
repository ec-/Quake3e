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

static qboolean recordInitialized = qfalse;

cvar_t *sv_adminSpectatorPassword;
cvar_t *sv_adminSpectatorSlots;

cvar_t *sv_recordAutoRecording;
cvar_t *sv_recordFilenameIncludeMap;
cvar_t *sv_recordFullBotData;
cvar_t *sv_recordFullUsercmdData;

cvar_t *sv_recordConvertWeptiming;
cvar_t *sv_recordConvertSimulateFollow;

cvar_t *sv_recordDebug;
cvar_t *sv_recordVerifyData;

/* ******************************************************************************** */
// Server Calls
/* ******************************************************************************** */

/*
==================
Record_ProcessUsercmd
==================
*/
void Record_ProcessUsercmd( int clientNum, usercmd_t *usercmd ) {
	if ( !recordInitialized ) {
		return;
	}
	Record_Spectator_ProcessUsercmd( clientNum, usercmd );
	Record_Writer_ProcessUsercmd( usercmd, clientNum );
}

/*
==================
Record_ProcessConfigstring
==================
*/
void Record_ProcessConfigstring( int index, const char *value ) {
	if ( !recordInitialized ) {
		return;
	}
	Record_Spectator_ProcessConfigstring( index, value );
	Record_Writer_ProcessConfigstring( index, value );
}

/*
==================
Record_ProcessServercmd
==================
*/
void Record_ProcessServercmd( int clientNum, const char *value ) {
	if ( !recordInitialized ) {
		return;
	}
	Record_Spectator_ProcessServercmd( clientNum, value );
	Record_Writer_ProcessServercmd( clientNum, value );
}

/*
==================
Record_ProcessMapLoaded
==================
*/
void Record_ProcessMapLoaded( void ) {
	if ( !recordInitialized ) {
		return;
	}
	Record_Spectator_ProcessMapLoaded();
}

/*
==================
Record_ProcessSnapshot
==================
*/
void Record_ProcessSnapshot( void ) {
	if ( !recordInitialized ) {
		return;
	}
	Record_Spectator_ProcessSnapshot();
	Record_Writer_ProcessSnapshot();
}

/*
==================
Record_ProcessGameShutdown
==================
*/
void Record_ProcessGameShutdown( void ) {
	if ( !recordInitialized ) {
		return;
	}
	Record_StopWriter();
}

/*
==================
Record_ProcessClientConnect

Returns qtrue to suppress normal handling of connection, qfalse otherwise
==================
*/
qboolean Record_ProcessClientConnect( const netadr_t *address, const char *userinfo, int challenge,
		int qport, qboolean compat ) {
	if ( !recordInitialized ) {
		return qfalse;
	}
	return Record_Spectator_ProcessConnection( address, userinfo, challenge, qport, compat );
}

/*
==================
Record_ProcessPacketEvent

Returns qtrue to suppress normal handling of packet, qfalse otherwise
==================
*/
qboolean Record_ProcessPacketEvent( const netadr_t *address, msg_t *msg, int qport ) {
	if ( !recordInitialized ) {
		return qfalse;
	}
	return Record_Spectator_ProcessPacketEvent( address, msg, qport );
}

/* ******************************************************************************** */
// Initialization
/* ******************************************************************************** */

/*
==================
Record_Initialize
==================
*/
void Record_Initialize( void ) {
	sv_adminSpectatorPassword = Cvar_Get( "sv_adminSpectatorPassword", "", 0 );
	Cvar_SetDescription( sv_adminSpectatorPassword, "Password to join game in admin spectator mode,"
			" or empty string to disable admin spectator support."
			" On the client, set 'password' to 'spect_' plus this value to join in spectator mode.");
	sv_adminSpectatorSlots = Cvar_Get( "sv_adminSpectatorSlots", "32", 0 );
	Cvar_CheckRange( sv_adminSpectatorSlots, "1", "1024", CV_INTEGER );
	Cvar_SetDescription( sv_adminSpectatorSlots, "Maximum simultaneous users in admin spectator mode." );

	sv_recordAutoRecording = Cvar_Get( "sv_recordAutoRecording", "0", 0 );
	Cvar_SetDescription( sv_recordAutoRecording, "Enables automatic server-side recording of all games." );
	sv_recordFilenameIncludeMap = Cvar_Get( "sv_recordFilenameIncludeMap", "1", 0 );
	Cvar_SetDescription( sv_recordFilenameIncludeMap, "Add map name to server side recording filenames." );
	sv_recordFullBotData = Cvar_Get( "sv_recordFullBotData", "0", 0 );
	Cvar_SetDescription( sv_recordFullBotData, "Add record data to generate demos from bot perspective." );
	sv_recordFullUsercmdData = Cvar_Get( "sv_recordFullUsercmdData", "0", 0 );
	Cvar_SetDescription( sv_recordFullUsercmdData, "Write all usercmds to record file. Normally has no effect"
			" except increasing record file size, but may be useful to advanced users." );

	sv_recordConvertWeptiming = Cvar_Get( "sv_recordConvertWeptiming", "0", 0 );
	Cvar_SetDescription( sv_recordConvertWeptiming, "Add 'firing' and 'ceased' messages to converted demo file"
			" to track fire button presses." );
	sv_recordConvertSimulateFollow = Cvar_Get( "sv_recordConvertSimulateFollow", "1", 0 );
	Cvar_SetDescription( sv_recordConvertSimulateFollow, "Add follow spectator flag to converted demo file"
			" to display 'following' message and player name on screen during replay." );

	sv_recordVerifyData = Cvar_Get( "sv_recordVerifyData", "0", 0 );
	Cvar_SetDescription( sv_recordVerifyData, "Enables extra debug checks during server-side recording." );
	sv_recordDebug = Cvar_Get( "sv_recordDebug", "0", 0 );
	Cvar_SetDescription( sv_recordDebug, "Enables additional debug prints." );

	Cmd_AddCommand( "record_start", Record_StartCmd );
	Cmd_AddCommand( "record_stop", Record_StopCmd );
	Cmd_AddCommand( "record_convert", Record_Convert_Cmd );
	Cmd_AddCommand( "record_scan", Record_Scan_Cmd );
	Cmd_AddCommand( "spect_status", Record_Spectator_PrintStatus );

	recordInitialized = qtrue;
}
