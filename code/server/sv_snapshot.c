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


/*
=============================================================================

Delta encode a client frame onto the network channel

A normal server packet will look like:

4	sequence number (high bit set if an oversize fragment)
<optional reliable commands>
1	svc_snapshot
4	last client reliable command
4	serverTime
1	lastframe for delta compression
1	snapFlags
1	areaBytes
<areabytes>
<playerstate>
<packetentities>

=============================================================================
*/

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entityState_t list to the message.
=============
*/
static void SV_EmitPacketEntities( const clientSnapshot_t *from, const clientSnapshot_t *to, msg_t *msg ) {
	entityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->num_entities;
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;
	while ( newindex < to->num_entities || oldindex < from_num_entities ) {
		if ( newindex >= to->num_entities ) {
			newnum = MAX_GENTITIES+1;
		} else {
			newent = to->ents[ newindex ];
			newnum = newent->number;
		}

		if ( oldindex >= from_num_entities ) {
			oldnum = MAX_GENTITIES+1;
		} else {
			oldent = from->ents[ oldindex ];
			oldnum = oldent->number;
		}

		if ( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is qfalse, this will not result
			// in any bytes being emited if the entity has not changed at all
			MSG_WriteDeltaEntity (msg, oldent, newent, qfalse );
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity (msg, &sv.svEntities[newnum].baseline, newent, qtrue );
			newindex++;
			continue;
		}

		if ( newnum > oldnum ) {
			// the old entity isn't present in the new message
			MSG_WriteDeltaEntity (msg, oldent, NULL, qtrue );
			oldindex++;
			continue;
		}
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}


/*
==================
SV_WriteSnapshotToClient
==================
*/
static void SV_WriteSnapshotToClient( const client_t *client, msg_t *msg ) {
	const clientSnapshot_t	*oldframe;
	const clientSnapshot_t	*frame;
	int					lastframe;
	int					i;
	int					snapFlags;

	// this is the snapshot we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// try to use a previous frame as the source for delta compressing the snapshot
	if ( client->deltaMessage <= 0 || client->state != CS_ACTIVE ) {
		// client is asking for a retransmit
		oldframe = NULL;
		lastframe = 0;
	} else if ( client->netchan.outgoingSequence - client->deltaMessage 
		>= (PACKET_BACKUP - 3) ) {
		// client hasn't gotten a good message through in a long time
		Com_DPrintf( "%s: Delta request from out of date packet.\n", client->name );
		oldframe = NULL;
		lastframe = 0;
	} else {
		// we have a valid snapshot to delta from
		oldframe = &client->frames[ client->deltaMessage & PACKET_MASK ];
		lastframe = client->netchan.outgoingSequence - client->deltaMessage;
		// we may refer on outdated frame
		if ( svs.lastValidFrame > oldframe->frameNum ) {
			Com_DPrintf( "%s: Delta request from out of date frame.\n", client->name );
			oldframe = NULL;
			lastframe = 0;
		}
	}

	MSG_WriteByte (msg, svc_snapshot);

	// NOTE, MRE: now sent at the start of every message from server to client
	// let the client know which reliable clientCommands we have received
	//MSG_WriteLong( msg, client->lastClientCommand );

	// send over the current server time so the client can drift
	// its view of time to try to match
	if( client->oldServerTime ) {
		// The server has not yet got an acknowledgement of the
		// new gamestate from this client, so continue to send it
		// a time as if the server has not restarted. Note from
		// the client's perspective this time is strictly speaking
		// incorrect, but since it'll be busy loading a map at
		// the time it doesn't really matter.
		MSG_WriteLong (msg, sv.time + client->oldServerTime);
	} else {
		MSG_WriteLong (msg, sv.time);
	}

	// what we are delta'ing from
	MSG_WriteByte (msg, lastframe);

	snapFlags = svs.snapFlagServerBit;
	if ( client->rateDelayed ) {
		snapFlags |= SNAPFLAG_RATE_DELAYED;
	}
	if ( client->state != CS_ACTIVE ) {
		snapFlags |= SNAPFLAG_NOT_ACTIVE;
	}

	MSG_WriteByte (msg, snapFlags);

	// send over the areabits
	MSG_WriteByte (msg, frame->areabytes);
	MSG_WriteData (msg, frame->areabits, frame->areabytes);

	// don't send any changes to zombies
	if ( client->state <= CS_ZOMBIE ) {
		// playerstate
		MSG_WriteByte( msg, 0 ); // # of changes
		MSG_WriteBits( msg, 0, 1 ); // no array changes
		// packet entities
		MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );
		return;
	}

	// delta encode the playerstate
	if ( oldframe ) {
		MSG_WriteDeltaPlayerstate( msg, &oldframe->ps, &frame->ps );
	} else {
		MSG_WriteDeltaPlayerstate( msg, NULL, &frame->ps );
	}

	// delta encode the entities
	SV_EmitPacketEntities (oldframe, frame, msg);

	// padding for rate debugging
	if ( sv_padPackets->integer ) {
		for ( i = 0 ; i < sv_padPackets->integer ; i++ ) {
			MSG_WriteByte (msg, svc_nop);
		}
	}
}


