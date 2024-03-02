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

#include "tr_local.h"
#include "vk.h"

#ifdef USE_VBO

/*

General concept of this VBO implementation is to store all possible static data
(vertexes,colors,tex.coords[0..1],normals) in device-local memory
and accessing it via indexes ONLY.

Static data in current meaning is a world surfaces whose shader data
can be evaluated at map load time.

Every static surface gets unique item index which will be added to queue
instead of tesselation like for regular surfaces. Using items queue also
eleminates run-time tesselation limits.

When it is time to render - we sort queued items to get longest possible
index sequence run to check if it is long enough i.e. worth issuing a draw call.
So long device-local index runs are rendered via multiple draw calls,
all remaining short index sequences are grouped together into single
host-visible index buffer which is finally rendered via single draw call.

*/

#define MAX_VBO_STAGES MAX_SHADER_STAGES

#define MIN_IBO_RUN 320

//[ibo]: [index0][index1][index2]
//[vbo]: [index0][vertex0...][index1][vertex1...][index2][vertex2...]

typedef struct vbo_item_s {
	int			index_offset;  // device-local, relative to current shader
	int			soft_offset;   // host-visible, absolute
	int			num_indexes;
	int			num_vertexes;
} vbo_item_t;

typedef struct ibo_item_s {
	int offset;
	int length;
} ibo_item_t;

typedef struct vbo_s {
	byte *vbo_buffer;
	int vbo_offset;
	int vbo_size;

	byte *ibo_buffer;
	int ibo_offset;
	int ibo_size;

	uint32_t soft_buffer_indexes;
	uint32_t soft_buffer_offset;

	ibo_item_t *ibo_items;
	int ibo_items_count;

	vbo_item_t *items;
	int items_count;

	int *items_queue;
	int items_queue_count;

} vbo_t;

static vbo_t world_vbo;

void VBO_Cleanup( void );

static bool isStaticRGBgen( colorGen_t cgen )
{
	switch ( cgen )
	{
		case CGEN_BAD:
		case CGEN_IDENTITY_LIGHTING:	// tr.identityLight
		case CGEN_IDENTITY:				// always (1,1,1,1)
		case CGEN_ENTITY:				// grabbed from entity's modulate field
		case CGEN_ONE_MINUS_ENTITY:		// grabbed from 1 - entity.modulate
		case CGEN_EXACT_VERTEX:			// tess.vertexColors
		case CGEN_VERTEX:				// tess.vertexColors * tr.identityLight
		case CGEN_ONE_MINUS_VERTEX:
		// case CGEN_WAVEFORM:			// programmatically generated
		case CGEN_LIGHTING_DIFFUSE:
		//case CGEN_FOG:				// standard fog
		case CGEN_CONST:				// fixed color
			return true;
		default:
			return false;
	}
}


static bool isStaticTCgen( const shaderStage_t *stage, int bundle )
{
	switch ( stage->bundle[bundle].tcGen )
	{
		case TCGEN_BAD:
		case TCGEN_IDENTITY:	// clear to 0,0
		case TCGEN_LIGHTMAP:
		case TCGEN_TEXTURE:
		//case TCGEN_ENVIRONMENT_MAPPED:
		//case TCGEN_ENVIRONMENT_MAPPED_FP:
		//case TCGEN_FOG:
		case TCGEN_VECTOR:		// S and T from world coordinates
			return true;
		case TCGEN_ENVIRONMENT_MAPPED:
			if ( bundle == 0 && (stage->tessFlags & TESS_ENV) )
				return true;
			else
				return false;
		default:
			return false;
	}
}


static bool isStaticTCmod( const textureBundle_t *bundle )
{
	int i;

	for ( i = 0; i < bundle->numTexMods; i++ ) {
		switch ( bundle->texMods[i].type ) {
			case TMOD_NONE:
			case TMOD_SCALE:
			case TMOD_TRANSFORM:
			case TMOD_OFFSET:
			case TMOD_SCALE_OFFSET:
			case TMOD_OFFSET_SCALE:
				break;
			default:
				return false;
		}
	}

	return true;
}


