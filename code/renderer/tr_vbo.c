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

/*

General concept of this VBO implementation is to store all possible static data 
(vertexes,colors,tex.coords[2]) in VBO and accessing it via indexes ONLY.
Static data in current meaning is a world surfaces with 1-stage shaders
that can be evaluated at map load time.

Every static surface gets unique item index which will be added to queue 
instead of tesselation like for regular surfaces. Using items queue also
eleminates run-time tesselation limits.

When it is time to render - we sort queued items to get longest possible 
index sequence run to check if its long enough i.e. worth switching to GPU-side IBO.
So long index runs are rendered via multiple glDrawElements() calls, 
all remaining short index sequences are grouped together into single software index
which is finally rendered via single legacy index array transfer.

For VBO storage 'Structure of Arrays' approach were selected as it is much easier to
maintain, also it can be used for re-tesselation in future. 
No performance differences from 'Array of Structures' were observed.

*/

//#define USE_NORMALS
#define MAX_VBO_STAGES 1

#define MIN_IBO_RUN 256

typedef struct vbo_stage_s {
	int color_offset;
	int tex_offset[2];
} vbo_stage_t;

typedef struct vbo_item_s {
	vbo_stage_t	stages[ MAX_VBO_STAGES ];
	int			index_offset;
#ifdef USE_NORMALS
	int			normal_offset;	
#endif
	int			num_indexes;
	int			num_vertexes;
} vbo_item_t;

typedef struct ibo_item_s {
	int offset;
	int length;
} ibo_item_t;

typedef struct vbo_s {
	byte *ibo_buffer;
	int ibo_buffer_used;
	int ibo_buffer_size;
	int ibo_index_count;

	glIndex_t *soft_buffer;
	int soft_buffer_indexes;

	ibo_item_t *ibo_items;
	int ibo_items_count;

	byte *vbo_buffer;
	int vbo_buffer_used;
	int vbo_buffer_size;
	int vbo_vertex_count;

	vbo_item_t *items;
	int items_count;

	int *items_queue;
	int items_queue_count;

	intptr_t index_base;
	int index_used;

	intptr_t vertex_base;
	int vertex_used;
	int vertex_stride;

#ifdef USE_NORMALS
	intptr_t normal_base;
	int normal_used;
	int normal_stride;
#endif

	intptr_t color_base;
	int color_used;
	int color_stride;

	intptr_t tex_base[2];
	int tex_used[2];
	int texture_stride;

} vbo_t;

vbo_t world_vbo;

GLuint VBO_world_data;
GLuint VBO_world_indexes;

int VBO_indices_total;
int VBO_indices_longest_sw_run;
int VBO_indices_longest_hw_run;
int VBO_indices_num_sw_items;
int VBO_indices_num_hw_items;

void VBO_Cleanup( void );

static const char *genATestFP( int function )
{
	switch ( function )
	{
		case GLS_ATEST_GT_0:
			return
				"MOV t.x, -base.a; \n" // '>0' -> '<0'
				"SLT t.x, t.x, 0.0; \n" // if ( t.x < 0 ) t.x = 1; else t.x = 0;
				"SUB t.x, t.x, 0.5; \n" // if (( t.x - 0.5 ) < 0) kill_fragment;
				"KIL t.x;\n ";
		case GLS_ATEST_LT_80:
			return
				"SGE t.x, base.a, 0.5; \n" 
				"MOV t.x, -t.x; \n" // "MUL t.x, t.x, {-1.0}; \n"
				"KIL t.x;\n ";
		case GLS_ATEST_GE_80:
			return
				"SGE t.x, base.a, 0.5; \n"
				"SUB t.x, t.x, {0.5}; \n"
				"KIL t.x;\n";
		default:
			return "";
	}
}


