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

#include "server.h"

serverStatic_t	svs;				// persistant server info
server_t		sv;					// local server
vm_t			*gvm = NULL;		// game virtual machine

cvar_t	*sv_fps;				// time rate for running non-clients
cvar_t	*sv_timeout;			// seconds without any message
cvar_t	*sv_zombietime;			// seconds to sink messages after disconnect
cvar_t	*sv_rconPassword;		// password for remote server commands
cvar_t	*sv_privatePassword;	// password for the privateClient slots
cvar_t	*sv_allowDownload;
cvar_t	*sv_maxclients;
cvar_t	*sv_maxconcurrent;

cvar_t	*sv_privateClients;		// number of clients reserved for password
cvar_t	*sv_hostname;
cvar_t	*sv_master[MAX_MASTER_SERVERS];		// master server ip address
cvar_t	*sv_reconnectlimit;		// minimum seconds between connect messages
cvar_t	*sv_padPackets;			// add nop bytes to messages
cvar_t	*sv_killserver;			// menu system can set to 1 to shut server down
cvar_t	*sv_mapname;
cvar_t	*sv_mapChecksum;
cvar_t	*sv_serverid;
cvar_t	*sv_minRate;
cvar_t	*sv_maxRate;
cvar_t	*sv_dlRate;
cvar_t	*sv_gametype;
cvar_t	*sv_pure;
cvar_t	*sv_floodProtect;
cvar_t	*sv_lanForceRate; // dedicated 1 (LAN) server forces local client rates to 99999 (bug #491)

cvar_t *sv_levelTimeReset;

#ifdef USE_BANS
cvar_t	*sv_banFile;
serverBan_t serverBans[SERVER_MAXBANS];
int serverBansCount = 0;
#endif

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
===============
SV_ExpandNewlines

Converts newlines to "\n" so a line prints nicer
===============
*/
static const char *SV_ExpandNewlines( const char *in ) {
	static char string[MAX_STRING_CHARS*2];
	int		l;

	l = 0;
	while ( *in && l < sizeof(string) - 3 ) {
		if ( *in == '\n' ) {
			string[l++] = '\\';
			string[l++] = 'n';
		} else {
			string[l++] = *in;
		}
		in++;
	}
	string[l] = '\0';

	return string;
}


/*
======================
SV_ReplacePendingServerCommands

FIXME: This is ugly
======================
*/
#if 0 // unused
static int SV_ReplacePendingServerCommands( client_t *client, const char *cmd ) {
	int i, index, csnum1, csnum2;

	for ( i = client->reliableSent+1; i <= client->reliableSequence; i++ ) {
		index = i & ( MAX_RELIABLE_COMMANDS - 1 );
		//
		if ( !Q_strncmp(cmd, client->reliableCommands[ index ], strlen("cs")) ) {
			sscanf(cmd, "cs %i", &csnum1);
			sscanf(client->reliableCommands[ index ], "cs %i", &csnum2);
			if ( csnum1 == csnum2 ) {
				Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
				/*
				if ( client->netchan.remoteAddress.type != NA_BOT ) {
					Com_Printf( "WARNING: client %i removed double pending config string %i: %s\n", client-svs.clients, csnum1, cmd );
				}
				*/
				return qtrue;
			}
		}
	}
	return qfalse;
}
#endif


/*
======================
SV_AddServerCommand

The given command will be transmitted to the client, and is guaranteed to
not have future snapshot_t executed before it is executed
======================
*/
void SV_AddServerCommand( client_t *client, const char *cmd ) {
	int		index, i;

	// this is very ugly but it's also a waste to for instance send multiple config string updates
	// for the same config string index in one snapshot
//	if ( SV_ReplacePendingServerCommands( client, cmd ) ) {
//		return;
//	}

	// do not send commands until the gamestate has been sent
	if ( client->state < CS_PRIMED )
		return;

	client->reliableSequence++;
	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// we check == instead of >= so a broadcast print added by SV_DropClient()
	// doesn't cause a recursive drop client
	if ( client->reliableSequence - client->reliableAcknowledge == MAX_RELIABLE_COMMANDS + 1 ) {
		Com_Printf( "===== pending server commands =====\n" );
		for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
			Com_Printf( "cmd %5d: %s\n", i, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
		}
		Com_Printf( "cmd %5d: %s\n", i, cmd );
		SV_DropClient( client, "Server command overflow" );
		return;
	}
	index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
}


