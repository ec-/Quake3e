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
// sv_client.c -- server code for dealing with clients

#include "server.h"

static void SV_CloseDownload( client_t *cl );

//
// Server-side Stateless Challenges
// backported from https://github.com/JACoders/OpenJK/pull/832
//

#define TS_SHIFT 14 // ~16 seconds to reply to the challenge

/*
=================
SV_CreateChallenge

Create an unforgeable, temporal challenge for the given client address
=================
*/
static int SV_CreateChallenge( int timestamp, const netadr_t *from )
{
	int challenge;

	// Create an unforgeable, temporal challenge for this client using HMAC(secretKey, clientParams + timestamp)
	// Use first 4 bytes of the HMAC digest as an int (client only deals with numeric challenges)
	// The most-significant bit stores whether the timestamp is odd or even. This lets later verification code handle the
	// case where the engine timestamp has incremented between the time this challenge is sent and the client replies.
	challenge = Com_MD5Addr( from, timestamp );
	challenge &= 0x7FFFFFFF;
	challenge |= (unsigned int)(timestamp & 0x1) << 31;

	return challenge;
}


/*
=================
SV_CreateChallenge

Verify a challenge received by the client matches the expected challenge
=================
*/
static qboolean SV_VerifyChallenge( int receivedChallenge, const netadr_t *from )
{
	int currentTimestamp = svs.time >> TS_SHIFT;
	int currentPeriod = currentTimestamp & 0x1;

	// Use the current timestamp for verification if the current period matches the client challenge's period.
	// Otherwise, use the previous timestamp in case the current timestamp incremented in the time between the
	// client being sent a challenge and the client's reply that's being verified now.
	int challengePeriod = ((unsigned int)receivedChallenge >> 31) & 0x1;
	int challengeTimestamp = currentTimestamp - ( currentPeriod ^ challengePeriod );

	int expectedChallenge = SV_CreateChallenge( challengeTimestamp, from );

	return (receivedChallenge == expectedChallenge) ? qtrue : qfalse;
}


/*
=================
SV_InitChallenger
=================
*/
void SV_InitChallenger( void )
{
	Com_MD5Init();
}


/*
=================
SV_GetChallenge

A "getchallenge" OOB command has been received
Returns a challenge number that can be used
in a subsequent connectResponse command.
We do this to prevent denial of service attacks that
flood the server with invalid connection IPs.  With a
challenge, they must give a valid IP address.

If we are authorizing, a challenge request will cause a packet
to be sent to the authorize server.

When an authorizeip is returned, a challenge response will be
sent to that ip.

ioquake3: we added a possibility for clients to add a challenge
to their packets, to make it more difficult for malicious servers
to hi-jack client connections.
Also, the auth stuff is completely disabled for com_standalone games
as well as IPv6 connections, since there is no way to use the
v4-only auth server for these new types of connections.
=================
*/
void SV_GetChallenge( const netadr_t *from ) {
	int		challenge;
	int		clientChallenge;

	// ignore if we are in single player
#ifndef DEDICATED
	if ( Cvar_VariableIntegerValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableIntegerValue("ui_singlePlayerActive")) {
		return;
	}
#endif

	// Prevent using getchallenge as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SV_GetChallenge: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	// Create a unique challenge for this client without storing state on the server
	challenge = SV_CreateChallenge( svs.time >> TS_SHIFT, from );
	
	if ( Cmd_Argc() < 2 ) {
		// legacy client query, don't send unneeded information
		NET_OutOfBandPrint( NS_SERVER, from, "challengeResponse %i", challenge );
	} else {
		// Grab the client's challenge to echo back (if given)
		clientChallenge = atoi( Cmd_Argv( 1 ) );

		NET_OutOfBandPrint( NS_SERVER, from, "challengeResponse %i %i %i",
			challenge, clientChallenge, NEW_PROTOCOL_VERSION );
	}
}


/*
==================
SV_IsBanned

Check whether a certain address is banned
==================
*/
#ifdef USE_BANS

static qboolean SV_IsBanned( const netadr_t *from, qboolean isexception )
{
	int index;
	serverBan_t *curban;
	
	if(!isexception)
	{
		// If this is a query for a ban, first check whether the client is excepted
		if(SV_IsBanned(from, qtrue))
			return qfalse;
	}
	
	for(index = 0; index < serverBansCount; index++)
	{
		curban = &serverBans[index];
		
		if(curban->isexception == isexception)
		{
			if(NET_CompareBaseAdrMask(&curban->ip, from, curban->subnet))
				return qtrue;
		}
	}
	
	return qfalse;
}
#endif