const char *BuildFogVP( int multitexture, int fogmode )
{
	static char buf[1024];

	strcpy( buf,
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n" );

	switch ( fogmode ) {
		case 0: strcat( buf, fogInVPCode ); break;
		case 1: strcat( buf, fogOutVPCode ); break;
		default: break;
	}

	switch ( multitexture ) {
		case GL_ADD:
		case GL_MODULATE:
			strcat( buf,
			"MOV result.texcoord[0], vertex.texcoord[0]; \n"
			"MOV result.texcoord[1], vertex.texcoord[1]; \n" );
			break;
		case GL_REPLACE:
			strcat( buf,
			"MOV result.texcoord[1], vertex.texcoord[1]; \n" );
			break;
		default:
			strcat( buf,
			"MOV result.texcoord[0], vertex.texcoord[0]; \n" );
			break;
	}

	strcat( buf,
	"MOV result.color, vertex.color; \n"
	"END \n" );

	return buf;
}


const char *BuildFogFP( int multitexture, int alphatest )
{
	static char buf[1024];

	strcpy( buf, "!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"TEMP base, t; \n" );

	switch ( multitexture ) {
		case 0:
			strcat( buf, "TEX base, fragment.texcoord[0], texture[0], 2D; \n" );
			strcat( buf, genATestFP( alphatest ) );
			break;
		case GL_ADD:
			strcat( buf, "TEX base, fragment.texcoord[0], texture[0], 2D; \n" );
			strcat( buf, genATestFP( alphatest ) );
			strcat( buf, "TEX t,    fragment.texcoord[1], texture[1], 2D; \n"
			"ADD base, base, t; \n" );
			break;
		case GL_MODULATE:
			strcat( buf, "TEX base, fragment.texcoord[0], texture[0], 2D; \n" );
			strcat( buf, genATestFP( alphatest ) );
			strcat( buf, "TEX t,    fragment.texcoord[1], texture[1], 2D; \n" );
			strcat( buf, "MUL base, base, t; \n" );
			break;
		case GL_REPLACE:
			strcat( buf, "TEX base, fragment.texcoord[1], texture[1], 2D; \n" );
			//strcat( buf, genATestFP( alphatest ) );
			break;
		default:
			ri.Error( ERR_DROP, "Invalid multitexture mode %04x", multitexture );
			break;
	}

	//strcat( buf, "MUL_SAT base, base, fragment.color; \n" );
	strcat( buf, "MUL base, base, fragment.color; \n" );

	strcat( buf, "TEMP fog; \n"
	"TEX fog, fragment.texcoord[4], texture[2], 2D; \n"
	"MUL fog, fog, program.local[0]; \n"
	//"LRP_SAT base, fog.a, fog, base; \n" );
	"LRP_SAT result.color, fog.a, fog, base; \n" );

	//strcat( buf, "MOV result.color, base; \n" );
	strcat( buf, "END \n" );
	return buf;
}


// multitexture modes: disabled, add, modulate, replace
// fog modes: eye-in, eye-out
static GLuint vbo_vp[4*2];

// multitexture modes: disabled, add, modulate, replace
// alpha test modes: disabled, GT0, LT80, GE80
static GLuint vbo_fp[4*4];


static int getVPindex( int multitexture, int fogmode )
{
	int index;
	switch( multitexture )
	{
		default:			index = 0; break;
		case GL_ADD:		index = 1; break;
		case GL_MODULATE:	index = 2; break;
		case GL_REPLACE:	index = 3; break;
	}
	index <<= 1;
	index |= fogmode & 1; // eye-in | eye-out

	return index;
}


static int getFPindex( int multitexture, int atest )
{
	int index;
	switch( multitexture )
	{
		default:			index = 0; break;
		case GL_ADD:		index = 1; break;
		case GL_MODULATE:	index = 2; break;
		case GL_REPLACE:	index = 3; break;
	}
	index <<= 2;
	switch ( atest )
	{
		case GLS_ATEST_GT_0: index |= 1; break;
		case GLS_ATEST_LT_80: index |= 2; break;
		case GLS_ATEST_GE_80: index |= 3; break;
		default: break;
	}
	return index;
}


static qboolean isStaticRGBgen( colorGen_t cgen )
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
		// case CGEN_WAVEFORM,			// programmatically generated
		case CGEN_LIGHTING_DIFFUSE:
		//case CGEN_FOG,				// standard fog
		case CGEN_CONST:				// fixed color
			return qtrue;
		default: 
			return qfalse;
	}
}