/*
==================
SV_UpdateServerCommandsToClient

(re)send all server commands the client hasn't acknowledged yet
==================
*/
void SV_UpdateServerCommandsToClient( client_t *client, msg_t *msg ) {
	int		i;

	// write any unacknowledged serverCommands
	for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
		MSG_WriteByte( msg, svc_serverCommand );
		MSG_WriteLong( msg, i );
		MSG_WriteString( msg, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}
	client->reliableSent = client->reliableSequence;
}

/*
=============================================================================

Build a client snapshot structure

=============================================================================
*/


typedef int entityNum_t;
typedef struct {
	int		numSnapshotEntities;
	entityNum_t	snapshotEntities[ MAX_SNAPSHOT_ENTITIES ];
	qboolean unordered;
} snapshotEntityNumbers_t;


/*
=============
SV_SortEntityNumbers

Insertion sort is about 10 times faster than quicksort for our task
=============
*/
static void SV_SortEntityNumbers( entityNum_t *num, const int size ) {
	entityNum_t tmp;
	int i, d;
	for ( i = 1 ; i < size; i++ ) {
		d = i;
		while ( d > 0 && num[d] < num[d-1] ) {
			tmp = num[d];
			num[d] = num[d-1];
			num[d-1] = tmp;
			d--;
		}
	}
	// consistency check for delta encoding
	for ( i = 1 ; i < size; i++ ) {
		if ( num[i-1] >= num[i] ) {
			Com_Error( ERR_DROP, "%s: invalid entity number %i", __func__, num[ i ] );
		}
	}
}


/*
===============
SV_AddIndexToSnapshot
===============
*/
static void SV_AddIndexToSnapshot( svEntity_t *svEnt, int index, snapshotEntityNumbers_t *eNums ) {

	svEnt->snapshotCounter = sv.snapshotCounter;

	// if we are full, silently discard entities
	if ( eNums->numSnapshotEntities >= MAX_SNAPSHOT_ENTITIES ) {
		return;
	}

	eNums->snapshotEntities[ eNums->numSnapshotEntities ] = index;
	eNums->numSnapshotEntities++;
}