static bool isStaticAgen( alphaGen_t agen )
{
	switch ( agen )
	{
		case AGEN_IDENTITY:
		case AGEN_SKIP:
		case AGEN_ENTITY:
		case AGEN_ONE_MINUS_ENTITY:
		case AGEN_VERTEX:
		case AGEN_ONE_MINUS_VERTEX:
		//case AGEN_LIGHTING_SPECULAR:
		//case AGEN_WAVEFORM:
		//case AGEN_PORTAL:
		case AGEN_CONST:
			return true;
		default:
			return false;
	}
}


/*
=============
isStaticShader

Decide if we can put surface in static vbo
=============
*/
static bool isStaticShader( shader_t *shader )
{
	const shaderStage_t* stage;
	int i, b, svarsSize;

	if ( shader->isStaticShader )
		return true;

	if ( shader->isSky || shader->remappedShader )
		return false;

	if ( shader->numDeforms || shader->numUnfoggedPasses > MAX_VBO_STAGES )
		return false;

	svarsSize = 0;

	for ( i = 0; i < shader->numUnfoggedPasses; i++ )
	{
		stage = shader->stages[ i ];
		if ( !stage || !stage->active )
			break;
		if ( stage->depthFragment )
			return false;
		for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
			if ( !isStaticTCmod( &stage->bundle[b] ) )
				return false;
			if ( !isStaticTCgen( stage, b ) )
				return false;
			if ( stage->bundle[b].adjustColorsForFog != ACFF_NONE )
				return false;
			if ( !isStaticRGBgen( stage->bundle[b].rgbGen ) )
				return false;
			if ( !isStaticAgen( stage->bundle[b].alphaGen ) )
				return false;
		}
		if ( stage->tessFlags & TESS_RGBA0 )
			svarsSize += sizeof( color4ub_t );
		if ( stage->tessFlags & TESS_RGBA1 )
			svarsSize += sizeof( color4ub_t );
		if ( stage->tessFlags & TESS_RGBA2 )
			svarsSize += sizeof( color4ub_t );
		if ( stage->tessFlags & TESS_ST0 )
			svarsSize += sizeof( vec2_t );
		if ( stage->tessFlags & TESS_ST1 )
			svarsSize += sizeof( vec2_t );
		if ( stage->tessFlags & TESS_ST2 )
			svarsSize += sizeof( vec2_t );
	}

	if ( i == 0 )
		return false;

	shader->isStaticShader = true;

	// TODO: alloc separate structure?
	shader->svarsSize = svarsSize;
	shader->iboOffset = -1;
	shader->vboOffset = -1;
	shader->curIndexes = 0;
	shader->curVertexes = 0;
	shader->numIndexes = 0;
	shader->numVertexes = 0;

	return true;
}