static qboolean isStaticTCgen( texCoordGen_t tcgen )
{
	switch ( tcgen )
	{
		case TCGEN_BAD:
		case TCGEN_IDENTITY:	// clear to 0,0
		case TCGEN_LIGHTMAP:
		case TCGEN_TEXTURE:
		//case TCGEN_ENVIRONMENT_MAPPED:
		//case TCGEN_ENVIRONMENT_MAPPED_FP:
		//case TCGEN_FOG:
		case TCGEN_VECTOR:		// S and T from world coordinates
			return qtrue;
		default:
			return qfalse;
	}
} 


static qboolean isStaticAgen( alphaGen_t agen )
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
			return qtrue;
		default: 
			return qfalse;
	}
}


/*
=============
isStaticShader

Decide if we can put surface in static vbo
=============
*/
static qboolean isStaticShader( shader_t *shader )
{
	const shaderStage_t* stage;
	int i, mtx;

	if ( shader->isStaticShader )
		return qtrue;

	if ( shader->isSky )
		return qfalse;

	if ( shader->numDeforms || shader->numUnfoggedPasses > MAX_VBO_STAGES )
		return qfalse;

	for ( i = 0; i < shader->numUnfoggedPasses; i++ )
	{
		stage = shader->stages[ i ];
		if ( !stage || !stage->active )
			break;
		if ( stage->depthFragment )
			return qfalse;
		if ( stage->adjustColorsForFog != ACFF_NONE )
			return qfalse;
		if ( stage->bundle[0].numTexMods || ( stage->bundle[1].image[0] && stage->bundle[1].numTexMods ) )
			return qfalse;
		if ( !isStaticRGBgen( stage->rgbGen ) )
			return qfalse;
		if ( !isStaticTCgen( stage->bundle[0].tcGen ) || ( stage->bundle[1].image[0] && !isStaticTCgen( stage->bundle[1].tcGen ) ) )
			return qfalse;
		if ( !isStaticAgen( stage->alphaGen ) )
			return qfalse;
	}

	if ( i == 0 )
		return qfalse;

	shader->isStaticShader = qtrue;
	mtx = shader->stages[0]->bundle[1].image[0] ? shader->multitextureEnv : 0;

	shader->vboVPindex = getVPindex( mtx, 0 );
	// generate vertex programs
	if ( !vbo_vp[ shader->vboVPindex ] )
	{
		qglGenProgramsARB( 2, &vbo_vp[ shader->vboVPindex ] );
		ARB_CompileProgram( Vertex, BuildFogVP( mtx, 0 ), vbo_vp[ shader->vboVPindex + 0 ] );
		ARB_CompileProgram( Vertex, BuildFogVP( mtx, 1 ), vbo_vp[ shader->vboVPindex + 1 ] );
	}

	shader->vboFPindex = getFPindex( mtx, shader->stages[0]->stateBits & GLS_ATEST_BITS );
	// generate fragment programs
	if ( !vbo_fp[ shader->vboFPindex ] )
	{
		qglGenProgramsARB( 1, &vbo_fp[ shader->vboFPindex ] );
		ARB_CompileProgram( Fragment, BuildFogFP( mtx, shader->stages[0]->stateBits & GLS_ATEST_BITS), vbo_fp[ shader->vboFPindex ] );
	}

	return qtrue;
}