/*
=================
SV_SendServerCommand

Sends a reliable command string to be interpreted by 
the client game module: "cp", "print", "chat", etc
A NULL client will broadcast to all clients
=================
*/
void QDECL SV_SendServerCommand( client_t *cl, const char *fmt, ... ) {
	va_list		argptr;
	char		message[MAX_STRING_CHARS+128]; // slightly larger than allowed, to detect overflows
	client_t	*client;
	int			j, len;
	
	va_start( argptr, fmt );
	len = Q_vsnprintf( message, sizeof( message ), fmt, argptr );
	va_end( argptr );

	if ( cl != NULL ) {
		// outdated clients can't properly decode 1023-chars-long strings
		// http://aluigi.altervista.org/adv/q3msgboom-adv.txt
		if ( len <= 1022 || cl->longstr ) {
			SV_AddServerCommand( cl, message );
		}
		return;
	}

	// hack to echo broadcast prints to console
	if ( com_dedicated->integer && !strncmp( message, "print", 5 ) ) {
		Com_Printf( "broadcast: %s\n", SV_ExpandNewlines( message ) );
	}

	// send the data to all relevant clients
	for ( j = 0, client = svs.clients; j < sv_maxclients->integer ; j++, client++ ) {
		if ( len <= 1022 || client->longstr ) {
			SV_AddServerCommand( client, message );
		}
	}
}


/*
==============================================================================

MASTER SERVER FUNCTIONS

==============================================================================
*/

/*
================
SV_MasterHeartbeat

Send a message to the masters every few minutes to
let it know we are alive, and log information.
We will also have a heartbeat sent when a server
changes from empty to non-empty, and full to non-full,
but not on every player enter or exit.
================
*/
#define	HEARTBEAT_MSEC	300*1000
#define	MASTERDNS_MSEC	24*60*60*1000
static void SV_MasterHeartbeat( const char *message )
{
	static netadr_t	adr[MAX_MASTER_SERVERS][2]; // [2] for v4 and v6 address for the same address string.
	int			i;
	int			res;
	int			netenabled;

	netenabled = Cvar_VariableIntegerValue("net_enabled");

	// "dedicated 1" is for lan play, "dedicated 2" is for inet public play
	if (!com_dedicated || com_dedicated->integer != 2 || !(netenabled & (NET_ENABLEV4 | NET_ENABLEV6)))
		return;		// only dedicated servers send heartbeats

	// if not time yet, don't send anything
	if ( svs.time < svs.nextHeartbeatTime )
		return;

	svs.nextHeartbeatTime = svs.time + HEARTBEAT_MSEC;

	// send to group masters
	for (i = 0; i < MAX_MASTER_SERVERS; i++)
	{
		if(!sv_master[i]->string[0])
			continue;

		// see if we haven't already resolved the name or if it's been over 24 hours
		// resolving usually causes hitches on win95, so only do it when needed
		if ( sv_master[i]->modified || svs.time > svs.masterResolveTime[i] )
		{
			sv_master[i]->modified = qfalse;
			svs.masterResolveTime[i] = svs.time + MASTERDNS_MSEC;
			
			if(netenabled & NET_ENABLEV4)
			{
				Com_Printf("Resolving %s (IPv4)\n", sv_master[i]->string);
				res = NET_StringToAdr(sv_master[i]->string, &adr[i][0], NA_IP);

				if(res == 2)
				{
					// if no port was specified, use the default master port
					adr[i][0].port = BigShort(PORT_MASTER);
				}
				
				if(res)
					Com_Printf( "%s resolved to %s\n", sv_master[i]->string, NET_AdrToStringwPort( &adr[i][0] ) );
				else
					Com_Printf( "%s has no IPv4 address.\n", sv_master[i]->string );
			}
			
			if(netenabled & NET_ENABLEV6)
			{
				Com_Printf("Resolving %s (IPv6)\n", sv_master[i]->string);
				res = NET_StringToAdr(sv_master[i]->string, &adr[i][1], NA_IP6);

				if(res == 2)
				{
					// if no port was specified, use the default master port
					adr[i][1].port = BigShort(PORT_MASTER);
				}
				
				if(res)
					Com_Printf( "%s resolved to %s\n", sv_master[i]->string, NET_AdrToStringwPort( &adr[i][1] ) );
				else
					Com_Printf( "%s has no IPv6 address.\n", sv_master[i]->string );
			}
		}

		if( adr[i][0].type == NA_BAD && adr[i][1].type == NA_BAD )
		{
			continue;
		}


		Com_Printf ("Sending heartbeat to %s\n", sv_master[i]->string );

		// this command should be changed if the server info / status format
		// ever incompatably changes

		if(adr[i][0].type != NA_BAD)
			NET_OutOfBandPrint( NS_SERVER, &adr[i][0], "heartbeat %s\n", message);
		if(adr[i][1].type != NA_BAD)
			NET_OutOfBandPrint( NS_SERVER, &adr[i][1], "heartbeat %s\n", message);
	}
}


/*
=================
SV_MasterShutdown

Informs all masters that this server is going down
=================
*/
void SV_MasterShutdown( void ) {
	// send a heartbeat right now
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat(HEARTBEAT_FOR_MASTER);

	// send it again to minimize chance of drops
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat(HEARTBEAT_FOR_MASTER);

	// when the master tries to poll the server, it won't respond, so
	// it will be removed from the list
}


/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

// This is deliberately quite large to make it more of an effort to DoS
#define MAX_BUCKETS        16384
#define MAX_HASHES          1024