/*
===============
SV_AddEntitiesVisibleFromPoint
===============
*/
static void SV_AddEntitiesVisibleFromPoint( const vec3_t origin, clientSnapshot_t *frame,
									snapshotEntityNumbers_t *eNums, qboolean portal ) {
	int		e, i;
	sharedEntity_t *ent;
	svEntity_t	*svEnt;
	entityState_t  *es;
	int		l;
	int		clientarea, clientcluster;
	int		leafnum;
	byte	*clientpvs;
	byte	*bitvector;

	// during an error shutdown message we may need to transmit
	// the shutdown message after the server has shutdown, so
	// specfically check for it
	if ( sv.state == SS_DEAD ) {
		return;
	}

	leafnum = CM_PointLeafnum (origin);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);

	// calculate the visible areas
	frame->areabytes = CM_WriteAreaBits( frame->areabits, clientarea );

	clientpvs = CM_ClusterPVS (clientcluster);

	for ( e = 0 ; e < svs.currFrame->count; e++ ) {
		es = svs.currFrame->ents[ e ];
		ent = SV_GentityNum( es->number );

		// entities can be flagged to be sent to only one client
		if ( ent->r.svFlags & SVF_SINGLECLIENT ) {
			if ( ent->r.singleClient != frame->ps.clientNum ) {
				continue;
			}
		}
		// entities can be flagged to be sent to everyone but one client
		if ( ent->r.svFlags & SVF_NOTSINGLECLIENT ) {
			if ( ent->r.singleClient == frame->ps.clientNum ) {
				continue;
			}
		}
		// entities can be flagged to be sent to a given mask of clients
		if ( ent->r.svFlags & SVF_CLIENTMASK ) {
			if (frame->ps.clientNum >= 32)
				Com_Error( ERR_DROP, "SVF_CLIENTMASK: clientNum >= 32" );
			if (~ent->r.singleClient & (1 << frame->ps.clientNum))
				continue;
		}

		svEnt = &sv.svEntities[ es->number ];

		// don't double add an entity through portals
		if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
			continue;
		}

		// broadcast entities are always sent
		if ( ent->r.svFlags & SVF_BROADCAST ) {
			SV_AddIndexToSnapshot( svEnt, e, eNums );
			continue;
		}

		// ignore if not touching a PV leaf
		// check area
		if ( !CM_AreasConnected( clientarea, svEnt->areanum ) ) {
			// doors can legally straddle two areas, so
			// we may need to check another one
			if ( !CM_AreasConnected( clientarea, svEnt->areanum2 ) ) {
				continue;		// blocked by a door
			}
		}

		bitvector = clientpvs;

		// check individual leafs
		if ( !svEnt->numClusters ) {
			continue;
		}
		l = 0;
		for ( i=0 ; i < svEnt->numClusters ; i++ ) {
			l = svEnt->clusternums[i];
			if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
				break;
			}
		}

		// if we haven't found it to be visible,
		// check overflow clusters that coudln't be stored
		if ( i == svEnt->numClusters ) {
			if ( svEnt->lastCluster ) {
				for ( ; l <= svEnt->lastCluster ; l++ ) {
					if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
						break;
					}
				}
				if ( l == svEnt->lastCluster ) {
					continue;	// not visible
				}
			} else {
				continue;
			}
		}

		// add it
		SV_AddIndexToSnapshot( svEnt, e, eNums );

		// if it's a portal entity, add everything visible from its camera position
		if ( ent->r.svFlags & SVF_PORTAL && !portal ) {
			if ( ent->s.generic1 ) {
				vec3_t dir;
				VectorSubtract(ent->s.origin, origin, dir);
				if ( VectorLengthSquared(dir) > (float) ent->s.generic1 * ent->s.generic1 ) {
					continue;
				}
			}
			eNums->unordered = qtrue;
			SV_AddEntitiesVisibleFromPoint( ent->s.origin2, frame, eNums, portal );
		}
	}

	ent = SV_GentityNum( frame->ps.clientNum );
	// merge second PVS at ent->r.s.origin2
	if ( ent->r.svFlags & SVF_SELF_PORTAL2 && !portal ) {
		SV_AddEntitiesVisibleFromPoint( ent->r.s.origin2, frame, eNums, qtrue );
		eNums->unordered = qtrue;
	}
}


/*
===============
SV_InitSnapshotStorage
===============
*/
void SV_InitSnapshotStorage( void ) 
{
	// initialize snapshot storage
	Com_Memset( svs.snapFrames, 0, sizeof( svs.snapFrames ) );
	svs.freeStorageEntities = svs.numSnapshotEntities;
	svs.currentStoragePosition = 0;

	svs.snapshotFrame = 0;
	svs.currentSnapshotFrame = 0;
	svs.lastValidFrame = 0;

	svs.currFrame = NULL;
}


/*
===============
SV_IssueNewSnapshot

This should be called before any new client snaphot built
===============
*/
void SV_IssueNewSnapshot( void ) 
{
	svs.currFrame = NULL;
	
	// value that clients can use even for their empty frames
	// as it will not increment on new snapshot built
	svs.currentSnapshotFrame = svs.snapshotFrame;
}