static void VBO_AddGeometry( vbo_t *vbo, vbo_item_t *vi, shaderCommands_t *input )
{
	uint32_t size, offs;
	uint32_t offs_st[NUM_TEXTURE_BUNDLES];
	uint32_t offs_cl[NUM_TEXTURE_BUNDLES];
	int i;

	offs_st[0] = offs_st[1] = offs_st[2] = 0;
	offs_cl[0] = offs_cl[1] = offs_cl[2] = 0;

	if ( input->shader->iboOffset == -1 || input->shader->vboOffset == -1 ) {

		// allocate indexes
		input->shader->iboOffset = vbo->vbo_offset;
		vbo->vbo_offset += input->shader->numIndexes * sizeof( input->indexes[0] );

		// allocate xyz + normals + svars
		input->shader->vboOffset = vbo->vbo_offset;
		vbo->vbo_offset += input->shader->numVertexes * ( sizeof( input->xyz[0] ) + sizeof( input->normal[0] ) + input->shader->svarsSize );

		// go to normals offset
		input->shader->normalOffset = input->shader->vboOffset + input->shader->numVertexes * sizeof( input->xyz[0] );

		// go to first color offset
		offs = input->shader->normalOffset + input->shader->numVertexes * sizeof( input->normal[0] );

		for ( i = 0; i < MAX_VBO_STAGES; i++ )
		{
			shaderStage_t *pStage = input->xstages[ i ];
			if ( !pStage )
				break;

			if ( pStage->tessFlags & TESS_RGBA0 ) {
				offs_cl[0] = offs;
				pStage->rgb_offset[0] = offs; offs += input->shader->numVertexes * sizeof( color4ub_t );
			} else {
				pStage->rgb_offset[0] = offs_cl[0];
			}

			if ( pStage->tessFlags & TESS_RGBA1 ) {
				offs_cl[1] = offs;
				pStage->rgb_offset[1] = offs; offs += input->shader->numVertexes * sizeof( color4ub_t );
			} else {
				pStage->rgb_offset[1] = offs_cl[1];
			}

			if ( pStage->tessFlags & TESS_RGBA2 ) {
				offs_cl[2] = offs;
				pStage->rgb_offset[2] = offs; offs += input->shader->numVertexes * sizeof( color4ub_t );
			} else {
				pStage->rgb_offset[2] = offs_cl[2];
			}

			if ( pStage->tessFlags & TESS_ST0 )	{
				offs_st[0] = offs;
				pStage->tex_offset[0] = offs; offs += input->shader->numVertexes * sizeof( vec2_t );
			} else {
				pStage->tex_offset[0] = offs_st[0];
			}
			if ( pStage->tessFlags & TESS_ST1 ) {
				offs_st[1] = offs;
				pStage->tex_offset[1] = offs; offs += input->shader->numVertexes * sizeof( vec2_t );
			} else {
				pStage->tex_offset[1] = offs_st[1];
			}
			if ( pStage->tessFlags & TESS_ST2 ) {
				offs_st[2] = offs;
				pStage->tex_offset[2] = offs; offs += input->shader->numVertexes * sizeof( vec2_t );
			} else {
				pStage->tex_offset[2] = offs_st[2];
			}
		}

		input->shader->curVertexes = 0;
		input->shader->curIndexes = 0;
	}

	// shift indexes relative to current shader
	for ( i = 0; i < input->numIndexes; i++ )
		input->indexes[ i ] += input->shader->curVertexes;

	if ( vi->index_offset == -1 ) // one-time initialization
	{
		// initialize geometry offsets relative to current shader
		vi->index_offset = input->shader->curIndexes;
		vi->soft_offset = vbo->ibo_offset;
	}

	offs = input->shader->iboOffset + input->shader->curIndexes * sizeof( input->indexes[0] );
	size = input->numIndexes * sizeof( input->indexes[ 0 ] );
	if ( offs + size > vbo->vbo_size ) {
		ri.Error( ERR_DROP, "Index0 overflow" );
	}
	memcpy( vbo->vbo_buffer + offs, input->indexes, size );

	// fill soft buffer too
	if ( vbo->ibo_offset + size > vbo->ibo_size ) {
		ri.Error( ERR_DROP, "Index1 overflow" );
	}
	memcpy( vbo->ibo_buffer + vbo->ibo_offset, input->indexes, size );
	vbo->ibo_offset += size;
	//Com_Printf( "i offs=%i size=%i\n", offs, size );

	// vertexes
	offs = input->shader->vboOffset + input->shader->curVertexes * sizeof( input->xyz[0] );
	size = input->numVertexes * sizeof( input->xyz[ 0 ] );
	if ( offs + size > vbo->vbo_size ) {
		ri.Error( ERR_DROP, "Vertex overflow" );
	}
	//Com_Printf( "v offs=%i size=%i\n", offs, size );
	memcpy( vbo->vbo_buffer + offs, input->xyz, size );

	// normals
	offs = input->shader->normalOffset + input->shader->curVertexes * sizeof( input->normal[0] );
	size = input->numVertexes * sizeof( input->normal[ 0 ] );
	if ( offs + size > vbo->vbo_size ) {
		ri.Error( ERR_DROP, "Normals overflow" );
	}
	//Com_Printf( "v offs=%i size=%i\n", offs, size );
	memcpy( vbo->vbo_buffer + offs, input->normal, size );

	vi->num_indexes += input->numIndexes;
	vi->num_vertexes += input->numVertexes;
}


static void VBO_AddStageColors( vbo_t *vbo, const int stage, const shaderCommands_t *input, const int bundle )
{
	const int offs = input->xstages[ stage ]->rgb_offset[ bundle ] + input->shader->curVertexes * sizeof( color4ub_t );
	const int size = input->numVertexes * sizeof( color4ub_t );

	memcpy( vbo->vbo_buffer + offs, input->svars.colors[bundle], size );
}


