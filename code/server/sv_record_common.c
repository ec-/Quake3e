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
// Data Stream
/* ******************************************************************************** */

/*
==================
Record_Stream_Error

General purpose error that can be called by any function doing encoding/decoding on the stream
Uses stream abort jump if set, otherwise calls Com_Error
==================
*/
void Record_Stream_Error( record_data_stream_t *stream, const char *message ) {
	Record_Printf( RP_ALL, "%s\n", message );
	if ( stream->abortSet ) {
		stream->abortSet = qfalse;
		longjmp( stream->abort, 1 );
	}
	Com_Error( ERR_FATAL, "%s", message );
}

/*
==================
Record_Stream_Allocate
==================
*/
char *Record_Stream_Allocate( int size, record_data_stream_t *stream ) {
	char *data = stream->data + stream->position;
	if ( stream->position + size > stream->size || stream->position + size < stream->position ) {
		Record_Stream_Error( stream, "Record_Stream_Allocate: stream overflow" );
	}
	stream->position += size;
	return data;
}

/*
==================
Record_Stream_Write
==================
*/
void Record_Stream_Write( void *data, int size, record_data_stream_t *stream ) {
	char *target = Record_Stream_Allocate( size, stream );
	if ( target ) {
		Com_Memcpy( target, data, size );
	}
}

/*
==================
Record_Stream_WriteValue
==================
*/
void Record_Stream_WriteValue( int value, int size, record_data_stream_t *stream ) {
	Record_Stream_Write( &value, size, stream );
}

/*
==================
Record_Stream_ReadStatic
==================
*/
char *Record_Stream_ReadStatic( int size, record_data_stream_t *stream ) {
	char *output = stream->data + stream->position;
	if ( stream->position + size > stream->size || stream->position + size < stream->position ) {
		Record_Stream_Error( stream, "Record_Stream_ReadStatic: stream overflow" );
	}
	stream->position += size;
	return output;
}

/*
==================
Record_Stream_ReadBuffer
==================
*/
void Record_Stream_ReadBuffer( void *output, int size, record_data_stream_t *stream ) {
	void *data = Record_Stream_ReadStatic( size, stream );
	if ( data ) {
		Com_Memcpy( output, data, size );
	}
}

/*
==================
Record_Stream_DumpToFile
==================
*/
void Record_Stream_DumpToFile( record_data_stream_t *stream, fileHandle_t file ) {
	FS_Write( stream->data, stream->position, file );
	stream->position = 0;
}

/* ******************************************************************************** */
// Memory allocation
/* ******************************************************************************** */

int alloc_count = 0;

/*
==================
Record_Calloc
==================
*/
void *Record_Calloc( unsigned int size ) {
	++alloc_count;
	return calloc( size, 1 );
}

/*
==================
Record_Free
==================
*/
void Record_Free( void *ptr ) {
	--alloc_count;
	free( ptr );
}

/* ******************************************************************************** */
// Bit operations
/* ******************************************************************************** */

/*
==================
Record_Bit_Set
==================
*/
void Record_Bit_Set( int *target, int position ) {
	target[position / 32] |= 1 << ( position % 32 );
}

/*
==================
Record_Bit_Unset
==================
*/
void Record_Bit_Unset( int *target, int position ) {
	target[position / 32] &= ~( 1 << ( position % 32 ) );
}

/*
==================
Record_Bit_Get
==================
*/
int Record_Bit_Get( int *source, int position ) {
	return ( source[position / 32] >> ( position % 32 ) ) & 1;
}

/* ******************************************************************************** */
// Flag operations
/* ******************************************************************************** */

// These flags are potentially game/mod specific, so their access is aggregated here
// so game-specific changes can be made in one place if needed

/*
==================
Record_UsercmdIsFiringWeapon
==================
*/
qboolean Record_UsercmdIsFiringWeapon( const usercmd_t *cmd ) {
	if ( cmd->buttons & BUTTON_ATTACK ) {
		return qtrue;
	}
	return qfalse;
}

/*
==================
Record_PlayerstateIsSpectator
==================
*/
qboolean Record_PlayerstateIsSpectator( const playerState_t *ps ) {
	if ( ps->pm_type == PM_SPECTATOR || ps->pm_flags & PMF_FOLLOW ) {
		return qtrue;
	}
	return qfalse;
}