/*
==================
SV_DirectConnect

A "connect" OOB command has been received
==================
*/
void SV_DirectConnect( const netadr_t *from ) {
	static		leakyBucket_t bucket;
	char		userinfo[MAX_INFO_STRING];
	int			i, n;
	client_t	*cl, *newcl;
	//sharedEntity_t *ent;
	int			clientNum;
	int			version;
	int			qport;
	int			challenge;
	char		*password;
	int			startIndex;
	intptr_t	denied;
	int			count;
	const char	*ip, *info, *v;
	qboolean	compat = qfalse;
	qboolean	longstr;

	Com_DPrintf( "SVC_DirectConnect()\n" );

#ifdef USE_BANS
	// Check whether this client is banned.
	if(SV_IsBanned(from, qfalse))
	{
		NET_OutOfBandPrint(NS_SERVER, &from, "print\nYou are banned from this server.\n");
		return;
	}
#endif

	// Prevent using connect as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SV_DirectConnect: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	// check for concurrent connections
	for ( i = 0, n = 0; i < sv_maxclients->integer; i++ ) {
		const netadr_t *addr = &svs.clients[ i ].netchan.remoteAddress;
		if ( addr->type != NA_BOT && NET_CompareBaseAdr( addr, from ) ) {
			if ( svs.clients[ i ].state >= CS_CONNECTED && !svs.clients[ i ].justConnected ) {
				if ( ++n >= sv_maxconcurrent->integer ) {
					// avoid excessive outgoing traffic
					if ( !SVC_RateLimit( &bucket, 10, 200 ) ) {
						NET_OutOfBandPrint( NS_SERVER, from, "print\nToo many connections.\n" );
					}
					return;
				}
			}
		}
	}

	// verify challenge in first place
	info = Cmd_Argv( 1 );
	v = Info_ValueForKey( info, "challenge" );
	if ( *v == '\0' )
	{
		if ( !SVC_RateLimit( &bucket, 10, 200 ) )
		{
			NET_OutOfBandPrint( NS_SERVER, from, "print\nMissing challenge in userinfo.\n" );
		}
		return;
	}
	challenge = atoi( v );

	// see if the challenge is valid (localhost clients don't need to challenge)
	if ( !NET_IsLocalAddress( from ) )
	{
		// Verify the received challenge against the expected challenge
		if ( !SV_VerifyChallenge( challenge, from ) )
		{
			// avoid excessive outgoing traffic
			if ( !SVC_RateLimit( &bucket, 10, 200 ) )
			{
				NET_OutOfBandPrint( NS_SERVER, from, "print\nIncorrect challenge, please reconnect.\n" );
			}
			return;
		}
	}

	Q_strncpyz( userinfo, info, sizeof( userinfo ) );

	v = Info_ValueForKey( userinfo, "protocol" );
	if ( *v == '\0' )
	{
		if ( !SVC_RateLimit( &bucket, 10, 200 ) )
		{
			NET_OutOfBandPrint( NS_SERVER, from, "print\nMissing protocol in userinfo.\n" );
		}
		return;
	}
	version = atoi( v );
	
	if ( version == PROTOCOL_VERSION )
		compat = qtrue;
	else
	{
		if ( version != NEW_PROTOCOL_VERSION )
		{
			// avoid excessive outgoing traffic
			if ( !SVC_RateLimit( &bucket, 10, 200 ) )
			{
				NET_OutOfBandPrint( NS_SERVER, from, "print\nServer uses protocol version %i "
					"(yours is %i).\n", NEW_PROTOCOL_VERSION, version );
			}
			Com_DPrintf( "    rejected connect from version %i\n", version );
			return;
		}
	}

	v = Info_ValueForKey( userinfo, "qport" );
	if ( *v == '\0' )
	{
		if ( !SVC_RateLimit( &bucket, 10, 200 ) )
		{
			NET_OutOfBandPrint( NS_SERVER, from, "print\nMissing qport in userinfo.\n" );
		}
		return;
	}
	qport = atoi( Info_ValueForKey( userinfo, "qport" ) );

	// if "client" is present in userinfo and it is a modern client
	// then assume it can properly decode long strings
	if ( !compat && *Info_ValueForKey( userinfo, "client" ) != '\0' )
		longstr = qtrue;
	else
		longstr = qfalse;

	// we don't need these keys after connection, release some space in userinfo
	Info_RemoveKey( userinfo, "challenge" );
	Info_RemoveKey( userinfo, "qport" );
	Info_RemoveKey( userinfo, "protocol" );
	Info_RemoveKey( userinfo, "client" );

	// don't let "ip" overflow userinfo string
	if ( NET_IsLocalAddress( from ) )
		ip = "localhost";
	else
		ip = NET_AdrToString( from );

	if ( !Info_SetValueForKey( userinfo, "ip", ip ) ) {
		// avoid excessive outgoing traffic
		if ( !SVC_RateLimit( &bucket, 10, 200 ) ) {
			NET_OutOfBandPrint( NS_SERVER, from, "print\nUserinfo string length exceeded.  "
				"Try removing setu cvars from your config.\n" );
		}
		return;
	}

	// restore burst capacity
	SVC_RateRestoreBurstAddress( from, 10, 1000 );

	// quick reject
	newcl = NULL;
	for ( i = 0, cl = svs.clients ; i < sv_maxclients->integer ; i++, cl++ ) {
		if ( NET_CompareAdr( from, &cl->netchan.remoteAddress ) ) {
			int elapsed = svs.time - cl->lastConnectTime;
			if ( elapsed < ( sv_reconnectlimit->integer * 1000 ) && elapsed >= 0 ) {
				int remains = ( ( sv_reconnectlimit->integer * 1000 ) - elapsed + 999 ) / 1000;
				if ( com_developer->integer ) {
					Com_Printf( "%s:reconnect rejected : too soon\n", NET_AdrToString( from ) );
				}
				// avoid excessive outgoing traffic
				if ( !SVC_RateLimit( &bucket, 10, 200 ) ) {
					NET_OutOfBandPrint( NS_SERVER, from, "print\nReconnecting, please wait %i second%s.\n",
						remains, (remains != 1) ? "s" : "" );
				}
				return;
			}
			newcl = cl; // we may reuse this slot
			break;
		}
	}

	// if there is already a slot for this ip, reuse it
	for ( i = 0, cl = svs.clients ; i < sv_maxclients->integer ; i++, cl++ ) {
		if ( cl->state == CS_FREE ) {
			continue;
		}
		if ( NET_CompareAdr( from, &cl->netchan.remoteAddress ) && cl->netchan.qport == qport ) {
			// both qport and netport should match for a reconnecting client
			Com_Printf( "%s:reconnect\n", NET_AdrToString( from ) );
			newcl = cl;

			// this doesn't work because it nukes the players userinfo

//			// disconnect the client from the game first so any flags the
//			// player might have are dropped
//			VM_Call( gvm, GAME_CLIENT_DISCONNECT, newcl - svs.clients );
			//
			goto gotnewcl;
		}
	}

	// find a client slot
	// if "sv_privateClients" is set > 0, then that number
	// of client slots will be reserved for connections that
	// have "password" set to the value of "sv_privatePassword"
	// Info requests will report the maxclients as if the private
	// slots didn't exist, to prevent people from trying to connect
	// to a full server.
	// This is to allow us to reserve a couple slots here on our
	// servers so we can play without having to kick people.

	// check for privateClient password
	password = Info_ValueForKey( userinfo, "password" );
	if ( *password && !strcmp( password, sv_privatePassword->string ) ) {
		startIndex = 0;
	} else {
		// skip past the reserved slots
		startIndex = sv_privateClients->integer;
	}

	if ( newcl && newcl >= svs.clients + startIndex && newcl->state == CS_FREE ) {
		Com_Printf( "%s: reuse slot %i\n", NET_AdrToString( from ), (int)(newcl - svs.clients) );
		goto gotnewcl;
	}

	newcl = NULL;
	for ( i = startIndex; i < sv_maxclients->integer ; i++ ) {
		cl = &svs.clients[i];
		if (cl->state == CS_FREE) {
			newcl = cl;
			break;
		}
	}

	if ( !newcl ) {
		if ( NET_IsLocalAddress( from ) ) {
			count = 0;
			for ( i = startIndex; i < sv_maxclients->integer ; i++ ) {
				cl = &svs.clients[i];
				if (cl->netchan.remoteAddress.type == NA_BOT) {
					count++;
				}
			}
			// if they're all bots
			if (count >= sv_maxclients->integer - startIndex) {
				SV_DropClient(&svs.clients[sv_maxclients->integer - 1], "only bots on server");
				newcl = &svs.clients[sv_maxclients->integer - 1];
			}
			else {
				Com_Error( ERR_DROP, "server is full on local connect" );
				return;
			}
		}
		else {
			NET_OutOfBandPrint( NS_SERVER, from, "print\nServer is full.\n" );
			Com_DPrintf ("Rejected a connection.\n");
			return;
		}
	}

gotnewcl:	
	// build a new connection
	// accept the new client
	// this is the only place a client_t is ever initialized
	// we got a newcl, so reset the reliableSequence and reliableAcknowledge
	Com_Memset( newcl, 0, sizeof( *newcl ) );
	clientNum = newcl - svs.clients;
#if 0 // skip this until CS_PRIMED
	//ent = SV_GentityNum( clientNum );
	//newcl->gentity = ent;
#endif

	// save the challenge
	newcl->challenge = challenge;

	// save the address
	newcl->compat = compat;
	Netchan_Setup( NS_SERVER, &newcl->netchan, from, qport, challenge, compat );

	// init the netchan queue
	newcl->netchan_end_queue = &newcl->netchan_start_queue;

	// save the userinfo
	Q_strncpyz( newcl->userinfo, userinfo, sizeof(newcl->userinfo) );

	newcl->longstr = longstr;

	SV_UserinfoChanged( newcl, qtrue );

	// get the game a chance to reject this connection or modify the userinfo
	denied = VM_Call( gvm, GAME_CLIENT_CONNECT, clientNum, qtrue, qfalse ); // firstTime = qtrue
	if ( denied ) {
		// we can't just use VM_ArgPtr, because that is only valid inside a VM_Call
		const char *str = GVM_ArgPtr( denied );

		NET_OutOfBandPrint( NS_SERVER, from, "print\n%s\n", str );
		Com_DPrintf( "Game rejected a connection: %s.\n", str );
		return;
	}

	// send the connect packet to the client
	NET_OutOfBandPrint( NS_SERVER, from, "connectResponse %d", challenge );

	Com_DPrintf( "Going from CS_FREE to CS_CONNECTED for %s\n", newcl->name );

	newcl->state = CS_CONNECTED;
	newcl->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
	newcl->lastPacketTime = svs.time;
	newcl->lastConnectTime = svs.time;

	SVC_RateRestoreToxicAddress( &newcl->netchan.remoteAddress, 10, 1000 );
	newcl->justConnected = qtrue;

	// when we receive the first packet from the client, we will
	// notice that it is from a different serverid and that the
	// gamestate message was not just sent, forcing a retransmit
	newcl->gamestateMessageNum = -1;

	// if this was the first client on the server, or the last client
	// the server can hold, send a heartbeat to the master.
	count = 0;
	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			count++;
		}
	}
	if ( count == 1 || count == sv_maxclients->integer ) {
		SV_Heartbeat_f();
	}
}