static void VBO_AddGeometry( vbo_t *vbo, vbo_item_t *vi, shaderCommands_t *input )
{
	int i, size, offs;
	glIndex_t *idx;

	// shift indexes
	for ( i = 0; i < input->numIndexes; i++ )
		input->indexes[ i ] += vbo->vbo_vertex_count;

	offs = vbo->index_base + vbo->index_used;
	if ( vi->index_offset == -1 ) // set only once
		vi->index_offset = offs;

	size = input->numIndexes * sizeof( input->indexes[ 0 ] );
	idx = (glIndex_t*)(vbo->ibo_buffer + offs);
	for ( i = 0; i < input->numIndexes; i++ )
		idx[ i ] = input->indexes[ i ];
	vbo->index_used += size;
	vi->num_indexes += input->numIndexes;
	vbo->ibo_index_count += input->numIndexes;

	// vertexes
	offs = vbo->vertex_base + vbo->vertex_used;

	if ( vbo->vertex_stride == sizeof( input->xyz[ 0 ] ) )
	{
		// better for potential re-tesselation
		size = input->numVertexes * sizeof( input->xyz[ 0 ] );
		memcpy( vbo->vbo_buffer + offs, input->xyz, size );
	}
	else
	{
		// compact vec3_t representation 
		float *v;
		size = input->numVertexes * sizeof( vec3_t );
		v = (float*)(vbo->vbo_buffer + offs);
		for ( i = 0; i < input->numVertexes ; i++ ) {
			v[0] = input->xyz[i][0];
			v[1] = input->xyz[i][1];
			v[2] = input->xyz[i][2];
			v += 3;
		}
	}

	vi->num_vertexes += input->numVertexes; // increase instead of assign
	vbo->vertex_used += size;

	// normals
#ifdef USE_NORMALS
	offs = vbo->normal_base + vbo->normal_used;
	vi->normal_offset = offs;
	size = input->numVertexes * sizeof( input->normal[ 0 ] );
	memcpy( vbo->vbo_buffer + offs, input->normal, size );
	vbo->normal_used += size;
#endif

	vbo->vbo_vertex_count += input->numVertexes; // update for index base
}


static void VBO_AddStageColors( vbo_t *vbo, vbo_item_t *vi, const shaderCommands_t *input )
{
	vbo_stage_t *st = vi->stages + 0;
	int offs = vbo->color_base + vbo->color_used;
	int size;
	if ( st->color_offset == -1 ) // set only once
		st->color_offset = offs;
	size = input->numVertexes * sizeof( input->vertexColors[ 0 ] );
	memcpy( vbo->vbo_buffer + offs, input->svars.colors, size );
	vbo->color_used += size;
}


static void VBO_AddStageTxCoords( vbo_t *vbo, vbo_item_t *vi, const shaderCommands_t *input, int unit )
{
	vbo_stage_t *st = vi->stages + 0;
	int offs = vbo->tex_base[ unit ] + vbo->tex_used[ unit ];
	int size;
	if ( st->tex_offset[ unit ] == -1 ) // set only once
		st->tex_offset[ unit ] = offs;
	size = input->numVertexes * sizeof( input->svars.texcoords[ 0 ][ 0 ] );
	memcpy( vbo->vbo_buffer + offs, &input->svars.texcoords[ unit ][ 0 ], size );
	vbo->tex_used[ unit ] += size;
}


void VBO_PushData( int itemIndex, shaderCommands_t *input )
{
	const shaderStage_t *pStage = input->xstages[ 0 ];
	vbo_t *vbo = &world_vbo;
	vbo_item_t *vi = vbo->items + itemIndex;
	
	VBO_AddGeometry( vbo, vi, input );

	R_ComputeColors( pStage );
	R_ComputeTexCoords( pStage );
	VBO_AddStageColors( vbo, vi, input );
	VBO_AddStageTxCoords( vbo, vi, input, 0 );
	VBO_AddStageTxCoords( vbo, vi, input, 1 );
}


static GLuint curr_index_bind = 0;
static GLuint curr_vertex_bind = 0;

int VBO_Active( void )
{
	return curr_vertex_bind;
}


static qboolean VBO_BindData( void )
{
	if ( curr_vertex_bind )
		return qfalse;

	qglBindBufferARB( GL_ARRAY_BUFFER_ARB, VBO_world_data ); 
	curr_vertex_bind = VBO_world_data;
	return qtrue;
}