static leakyBucket_t buckets[ MAX_BUCKETS ];
static leakyBucket_t *bucketHashes[ MAX_HASHES ];
leakyBucket_t outboundLeakyBucket;

/*
================
SVC_HashForAddress
================
*/
static int SVC_HashForAddress( const netadr_t *address ) {
	const byte	*ip = NULL;
	int			size = 0;
	int			hash = 0;
	int			i;

	switch ( address->type ) {
		case NA_IP:  ip = address->ipv._4; size = 4;  break;
		case NA_IP6: ip = address->ipv._6; size = 16; break;
		default: break;
	}

	for ( i = 0; i < size; i++ ) {
		hash += (int)( ip[ i ] ) * ( i + 119 );
	}

	hash = ( hash ^ ( hash >> 10 ) ^ ( hash >> 20 ) );
	hash &= ( MAX_HASHES - 1 );

	return hash;
}


/*
================
SVC_RelinkToHead
================
*/
static void SVC_RelinkToHead( leakyBucket_t *bucket, int hash ) {

	if ( bucket->prev != NULL ) {
		bucket->prev->next = bucket->next;
	} else {
		return;
	}

	if ( bucket->next != NULL ) {
		bucket->next->prev = bucket->prev;
	}

	bucket->next = bucketHashes[ hash ];
	if ( bucketHashes[ hash ] != NULL ) {
		bucketHashes[ hash ]->prev = bucket;
	}

	bucket->prev = NULL;
	bucketHashes[ hash ] = bucket;
}


/*
================
SVC_BucketForAddress

Find or allocate a bucket for an address
================
*/
static leakyBucket_t *SVC_BucketForAddress( const netadr_t *address, int burst, int period ) {
	static leakyBucket_t dummy = { 0 };
	static int		start = 0;
	const int		hash = SVC_HashForAddress( address );
	const int		now = Sys_Milliseconds();
	leakyBucket_t	*bucket;
	int				i, n;

	for ( bucket = bucketHashes[ hash ], n = 0; bucket; bucket = bucket->next, n++ ) {
		switch ( bucket->type ) {
			case NA_IP:
				if ( memcmp( bucket->ipv._4, address->ipv._4, 4 ) == 0 ) {
					if ( n > 8 ) {
						SVC_RelinkToHead( bucket, hash );
					}
					return bucket;
				}
				break;

			case NA_IP6:
				if ( memcmp( bucket->ipv._6, address->ipv._6, 16 ) == 0 ) {
					if ( n > 8 ) {
						SVC_RelinkToHead( bucket, hash );
					}
					return bucket;
				}
				break;

			default:
				return &dummy;
		}
	}

	for ( i = 0; i < MAX_BUCKETS; i++ ) {
		int interval;

		if ( start >= MAX_BUCKETS )
			start = 0;
		bucket = &buckets[ start++ ];
		interval = now - bucket->lastTime;

		// Reclaim expired buckets
		if ( bucket->type != NA_BAD && (unsigned)interval > ( bucket->burst * period ) ) {
			if ( bucket->prev != NULL ) {
				bucket->prev->next = bucket->next;
			} else {
				bucketHashes[ bucket->hash ] = bucket->next;
			}
			
			if ( bucket->next != NULL ) {
				bucket->next->prev = bucket->prev;
			}

			bucket->type = NA_BAD;
		}

		if ( bucket->type == NA_BAD ) {
			bucket->type = address->type;
			switch ( address->type ) {
				case NA_IP:  Com_Memcpy( bucket->ipv._4, address->ipv._4, 4 );  break;
				case NA_IP6: Com_Memcpy( bucket->ipv._6, address->ipv._6, 16 ); break;
				default: break;
			}

			bucket->lastTime = now;
			bucket->hash = hash;
			bucket->burst = 0;
			bucket->toxic = 0;

			// Add to the head of the relevant hash chain
			bucket->next = bucketHashes[ hash ];
			if ( bucketHashes[ hash ] != NULL ) {
				bucketHashes[ hash ]->prev = bucket;
			}

			bucket->prev = NULL;
			bucketHashes[ hash ] = bucket;

			return bucket;
		}
	}

	// Couldn't allocate a bucket for this address
	return NULL;
}


/*
================
SVC_RateLimit
================
*/
qboolean SVC_RateLimit( leakyBucket_t *bucket, int burst, int period ) {
	if ( bucket != NULL ) {
		int now = Sys_Milliseconds();
		int interval = now - bucket->lastTime;
		int expired = interval / period;
		int expiredRemainder = interval % period;

		if ( expired > bucket->burst || interval < 0 ) {
			bucket->burst = 0;
			bucket->lastTime = now;
		} else {
			bucket->burst -= expired;
			bucket->lastTime = now - expiredRemainder;
		}

		if ( bucket->burst < burst ) {
			bucket->burst++;
			return qfalse;
		}
	}

	return qtrue;
}