/*
=====================
SV_FreeClient

Destructor for data allocated in a client structure
=====================
*/
void SV_FreeClient(client_t *client)
{
	SV_Netchan_FreeQueue(client);
	SV_CloseDownload(client);
}


/*
=====================
SV_DropClient

Called when the player is totally leaving the server, either willingly
or unwillingly.  This is NOT called if the entire server is quiting
or crashing -- SV_FinalMessage() will handle that
=====================
*/
void SV_DropClient( client_t *drop, const char *reason ) {
	char	name[ MAX_NAME_LENGTH ];
	qboolean isBot;
	int		i;

	if ( drop->state == CS_ZOMBIE ) {
		return;		// already dropped
	}

	isBot = drop->netchan.remoteAddress.type == NA_BOT;

	Q_strncpyz( name, drop->name, sizeof( name ) );	// for further DPrintf() because drop->name will be nuked in SV_SetUserinfo()

	// Free all allocated data on the client structure
	SV_FreeClient( drop );

	// tell everyone why they got dropped
	if ( reason ) {
		SV_SendServerCommand( NULL, "print \"%s" S_COLOR_WHITE " %s\n\"", name, reason );
	}

	// call the prog function for removing a client
	// this will remove the body, among other things
	VM_Call( gvm, GAME_CLIENT_DISCONNECT, drop - svs.clients );

	// add the disconnect command
	if ( reason ) {
		SV_SendServerCommand( drop, "disconnect \"%s\"", reason );
	}

	if ( isBot ) {
		SV_BotFreeClient( drop - svs.clients );
	}

	// nuke user info
	SV_SetUserinfo( drop - svs.clients, "" );

	drop->justConnected = qfalse;

	if ( isBot ) {
		// bots shouldn't go zombie, as there's no real net connection.
		drop->state = CS_FREE;
	} else {
		Com_DPrintf( "Going to CS_ZOMBIE for %s\n", name );
		drop->state = CS_ZOMBIE;		// become free in a few seconds
	}

	if ( !reason ) {
		return;
	}

	// if this was the last client on the server, send a heartbeat
	// to the master so it is known the server is empty
	// send a heartbeat now so the master will get up to date info
	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			break;
		}
	}
	if ( i == sv_maxclients->integer ) {
		SV_Heartbeat_f();
	}
}


/*
================
SV_RemainingGameState

estimates free space available for additional systeminfo keys
================
*/
int SV_RemainingGameState( void )
{
	int			len;
	int			start, i;
	entityState_t nullstate;
	const svEntity_t *svEnt;
	msg_t		msg;
	byte		msgBuffer[ MAX_MSGLEN_BUF ];

	MSG_Init( &msg, msgBuffer, MAX_MSGLEN );

	MSG_WriteLong( &msg, 7 ); // last client command

	for ( i = 0; i < 256; i++ ) // simulate dummy client commands
		MSG_WriteByte( &msg, i & 127 );

	// send the gamestate
	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, 7 ); // client->reliableSequence

	// write the configstrings
	for ( start = 0 ; start < MAX_CONFIGSTRINGS ; start++ ) {
		if ( start == CS_SERVERINFO ) {
			MSG_WriteByte( &msg, svc_configstring );
			MSG_WriteShort( &msg, start );
			MSG_WriteBigString( &msg, Cvar_InfoString( CVAR_SERVERINFO, NULL ) );
			continue;
		}
		if ( start == CS_SYSTEMINFO ) {
			MSG_WriteByte( &msg, svc_configstring );
			MSG_WriteShort( &msg, start );
			MSG_WriteBigString( &msg, Cvar_InfoString_Big( CVAR_SYSTEMINFO, NULL ) );
			continue;
		}
		if ( sv.configstrings[start][0] ) {
			MSG_WriteByte( &msg, svc_configstring );
			MSG_WriteShort( &msg, start );
			MSG_WriteBigString( &msg, sv.configstrings[start] );
		}
	}

	// write the baselines
	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( start = 0 ; start < MAX_GENTITIES; start++ ) {
		if ( !sv.baselineUsed[ start ] ) {
			continue;
		}
		svEnt = &sv.svEntities[ start ];
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, &svEnt->baseline, qtrue );
	}

	MSG_WriteByte( &msg, svc_EOF );

	MSG_WriteLong( &msg, 7 ); // client num

	// write the checksum feed
	MSG_WriteLong( &msg, sv.checksumFeed );

	// finalize packet
	MSG_WriteByte( &msg, svc_EOF );

	len = PAD( msg.bit, 8 ) / 8;

	// reserve some space for potential userinfo expansion
	len += 512;
	
	return MAX_MSGLEN - len;
}