static void VBO_BindIndex( qboolean enable )
{
	if ( !enable )
	{
		if ( curr_index_bind )
		{
			qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
			curr_index_bind = 0;
		}
	}
	else
	{
		if ( !curr_index_bind )
		{
			qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, VBO_world_indexes );
			curr_index_bind = VBO_world_indexes;
		}
	}
}


void VBO_UnBind( void )
{
	if ( curr_index_bind )
	{
		qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
		curr_index_bind = 0;
	}

	if ( curr_vertex_bind )
	{
		qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		curr_vertex_bind = 0;
	}

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
	item->stages[0].color_offset = -1;
	item->stages[0].tex_offset[0] = -1;
	item->stages[0].tex_offset[1] = -1;
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
	int tex_size;
	int col_size;
	int vert_size;
#ifdef USE_NORMALS
	int norm_size;
#endif
	GLenum err;

	int numStaticSurfaces = 0;
	int numStaticIndexes = 0;
	int numStaticVertexes = 0;

	if ( !qglBindBufferARB || !r_vbo->integer )
		return;

	if ( glConfig.numTextureUnits < 3 ) {
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "... not enough texture units for VBO" );
		return;
	}

	VBO_Cleanup();

	// initial scan to count surfaces/indexes/vertexes for memory allocation
	for ( i = 0, sf = surf; i < surfCount; i++, sf++ ) {
		face = (srfSurfaceFace_t *) sf->data;
		if ( face->surfaceType == SF_FACE && isStaticShader( sf->shader ) ) {
			face->vboItemIndex = ++numStaticSurfaces;
			numStaticVertexes += face->numPoints;	
			numStaticIndexes += face->numIndices;
			continue;
		}
		tris = (srfTriangles_t *) sf->data;
		if ( tris->surfaceType == SF_TRIANGLES && isStaticShader( sf->shader ) ) {
			tris->vboItemIndex = ++numStaticSurfaces;
			numStaticVertexes += tris->numVerts;
			numStaticIndexes += tris->numIndexes;
			continue;
		}
		grid = (srfGridMesh_t *) sf->data;
		if ( grid->surfaceType == SF_GRID && isStaticShader( sf->shader ) ) {
			grid->vboItemIndex = ++numStaticSurfaces;
			RB_SurfaceGridEstimate( grid, &grid->vboExpectVertices, &grid->vboExpectIndices );
			numStaticVertexes += grid->vboExpectVertices;
			numStaticIndexes += grid->vboExpectIndices;
			continue;
		}
	}

	if ( !numStaticSurfaces ) {
		ri.Printf( PRINT_ALL, "...no static surfaces for VBO\n" );
		return;
	}

	Com_Memset( vbo, 0, sizeof( *vbo ) );

	// 0 item is unused
	vbo->items = ri.Hunk_Alloc( ( numStaticSurfaces + 1 ) * sizeof( vbo_item_t ), h_low );
	vbo->items_count = numStaticSurfaces;

	// last item will be used for run length termination
	vbo->items_queue = ri.Hunk_Alloc( ( numStaticSurfaces + 1 ) * sizeof( int ), h_low );
	vbo->items_queue_count = 0;

	ri.Printf( PRINT_ALL, "...found %i VBO surfaces (%i vertexes, %i indexes)\n", 
		numStaticSurfaces, numStaticVertexes, numStaticIndexes );
	
	// setup vertex buffers
	vbo->vertex_stride = sizeof( vec3_t ); // compact
	//vbo->vertex_stride = sizeof( tess.xyz[0] ); // better for re-tesselation
	vert_size = numStaticVertexes * vbo->vertex_stride;

#ifdef USE_NORMALS
	vbo->normal_stride = sizeof( tess.normal[0] );
	norm_size = numStaticVertexes * vbo->normal_stride;
#endif

	col_size = numStaticVertexes * sizeof( tess.vertexColors[0] );
	tex_size = numStaticVertexes * sizeof( tess.texCoords[0][0] );

	vbo->vertex_base = 0;