static void VBO_AddStageTxCoords( vbo_t *vbo, const int stage, const shaderCommands_t *input, const int bundle )
{
	const int offs = input->xstages[ stage ]->tex_offset[ bundle ] + input->shader->curVertexes * sizeof( vec2_t );
	const int size = input->numVertexes * sizeof( vec2_t );

	memcpy( vbo->vbo_buffer + offs, input->svars.texcoordPtr[ bundle ], size );
}


void VBO_PushData( int itemIndex, shaderCommands_t *input )
{
	const shaderStage_t *pStage;
	vbo_t *vbo = &world_vbo;
	vbo_item_t *vi = vbo->items + itemIndex;
	int i;

	VBO_AddGeometry( vbo, vi, input );

	for ( i = 0; i < MAX_VBO_STAGES; i++ )
	{
		pStage = input->xstages[ i ];
		if ( !pStage )
			break;

		if ( pStage->tessFlags & TESS_RGBA0 )
		{
			R_ComputeColors( 0, tess.svars.colors[0], pStage );
			VBO_AddStageColors( vbo, i, input, 0 );
		}
		if ( pStage->tessFlags & TESS_RGBA1 )
		{
			R_ComputeColors( 1, tess.svars.colors[1], pStage );
			VBO_AddStageColors( vbo, i, input, 1 );
		}
		if ( pStage->tessFlags & TESS_RGBA2 )
		{
			R_ComputeColors( 2, tess.svars.colors[2], pStage );
			VBO_AddStageColors( vbo, i, input, 2 );
		}

		if ( pStage->tessFlags & TESS_ST0 )
		{
			R_ComputeTexCoords( 0, &pStage->bundle[0] );
			VBO_AddStageTxCoords( vbo, i, input, 0 );
		}
		if ( pStage->tessFlags & TESS_ST1 )
		{
			R_ComputeTexCoords( 1, &pStage->bundle[1] );
			VBO_AddStageTxCoords( vbo, i, input, 1 );
		}
		if ( pStage->tessFlags & TESS_ST2 )
		{
			R_ComputeTexCoords( 2, &pStage->bundle[2] );
			VBO_AddStageTxCoords( vbo, i, input, 2 );
		}
	}

	input->shader->curVertexes += input->numVertexes;
	input->shader->curIndexes += input->numIndexes;

	//Com_Printf( "%s: vert %i (of %i), ind %i (of %i)\n", input->shader->name,
	//	input->shader->curVertexes, input->shader->numVertexes,
	//	input->shader->curIndexes, input->shader->numIndexes );
}


void VBO_UnBind( void )
{
	tess.vboIndex = 0;
}


static int surfSortFunc( const void *a, const void *b )
{
	const msurface_t **sa = (const msurface_t **)a;
	const msurface_t **sb = (const msurface_t **)b;
	return (*sa)->shader - (*sb)->shader;
}


static void initItem( vbo_item_t *item )
{
	item->num_vertexes = 0;
	item->num_indexes = 0;

	item->index_offset = -1;
	item->soft_offset = -1;
}