/*
==================
Record_SetPlayerstateFollowFlag
==================
*/
void Record_SetPlayerstateFollowFlag( playerState_t *ps ) {
	ps->pm_flags |= PMF_FOLLOW;
}

/* ******************************************************************************** */
// Message printing
/* ******************************************************************************** */

/*
==================
Record_Printf
==================
*/
void QDECL Record_Printf( record_print_mode_t mode, const char *fmt, ... ) {
	va_list argptr;
	char message[1024];

	if ( mode == RP_DEBUG && !sv_recordDebug->integer ) {
		return;
	}

	va_start( argptr, fmt );
	Q_vsnprintf( message, sizeof( message ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "%s", message );
}

/* ******************************************************************************** */
// Record State Functions
/* ******************************************************************************** */

/*
==================
Record_AllocateState
==================
*/
record_state_t *Record_AllocateState( int maxClients ) {
	int i;
	record_state_t *rs = (record_state_t *)Record_Calloc( sizeof( *rs ) );
	rs->clients = (record_state_client_t *)Record_Calloc( sizeof( *rs->clients ) * maxClients );
	rs->maxClients = maxClients;

	// Initialize configstrings
	for ( i = 0; i < MAX_CONFIGSTRINGS; ++i ) {
		rs->configstrings[i] = CopyString( "" );
	}

	rs->currentServercmd = CopyString( "" );
	return rs;
}

/*
==================
Record_FreeState
==================
*/
void Record_FreeState( record_state_t *rs ) {
	int i;
	Record_Free( rs->clients );
	for ( i = 0; i < MAX_CONFIGSTRINGS; ++i ) {
		if ( rs->configstrings[i] )
			Z_Free( rs->configstrings[i] );
	}
	if ( rs->currentServercmd ) {
		Z_Free( rs->currentServercmd );
	}
	Record_Free( rs );
}

/* ******************************************************************************** */
// Structure Encoding/Decoding Functions
/* ******************************************************************************** */

// ***** Strings *****

/*
==================
Record_EncodeString
==================
*/
void Record_EncodeString( char *string, record_data_stream_t *stream ) {
	int length = strlen( string );
	Record_Stream_WriteValue( length, 4, stream );
	Record_Stream_Write( string, length + 1, stream );
}

/*
==================
Record_DecodeString
==================
*/
char *Record_DecodeString( record_data_stream_t *stream ) {
	int length = *(int *)Record_Stream_ReadStatic( 4, stream );
	char *string;
	if ( length < 0 ) {
		Record_Stream_Error( stream, "Record_DecodeString: invalid length" );
	}
	string = Record_Stream_ReadStatic( length + 1, stream );
	if ( string[length] ) {
		Record_Stream_Error( stream, "Record_DecodeString: string not null terminated" );
	}
	return string;
}

// ***** Generic Structure *****

/*
==================
Record_EncodeStructure

Basic structure encoding sends the index byte followed by data chunk
Field encoding sends the index byte with high bit set, followed by byte field indicating
   the following 8 indexes to send, followed by specified data chunks
In byte_pass mode only data chunks that can be encoded as 1 byte are encoded, otherwise
   chunks are 4 bytes
==================
*/
static void Record_EncodeStructure( qboolean byte_pass, unsigned int *state, unsigned int *source, int size,
		record_data_stream_t *stream ) {
	int i, j;
	unsigned char *field = 0;
	int fieldPosition = 0;

	for ( i = 0; i < size; ++i ) {
		if ( state[i] != source[i] && ( !byte_pass || ( state[i] & ~255 ) == ( source[i] & ~255 ) ) ) {
			if ( field && i - fieldPosition < 8 ) {
				{
					*field |= ( 1 << ( i - fieldPosition ) );
				}
			} else {
				int fieldHits = 0;
				for ( j = i + 1; j < i + 9 && j < size; ++j ) {
					if ( state[j] != source[j] && ( !byte_pass || ( state[j] & ~255 ) == ( source[j] & ~255 ) ) ) {
						++fieldHits;
					}
				}
				if ( fieldHits > 1 ) {
					Record_Stream_WriteValue( i | 128, 1, stream );
					field = (unsigned char *)Record_Stream_Allocate( 1, stream );
					*field = 0;
					fieldPosition = i + 1;
				} else {
					Record_Stream_WriteValue( i, 1, stream );
				}
			}
			Record_Stream_WriteValue( state[i] ^ source[i], byte_pass ? 1 : 4, stream );
			state[i] = source[i];
		}
	}

	Record_Stream_WriteValue( 255, 1, stream );
}

/*
==================
Record_DecodeStructure
==================
*/
static void Record_DecodeStructure( qboolean byte_pass, unsigned int *state, unsigned int size, record_data_stream_t *stream ) {
	while ( 1 ) {
		unsigned char cmd = *(unsigned char *)Record_Stream_ReadStatic( 1, stream );
		int index = cmd & 127;
		int field = 0;

		if ( cmd == 255 ) {
			break;
		}
		if ( cmd & 128 ) {
			field = *(unsigned char *)Record_Stream_ReadStatic( 1, stream );
		}
		field = ( field << 1 ) | 1;

		while ( field ) {
			if ( field & 1 ) {
				if ( index >= size ) {
					Record_Stream_Error( stream, "Record_DecodeStructure: out of bounds" );
				}
				if ( byte_pass ) {
					state[index] ^= *(unsigned char *)Record_Stream_ReadStatic( 1, stream );
				} else {
					state[index] ^= *(unsigned int *)Record_Stream_ReadStatic( 4, stream );
				}
			}
			field >>= 1;
			++index;
		}
	}
}

// ***** Playerstates *****

/*
==================
Record_EncodePlayerstate
==================
*/
void Record_EncodePlayerstate(playerState_t *state, playerState_t *source, record_data_stream_t *stream) {
	Record_EncodeStructure(qtrue, (unsigned int *)state, (unsigned int *)source, sizeof(*state)/4, stream);
	Record_EncodeStructure(qfalse, (unsigned int *)state, (unsigned int *)source, sizeof(*state)/4, stream); }

/*
==================
Record_DecodePlayerstate
==================
*/
void Record_DecodePlayerstate(playerState_t *state, record_data_stream_t *stream) {
	Record_DecodeStructure(qtrue, (unsigned int *)state, sizeof(*state)/4, stream);
	Record_DecodeStructure(qfalse, (unsigned int *)state, sizeof(*state)/4, stream); }

// ***** Entitystates *****

/*
==================
Record_EncodeEntitystate
==================
*/
void Record_EncodeEntitystate( entityState_t *state, entityState_t *source, record_data_stream_t *stream ) {
	Record_EncodeStructure( qtrue, (unsigned int *)state, (unsigned int *)source, sizeof( *state ) / 4, stream );
	Record_EncodeStructure( qfalse, (unsigned int *)state, (unsigned int *)source, sizeof( *state ) / 4, stream );
}

/*
==================
Record_DecodeEntitystate
==================
*/
void Record_DecodeEntitystate( entityState_t *state, record_data_stream_t *stream ) {
	Record_DecodeStructure( qtrue, (unsigned int *)state, sizeof( *state ) / 4, stream );
	Record_DecodeStructure( qfalse, (unsigned int *)state, sizeof( *state ) / 4, stream );
}

// ***** Entitysets *****

/*
==================
Record_EncodeEntityset

Sets state equal to source, and writes delta change to stream
==================
*/
void Record_EncodeEntityset( record_entityset_t *state, record_entityset_t *source, record_data_stream_t *stream ) {
	int i;
	for ( i = 0; i < MAX_GENTITIES; ++i ) {
		if ( !Record_Bit_Get( state->activeFlags, i ) && !Record_Bit_Get( source->activeFlags, i ) )
			continue;
		else if ( Record_Bit_Get( state->activeFlags, i ) && !Record_Bit_Get( source->activeFlags, i ) ) {
			// Com_Printf( "encode remove %i\n", i );
			Record_Stream_WriteValue( i | ( 1 << 12 ), 2, stream );
			Record_Bit_Unset( state->activeFlags, i );
		} else if ( !Record_Bit_Get( state->activeFlags, i ) ||
				memcmp( &state->entities[i], &source->entities[i], sizeof( state->entities[i] ) ) ) {
			// Com_Printf( "encode modify %i\n", i );
			Record_Stream_WriteValue( i | ( 2 << 12 ), 2, stream );
			Record_EncodeEntitystate( &state->entities[i], &source->entities[i], stream );
			Record_Bit_Set( state->activeFlags, i );
		}
	}

	// Finished
	Record_Stream_WriteValue( -1, 2, stream );
}

/*
==================
Record_DecodeEntityset

Modifies state to reflect delta changes in stream
==================
*/
void Record_DecodeEntityset( record_entityset_t *state, record_data_stream_t *stream ) {
	while ( 1 ) {
		short data = *(short *)Record_Stream_ReadStatic( 2, stream );
		short newnum = data & ( ( 1 << 12 ) - 1 );
		short command = data >> 12;

		// Finished
		if ( data == -1 ) {
			break;
		}

		if ( newnum < 0 || newnum >= MAX_GENTITIES ) {
			Record_Stream_Error( stream, "Record_DecodeEntityset: bad entity number" );
		}

		if ( command == 1 ) {
			// Com_Printf( "decode remove %i\n", newnum );
			Record_Bit_Unset( state->activeFlags, newnum );
		} else if ( command == 2 ) {
			// Com_Printf( "decode modify %i\n", newnum );
			Record_DecodeEntitystate( &state->entities[newnum], stream );
			Record_Bit_Set( state->activeFlags, newnum );
		} else {
			Record_Stream_Error( stream, "Record_DecodeEntityset: bad command" );
		}
	}
}

// ***** Visibility States *****

/*
==================
Record_EncodeVisibilityState
==================
*/
void Record_EncodeVisibilityState( record_visibility_state_t *state, record_visibility_state_t *source,
		record_data_stream_t *stream ) {
	Record_EncodeStructure( qfalse, (unsigned int *)state, (unsigned int *)source, sizeof( *state ) / 4, stream );
}

/*
==================
Record_DecodeVisibilityState
==================
*/
void Record_DecodeVisibilityState( record_visibility_state_t *state, record_data_stream_t *stream ) {
	Record_DecodeStructure( qfalse, (unsigned int *)state, sizeof( *state ) / 4, stream );
}

// ***** Usercmd States *****

/*
==================
Record_EncodeUsercmd
==================
*/
void Record_EncodeUsercmd( usercmd_t *state, usercmd_t *source, record_data_stream_t *stream ) {
	Record_EncodeStructure( qfalse, (unsigned int *)state, (unsigned int *)source, sizeof( *state ) / 4, stream );
}

/*
==================
Record_DecodeUsercmd
==================
*/
void Record_DecodeUsercmd( usercmd_t *state, record_data_stream_t *stream ) {
	Record_DecodeStructure( qfalse, (unsigned int *)state, sizeof( *state ) / 4, stream );
}

/* ******************************************************************************** */
// Entity Set Building
/* ******************************************************************************** */

/*
==================
Record_GetCurrentEntities
==================
*/
void Record_GetCurrentEntities( record_entityset_t *target ) {
	int i;
	if ( sv.num_entities > MAX_GENTITIES ) {
		Record_Printf( RP_ALL, "Record_GetCurrentEntities: sv.num_entities > MAX_GENTITIES\n" );
		return;
	}

	memset( target->activeFlags, 0, sizeof( target->activeFlags ) );

	for ( i = 0; i < sv.num_entities; ++i ) {
		sharedEntity_t *ent = SV_GentityNum( i );
		if ( !ent->r.linked )
			continue;
		if ( ent->s.number != i ) {
			Record_Printf( RP_DEBUG, "Record_GetCurrentEntities: bad ent->s.number\n" );
			continue;
		}
		target->entities[i] = ent->s;
		Record_Bit_Set( target->activeFlags, i );
	}
}

/*
==================
Record_GetCurrentBaselines
==================
*/
void Record_GetCurrentBaselines( record_entityset_t *target ) {
	int i;
	memset( target->activeFlags, 0, sizeof( target->activeFlags ) );

	for ( i = 0; i < MAX_GENTITIES; ++i ) {
		if ( !sv.svEntities[i].baseline.number )
			continue;
		if ( sv.svEntities[i].baseline.number != i ) {
			Record_Printf( RP_DEBUG, "Record_GetCurrentBaselines: bad baseline number\n" );
			continue;
		}
		target->entities[i] = sv.svEntities[i].baseline;
		Record_Bit_Set( target->activeFlags, i );
	}
}

/* ******************************************************************************** */
// Visibility Building
/* ******************************************************************************** */

/*
==================
Record_SetVisibleEntities

Based on sv_snapshot.c->SV_AddEntitiesVisibleFromPoint
==================
*/
static void Record_SetVisibleEntities( int clientNum, vec3_t origin, qboolean portal, record_visibility_state_t *target ) {
	int		e, i;
	sharedEntity_t *ent;
	svEntity_t	*svEnt;
	int		l;
	int		clientarea, clientcluster;
	int		leafnum;
	byte	*clientpvs;
	byte	*bitvector;

	if ( !sv.state ) {
		Record_Printf(RP_ALL, "Record_SetVisibleEntities: sv.state error\n");
		return;
	}

	leafnum = CM_PointLeafnum (origin);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);

	// calculate the visible areas
	target->areaVisibilitySize = CM_WriteAreaBits( (byte *)target->areaVisibility, clientarea );

	clientpvs = CM_ClusterPVS (clientcluster);

	for ( e = 0 ; e < sv.num_entities ; e++ ) {
		ent = SV_GentityNum(e);

		// never send entities that aren't linked in
		if ( !ent->r.linked ) {
			continue;
		}

		/*
		if (ent->s.number != e) {
			Com_DPrintf ("FIXING ENT->S.NUMBER!!!\n");
			ent->s.number = e;
		}
		*/

		// entities can be flagged to explicitly not be sent to the client
		if ( ent->r.svFlags & SVF_NOCLIENT ) {
			continue;
		}

		// entities can be flagged to be sent to only one client
		if ( ent->r.svFlags & SVF_SINGLECLIENT ) {
			if ( ent->r.singleClient != clientNum ) {
				continue;
			}
		}
		// entities can be flagged to be sent to everyone but one client
		if ( ent->r.svFlags & SVF_NOTSINGLECLIENT ) {
			if ( ent->r.singleClient == clientNum ) {
				continue;
			}
		}
		// entities can be flagged to be sent to a given mask of clients
		if ( ent->r.svFlags & SVF_CLIENTMASK ) {
			if (clientNum >= 32) {
				Record_Printf(RP_DEBUG, "Record_SetVisibleEntities: clientNum >= 32\n");
				continue; }
			if (~ent->r.singleClient & (1 << clientNum))
				continue;
		}

		svEnt = SV_SvEntityForGentity( ent );

		// don't double add an entity through portals
		if ( Record_Bit_Get(target->entVisibility, e) ) {
			continue;
		}

		// broadcast entities are always sent
		if ( ent->r.svFlags & SVF_BROADCAST ) {
			Record_Bit_Set(target->entVisibility, e);
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
		Record_Bit_Set(target->entVisibility, e);

		// if it's a portal entity, add everything visible from its camera position
		if ( ent->r.svFlags & SVF_PORTAL ) {
			if ( ent->s.generic1 ) {
				vec3_t dir;
				VectorSubtract(ent->s.origin, origin, dir);
				if ( VectorLengthSquared(dir) > (float) ent->s.generic1 * ent->s.generic1 ) {
					continue;
				}
			}
			Record_SetVisibleEntities( clientNum, ent->s.origin2, qtrue, target );
		}
	}

	ent = SV_GentityNum( clientNum );
	// extension: merge second PVS at ent->r.s.origin2
	if ( ent->r.svFlags & SVF_SELF_PORTAL2 && !portal ) {
		Record_SetVisibleEntities( clientNum, ent->r.s.origin2, qtrue, target );
	}
}

/*
==================
Record_CalculateCurrentVisibility

Based on sv_snapshot.c->SV_BuildClientSnapshot
==================
*/
static void Record_CalculateCurrentVisibility( int clientNum, record_visibility_state_t *target ) {
	playerState_t *ps = SV_GameClientNum( clientNum );
	vec3_t org;

	memset( target, 0, sizeof( *target ) );

	// find the client's viewpoint
	VectorCopy( ps->origin, org );
	org[2] += ps->viewheight;

	// Account for behavior of SV_BuildClientSnapshot under "never send client's own entity..."
	Record_Bit_Set( target->entVisibility, ps->clientNum );

	Record_SetVisibleEntities( ps->clientNum, org, qfalse, target );

	Record_Bit_Unset( target->entVisibility, ps->clientNum );
}

/*
==================
Record_GetCurrentVisibility

Try to get visibility from previously calculated snapshot, but if not available
(e.g. due to rate limited client) run the calculation directly
==================
*/
void Record_GetCurrentVisibility( int clientNum, record_visibility_state_t *target ) {
	int i;
	client_t *client = &svs.clients[clientNum];
	clientSnapshot_t *frame = &client->frames[ ( client->netchan.outgoingSequence - 1 ) & PACKET_MASK ];
	if ( !svs.currFrame || svs.currFrame->frameNum != frame->frameNum ) {
		Record_CalculateCurrentVisibility( clientNum, target );
		return;
	}

	memset( target, 0, sizeof( *target ) );

	target->areaVisibilitySize = frame->areabytes;
	for ( i = 0; i < MAX_MAP_AREA_BYTES / sizeof( int ); i++ ) {
		( (int *)target->areaVisibility )[i] = ( (int *)frame->areabits )[i] ^ -1;
	}

	for ( i = 0; i < frame->num_entities; ++i ) {
		int num = frame->ents[i]->number;
		if ( num < 0 || num >= MAX_GENTITIES ) {
			Record_Printf( RP_ALL, "Record_GetCurrentVisibility: invalid entity number\n" );
			Record_CalculateCurrentVisibility( clientNum, target );
			return;
		}
		Record_Bit_Set( target->entVisibility, num );
	}

	if ( sv_recordVerifyData->integer ) {
		record_visibility_state_t testVisibility;
		Record_CalculateCurrentVisibility( clientNum, &testVisibility );

		if ( memcmp( testVisibility.entVisibility, target->entVisibility, sizeof( testVisibility.entVisibility ) ) ) {
			Record_Printf( RP_ALL, "Record_GetCurrentVisibility: entVisibility discrepancy for client %i\n", clientNum );
		}

		if ( memcmp( testVisibility.areaVisibility, target->areaVisibility, sizeof( testVisibility.areaVisibility ) ) ) {
			Record_Printf( RP_ALL, "Record_GetCurrentVisibility: areaVisibility discrepancy for client %i\n", clientNum );
		}

		if ( testVisibility.areaVisibilitySize != target->areaVisibilitySize ) {
			Record_Printf( RP_ALL, "Record_GetCurrentVisibility: areaVisibilitySize discrepancy for client %i\n", clientNum );
		}
	}
}

/*
==================
Record_OptimizeInactiveVisibility

Sets bits for inactive entities that are set in the previous visibility, to reduce data usage
==================
*/
void Record_OptimizeInactiveVisibility( record_entityset_t *entityset, record_visibility_state_t *oldVisibility,
		record_visibility_state_t *source, record_visibility_state_t *target ) {
	int i;
	*target = *source; // Deal with non-entity stuff
	for ( i = 0; i < 32; ++i ) {
		// We should be able to assume no inactive entities are set as visible in the source
		if ( ( source->entVisibility[i] & entityset->activeFlags[i] ) != source->entVisibility[i] ) {
			Record_Printf( RP_ALL, "Record_OptimizeInactiveVisibility: inactive entity was visible in source\n" );
		}

		// Toggle visibility of inactive entities that are visible in the old visibility
		target->entVisibility[i] = source->entVisibility[i] |
				( oldVisibility->entVisibility[i] & ~entityset->activeFlags[i] );
	}
}

/* ******************************************************************************** */
// Message Building
/* ******************************************************************************** */

/*
==================
Record_CalculateBaselineCutoff

Returns first baseline index to drop due to msg overflow
==================
*/
static int Record_CalculateBaselineCutoff( record_entityset_t *baselines, msg_t msg ) {
	int i;
	byte buffer[MAX_MSGLEN];
	entityState_t nullstate;

	msg.data = buffer;
	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( i = 0; i < MAX_GENTITIES; ++i ) {
		if ( !Record_Bit_Get( baselines->activeFlags, i ) ) {
			continue;
		}
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, &baselines->entities[i], qtrue );
		if ( msg.cursize + 32 >= msg.maxsize ) {
			return i;
		}
	}

	return MAX_GENTITIES;
}