#ifdef USE_NORMALS
	vbo->normal_base = vbo->vertex_base + vert_size;
	vbo->color_base = vbo->normal_base + norm_size;
#else
	vbo->color_base = vbo->vertex_base + vert_size;
#endif
	vbo->tex_base[0] = vbo->color_base + col_size;
	vbo->tex_base[1] = vbo->tex_base[0] + tex_size;

#ifdef USE_NORMALS	
	vbo_size = vbo->vertex_stride + vbo->normal_stride + 1 * ( sizeof( tess.vertexColors[0] ) + sizeof( tess.texCoords[0] ) );
#else
	vbo_size = vbo->vertex_stride + 1 * ( sizeof( tess.vertexColors[0] ) + sizeof( tess.texCoords[0] ) );
#endif
	vbo_size *= numStaticVertexes;

	// index buffer
	ibo_size = numStaticIndexes * sizeof( tess.indexes[0] );
	vbo->ibo_buffer = ri.Hunk_Alloc( ibo_size, h_low );	

	// soft index buffer
	vbo->soft_buffer = ri.Hunk_Alloc( ibo_size, h_low );
	vbo->soft_buffer_indexes = 0;

	// ibo runs buffer
	vbo->ibo_items = ri.Hunk_Alloc( ( (numStaticIndexes / MIN_IBO_RUN) + 1 ) * sizeof( ibo_item_t ), h_low );
	vbo->ibo_items_count = 0;

	vbo->index_base = 0;

	vbo->vbo_buffer = ri.Hunk_AllocateTempMemory( vbo_size );

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
			face->vboItemIndex = i+1;
		else if ( tris->surfaceType == SF_TRIANGLES ) {
			tris->vboItemIndex = i+1;
		} else if ( grid->surfaceType == SF_GRID ){
			grid->vboItemIndex = i+1;
		} else {
			ri.Error( ERR_DROP, "Unexpected surface type" );
		}
		initItem( vbo->items + i + 1 );
		RB_BeginSurface( sf->shader, 0 );
		tess.allowVBO = qfalse; // block execution of VBO path as we need to tesselate geometry
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

	 // fixup vertex stride
	if ( vbo->vertex_stride == sizeof( vec3_t ) )
		vbo->vertex_stride = 0;

	vbo->ibo_buffer_used = ibo_size;
	vbo->vbo_buffer_used = vbo_size;

	// reset error state
	qglGetError();
	err = GL_NO_ERROR;

	if ( !VBO_world_data ) {
		qglGenBuffersARB( 1, &VBO_world_data );
		if ( (err = qglGetError()) != GL_NO_ERROR )
			goto __fail;
	}

	// upload vertex array & colors & textures
	if ( VBO_world_data ) {
		VBO_BindData();
		qglBufferDataARB( GL_ARRAY_BUFFER_ARB, vbo->vbo_buffer_used, vbo->vbo_buffer, GL_STATIC_DRAW_ARB );
		if ( (err = qglGetError()) != GL_NO_ERROR )
			goto __fail;
	}

	if ( !VBO_world_indexes ) {
		qglGenBuffersARB( 1, &VBO_world_indexes );
		if ( (err = qglGetError()) != GL_NO_ERROR )
			goto __fail;
	}

	// upload index array
	if ( VBO_world_indexes ) {
		VBO_BindIndex( qtrue );
		qglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, vbo->ibo_buffer_used, vbo->ibo_buffer, GL_STATIC_DRAW_ARB );
		if ( (err = qglGetError()) != GL_NO_ERROR )
			goto __fail;
	}

	VBO_UnBind();
	ri.Hunk_FreeTempMemory( vbo->vbo_buffer );
	vbo->vbo_buffer = NULL;
	return;

__fail:

	if ( err == GL_OUT_OF_MEMORY )
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "%s: out of memory\n", __func__ );
	else
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "%s: error %i\n", __func__, err );

	// reset vbo markers
	for ( i = 0, n = 0, sf = surf; i < surfCount; i++, sf++ ) {
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

	VBO_UnBind();

	// release host memory
	ri.Hunk_FreeTempMemory( vbo->vbo_buffer );
	
	// release GPU resources
	VBO_Cleanup();
}