/*
================
SVC_RateDrop
================
*/
static void SVC_RateDrop( leakyBucket_t *bucket, int burst ) {
	if ( bucket != NULL ) {
		if ( bucket->toxic < 10000 )
			++bucket->toxic;
		bucket->burst = burst * bucket->toxic;
		bucket->lastTime = Sys_Milliseconds();
	}
}


/*
================
SVC_RateRestoreBurst
================
*/
static void SVC_RateRestoreBurst( leakyBucket_t *bucket ) {
	if ( bucket != NULL ) {
		if ( bucket->burst > 0 ) {
			bucket->burst--;
		}
	}
}


/*
================
SVC_RateRestoreToxic
================
*/
static void SVC_RateRestoreToxic( leakyBucket_t *bucket ) {
	if ( bucket != NULL ) {
		if ( bucket->toxic > 0 ) {
			bucket->toxic--;
		}
	}
}


/*
================
SVC_RateLimitAddress

Rate limit for a particular address
================
*/
qboolean SVC_RateLimitAddress( const netadr_t *from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	return SVC_RateLimit( bucket, burst, period );
}


/*
================
SVC_RateRestoreAddress

Decrease burst rate
================
*/
void SVC_RateRestoreBurstAddress( const netadr_t *from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	SVC_RateRestoreBurst( bucket );
}


/*
================
SVC_RateRestoreToxicAddress

Decrease toxicity
================
*/
void SVC_RateRestoreToxicAddress( const netadr_t *from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	SVC_RateRestoreToxic( bucket );
}


/*
================
SVC_RateDropAddress
================
*/
void SVC_RateDropAddress( const netadr_t *from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	SVC_RateDrop( bucket, burst );
}


/*
================
SVC_Status

Responds with all the info that qplug or qspy can see about the server
and all connected players.  Used for getting detailed information after
the simple info query.
================
*/
static void SVC_Status( const netadr_t *from ) {
	char	player[MAX_NAME_LENGTH + 32]; // score + ping + name
	char	status[MAX_PACKETLEN];
	char	*s;
	int		i;
	client_t	*cl;
	playerState_t	*ps;
	int		statusLength;
	int		playerLength;
	char	infostring[MAX_INFO_STRING+160]; // add some space for challenge string

	// ignore if we are in single player
#ifndef DEDICATED
	if ( Cvar_VariableIntegerValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableIntegerValue("ui_singlePlayerActive")) {
		return;
	}
#endif

	// Prevent using getstatus as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SVC_Status: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	// Allow getstatus to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundLeakyBucket, 10, 100 ) ) {
		Com_DPrintf( "SVC_Status: rate limit exceeded, dropping request\n" );
		return;
	}

	// A maximum challenge length of 128 should be more than plenty.
	if ( strlen( Cmd_Argv( 1 ) ) > 128 )
		return;

	Q_strncpyz( infostring, Cvar_InfoString( CVAR_SERVERINFO ), sizeof( infostring ) );

	// echo back the parameter to status. so master servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv( 1 ) );

	s = status;
	status[0] = '\0';
	statusLength = strlen( infostring ) + 16; // strlen( "statusResponse\n\n" )

	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		cl = &svs.clients[i];
		if ( cl->state >= CS_CONNECTED ) {

			ps = SV_GameClientNum( i );
			playerLength = Com_sprintf( player, sizeof( player ), "%i %i \"%s\"\n", 
				ps->persistant[ PERS_SCORE ], cl->ping, cl->name );
			
			if ( statusLength + playerLength >= MAX_PACKETLEN-4 )
				break; // can't hold any more
			
			s = Q_stradd( s, player );
			statusLength += playerLength;
		}
	}

	NET_OutOfBandPrint( NS_SERVER, from, "statusResponse\n%s\n%s", infostring, status );
}


/*
================
SVC_Info

Responds with a short info message that should be enough to determine
if a user is interested in a server to do a full status
================
*/
static void SVC_Info( const netadr_t *from ) {
	int		i, count, humans;
	const char	*gamedir;
	char	infostring[MAX_INFO_STRING];

	// ignore if we are in single player
#ifndef DEDICATED
	if ( Cvar_VariableIntegerValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableIntegerValue("ui_singlePlayerActive")) {
		return;
	}
#endif

	// Prevent using getinfo as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SVC_Info: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	// Allow getinfo to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundLeakyBucket, 10, 100 ) ) {
		Com_DPrintf( "SVC_Info: rate limit exceeded, dropping request\n" );
		return;
	}

	/*
	 * Check whether Cmd_Argv(1) has a sane length. This was not done in the original Quake3 version which led
	 * to the Infostring bug discovered by Luigi Auriemma. See http://aluigi.altervista.org/ for the advisory.
	 */

	// A maximum challenge length of 128 should be more than plenty.
	if ( strlen( Cmd_Argv( 1 ) ) > 128 )
		return;

	// don't count privateclients
	count = humans = 0;
	for ( i = sv_privateClients->integer ; i < sv_maxclients->integer ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			count++;
			if (svs.clients[i].netchan.remoteAddress.type != NA_BOT) {
				humans++;
			}
		}
	}

	infostring[0] = '\0';

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	Info_SetValueForKey( infostring, "protocol", va("%i", PROTOCOL_VERSION) );
	Info_SetValueForKey( infostring, "hostname", sv_hostname->string );
	Info_SetValueForKey( infostring, "mapname", sv_mapname->string );
	Info_SetValueForKey( infostring, "clients", va("%i", count) );
	Info_SetValueForKey(infostring, "g_humanplayers", va("%i", humans));
	Info_SetValueForKey( infostring, "sv_maxclients", 
		va("%i", sv_maxclients->integer - sv_privateClients->integer ) );
	Info_SetValueForKey( infostring, "gametype", va("%i", sv_gametype->integer ) );
	Info_SetValueForKey( infostring, "pure", va("%i", sv_pure->integer ) );
	Info_SetValueForKey(infostring, "g_needpass", va("%d", Cvar_VariableIntegerValue("g_needpass")));
	gamedir = Cvar_VariableString( "fs_game" );
	if( *gamedir ) {
		Info_SetValueForKey( infostring, "game", gamedir );
	}

	NET_OutOfBandPrint( NS_SERVER, from, "infoResponse\n%s", infostring );
}