/*
================
SV_SendClientGameState

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each new map load.

It will be resent if the client acknowledges a later message but has
the wrong gamestate.
================
*/
static void SV_SendClientGameState( client_t *client ) {
	int			start;
	entityState_t nullstate;
	const svEntity_t *svEnt;
	msg_t		msg;
	byte		msgBuffer[ MAX_MSGLEN_BUF ];

 	Com_DPrintf( "SV_SendClientGameState() for %s\n", client->name );
	Com_DPrintf( "Going from CS_CONNECTED to CS_PRIMED for %s\n", client->name );
	client->state = CS_PRIMED;
	client->pureAuthentic = qfalse;
	client->gotCP = qfalse;

	// to start generating delta for packet entities
	client->gentity = SV_GentityNum( client - svs.clients );

	// when we receive the first packet from the client, we will
	// notice that it is from a different serverid and that the
	// gamestate message was not just sent, forcing a retransmit
	client->gamestateMessageNum = client->netchan.outgoingSequence;

	MSG_Init( &msg, msgBuffer, MAX_MSGLEN );

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

	// send any server commands waiting to be sent first.
	// we have to do this cause we send the client->reliableSequence
	// with a gamestate and it sets the clc.serverCommandSequence at
	// the client side
	SV_UpdateServerCommandsToClient( client, &msg );

	// send the gamestate
	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, client->reliableSequence );

	// write the configstrings
	for ( start = 0 ; start < MAX_CONFIGSTRINGS ; start++ ) {
		if (sv.configstrings[start][0]) {
			MSG_WriteByte( &msg, svc_configstring );
			MSG_WriteShort( &msg, start );
			MSG_WriteBigString( &msg, sv.configstrings[start] );
		}
	}

	// write the baselines
	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( start = 0 ; start < MAX_GENTITIES; start++ ) {
		if ( !sv.baselineUsed[ start ] ) {
			continue;
		}
		svEnt = &sv.svEntities[ start ];
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, &svEnt->baseline, qtrue );
	}

	MSG_WriteByte( &msg, svc_EOF );

	MSG_WriteLong( &msg, client - svs.clients );

	// write the checksum feed
	MSG_WriteLong( &msg, sv.checksumFeed );

	// it is important to handle gamestate overflow
	// but at this stage client can't process any reliable commands
	// so at least try to inform him in console and release connection slot
	if ( msg.overflowed ) {
		if ( client->netchan.remoteAddress.type == NA_LOOPBACK ) {
			Com_Error( ERR_DROP, "gamestate overflow" );
		} else {
			NET_OutOfBandPrint( NS_SERVER, &client->netchan.remoteAddress, "print\n" S_COLOR_RED "SERVER ERROR: gamestate overflow\n" );
			SV_DropClient( client, "gamestate overflow" );
		}
		return;
	}

	// deliver this to the client
	SV_SendMessageToClient( &msg, client );
}


/*
==================
SV_ClientEnterWorld
==================
*/
void SV_ClientEnterWorld( client_t *client, usercmd_t *cmd ) {
	int		clientNum;
	sharedEntity_t *ent;

	Com_DPrintf( "Going from CS_PRIMED to CS_ACTIVE for %s\n", client->name );
	client->state = CS_ACTIVE;

	// resend all configstrings using the cs commands since these are
	// no longer sent when the client is CS_PRIMED
	SV_UpdateConfigstrings( client );

	// set up the entity for the client
	clientNum = client - svs.clients;
	ent = SV_GentityNum( clientNum );
	ent->s.number = clientNum;
	client->gentity = ent;

	client->deltaMessage = -1;
	client->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately

	if(cmd)
		memcpy(&client->lastUsercmd, cmd, sizeof(client->lastUsercmd));
	else
		memset(&client->lastUsercmd, '\0', sizeof(client->lastUsercmd));

	// call the game begin function
	VM_Call( gvm, GAME_CLIENT_BEGIN, client - svs.clients );
}


/*
============================================================

CLIENT COMMAND EXECUTION

============================================================
*/

/*
==================
SV_CloseDownload

clear/free any download vars
==================
*/
static void SV_CloseDownload( client_t *cl ) {
	int i;

	// EOF
	if ( cl->download != FS_INVALID_HANDLE ) {
		FS_FCloseFile( cl->download );
		cl->download = FS_INVALID_HANDLE;
	}

	*cl->downloadName = '\0';

	// Free the temporary buffer space
	for (i = 0; i < MAX_DOWNLOAD_WINDOW; i++) {
		if (cl->downloadBlocks[i]) {
			Z_Free( cl->downloadBlocks[i] );
			cl->downloadBlocks[i] = NULL;
		}
	}

}


/*
==================
SV_StopDownload_f

Abort a download if in progress
==================
*/
static void SV_StopDownload_f( client_t *cl ) {
	if (*cl->downloadName)
		Com_DPrintf( "clientDownload: %d : file \"%s\" aborted\n", (int) (cl - svs.clients), cl->downloadName );

	SV_CloseDownload( cl );
}


/*
==================
SV_DoneDownload_f

Downloads are finished
==================
*/
static void SV_DoneDownload_f( client_t *cl ) {
	if ( cl->state == CS_ACTIVE )
		return;

	Com_DPrintf( "clientDownload: %s Done\n", cl->name);
	// resend the game state to update any clients that entered during the download
	SV_SendClientGameState(cl);
}


/*
==================
SV_NextDownload_f

The argument will be the last acknowledged block from the client, it should be
the same as cl->downloadClientBlock
==================
*/
static void SV_NextDownload_f( client_t *cl )
{
	int block = atoi( Cmd_Argv(1) );

	if (block == cl->downloadClientBlock) {
		Com_DPrintf( "clientDownload: %d : client acknowledge of block %d\n", (int) (cl - svs.clients), block );

		// Find out if we are done.  A zero-length block indicates EOF
		if (cl->downloadBlockSize[cl->downloadClientBlock % MAX_DOWNLOAD_WINDOW] == 0) {
			Com_Printf( "clientDownload: %d : file \"%s\" completed\n", (int) (cl - svs.clients), cl->downloadName );
			SV_CloseDownload( cl );
			return;
		}

		cl->downloadSendTime = svs.time;
		cl->downloadClientBlock++;
		return;
	}
	// We aren't getting an acknowledge for the correct block, drop the client
	// FIXME: this is bad... the client will never parse the disconnect message
	//			because the cgame isn't loaded yet
	SV_DropClient( cl, "broken download" );
}


/*
==================
SV_BeginDownload_f
==================
*/
static void SV_BeginDownload_f( client_t *cl ) {

	// Kill any existing download
	SV_CloseDownload( cl );

	// cl->downloadName is non-zero now, SV_WriteDownloadToClient will see this and open
	// the file itself
	Q_strncpyz( cl->downloadName, Cmd_Argv(1), sizeof(cl->downloadName) );
}