void R_BuildWorldVBO( msurface_t *surf, int surfCount )
{
	vbo_t *vbo = &world_vbo;
	msurface_t **surfList;
	srfSurfaceFace_t *face;
	srfTriangles_t *tris;
	srfGridMesh_t *grid;
	msurface_t *sf;
	int ibo_size;
	int vbo_size;
	int i, n;

	int numStaticSurfaces = 0;
	int numStaticIndexes = 0;
	int numStaticVertexes = 0;

	if ( !r_vbo->integer )
		return;

	if ( glConfig.numTextureUnits < 3 ) {
		ri.Printf( PRINT_WARNING, "... not enough texture units for VBO\n" );
		return;
	}

	VBO_Cleanup();

	vbo_size = 0;

	// initial scan to count surfaces/indexes/vertexes for memory allocation
	for ( i = 0, sf = surf; i < surfCount; i++, sf++ ) {
		face = (srfSurfaceFace_t *) sf->data;
		if ( face->surfaceType == SF_FACE && isStaticShader( sf->shader ) ) {
			face->vboItemIndex = ++numStaticSurfaces;
			numStaticVertexes += face->numPoints;
			numStaticIndexes += face->numIndices;

			vbo_size += face->numPoints * (sf->shader->svarsSize + sizeof( tess.xyz[0] ) + sizeof( tess.normal[0] ) );
			sf->shader->numVertexes += face->numPoints;
			sf->shader->numIndexes += face->numIndices;
			continue;
		}
		tris = (srfTriangles_t *) sf->data;
		if ( tris->surfaceType == SF_TRIANGLES && isStaticShader( sf->shader ) ) {
			tris->vboItemIndex = ++numStaticSurfaces;
			numStaticVertexes += tris->numVerts;
			numStaticIndexes += tris->numIndexes;

			vbo_size += tris->numVerts * (sf->shader->svarsSize + sizeof( tess.xyz[0] ) + sizeof( tess.normal[0] ) );
			sf->shader->numVertexes += tris->numVerts;
			sf->shader->numIndexes += tris->numIndexes;
			continue;
		}
		grid = (srfGridMesh_t *) sf->data;
		if ( grid->surfaceType == SF_GRID && isStaticShader( sf->shader ) ) {
			grid->vboItemIndex = ++numStaticSurfaces;
			RB_SurfaceGridEstimate( grid, &grid->vboExpectVertices, &grid->vboExpectIndices );
			numStaticVertexes += grid->vboExpectVertices;
			numStaticIndexes += grid->vboExpectIndices;

			vbo_size += grid->vboExpectVertices * (sf->shader->svarsSize + sizeof( tess.xyz[0] ) + sizeof( tess.normal[0] ) );
			sf->shader->numVertexes += grid->vboExpectVertices;
			sf->shader->numIndexes += grid->vboExpectIndices;
			continue;
		}
	}

	if ( numStaticSurfaces == 0 ) {
		ri.Printf( PRINT_ALL, "...no static surfaces for VBO\n" );
		return;
	}

	vbo_size = PAD( vbo_size, 32 );

	ibo_size = numStaticIndexes * sizeof( tess.indexes[0] );
	ibo_size = PAD( ibo_size, 32 );

	// 0 item is unused
	vbo->items = ri.Hunk_Alloc( ( numStaticSurfaces + 1 ) * sizeof( vbo_item_t ), h_low );
	vbo->items_count = numStaticSurfaces;

	// last item will be used for run length termination
	vbo->items_queue = ri.Hunk_Alloc( ( numStaticSurfaces + 1 ) * sizeof( int ), h_low );
	vbo->items_queue_count = 0;

	ri.Printf( PRINT_ALL, "...found %i VBO surfaces (%i vertexes, %i indexes)\n",
		numStaticSurfaces, numStaticVertexes, numStaticIndexes );

	//Com_Printf( S_COLOR_CYAN "VBO size: %i\n", vbo_size );
	//Com_Printf( S_COLOR_CYAN "IBO size: %i\n", ibo_size );

	// vertex buffer
	vbo_size += ibo_size;
	vbo->vbo_buffer = ri.Hunk_AllocateTempMemory( vbo_size );
	vbo->vbo_offset = 0;
	vbo->vbo_size = vbo_size;

	// index buffer
	vbo->ibo_buffer = ri.Hunk_Alloc( ibo_size, h_low );
	vbo->ibo_offset = 0;
	vbo->ibo_size = ibo_size;

	// ibo runs buffer
	vbo->ibo_items = ri.Hunk_Alloc( ( (numStaticIndexes / MIN_IBO_RUN) + 1 ) * sizeof( ibo_item_t ), h_low );
	vbo->ibo_items_count = 0;

	surfList = ri.Hunk_AllocateTempMemory( numStaticSurfaces * sizeof( msurface_t* ) );

	for ( i = 0, n = 0, sf = surf; i < surfCount; i++, sf++ ) {
		face = (srfSurfaceFace_t *) sf->data;
		if ( face->surfaceType == SF_FACE && face->vboItemIndex ) {
			surfList[ n++ ] = sf;
			continue;
		}
		tris = (srfTriangles_t *) sf->data;
		if ( tris->surfaceType == SF_TRIANGLES && tris->vboItemIndex ) {
			surfList[ n++ ] = sf;
			continue;
		}
		grid = (srfGridMesh_t *) sf->data;
		if ( grid->surfaceType == SF_GRID && grid->vboItemIndex ) {
			surfList[ n++ ] = sf;
			continue;
		}
	}

	if ( n != numStaticSurfaces ) {
		ri.Error( ERR_DROP, "Invalid VBO surface count" );
	}

	// sort surfaces by shader
	qsort( surfList, numStaticSurfaces, sizeof( surfList[0] ), surfSortFunc );

	tess.numIndexes = 0;
	tess.numVertexes = 0;

	Com_Memset( &backEnd.viewParms, 0, sizeof( backEnd.viewParms ) );
	backEnd.currentEntity = &tr.worldEntity;

	for ( i = 0; i < numStaticSurfaces; i++ )
	{
		sf = surfList[ i ];
		face = (srfSurfaceFace_t *) sf->data;
		tris = (srfTriangles_t *) sf->data;
		grid = (srfGridMesh_t *) sf->data;
		if ( face->surfaceType == SF_FACE )
			face->vboItemIndex = i + 1;
		else if ( tris->surfaceType == SF_TRIANGLES ) {
			tris->vboItemIndex = i + 1;
		} else if ( grid->surfaceType == SF_GRID ){
			grid->vboItemIndex = i + 1;
		} else {
			ri.Error( ERR_DROP, "Unexpected surface type" );
		}
		initItem( vbo->items + i + 1 );
		RB_BeginSurface( sf->shader, 0 );
		tess.allowVBO = false; // block execution of VBO path as we need to tesselate geometry
#ifdef USE_TESS_NEEDS_NORMAL
		tess.needsNormal = true;
#endif
#ifdef USE_TESS_NEEDS_ST2
		tess.needsST2 = true;
#endif
		// tesselate
		rb_surfaceTable[ *sf->data ]( sf->data ); // VBO_PushData() may be called multiple times there
		// setup colors and texture coordinates
		VBO_PushData( i + 1, &tess );
		if ( grid->surfaceType == SF_GRID ) {
			vbo_item_t *vi = vbo->items + i + 1;
			if ( vi->num_vertexes != grid->vboExpectVertices || vi->num_indexes != grid->vboExpectIndices ) {
				ri.Error( ERR_DROP, "Unexpected grid vertexes/indexes count" );
			}
		}
		tess.numIndexes = 0;
		tess.numVertexes = 0;
	}

	ri.Hunk_FreeTempMemory( surfList );

//__fail:
	vk_alloc_vbo( vbo->vbo_buffer, vbo->vbo_size );

	//if ( err == GL_OUT_OF_MEMORY )
	//	ri.Printf( PRINT_WARNING, "%s: out of memory\n", __func__ );
	//else
	//	ri.Printf( PRINT_ERROR, "%s: error %i\n", __func__, err );
#if 0
	// reset vbo markers
	for ( i = 0, sf = surf; i < surfCount; i++, sf++ ) {
		face = (srfSurfaceFace_t *) sf->data;
		if ( face->surfaceType == SF_FACE ) {
			face->vboItemIndex = 0;
			continue;
		}
		tris = (srfTriangles_t *) sf->data;
		if ( tris->surfaceType == SF_TRIANGLES ) {
			tris->vboItemIndex = 0;
			continue;
		}
		grid = (srfGridMesh_t *) sf->data;
		if ( grid->surfaceType == SF_GRID ) {
			grid->vboItemIndex = 0;
			continue;
		}
	}
#endif

	// release host memory
	ri.Hunk_FreeTempMemory( vbo->vbo_buffer );
	vbo->vbo_buffer = NULL;

	// release GPU resources
	//VBO_Cleanup();
}