/*
================
SV_FlushRedirect
================
*/
static netadr_t redirectAddress; // for rcon return messages

static void SV_FlushRedirect( const char *outputbuf )
{
	if ( *outputbuf )
	{
		NET_OutOfBandPrint( NS_SERVER, &redirectAddress, "print\n%s", outputbuf );
	}
}


/*
===============
SVC_RemoteCommand

An rcon packet arrived from the network.
Shift down the remaining args
Redirect all printfs
===============
*/
static void SVC_RemoteCommand( const netadr_t *from ) {
	static leakyBucket_t bucket;
	qboolean	valid;
	// TTimo - scaled down to accumulate, but not overflow anything network wise, print wise etc.
	// (OOB messages are the bottleneck here)
	char		sv_outputbuf[1024 - 16];
	const char	*cmd_aux, *pw;

	// Prevent using rcon as an amplifier and make dictionary attacks impractical
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SVC_RemoteCommand: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	pw = Cmd_Argv( 1 );
	if ( ( sv_rconPassword->string[0] && strcmp( pw, sv_rconPassword->string ) == 0 ) ||
		( rconPassword2[0] && strcmp( pw, rconPassword2 ) == 0 ) ) {
		valid = qtrue;
		Com_Printf( "Rcon from %s: %s\n", NET_AdrToString( from ), Cmd_ArgsFrom( 2 ) );
	} else {
		// Make DoS via rcon impractical
		if ( SVC_RateLimit( &bucket, 10, 1000 ) ) {
			Com_DPrintf( "SVC_RemoteCommand: rate limit exceeded, dropping request\n" );
			return;
		}

		valid = qfalse;
		Com_Printf( "Bad rcon from %s: %s\n", NET_AdrToString( from ), Cmd_ArgsFrom( 2 ) );
	}

	// start redirecting all print outputs to the packet
	redirectAddress = *from;
	Com_BeginRedirect( sv_outputbuf, sizeof( sv_outputbuf ), SV_FlushRedirect );

	if ( !sv_rconPassword->string[0] && !rconPassword2[0] ) {
		Com_Printf( "No rconpassword set on the server.\n" );
	} else if ( !valid ) {
		Com_Printf( "Bad rconpassword.\n" );
	} else {
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
		// get the command directly, "rcon <pass> <command>" to avoid quoting issues
		// extract the command by walking
		// since the cmd formatting can fuckup (amount of spaces), using a dumb step by step parsing
		cmd_aux = Cmd_Cmd();
		while ( *cmd_aux && *cmd_aux <= ' ' ) // skip whitespace
			cmd_aux++;
		cmd_aux += 4; // "rcon"
		while ( *cmd_aux == ' ' )
			cmd_aux++;
		if ( *cmd_aux == '"' ) {
			cmd_aux++;
			while ( *cmd_aux && *cmd_aux != '"' ) // quoted password
				cmd_aux++;
			if ( *cmd_aux == '"' )
				cmd_aux++;
		} else {
			while ( *cmd_aux && *cmd_aux != ' ' ) // password
				cmd_aux++;
		}
		while ( *cmd_aux == ' ' )
			cmd_aux++;

		Cmd_ExecuteString( cmd_aux );
	}

	Com_EndRedirect();
}