/*
==================
SV_WriteDownloadToClient

Check to see if the client wants a file, open it if needed and start pumping the client
Fill up msg with data, return number of download blocks added
==================
*/
static int SV_WriteDownloadToClient( client_t *cl, msg_t *msg )
{
	int curindex;
	int unreferenced = 1;
	char errorMessage[1024];
	char pakbuf[MAX_QPATH], *pakptr;
	int numRefPaks;

	if (!*cl->downloadName)
		return 0;	// Nothing being downloaded

	if ( cl->download == FS_INVALID_HANDLE ) {
		qboolean idPack = qfalse;
		qboolean missionPack = qfalse;
 		// Chop off filename extension.
		Q_strncpyz( pakbuf, cl->downloadName, sizeof( pakbuf ) );
		pakptr = strrchr( pakbuf, '.' );
		
		if(pakptr)
		{
			*pakptr = '\0';

			// Check for pk3 filename extension
			if(!Q_stricmp(pakptr + 1, "pk3"))
			{
				const char *referencedPaks = FS_ReferencedPakNames();

				// Check whether the file appears in the list of referenced
				// paks to prevent downloading of arbitrary files.
				Cmd_TokenizeStringIgnoreQuotes(referencedPaks);
				numRefPaks = Cmd_Argc();

				for(curindex = 0; curindex < numRefPaks; curindex++)
				{
					if(!FS_FilenameCompare(Cmd_Argv(curindex), pakbuf))
					{
						unreferenced = 0;

						// now that we know the file is referenced,
						// check whether it's legal to download it.
						missionPack = FS_idPak(pakbuf, BASETA, NUM_TA_PAKS);
						idPack = missionPack || FS_idPak(pakbuf, BASEGAME, NUM_ID_PAKS);

						break;
					}
				}
			}
		}

		cl->download = FS_INVALID_HANDLE;

		// We open the file here
		if ( !(sv_allowDownload->integer & DLF_ENABLE) ||
			(sv_allowDownload->integer & DLF_NO_UDP) ||
			idPack || unreferenced ||
			( cl->downloadSize = FS_SV_FOpenFileRead( cl->downloadName, &cl->download ) ) < 0 ) {
			// cannot auto-download file
			if(unreferenced)
			{
				Com_Printf("clientDownload: %d : \"%s\" is not referenced and cannot be downloaded.\n", (int) (cl - svs.clients), cl->downloadName);
				Com_sprintf(errorMessage, sizeof(errorMessage), "File \"%s\" is not referenced and cannot be downloaded.", cl->downloadName);
			}
			else if (idPack) {
				Com_Printf("clientDownload: %d : \"%s\" cannot download id pk3 files\n", (int) (cl - svs.clients), cl->downloadName);
				if (missionPack) {
					Com_sprintf(errorMessage, sizeof(errorMessage), "Cannot autodownload Team Arena file \"%s\"\n"
									"The Team Arena mission pack can be found in your local game store.", cl->downloadName);
				}
				else {
					Com_sprintf(errorMessage, sizeof(errorMessage), "Cannot autodownload id pk3 file \"%s\"", cl->downloadName);
				}
			}
			else if ( !(sv_allowDownload->integer & DLF_ENABLE) ||
				(sv_allowDownload->integer & DLF_NO_UDP) ) {

				Com_Printf("clientDownload: %d : \"%s\" download disabled", (int) (cl - svs.clients), cl->downloadName);
				if (sv_pure->integer) {
					Com_sprintf(errorMessage, sizeof(errorMessage), "Could not download \"%s\" because autodownloading is disabled on the server.\n\n"
										"You will need to get this file elsewhere before you "
										"can connect to this pure server.\n", cl->downloadName);
				} else {
					Com_sprintf(errorMessage, sizeof(errorMessage), "Could not download \"%s\" because autodownloading is disabled on the server.\n\n"
                    "The server you are connecting to is not a pure server, "
                    "set autodownload to No in your settings and you might be "
                    "able to join the game anyway.\n", cl->downloadName);
				}
			} else {
        // NOTE TTimo this is NOT supposed to happen unless bug in our filesystem scheme?
        //   if the pk3 is referenced, it must have been found somewhere in the filesystem
				Com_Printf("clientDownload: %d : \"%s\" file not found on server\n", (int) (cl - svs.clients), cl->downloadName);
				Com_sprintf(errorMessage, sizeof(errorMessage), "File \"%s\" not found on server for autodownloading.\n", cl->downloadName);
			}
			MSG_WriteByte( msg, svc_download );
			MSG_WriteShort( msg, 0 ); // client is expecting block zero
			MSG_WriteLong( msg, -1 ); // illegal file size
			MSG_WriteString( msg, errorMessage );

			*cl->downloadName = '\0';
			
			if ( cl->download != FS_INVALID_HANDLE ) {
				FS_FCloseFile( cl->download );
				cl->download = FS_INVALID_HANDLE;
			}
			
			return 1;
		}
 
		Com_Printf( "clientDownload: %d : beginning \"%s\"\n", (int) (cl - svs.clients), cl->downloadName );
		
		// Init
		cl->downloadCurrentBlock = cl->downloadClientBlock = cl->downloadXmitBlock = 0;
		cl->downloadCount = 0;
		cl->downloadEOF = qfalse;
	}

	// Perform any reads that we need to
	while (cl->downloadCurrentBlock - cl->downloadClientBlock < MAX_DOWNLOAD_WINDOW &&
		cl->downloadSize != cl->downloadCount) {

		curindex = (cl->downloadCurrentBlock % MAX_DOWNLOAD_WINDOW);

		if (!cl->downloadBlocks[curindex])
			cl->downloadBlocks[curindex] = Z_Malloc( MAX_DOWNLOAD_BLKSIZE );

		cl->downloadBlockSize[curindex] = FS_Read( cl->downloadBlocks[curindex], MAX_DOWNLOAD_BLKSIZE, cl->download );

		if (cl->downloadBlockSize[curindex] < 0) {
			// EOF right now
			cl->downloadCount = cl->downloadSize;
			break;
		}

		cl->downloadCount += cl->downloadBlockSize[curindex];

		// Load in next block
		cl->downloadCurrentBlock++;
	}

	// Check to see if we have eof condition and add the EOF block
	if (cl->downloadCount == cl->downloadSize &&
		!cl->downloadEOF &&
		cl->downloadCurrentBlock - cl->downloadClientBlock < MAX_DOWNLOAD_WINDOW) {

		cl->downloadBlockSize[cl->downloadCurrentBlock % MAX_DOWNLOAD_WINDOW] = 0;
		cl->downloadCurrentBlock++;

		cl->downloadEOF = qtrue;  // We have added the EOF block
	}

	if (cl->downloadClientBlock == cl->downloadCurrentBlock)
		return 0; // Nothing to transmit

	// Write out the next section of the file, if we have already reached our window,
	// automatically start retransmitting
	if (cl->downloadXmitBlock == cl->downloadCurrentBlock)
	{
		// We have transmitted the complete window, should we start resending?
		if (svs.time - cl->downloadSendTime > 1000)
			cl->downloadXmitBlock = cl->downloadClientBlock;
		else
			return 0;
	}

	// Send current block
	curindex = (cl->downloadXmitBlock % MAX_DOWNLOAD_WINDOW);

	MSG_WriteByte( msg, svc_download );
	MSG_WriteShort( msg, cl->downloadXmitBlock );

	// block zero is special, contains file size
	if ( cl->downloadXmitBlock == 0 )
		MSG_WriteLong( msg, cl->downloadSize );

	MSG_WriteShort( msg, cl->downloadBlockSize[curindex] );

	// Write the block
	if(cl->downloadBlockSize[curindex])
		MSG_WriteData(msg, cl->downloadBlocks[curindex], cl->downloadBlockSize[curindex]);

	Com_DPrintf( "clientDownload: %d : writing block %d\n", (int) (cl - svs.clients), cl->downloadXmitBlock );

	// Move on to the next block
	// It will get sent with next snap shot.  The rate will keep us in line.
	cl->downloadXmitBlock++;
	cl->downloadSendTime = svs.time;

	return 1;
}


/*
==================
SV_SendQueuedMessages

Send one round of fragments, or queued messages to all clients that have data pending.
Return the shortest time interval for sending next packet to client
==================
*/
int SV_SendQueuedMessages( void )
{
	int i, retval = -1, nextFragT;
	client_t *cl;
	
	for( i = 0; i < sv_maxclients->integer; i++ )
	{
		cl = &svs.clients[i];
		
		if ( cl->state )
		{
			nextFragT = SV_RateMsec(cl);

			if(!nextFragT)
				nextFragT = SV_Netchan_TransmitNextFragment(cl);

			if(nextFragT >= 0 && (retval == -1 || retval > nextFragT))
				retval = nextFragT;
		}
	}

	return retval;
}