void VBO_Cleanup( void )
{
	int i;

	memset( &world_vbo, 0, sizeof( world_vbo ) );

	for ( i = 0; i < tr.numShaders; i++ )
	{
		tr.shaders[ i ]->isStaticShader = false;
		tr.shaders[ i ]->iboOffset = -1;
		tr.shaders[ i ]->vboOffset = -1;
	}
}


/*
=============
qsort_int
=============
*/
static void qsort_int( int *a, const int n ) {
	int temp, m;
	int i, j;

	if ( n < 32 ) { // CUTOFF
		for ( i = 1 ; i < n + 1 ; i++ ) {
			j = i;
			while ( j > 0 && a[j] < a[j-1] ) {
				temp = a[j];
				a[j] = a[j-1];
				a[j-1] = temp;
				j--;
			}
		}
		return;
	}

	i = 0;
	j = n;
	m = a[ n>>1 ];

	do {
		while ( a[i] < m ) i++;
		while ( a[j] > m ) j--;
		if ( i <= j ) {
			temp = a[i];
			a[i] = a[j];
			a[j] = temp;
			i++;
			j--;
		}
	} while ( i <= j );

	if ( j > 0 ) qsort_int( a, j );
	if ( n > i ) qsort_int( a+i, n-i );
}


static int run_length( const int *a, int from, int to, int *count )
{
	vbo_t *vbo = &world_vbo;
	int i, n, cnt;
	for ( cnt = 0, n = 1, i = from; i < to; i++, n++ )
	{
		cnt += vbo->items[a[i]].num_indexes;
		if ( a[i]+1 != a[i+1] )
			break;
	}
	*count = cnt;
	return n;
}