/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void SV_ConnectionlessPacket( const netadr_t *from, msg_t *msg ) {
	const char *s;
	const char *c;

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );		// skip the -1 marker

	if ( !memcmp( "connect ", msg->data + 4, 8 ) ) {
		if ( msg->cursize > MAX_INFO_STRING*2 ) { // if we assume 200% compression ratio on userinfo
			if ( com_developer->integer ) {
				Com_Printf( "%s : connect packet is too long - %i\n", NET_AdrToString( from ), msg->cursize );
			}
			return;
		}
		Huff_Decompress( msg, 12 );
	}

	s = MSG_ReadStringLine( msg );
	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);

	if ( com_developer->integer ) {
		Com_Printf( "SV packet %s : %s\n", NET_AdrToString( from ), c );
	}

	if ( !Q_stricmp(c, "rcon") ) {
		SVC_RemoteCommand( from );
		return;
	}

	if ( !com_sv_running->integer ) {
		return;
	}

	if (!Q_stricmp(c, "getstatus")) {
		SVC_Status( from );
	} else if (!Q_stricmp(c, "getinfo")) {
		SVC_Info( from );
	} else if (!Q_stricmp(c, "getchallenge")) {
		SV_GetChallenge( from );
	} else if (!Q_stricmp(c, "connect")) {
		SV_DirectConnect( from );
#ifndef STANDALONE
	} else if (!Q_stricmp(c, "ipAuthorize")) {
		// removed from codebase since stateless challenges
#endif
	} else if (!Q_stricmp(c, "disconnect")) {
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
	} else {
		if ( com_developer->integer ) {
			Com_Printf( "bad connectionless packet from %s:\n%s\n",
				NET_AdrToString( from ), s );
		}
	}
}

//============================================================================

/*
=================
SV_PacketEvent
=================
*/
void SV_PacketEvent( const netadr_t *from, msg_t *msg ) {
	int			i;
	client_t	*cl;
	int			qport;

	if ( msg->cursize < 6 ) // too short for anything
		return;

	// check for connectionless packet (0xffffffff) first
	if ( *(int *)msg->data == -1 ) {
		SV_ConnectionlessPacket( from, msg );
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg ); // sequence number
	qport = MSG_ReadShort( msg ) & 0xffff;

	// find which client the message is from
	for (i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if (cl->state == CS_FREE) {
			continue;
		}
		if ( !NET_CompareBaseAdr( from, &cl->netchan.remoteAddress ) ) {
			continue;
		}
		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if (cl->netchan.qport != qport) {
			continue;
		}

		// make sure it is a valid, in sequence packet
		if (SV_Netchan_Process(cl, msg)) {
			// the IP port can't be used to differentiate clients, because
			// some address translating routers periodically change UDP
			// port assignments
			if (cl->netchan.remoteAddress.port != from->port) {
				Com_Printf( "SV_PacketEvent: fixing up a translated port\n" );
				cl->netchan.remoteAddress.port = from->port;
			}
			// zombie clients still need to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if (cl->state != CS_ZOMBIE) {
				cl->lastPacketTime = svs.time;	// don't timeout
				SV_ExecuteClientMessage( cl, msg );
			}
			return;
		}
	}
}


/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
static void SV_CalcPings( void ) {
	int			i, j;
	client_t	*cl;
	int			total, count;
	int			delta;
	playerState_t	*ps;

	for (i=0 ; i < sv_maxclients->integer ; i++) {
		cl = &svs.clients[i];
		if ( cl->state != CS_ACTIVE ) {
			cl->ping = 999;
			continue;
		}
		if ( !cl->gentity ) {
			cl->ping = 999;
			continue;
		}
		if ( cl->netchan.remoteAddress.type == NA_BOT ) {
			cl->ping = 0;
			continue;
		}

		total = 0;
		count = 0;
		for ( j = 0 ; j < PACKET_BACKUP ; j++ ) {
			if ( cl->frames[j].messageAcked == 0 ) {
				continue;
			}
			delta = cl->frames[j].messageAcked - cl->frames[j].messageSent;
			count++;
			total += delta;
		}
		if (!count) {
			cl->ping = 999;
		} else {
			cl->ping = total/count;
			if ( cl->ping > 999 ) {
				cl->ping = 999;
			}
		}

		// let the game dll know about the ping
		ps = SV_GameClientNum( i );
		ps->ping = cl->ping;
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->integer 
seconds, drop the conneciton.  Server time is used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
static void SV_CheckTimeouts( void ) {
	int		i;
	client_t	*cl;
	int			droppoint;
	int			zombiepoint;

	droppoint = svs.time - 1000 * sv_timeout->integer;
	zombiepoint = svs.time - 1000 * sv_zombietime->integer;

	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		// message times may be wrong across a changelevel
		if (cl->lastPacketTime > svs.time) {
			cl->lastPacketTime = svs.time;
		}

		if ( cl->state == CS_ZOMBIE && cl->lastPacketTime < zombiepoint ) {
			// using the client id cause the cl->name is empty at this point
			Com_DPrintf( "Going from CS_ZOMBIE to CS_FREE for client %d\n", i );
			cl->state = CS_FREE;	// can now be reused
			continue;
		}
		if ( cl->justConnected && svs.time - cl->lastPacketTime > 4000 ) {
			// for real client 4 seconds is more than enough to respond
			SVC_RateDropAddress( &cl->netchan.remoteAddress, 10, 1000 ); // enforce burst with progressive multiplier
			SV_DropClient( cl, NULL ); // drop silently
			cl->state = CS_FREE;
			continue;
		}
		if ( cl->state >= CS_CONNECTED && cl->lastPacketTime < droppoint ) {
			// wait several frames so a debugger session doesn't
			// cause a timeout
			if ( ++cl->timeoutCount > 5 ) {
				SV_DropClient( cl, "timed out" );
				cl->state = CS_FREE;	// don't bother with zombie state
			}
		} else {
			cl->timeoutCount = 0;
		}
	}
}


/*
==================
SV_CheckPaused
==================
*/
static qboolean SV_CheckPaused( void ) {
	int		count;
	client_t	*cl;
	int		i;

#ifdef DEDICATED
	// can't pause on dedicated servers
	return qfalse;
#else
	if ( !cl_paused->integer ) {
		return qfalse;
	}
#endif

	// only pause if there is just a single client connected
	count = 0;
	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if ( cl->state >= CS_CONNECTED && cl->netchan.remoteAddress.type != NA_BOT ) {
			count++;
		}
	}

	if ( count > 1 ) {
		// don't pause
		if (sv_paused->integer)
			Cvar_Set("sv_paused", "0");
		return qfalse;
	}

	if (!sv_paused->integer)
		Cvar_Set("sv_paused", "1");
	return qtrue;
}