void VBO_Cleanup( void )
{
	int i;
	if ( qglGenBuffersARB )
	{
		VBO_UnBind();
		if ( VBO_world_data )
		{
			qglDeleteBuffersARB( 1, &VBO_world_data );
			VBO_world_data = 0;
		}
		if ( VBO_world_indexes )
		{
			qglDeleteBuffersARB( 1, &VBO_world_indexes );
			VBO_world_indexes = 0;
		}
	}

	for ( i = 0; i < ARRAY_LEN( vbo_vp ); i++ )
	{
		if ( vbo_vp[i] )
		{
			qglDeleteProgramsARB( 1, vbo_vp + i );
		}
	}
	memset( vbo_vp, 0, sizeof( vbo_vp ) );

	for ( i = 0; i < ARRAY_LEN( vbo_fp ); i++ )
	{
		if ( vbo_fp[i] )
		{
			qglDeleteProgramsARB( 1, vbo_fp + i );
		}
	}
	memset( vbo_fp, 0, sizeof( vbo_fp ) );

	memset( &world_vbo, 0, sizeof( world_vbo ) );
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

	if ( vbo->items_queue_count >= vbo->items_count ) 
	{
		ri.Error( ERR_DROP, "VBO queue overflow" );
		return;
	}

	vbo->items_queue[ vbo->items_queue_count++ ] = itemIndex;
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
	VBO_UnBind();
}


static void VBO_AddItemDataToSoftBuffer( int itemIndex )
{
	vbo_t *vbo = &world_vbo;
	vbo_item_t *vi = vbo->items + itemIndex;

	memcpy( &vbo->soft_buffer[ vbo->soft_buffer_indexes ],
		vbo->ibo_buffer + vi->index_offset,
		vi->num_indexes * sizeof( glIndex_t ) );

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


static void VBO_RenderSoftBuffer( void )
{
	vbo_t *vbo = &world_vbo;

	if ( vbo->soft_buffer_indexes )
	{
		VBO_BindIndex( qfalse );
		qglDrawElements( GL_TRIANGLES, vbo->soft_buffer_indexes, GL_INDEX_TYPE, vbo->soft_buffer );
	}
}


static void VBO_RenderIBOBuffer( void )
{
	vbo_t *vbo = &world_vbo;
	int i;

	if ( vbo->ibo_items_count )
	{ 
		VBO_BindIndex( qtrue );

		for ( i = 0; i < vbo->ibo_items_count; i++ )
		{
			qglDrawElements( GL_TRIANGLES, vbo->ibo_items[ i ].length, GL_INDEX_TYPE, (const GLvoid *)(intptr_t) vbo->ibo_items[ i ].offset );
		}
	}
}


static void VBO_RenderBuffers( void )
{
	if ( curr_index_bind )
	{
		VBO_RenderIBOBuffer();
		VBO_RenderSoftBuffer();
	}
	else
	{
		VBO_RenderSoftBuffer();
		VBO_RenderIBOBuffer();
	}
}


static void VBO_RenderIndexQueue( qboolean mtx )
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
			VBO_BindIndex( qtrue );
			n = (end->index_offset - start->index_offset) / sizeof(glIndex_t) + end->num_indexes;
			VBO_AddItemRangeToIBOBuffer( start->index_offset, n );
		}
		i += item_run;
	}

	VBO_RenderBuffers();

	if ( r_showtris->integer ) 
	{
		if ( mtx )
		{
			qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
			qglDisable( GL_TEXTURE_2D );
			GL_SelectTexture( 0 );
		}
		GL_Bind( tr.whiteImage );
		qglColor3f( 0.25f, 1.0f, 0.25f );
		GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE );
		qglDepthRange( 0, 0 );
		qglDisableClientState( GL_COLOR_ARRAY );
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		qglColor3f( 0.25f, 1.0f, 0.25f );
		VBO_RenderBuffers();
		qglDepthRange( 0, 1 );
		if ( mtx )
		{
			GL_SelectTexture( 1 );
		}
	}

	//vbo->soft_buffer_indexes = 0;
	//vbo->ibo_items_count = 0;
	// VBO_UnBind();
}