void VBO_QueueItem( int itemIndex )
{
	vbo_t *vbo = &world_vbo;

	if ( vbo->items_queue_count < vbo->items_count )
	{
		vbo->items_queue[vbo->items_queue_count++] = itemIndex;
	}
	else
	{
		ri.Error( ERR_DROP, "VBO queue overflow" );
	}
}


void VBO_ClearQueue( void )
{
	vbo_t *vbo = &world_vbo;
	vbo->items_queue_count = 0;
}


void VBO_Flush( void )
{
	if ( tess.vboIndex )
	{
		RB_EndSurface();
		tess.vboIndex = 0;
		RB_BeginSurface( tess.shader, tess.fogNum );
	}
}


static void VBO_AddItemDataToSoftBuffer( int itemIndex )
{
	vbo_t *vbo = &world_vbo;
	const vbo_item_t *vi = vbo->items + itemIndex;

	const uint32_t offset = vk_tess_index( vi->num_indexes, vbo->ibo_buffer + vi->soft_offset );

	if ( vbo->soft_buffer_indexes == 0 )
	{
		// start recording into host-visible memory
		vbo->soft_buffer_offset = offset;
	}

	vbo->soft_buffer_indexes += vi->num_indexes;
}


static void VBO_AddItemRangeToIBOBuffer( int offset, int length )
{
	vbo_t *vbo = &world_vbo;
	ibo_item_t *it;

	it = vbo->ibo_items + vbo->ibo_items_count++;

	it->offset = offset;
	it->length = length;
}


void VBO_RenderIBOItems( void )
{
	const vbo_t *vbo = &world_vbo;
	int i;

	// from device-local memory
	if ( vbo->ibo_items_count )
	{
		vk_bind_index_buffer( vk.vbo.vertex_buffer, tess.shader->iboOffset );

		for ( i = 0; i < vbo->ibo_items_count; i++ )
		{
			vk_draw_indexed( vbo->ibo_items[ i ].length, vbo->ibo_items[ i ].offset );
		}
	}

	// from host-visible memory
	if ( vbo->soft_buffer_indexes )
	{
		vk_bind_index_buffer( vk.cmd->vertex_buffer, vbo->soft_buffer_offset );

		vk_draw_indexed( vbo->soft_buffer_indexes, 0 );
	}
}


void VBO_PrepareQueues( void )
{
	vbo_t *vbo = &world_vbo;
	int i, item_run, index_run, n;
	const int *a;

	vbo->items_queue[ vbo->items_queue_count ] = 0; // terminate run

	// sort items so we can scan for longest runs
	if ( vbo->items_queue_count > 1 )
		qsort_int( vbo->items_queue, vbo->items_queue_count-1 );

	vbo->soft_buffer_indexes = 0;
	vbo->ibo_items_count = 0;

	a = vbo->items_queue;
	i = 0;
	while ( i < vbo->items_queue_count )
	{
		item_run = run_length( a, i, vbo->items_queue_count, &index_run );
		if ( index_run < MIN_IBO_RUN )
		{
			for ( n = 0; n < item_run; n++ )
				VBO_AddItemDataToSoftBuffer( a[ i + n ] );
		}
		else
		{
			vbo_item_t *start = vbo->items + a[ i ];
			vbo_item_t *end = vbo->items + a[ i + item_run - 1 ];
			n = (end->index_offset - start->index_offset) + end->num_indexes;
			VBO_AddItemRangeToIBOBuffer( start->index_offset, n );
		}
		i += item_run;
	}
}

#endif // USE_VBO