/*
==================
SV_FrameMsec
Return time in millseconds until processing of the next server frame.
==================
*/
int SV_FrameMsec( void )
{
	if ( sv_fps )
	{
		int frameMsec;
		
		frameMsec = 1000.0f / sv_fps->value;
		
		if ( frameMsec < sv.timeResidual )
			return 0;
		else
			return frameMsec - sv.timeResidual;
	}
	else
		return 1;
}


/*
==================
SV_TrackCvarChanges
==================
*/
void SV_TrackCvarChanges( void )
{
	client_t *cl;
	int i;

	if ( sv_maxRate->integer && sv_maxRate->integer < 1000 ) {
		Cvar_Set( "sv_maxRate", "1000" );
		Com_DPrintf( "sv_maxRate adjusted to 1000\n" );
	}

	if ( sv_minRate->integer && sv_minRate->integer < 1000 ) {
		Cvar_Set( "sv_minRate", "1000" );
		Com_DPrintf( "sv_minRate adjusted to 1000\n" );
	}

	Cvar_ResetGroup( CVG_SERVER, qfalse );

	if ( sv.state == SS_DEAD || !svs.clients )
		return;

	for ( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if ( cl->state >= CS_CONNECTED ) {
			SV_UserinfoChanged( cl, qfalse );
		}
	}
}


/*
==================
SV_Frame

Player movement occurs as a result of packet events, which
happen before SV_Frame is called
==================
*/
void SV_Frame( int msec ) {
	int		frameMsec;
	int		startTime;
	int		i, n;

	if ( Cvar_CheckGroup( CVG_SERVER ) )
		SV_TrackCvarChanges(); // update rate settings, etc.

	// the menu kills the server with this cvar
	if ( sv_killserver->integer ) {
		SV_Shutdown( "Server was killed" );
		Cvar_Set( "sv_killserver", "0" );
		return;
	}

	if ( !com_sv_running->integer )
	{
		if ( com_dedicated->integer )
		{
			// Block indefinitely until something interesting happens
			// on STDIN.
			Sys_Sleep( -1 );
		}
		return;
	}

	// allow pause if only the local client is connected
	if ( SV_CheckPaused() ) {
		return;
	}

	// if it isn't time for the next frame, do nothing

	frameMsec = 1000 / sv_fps->integer * com_timescale->value;
	// don't let it scale below 1ms
	if(frameMsec < 1)
	{
		Cvar_Set( "timescale", va( "%f", sv_fps->value / 1000.0f ) );
		Com_DPrintf( "timescale adjusted to %f\n", com_timescale->value );
		frameMsec = 1;
	}

	sv.timeResidual += msec;

	if ( !com_dedicated->integer )
		SV_BotFrame( sv.time + sv.timeResidual );

	// if time is about to hit the 32nd bit, kick all clients
	// and clear sv.time, rather
	// than checking for negative time wraparound everywhere.
	// 2giga-milliseconds = 23 days, so it won't be too often
	if ( svs.time > 0x78000000 ) {
		char mapName[ MAX_CVAR_VALUE_STRING ];
		Cvar_VariableStringBuffer( "mapname", mapName, sizeof( mapName ) );
		SV_Shutdown( "Restarting server due to time wrapping" );
		Cbuf_AddText( va( "map %s\n", mapName ) );
		return;
	}

	// try to do silent restart earlier if possible
	if ( svs.time > 0x40000000 || ( sv.time > (12*3600*1000) && sv_levelTimeReset->integer == 0 ) ) {
		n = 0;
		if ( svs.clients ) {
			for ( i = 0; i < sv_maxclients->integer; i++ ) {
				// FIXME: deal with bots (reconnect?)
				if ( svs.clients[i].state != CS_FREE && svs.clients[i].netchan.remoteAddress.type != NA_BOT ) {
					n = 1;
					break;
				}
			}
		}
		if ( !n ) {
			char mapName[ MAX_CVAR_VALUE_STRING ];
			Cvar_VariableStringBuffer( "mapname", mapName, sizeof( mapName ) );
			SV_Shutdown( "Restarting server" );
			Cbuf_AddText( va( "map %s\n", mapName ) );
			return;
		}
	}

	if( sv.restartTime && sv.time >= sv.restartTime ) {
		sv.restartTime = 0;
		Cbuf_AddText( "map_restart 0\n" );
		return;
	}

	// update infostrings if anything has been changed
	if ( cvar_modifiedFlags & CVAR_SERVERINFO ) {
		SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO ) );
		cvar_modifiedFlags &= ~CVAR_SERVERINFO;
	}
	if ( cvar_modifiedFlags & CVAR_SYSTEMINFO ) {
		SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString_Big( CVAR_SYSTEMINFO ) );
		cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	}

	if ( com_speeds->integer ) {
		startTime = Sys_Milliseconds ();
	} else {
		startTime = 0;	// quite a compiler warning
	}

	// update ping based on the all received frames
	SV_CalcPings();

	if (com_dedicated->integer) SV_BotFrame (sv.time);

	// run the game simulation in chunks
	while ( sv.timeResidual >= frameMsec ) {
		sv.timeResidual -= frameMsec;
		svs.time += frameMsec;
		sv.time += frameMsec;

		// let everything in the world think and move
		VM_Call (gvm, GAME_RUN_FRAME, sv.time);
	}

	if ( com_speeds->integer ) {
		time_game = Sys_Milliseconds () - startTime;
	}

	// check timeouts
	SV_CheckTimeouts();

	// reset current and build new snapshot on first query
	SV_IssueNewSnapshot();

	// send messages back to the clients
	SV_SendClientMessages();

	// send a heartbeat to the master if needed
	SV_MasterHeartbeat(HEARTBEAT_FOR_MASTER);
}