/*
==================
SV_SendDownloadMessages

Send one round of download messages to all clients
==================
*/
int SV_SendDownloadMessages( void )
{
	int i, numDLs = 0, retval;
	client_t *cl;
	msg_t msg;
	byte msgBuffer[ MAX_MSGLEN_BUF ];
	
	for( i = 0; i < sv_maxclients->integer; i++ )
	{
		cl = &svs.clients[i];
		
		if ( cl->state >= CS_CONNECTED && *cl->downloadName )
		{
			MSG_Init( &msg, msgBuffer, MAX_MSGLEN );
			MSG_WriteLong( &msg, cl->lastClientCommand );
			
			retval = SV_WriteDownloadToClient( cl, &msg );
				
			if ( retval )
			{
				MSG_WriteByte( &msg, svc_EOF );
				SV_Netchan_Transmit( cl, &msg );
				numDLs += retval;
			}
		}
	}

	return numDLs;
}


/*
=================
SV_Disconnect_f

The client is going to disconnect, so remove the connection immediately  FIXME: move to game?
=================
*/
static void SV_Disconnect_f( client_t *cl ) {
	SV_DropClient( cl, "disconnected" );
}


/*
=================
SV_VerifyPaks_f

If we are pure, disconnect the client if they do no meet the following conditions:

1. the first two checksums match our view of cgame and ui
2. there are no any additional checksums that we do not have

This routine would be a bit simpler with a goto but i abstained

=================
*/
static void SV_VerifyPaks_f( client_t *cl ) {
	int nChkSum1, nChkSum2, nClientPaks, i, j, nCurArg;
	int nClientChkSum[512];
	const char *pArg;
	qboolean bGood = qtrue;

	// if we are pure, we "expect" the client to load certain things from 
	// certain pk3 files, namely we want the client to have loaded the
	// ui and cgame that we think should be loaded based on the pure setting
	//
	if ( sv_pure->integer != 0 ) {

		nChkSum1 = nChkSum2 = 0;

		// we run the game, so determine which cgame and ui the client "should" be running
		bGood = FS_FileIsInPAK( "vm/cgame.qvm", &nChkSum1, NULL );
		bGood &= FS_FileIsInPAK( "vm/ui.qvm", &nChkSum2, NULL );

		nClientPaks = Cmd_Argc();

		if ( nClientPaks > ARRAY_LEN( nClientChkSum ) )
			nClientPaks = ARRAY_LEN( nClientChkSum );

		// start at arg 2 ( skip serverId cl_paks )
		nCurArg = 1;

		pArg = Cmd_Argv(nCurArg++);
		if ( !*pArg ) {
			bGood = qfalse;
		}
		else
		{
			// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=475
			// we may get incoming cp sequences from a previous checksumFeed, which we need to ignore
			// since serverId is a frame count, it always goes up
			if ( atoi( pArg ) < sv.checksumFeedServerId )
			{
				Com_DPrintf( "ignoring outdated cp command from client %s\n", cl->name );
				return;
			}
		}
	
		// we basically use this while loop to avoid using 'goto' :)
		while (bGood) {

			// must be at least 6: "cl_paks cgame ui @ firstref ... numChecksums"
			// numChecksums is encoded
			if (nClientPaks < 6) {
				bGood = qfalse;
				break;
			}
			// verify first to be the cgame checksum
			pArg = Cmd_Argv(nCurArg++);
			if ( !*pArg || *pArg == '@' || atoi(pArg) != nChkSum1 ) {
				bGood = qfalse;
				break;
			}
			// verify the second to be the ui checksum
			pArg = Cmd_Argv(nCurArg++);
			if ( !*pArg || *pArg == '@' || atoi(pArg) != nChkSum2 ) {
				bGood = qfalse;
				break;
			}
			// should be sitting at the delimeter now
			pArg = Cmd_Argv(nCurArg++);
			if (*pArg != '@') {
				bGood = qfalse;
				break;
			}
			// store checksums since tokenization is not re-entrant
			for (i = 0; nCurArg < nClientPaks; i++) {
				nClientChkSum[i] = atoi(Cmd_Argv(nCurArg++));
			}

			// store number to compare against (minus one cause the last is the number of checksums)
			nClientPaks = i - 1;

			// make sure none of the client check sums are the same
			// so the client can't send 5 the same checksums
			for (i = 0; i < nClientPaks; i++) {
				for (j = 0; j < nClientPaks; j++) {
					if (i == j)
						continue;
					if (nClientChkSum[i] == nClientChkSum[j]) {
						bGood = qfalse;
						break;
					}
				}
				if (bGood == qfalse)
					break;
			}
			if (bGood == qfalse)
				break;

			// check if the client has provided any pure checksums of pk3 files not loaded by the server
			for ( i = 0; i < nClientPaks; i++ ) {
				if ( !FS_IsPureChecksum( nClientChkSum[i] ) ) {
					bGood = qfalse;
					break;
				}
			}
			if ( bGood == qfalse ) {
				break;
			}

			// check if the number of checksums was correct
			nChkSum1 = sv.checksumFeed;
			for (i = 0; i < nClientPaks; i++) {
				nChkSum1 ^= nClientChkSum[i];
			}
			nChkSum1 ^= nClientPaks;
			if (nChkSum1 != nClientChkSum[nClientPaks]) {
				bGood = qfalse;
				break;
			}

			// break out
			break;
		}

		cl->gotCP = qtrue;

		if ( bGood ) {
			cl->pureAuthentic = qtrue;
		} else {
			cl->pureAuthentic = qfalse;
			cl->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
			cl->state = CS_ZOMBIE; // skip delta generation
			SV_SendClientSnapshot( cl );
			cl->state = CS_ACTIVE;
			SV_DropClient( cl, "Unpure client detected. Invalid .PK3 files referenced!" );
		}
	}
}


/*
=================
SV_ResetPureClient_f
=================
*/
static void SV_ResetPureClient_f( client_t *cl ) {
	cl->pureAuthentic = qfalse;
	cl->gotCP = qfalse;
}