/*
** RB_IterateStagesVBO
*/
static void RB_IterateStagesVBO( const shaderCommands_t *input )
{
	//static qboolean setupMultitexture = qtrue;
	const shaderStage_t *pStage = tess.xstages[ 0 ];
	const vbo_t *vbo = &world_vbo;
	const fogProgramParms_t *fparm;
	int stateBits;
	qboolean updateArrays;
	qboolean fogPass;
	GLuint vp, fp;

	fogPass = ( tess.fogNum && tess.shader->fogPass );
	stateBits = pStage->stateBits;

	fparm = NULL;

	if ( fogPass )
	{
		stateBits &= ~GLS_ATEST_BITS; // done in shaders

		fparm = RB_CalcFogProgramParms();

		if ( fparm->eyeOutside )
			vp = vbo_vp[ tess.shader->vboVPindex + 1 ];
		else
			vp = vbo_vp[ tess.shader->vboVPindex + 0 ];

		fp = vbo_fp[ tess.shader->vboFPindex ];
	}
	else
	{
		vp = fp = 0;
	}

	ARB_ProgramEnableExt( vp, fp );

	if ( fogPass )
	{
		GL_BindTexture( 2, tr.fogImage->texnum );
		
		qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 2, fparm->fogDistanceVector );
		qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 3, fparm->fogDepthVector );
		qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 4, fparm->eyeT, 0.0f, 0.0f, 0.0f );

		qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, fparm->fogColor );
	}

	GL_SelectTexture( 0 );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglEnableClientState( GL_COLOR_ARRAY );

	if ( ( updateArrays = VBO_BindData() ) != qfalse )
	{
		// bind geometry
		qglVertexPointer( 3, GL_FLOAT, vbo->vertex_stride, (const GLvoid *)vbo->vertex_base );
		// bind colors
		qglColorPointer( 4, GL_UNSIGNED_BYTE, vbo->color_stride, (const GLvoid *)vbo->color_base );
		// bind first texture array
		qglTexCoordPointer( 2, GL_FLOAT, vbo->texture_stride, (const GLvoid *)vbo->tex_base[0] );

		//setupMultitexture = qtrue;
	}

	R_BindAnimatedImage( &pStage->bundle[0] );

	GL_State( stateBits );

	if ( pStage->bundle[1].image[0] != NULL ) // multitexture
	{
		// bind second texture array
		GL_SelectTexture( 1 );
		qglEnable( GL_TEXTURE_2D );
		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

		if ( fp == 0 )
		{
			if ( r_lightmap->integer )
			{
				GL_TexEnv( GL_REPLACE );
			}
			else
			{
				GL_TexEnv( tess.shader->multitextureEnv );
			}
		}

		//if ( updateArrays || setupMultitexture )
			qglTexCoordPointer( 2, GL_FLOAT, vbo->texture_stride, (const GLvoid *)vbo->tex_base[1] );
		//setupMultitexture = qfalse;
	
		R_BindAnimatedImage( &pStage->bundle[1] );

		VBO_RenderIndexQueue( qtrue );

		// disable texturing on TEXTURE1, then select TEXTURE0
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		qglDisable( GL_TEXTURE_2D );
		GL_SelectTexture( 0 );
	}
	else
	{
		VBO_RenderIndexQueue( qfalse );
	}
	
	tess.vboIndex = 0;
	VBO_ClearQueue();
}


void RB_StageIteratorVBO( void )
{
	const shaderCommands_t *input;
	shader_t		*shader;

	input = &tess;
	shader = input->shader;

	// set face culling appropriately
	GL_Cull( shader->cullType );

	// set polygon offset if necessary
	if ( shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	RB_IterateStagesVBO( input );

	// reset polygon offset
	if ( shader->polygonOffset ) 
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}