/*
===============
SV_BuildCommonSnapshot

This always allocates new common snapshot frame
===============
*/
static void SV_BuildCommonSnapshot( void ) 
{
	sharedEntity_t	*list[ MAX_GENTITIES ];
	sharedEntity_t	*ent;
	
	snapshotFrame_t	*tmp;
	snapshotFrame_t	*sf;

	int count;
	int index;
	int	num;
	int i;

	count = 0;

	// gather all linked entities
	if ( sv.state != SS_DEAD ) {
		for ( num = 0 ; num < sv.num_entities ; num++ ) {
			ent = SV_GentityNum( num );

			// never send entities that aren't linked in
			if ( !ent->r.linked ) {
				continue;
			}
	
			if ( ent->s.number != num ) {
				Com_DPrintf( "FIXING ENT->S.NUMBER %i => %i\n", ent->s.number, num );
				ent->s.number = num;
			}

			// entities can be flagged to explicitly not be sent to the client
			if ( ent->r.svFlags & SVF_NOCLIENT ) {
				continue;
			}

			list[ count++ ] = ent;
			sv.svEntities[ num ].snapshotCounter = -1;
		}
	}

	sv.snapshotCounter = -1;

	sf = &svs.snapFrames[ svs.snapshotFrame % NUM_SNAPSHOT_FRAMES ];
	
	// track last valid frame
	if ( svs.snapshotFrame - svs.lastValidFrame > (NUM_SNAPSHOT_FRAMES-1) ) {
		svs.lastValidFrame = svs.snapshotFrame - (NUM_SNAPSHOT_FRAMES-1);
		// release storage
		svs.freeStorageEntities += sf->count;
		sf->count = 0;
	}

	// release more frames if needed
	while ( svs.freeStorageEntities < count && svs.lastValidFrame != svs.snapshotFrame ) {
		tmp = &svs.snapFrames[ svs.lastValidFrame % NUM_SNAPSHOT_FRAMES ];
		svs.lastValidFrame++;
		// release storage
		svs.freeStorageEntities += tmp->count;
		tmp->count = 0;
	}

	// should never happen but anyway
	if ( svs.freeStorageEntities < count ) {
		Com_Error( ERR_DROP, "Not enough snapshot storage: %i < %i", svs.freeStorageEntities, count );
	}

	// allocate storage
	sf->count = count;
	svs.freeStorageEntities -= count;

	sf->start = svs.currentStoragePosition; 
	svs.currentStoragePosition = ( svs.currentStoragePosition + count ) % svs.numSnapshotEntities;

	sf->frameNum = svs.snapshotFrame;
	svs.snapshotFrame++;

	svs.currFrame = sf; // clients can refer to this

	// setup start index
	index = sf->start;
	for ( i = 0 ; i < count ; i++, index = (index+1) % svs.numSnapshotEntities ) {
		//index %= svs.numSnapshotEntities;
		svs.snapshotEntities[ index ] = list[ i ]->s;
		sf->ents[ i ] = &svs.snapshotEntities[ index ];
	}
}