/*
=================
SV_UserinfoChanged

Pull specific info from a newly changed userinfo string
into a more C friendly form.
=================
*/
void SV_UserinfoChanged( client_t *cl, qboolean updateUserinfo ) {
	char buf[ MAX_NAME_LENGTH ];
	const char *val;
	const char *ip;
	int	i;

	if ( cl->netchan.remoteAddress.type == NA_BOT ) {
		cl->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
		cl->snapshotMsec = 1000 / sv_fps->integer;
		cl->rate = 0;
		return;
	}

	// rate command

	// if the client is on the same subnet as the server and we aren't running an
	// internet public server, assume they don't need a rate choke
	if ( cl->netchan.remoteAddress.type == NA_LOOPBACK || ( cl->netchan.isLANAddress && com_dedicated->integer != 2 && sv_lanForceRate->integer ) ) {
		cl->rate = 0; // lans should not rate limit
	} else {
		val = Info_ValueForKey( cl->userinfo, "rate" );
		if ( val[0] )
			cl->rate = atoi( val );
		else
			cl->rate = 10000; // was 3000

		if ( sv_maxRate->integer ) {
			if ( cl->rate > sv_maxRate->integer )
				cl->rate = sv_maxRate->integer;
		}

		if ( sv_minRate->integer ) {
			if ( cl->rate < sv_minRate->integer )
				cl->rate = sv_minRate->integer;
		}
	}

	// snaps command
	val = Info_ValueForKey( cl->userinfo, "snaps" );
	if ( val[0] && !NET_IsLocalAddress( &cl->netchan.remoteAddress ) )
		i = atoi( val );
	else
		i = sv_fps->integer; // sync with server

	// range check
	if ( i < 1 )
		i = 1;
	else if ( i > sv_fps->integer )
		i = sv_fps->integer;

	i = 1000 / i; // from FPS to milliseconds
	
	if ( i != cl->snapshotMsec )
	{
		// Reset last sent snapshot so we avoid desync between server frame time and snapshot send time
		cl->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
		cl->snapshotMsec = i;
	}

	if ( !updateUserinfo )
		return;

	// name for C code
	val = Info_ValueForKey( cl->userinfo, "name" );
	// truncate if it is too long as it may cause memory corruption in OSP mod
	if ( gvm->forceDataMask && strlen( val ) >= sizeof( buf ) ) {
		Q_strncpyz( buf, val, sizeof( buf ) );
		Info_SetValueForKey( cl->userinfo, "name", buf );
		val = buf;
	}
	Q_strncpyz( cl->name, val, sizeof( cl->name ) );

	val = Info_ValueForKey( cl->userinfo, "handicap" );
	if ( val[0] ) {
		i = atoi( val );
		if ( i <= 0 || i > 100 || strlen( val ) > 4 ) {
			Info_SetValueForKey( cl->userinfo, "handicap", "100" );
		}
	}

	// TTimo
	// maintain the IP information
	// the banning code relies on this being consistently present
	if ( NET_IsLocalAddress( &cl->netchan.remoteAddress ) )
		ip = "localhost";
	else
		ip = NET_AdrToString( &cl->netchan.remoteAddress );

	if ( !Info_SetValueForKey( cl->userinfo, "ip", ip ) )
		SV_DropClient( cl, "userinfo string length exceeded" );
}


/*
==================
SV_UpdateUserinfo_f
==================
*/
static void SV_UpdateUserinfo_f( client_t *cl ) {
	const char *info;

	info = Cmd_Argv( 1 );

	if ( Cmd_Argc() != 2 || *info == '\0' ) {
		// this is something erroneous, client should never send that
		return;
	}

	Q_strncpyz( cl->userinfo, info, sizeof( cl->userinfo ) );

	SV_UserinfoChanged( cl, qtrue );
	// call prog code to allow overrides
	VM_Call( gvm, GAME_CLIENT_USERINFO_CHANGED, cl - svs.clients );
}


typedef struct {
	const char *name;
	void (*func)( client_t *cl );
} ucmd_t;

static const ucmd_t ucmds[] = {
	{"userinfo", SV_UpdateUserinfo_f},
	{"disconnect", SV_Disconnect_f},
	{"cp", SV_VerifyPaks_f},
	{"vdr", SV_ResetPureClient_f},
	{"download", SV_BeginDownload_f},
	{"nextdl", SV_NextDownload_f},
	{"stopdl", SV_StopDownload_f},
	{"donedl", SV_DoneDownload_f},

	{NULL, NULL}
};

/*
==================
SV_ExecuteClientCommand

Also called by bot code
==================
*/
void SV_ExecuteClientCommand( client_t *cl, const char *s, qboolean clientOK ) {
	const ucmd_t *u;
	qboolean bProcessed = qfalse;
	
	Cmd_TokenizeString( s );

	// see if it is a server level command
	for (u=ucmds ; u->name ; u++) {
		if (!strcmp (Cmd_Argv(0), u->name) ) {
			u->func( cl );
			bProcessed = qtrue;
			break;
		}
	}

	if (clientOK) {
		// pass unknown strings to the game
		if (!u->name && sv.state == SS_GAME && cl->state >= CS_PRIMED ) {
			Cmd_Args_Sanitize();
			VM_Call( gvm, GAME_CLIENT_COMMAND, cl - svs.clients );
		}
	}
	else if (!bProcessed)
		Com_DPrintf( "client text ignored for %s: %s\n", cl->name, Cmd_Argv(0) );
}


/*
================
SV_FloodProtect
================
*/
static qboolean SV_FloodProtect( client_t *cl ) {
	if ( sv_floodProtect->integer ) {
		const int now = svs.time;
		const int burst = 8;
		const int period = 500;

		int interval = now - cl->cmd_time;
		int expired = interval / period;
		int expiredRemainder = interval % period;

		if ( expired > cl->cmd_burst || interval < 0 ) {
			cl->cmd_burst = 0;
			cl->cmd_time = now;
		} else {
			cl->cmd_burst -= expired;
			cl->cmd_time = now - expiredRemainder;
		}

		if ( cl->cmd_burst < burst ) {
			cl->cmd_burst++;
			return qfalse;
		}
		return qtrue;
	} else {
		return qfalse;
	}
}


/*
===============
SV_ClientCommand
===============
*/
static qboolean SV_ClientCommand( client_t *cl, msg_t *msg ) {
	int		seq;
	const char	*s;
	qboolean clientOk = qtrue;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );

	// see if we have already executed it
	if ( cl->lastClientCommand >= seq ) {
		return qtrue;
	}

	Com_DPrintf( "clientCommand: %s : %i : %s\n", cl->name, seq, s );

	// drop the connection if we have somehow lost commands
	if ( seq > cl->lastClientCommand + 1 ) {
		Com_Printf( "Client %s lost %i clientCommands\n", cl->name, 
			seq - cl->lastClientCommand + 1 );
		SV_DropClient( cl, "Lost reliable commands" );
		return qfalse;
	}

	// malicious users may try using too many string commands
	// to lag other players.  If we decide that we want to stall
	// the command, we will stop processing the rest of the packet,
	// including the usercmd.  This causes flooders to lag themselves
	// but not other people
	// We don't do this when the client hasn't been active yet since it's
	// normal to spam a lot of commands when downloading
#ifndef DEDICATED
	if ( !com_cl_running->integer && cl->state >= CS_ACTIVE && SV_FloodProtect( cl ) ) {
#else
	if ( cl->state >= CS_ACTIVE && SV_FloodProtect( cl ) ) {
#endif
		// ignore any other text messages from this client but let them keep playing
		// TTimo - moved the ignored verbose to the actual processing in SV_ExecuteClientCommand, only printing if the core doesn't intercept
		clientOk = qfalse;
	}

	SV_ExecuteClientCommand( cl, s, clientOk );

	cl->lastClientCommand = seq;
	Q_strncpyz( cl->lastClientCommandString, s, sizeof( cl->lastClientCommandString ) );

	return qtrue;		// continue procesing
}


//==================================================================================


/*
==================
SV_ClientThink

Also called by bot code
==================
*/
void SV_ClientThink (client_t *cl, usercmd_t *cmd) {
	cl->lastUsercmd = *cmd;

	if ( cl->state != CS_ACTIVE ) {
		return;		// may have been kicked during the last usercmd
	}

	VM_Call( gvm, GAME_CLIENT_THINK, cl - svs.clients );
}