/*
====================
SV_RateMsec

Return the number of msec until another message can be sent to
a client based on its rate settings
====================
*/

#define UDPIP_HEADER_SIZE 28
#define UDPIP6_HEADER_SIZE 48

int SV_RateMsec( const client_t *client )
{
	int rate, rateMsec;
	int messageSize;
	
	if ( !client->rate )
		return 0;

	messageSize = client->netchan.lastSentSize;

	if ( client->netchan.remoteAddress.type == NA_IP6 )
		messageSize += UDPIP6_HEADER_SIZE;
	else
		messageSize += UDPIP_HEADER_SIZE;
		
	rateMsec = messageSize * 1000 / ((int) (client->rate * com_timescale->value));
	rate = Sys_Milliseconds() - client->netchan.lastSentTime;
	
	if ( rate > rateMsec )
		return 0;
	else
		return rateMsec - rate;
}


/*
====================
SV_SendQueuedPackets

Send download messages and queued packets in the time that we're idle, i.e.
not computing a server frame or sending client snapshots.
Return the time in msec until we expect to be called next
====================
*/
int SV_SendQueuedPackets( void )
{
	int numBlocks;
	int dlStart, deltaT, delayT;
	static int dlNextRound = 0;
	int timeVal = INT_MAX;

	// Send out fragmented packets now that we're idle
	delayT = SV_SendQueuedMessages();
	if(delayT >= 0)
		timeVal = delayT;

	if(sv_dlRate->integer)
	{
		// Rate limiting. This is very imprecise for high
		// download rates due to millisecond timedelta resolution
		dlStart = Sys_Milliseconds();
		deltaT = dlNextRound - dlStart;

		if(deltaT > 0)
		{
			if(deltaT < timeVal)
				timeVal = deltaT + 1;
		}
		else
		{
			numBlocks = SV_SendDownloadMessages();

			if(numBlocks)
			{
				// There are active downloads
				deltaT = Sys_Milliseconds() - dlStart;

				delayT = 1000 * numBlocks * MAX_DOWNLOAD_BLKSIZE;
				delayT /= sv_dlRate->integer * 1024;

				if(delayT <= deltaT + 1)
				{
					// Sending the last round of download messages
					// took too long for given rate, don't wait for
					// next round, but always enforce a 1ms delay
					// between DL message rounds so we don't hog
					// all of the bandwidth. This will result in an
					// effective maximum rate of 1MB/s per user, but the
					// low download window size limits this anyways.
					if(timeVal > 2)
						timeVal = 2;

					dlNextRound = dlStart + deltaT + 1;
				}
				else
				{
					dlNextRound = dlStart + delayT;
					delayT -= deltaT;

					if(delayT < timeVal)
						timeVal = delayT;
				}
			}
		}
	}
	else
	{
		if(SV_SendDownloadMessages())
			timeVal = 0;
	}

	return timeVal;
}