/*
==================
Record_WriteGamestateMessage
==================
*/
void Record_WriteGamestateMessage( record_entityset_t *baselines, char **configstrings, int clientNum,
		int serverCommandSequence, msg_t *msg, int *baselineCutoffOut ) {
	int i;
	entityState_t nullstate;

	MSG_WriteByte( msg, svc_gamestate );
	MSG_WriteLong( msg, serverCommandSequence );

	// Write configstrings
	for ( i = 0; i < MAX_CONFIGSTRINGS; ++i ) {
		if ( !*configstrings[i] ) {
			continue;
		}
		MSG_WriteByte( msg, svc_configstring );
		MSG_WriteShort( msg, i );
		MSG_WriteBigString( msg, configstrings[i] );
	}

	*baselineCutoffOut = Record_CalculateBaselineCutoff( baselines, *msg );

	// Write the baselines
	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( i = 0; i < MAX_GENTITIES; ++i ) {
		if ( !Record_Bit_Get( baselines->activeFlags, i ) ) {
			continue;
		}
		if ( i >= *baselineCutoffOut ) {
			continue;
		}
		MSG_WriteByte( msg, svc_baseline );
		MSG_WriteDeltaEntity( msg, &nullstate, &baselines->entities[i], qtrue );
	}

	MSG_WriteByte( msg, svc_EOF );

	// write the client num
	MSG_WriteLong( msg, clientNum );

	// write the checksum feed
	MSG_WriteLong( msg, 0 );
}