/*
==================
SV_UserMove

The message usually contains all the movement commands 
that were in the last three packets, so that the information
in dropped packets can be recovered.

On very fast clients, there may be multiple usercmd packed into
each of the backup packets.
==================
*/
static void SV_UserMove( client_t *cl, msg_t *msg, qboolean delta ) {
	int			i, key;
	int			cmdCount;
	static const usercmd_t nullcmd = { 0 };
	usercmd_t	cmds[MAX_PACKET_USERCMDS], *cmd;
	const usercmd_t *oldcmd;

	if ( delta ) {
		cl->deltaMessage = cl->messageAcknowledge;
	} else {
		cl->deltaMessage = -1;
	}

	cmdCount = MSG_ReadByte( msg );

	if ( cmdCount < 1 ) {
		Com_Printf( "cmdCount < 1\n" );
		return;
	}

	if ( cmdCount > MAX_PACKET_USERCMDS ) {
		Com_Printf( "cmdCount > MAX_PACKET_USERCMDS\n" );
		return;
	}

	// use the checksum feed in the key
	key = sv.checksumFeed;
	// also use the message acknowledge
	key ^= cl->messageAcknowledge;
	// also use the last acknowledged server command in the key
	key ^= MSG_HashKey(cl->reliableCommands[ cl->reliableAcknowledge & (MAX_RELIABLE_COMMANDS-1) ], 32);

	oldcmd = &nullcmd;
	for ( i = 0 ; i < cmdCount ; i++ ) {
		cmd = &cmds[i];
		MSG_ReadDeltaUsercmdKey( msg, key, oldcmd, cmd );
		oldcmd = cmd;
	}

	// save time for ping calculation
	if ( cl->frames[ cl->messageAcknowledge & PACKET_MASK ].messageAcked == 0 )
		cl->frames[ cl->messageAcknowledge & PACKET_MASK ].messageAcked = Sys_Milliseconds();

	// TTimo
	// catch the no-cp-yet situation before SV_ClientEnterWorld
	// if CS_ACTIVE, then it's time to trigger a new gamestate emission
	// if not, then we are getting remaining parasite usermove commands, which we should ignore
	if ( sv_pure->integer != 0 && !cl->pureAuthentic && !cl->gotCP ) {
		if ( cl->state == CS_ACTIVE ) {
			// we didn't get a cp yet, don't assume anything and just send the gamestate all over again
			Com_DPrintf( "%s: didn't get cp command, resending gamestate\n", cl->name );
			SV_SendClientGameState( cl );
		}
		return;
	}			
	
	// if this is the first usercmd we have received
	// this gamestate, put the client into the world
	if ( cl->state == CS_PRIMED ) {
		SV_ClientEnterWorld( cl, &cmds[0] );
		// the moves can be processed normaly
	}
	
	// a bad cp command was sent, drop the client
	if ( sv_pure->integer != 0 && !cl->pureAuthentic ) {
		SV_DropClient( cl, "Cannot validate pure client!" );
		return;
	}

	if ( cl->state != CS_ACTIVE ) {
		cl->deltaMessage = -1;
		return;
	}

	// usually, the first couple commands will be duplicates
	// of ones we have previously received, but the servertimes
	// in the commands will cause them to be immediately discarded
	for ( i =  0 ; i < cmdCount ; i++ ) {
		// if this is a cmd from before a map_restart ignore it
		if ( cmds[i].serverTime > cmds[cmdCount-1].serverTime ) {
			continue;
		}
		// extremely lagged or cmd from before a map_restart
		//if ( cmds[i].serverTime > svs.time + 3000 ) {
		//	continue;
		//}
		// don't execute if this is an old cmd which is already executed
		// these old cmds are included when cl_packetdup > 0
		if ( cmds[i].serverTime <= cl->lastUsercmd.serverTime ) {
			continue;
		}
		SV_ClientThink (cl, &cmds[ i ]);
	}
}


/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

/*
===================
SV_ExecuteClientMessage

Parse a client packet
===================
*/
void SV_ExecuteClientMessage( client_t *cl, msg_t *msg ) {
	int			c;
	int			serverId;

	MSG_Bitstream(msg);

	serverId = MSG_ReadLong( msg );
	cl->messageAcknowledge = MSG_ReadLong( msg );

	if (cl->messageAcknowledge < 0) {
		// usually only hackers create messages like this
		// it is more annoying for them to let them hanging
#ifndef NDEBUG
		SV_DropClient( cl, "DEBUG: illegible client message" );
#endif
		return;
	}

	cl->reliableAcknowledge = MSG_ReadLong( msg );

	// NOTE: when the client message is fux0red the acknowledgement numbers
	// can be out of range, this could cause the server to send thousands of server
	// commands which the server thinks are not yet acknowledged in SV_UpdateServerCommandsToClient
	if (cl->reliableAcknowledge < cl->reliableSequence - MAX_RELIABLE_COMMANDS) {
		// usually only hackers create messages like this
		// it is more annoying for them to let them hanging
#ifndef NDEBUG
		SV_DropClient( cl, "DEBUG: illegible client message" );
#endif
		cl->reliableAcknowledge = cl->reliableSequence;
		return;
	}

	cl->justConnected = qfalse;

	// if this is a usercmd from a previous gamestate,
	// ignore it or retransmit the current gamestate
	// 
	// if the client was downloading, let it stay at whatever serverId and
	// gamestate it was at.  This allows it to keep downloading even when
	// the gamestate changes.  After the download is finished, we'll
	// notice and send it a new game state
	//
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=536
	// don't drop as long as previous command was a nextdl, after a dl is done, downloadName is set back to ""
	// but we still need to read the next message to move to next download or send gamestate
	// I don't like this hack though, it must have been working fine at some point, suspecting the fix is somewhere else
	if ( serverId != sv.serverId && !*cl->downloadName && !strstr(cl->lastClientCommandString, "nextdl") ) {
		if ( serverId >= sv.restartedServerId && serverId < sv.serverId ) { // TTimo - use a comparison here to catch multiple map_restart
			// they just haven't caught the map_restart yet
			Com_DPrintf("%s : ignoring pre map_restart / outdated client message\n", cl->name);
			return;
		}
		// if we can tell that the client has dropped the last
		// gamestate we sent them, resend it
		if ( cl->state != CS_ACTIVE && cl->messageAcknowledge > cl->gamestateMessageNum ) {
			Com_DPrintf( "%s : dropped gamestate, resending\n", cl->name );
			SV_SendClientGameState( cl );
		}
		return;
	}

	// this client has acknowledged the new gamestate so it's
	// safe to start sending it the real time again
	if( cl->oldServerTime && serverId == sv.serverId ){
		Com_DPrintf( "%s acknowledged gamestate\n", cl->name );
		cl->oldServerTime = 0;
	}

	// read optional clientCommand strings
	do {
		c = MSG_ReadByte( msg );
		if ( c != clc_clientCommand ) {
			break;
		}
		if ( !SV_ClientCommand( cl, msg ) ) {
			return;	// we couldn't execute it because of the flood protection
		}
		if ( cl->state == CS_ZOMBIE ) {
			return;	// disconnect command
		}
	} while ( 1 );

	// read the usercmd_t
	if ( c == clc_move ) {
		SV_UserMove( cl, msg, qtrue );
	} else if ( c == clc_moveNoDelta ) {
		SV_UserMove( cl, msg, qfalse );
	} else if ( c != clc_EOF ) {
		Com_Printf( "WARNING: bad command byte %i for client %i\n", c, (int) (cl - svs.clients) );
	}
//	if ( msg->readcount != msg->cursize ) {
//		Com_Printf( "WARNING: Junk at end of packet for client %i\n", cl - svs.clients );
//	}
}