/*
=============
SV_BuildClientSnapshot

Decides which entities are going to be visible to the client, and
copies off the playerstate and areabits.

This properly handles multiple recursive portals, but the render
currently doesn't.

For viewing through other player's eyes, clent can be something other than client->gentity
=============
*/
static void SV_BuildClientSnapshot( client_t *client ) {
	vec3_t						org;
	clientSnapshot_t			*frame;
	snapshotEntityNumbers_t		entityNumbers;
	int							i, cl;
	svEntity_t					*svEnt;
	int							clientNum;
	playerState_t				*ps;

	// this is the frame we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];
	cl = client - svs.clients;

	// clear everything in this snapshot
	Com_Memset( frame->areabits, 0, sizeof( frame->areabits ) );
	frame->areabytes = 0;

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=62
	frame->num_entities = 0;
	frame->frameNum = svs.currentSnapshotFrame;
	
	if ( client->state == CS_ZOMBIE )
		return;

	// grab the current playerState_t
	ps = SV_GameClientNum( cl );
	frame->ps = *ps;

	clientNum = frame->ps.clientNum;
	if ( clientNum < 0 || clientNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
	}

	// we set client->gentity only after sending gamestate
	// so don't send any packetentities changes until CS_PRIMED
	// because new gamestate will invalidate them anyway
	if ( !client->gentity ) {
		return;
	}

	if ( svs.currFrame == NULL ) {
		// this will always success and setup current frame
		SV_BuildCommonSnapshot();
	}

	// bump the counter used to prevent double adding
	sv.snapshotCounter++;

	// empty entities before visibility check
	entityNumbers.numSnapshotEntities = 0;

	frame->frameNum = svs.currFrame->frameNum;

	// never send client's own entity, because it can
	// be regenerated from the playerstate
	svEnt = &sv.svEntities[ clientNum ];
	svEnt->snapshotCounter = sv.snapshotCounter;

	// find the client's viewpoint
	VectorCopy( ps->origin, org );
	org[2] += ps->viewheight;

	// add all the entities directly visible to the eye, which
	// may include portal entities that merge other viewpoints
	entityNumbers.unordered = qfalse;
	SV_AddEntitiesVisibleFromPoint( org, frame, &entityNumbers, qfalse );

	// if there were portals visible, there may be out of order entities
	// in the list which will need to be resorted for the delta compression
	// to work correctly.  This also catches the error condition
	// of an entity being included twice.
	if ( entityNumbers.unordered ) {
		SV_SortEntityNumbers( &entityNumbers.snapshotEntities[0], 
			entityNumbers.numSnapshotEntities );
	}

	// now that all viewpoint's areabits have been OR'd together, invert
	// all of them to make it a mask vector, which is what the renderer wants
	for ( i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++ ) {
		((int *)frame->areabits)[i] = ((int *)frame->areabits)[i] ^ -1;
	}

	frame->num_entities = entityNumbers.numSnapshotEntities;
	// get pointers from common snapshot
	for ( i = 0 ; i < entityNumbers.numSnapshotEntities ; i++ )	{
		frame->ents[ i ] = svs.currFrame->ents[ entityNumbers.snapshotEntities[ i ] ];
	}
}


/*
=======================
SV_SendMessageToClient

Called by SV_SendClientSnapshot and SV_SendClientGameState
=======================
*/
void SV_SendMessageToClient( msg_t *msg, client_t *client )
{
	// record information about the message
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSize = msg->cursize;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSent = svs.msgTime;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageAcked = 0;

	// send the datagram
	SV_Netchan_Transmit( client, msg );
}


/*
=======================
SV_SendClientSnapshot

Also called by SV_FinalMessage

=======================
*/
void SV_SendClientSnapshot( client_t *client ) {
	byte		msg_buf[ MAX_MSGLEN_BUF ];
	msg_t		msg;

	// build the snapshot
	SV_BuildClientSnapshot( client );

	// bots need to have their snapshots build, but
	// the query them directly without needing to be sent
	if ( client->netchan.remoteAddress.type == NA_BOT ) {
		return;
	}

	MSG_Init( &msg, msg_buf, MAX_MSGLEN );
	msg.allowoverflow = qtrue;

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

	// (re)send any reliable server commands
	SV_UpdateServerCommandsToClient( client, &msg );

	// send over all the relevant entityState_t
	// and the playerState_t
	SV_WriteSnapshotToClient( client, &msg );

	// check for overflow
	if ( msg.overflowed ) {
		Com_Printf( "WARNING: msg overflowed for %s\n", client->name );
		MSG_Clear( &msg );
	}

	SV_SendMessageToClient( &msg, client );
}


/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages( void )
{
	int		i;
	client_t	*c;

	svs.msgTime = Sys_Milliseconds();

	// send a message to each connected client
	for( i = 0; i < sv_maxclients->integer; i++ )
	{
		c = &svs.clients[ i ];
		
		if ( c->state == CS_FREE )
			continue;		// not connected

		if ( *c->downloadName )
			continue;		// Client is downloading, don't send snapshots

		// 1. Local clients get snapshots every server frame
		// 2. Remote clients get snapshots depending from rate and requested number of updates

		if ( svs.time - c->lastSnapshotTime < c->snapshotMsec * com_timescale->value )
			continue;		// It's not time yet

		if ( c->netchan.unsentFragments || c->netchan_start_queue )
		{
			c->rateDelayed = qtrue;
			continue;		// Drop this snapshot if the packet queue is still full or delta compression will break
		}
	
		if ( SV_RateMsec( c ) > 0 )
		{
			// Not enough time since last packet passed through the line
			c->rateDelayed = qtrue;
			continue;
		}

		// generate and send a new message
		SV_SendClientSnapshot( c );
		c->lastSnapshotTime = svs.time;
		c->rateDelayed = qfalse;
	}
}