/*
==================
Record_WriteSnapshotMessage

Based on sv_snapshot.c->SV_SendClientSnapshot
For non-delta snapshot, set deltaEntities, deltaVisibility, deltaPs, and deltaFrame to null
==================
*/
void Record_WriteSnapshotMessage( record_entityset_t *entities, record_visibility_state_t *visibility, playerState_t *ps,
		record_entityset_t *deltaEntities, record_visibility_state_t *deltaVisibility, playerState_t *deltaPs,
		record_entityset_t *baselines, int baselineCutoff, int lastClientCommand, int deltaFrame, int snapFlags,
		int svTime, msg_t *msg ) {
	int i;

	MSG_WriteByte( msg, svc_snapshot );

	MSG_WriteLong( msg, svTime );

	// what we are delta'ing from
	MSG_WriteByte( msg, deltaFrame );

	// Write snapflags
	MSG_WriteByte( msg, snapFlags );

	// Write area visibility
	{
		int invertedAreaVisibility[8];
		for ( i = 0; i < 8; ++i ) {
			invertedAreaVisibility[i] = ~visibility->areaVisibility[i];
		}
		MSG_WriteByte( msg, visibility->areaVisibilitySize );
		MSG_WriteData( msg, invertedAreaVisibility, visibility->areaVisibilitySize );
	}

	// Write playerstate
	MSG_WriteDeltaPlayerstate( msg, deltaPs, ps );

	// Write entities
	for ( i = 0; i < MAX_GENTITIES; ++i ) {
		if ( Record_Bit_Get( entities->activeFlags, i ) && Record_Bit_Get( visibility->entVisibility, i ) ) {
			// Active and visible entity
			if ( deltaFrame && Record_Bit_Get( deltaEntities->activeFlags, i ) && Record_Bit_Get( deltaVisibility->entVisibility, i ) ) {
				// Keep entity (delta from previous entity)
				MSG_WriteDeltaEntity( msg, &deltaEntities->entities[i], &entities->entities[i], qfalse );
			} else {
				// New entity (delta from baseline if valid)
				if ( Record_Bit_Get( baselines->activeFlags, i ) && i < baselineCutoff ) {
					MSG_WriteDeltaEntity( msg, &baselines->entities[i], &entities->entities[i], qtrue );
				} else {
					entityState_t nullstate;
					Com_Memset( &nullstate, 0, sizeof( nullstate ) );
					MSG_WriteDeltaEntity( msg, &nullstate, &entities->entities[i], qtrue );
				}
			}
		} else if ( deltaFrame && Record_Bit_Get( deltaEntities->activeFlags, i ) && Record_Bit_Get( deltaVisibility->entVisibility, i ) ) {
			// Remove entity
			MSG_WriteBits( msg, i, GENTITYNUM_BITS );
			MSG_WriteBits( msg, 1, 1 );
		}
	}

	// End of entities
	MSG_WriteBits( msg, ( MAX_GENTITIES - 1 ), GENTITYNUM_BITS );
}
