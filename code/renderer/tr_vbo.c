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
(vertexes,colors,tex.coords[0..1],normals) in VBO and accessing it via indexes ONLY.

Static data in current meaning is a world surfaces whose shader data
can be evaluated at map load time.

Every static surface gets unique item index which will be added to queue 
instead of tesselation like for regular surfaces. Using items queue also
eleminates run-time tesselation limits.

When it is time to render - we sort queued items to get longest possible 
index sequence run to check if it is long enough i.e. worth switching to GPU-side IBO.
So long index runs are rendered via multiple glDrawElements() calls, 
all remaining short index sequences are grouped together into single software index
which is finally rendered via single legacy index array transfer.

For VBO storage 'Structure of Arrays' approach were selected as it is much easier to
maintain, also it can be used for re-tesselation in future. 
No performance differences from 'Array of Structures' were observed.

*/

#ifdef USE_VBO

#define MAX_VBO_STAGES MAX_SHADER_STAGES

#define MIN_IBO_RUN 320

//[ibo]: [index0][index1]...
//[vbo]: [vertex0][color0][tx0][vertex1][color1][tx1]...

typedef struct vbo_item_s {
	int			index_offset;  // int glIndex_t units, device-local, relative to current shader
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

	glIndex_t *soft_buffer;
	uint32_t soft_buffer_indexes;

	ibo_item_t *ibo_items;
	int ibo_items_count;

	vbo_item_t *items;
	int items_count;

	int *items_queue;
	int items_queue_count;

	int items_queue_vertexes;
	int items_queue_indexes;

	short fogFPindex;	// fog-only
	short fogVPindex[2];// eye-in/eye-out

} vbo_t;

static vbo_t world_vbo;

GLuint VBO_world_data;
GLuint VBO_world_indexes;
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

enum {
	VP_FOG_NONE,
	VP_FOG_EYE_IN,
	VP_FOG_EYE_OUT,
};

enum {
	FP_FOG_NONE,
	FP_FOG_BLEND,
	FP_FOG_ONLY
};

static const char *BuildVP( int multitexture, int fogmode, int texgen )
{
	static char buf[2048], b[256];
	const char *tex0;
	const char *tex1;

	strcpy( buf,
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n" );

	switch ( fogmode ) {
		default:
		case VP_FOG_NONE:
			break;
		case VP_FOG_EYE_IN:
			strcat( buf, fogInVPCode ); break;
		case VP_FOG_EYE_OUT:
			strcat( buf, fogOutVPCode ); break;
	}

	if ( texgen ) {

		// environment mapping

		strcat( buf,
		"TEMP viewer, d; \n"

		// VectorSubtract( backEnd.or.viewOrigin, v, viewer );
		"SUB viewer, program.local[0], vertex.position;\n"

		// VectorNormalize( viewer )
		"DP3 viewer.w, viewer, viewer; \n"
		"RSQ viewer.w, viewer.w; \n"
		"MUL viewer.xyz, viewer.w, viewer; \n"

		// d = DotProduct( normal, viewer );
		"DP3 d, vertex.normal, viewer; \n"

		//reflected[] = normal[]*2*d - viewer[];
		"MUL d, d, 2.0; \n"
		"MAD d, vertex.normal, d, -viewer; \n"

		//st[0] = 0.5 + reflected[1] * 0.5;
		//st[1] = 0.5 - reflected[2] * 0.5;
		//"MAD st.x, d.y,  0.5, 0.5; \n"
		//"MAD st.y, d.z, -0.5, 0.5; \n"
		"PARAM m = { 0.0, 0.5, -0.5, 0.0 }; \n"
		"MAD d, d, m, 0.5; \n" );
		if ( texgen & 1 )
			tex0 = "d.yzwx";
		else
			tex0 = "vertex.texcoord[0]";
		if ( texgen & 2 )
			tex1 = "d.yzwx";
		else
			tex1 = "vertex.texcoord[1]";
	} else {
		tex0 = "vertex.texcoord[0]";
		tex1 = "vertex.texcoord[1]";
	}

	switch ( multitexture ) {
		case GL_ADD:
		case GL_MODULATE:
			sprintf( b,
				"MOV result.texcoord[0], %s; \n"
				"MOV result.texcoord[1], %s; \n",
				tex0, tex1 );
			break;
		case GL_REPLACE:
			sprintf( b, "MOV result.texcoord[1], %s; \n", tex1 );
			break;
		default:
			sprintf( b, "MOV result.texcoord[0], %s; \n", tex0 );
			break;
	}

	strcat( buf, b );

	strcat( buf,
	"MOV result.color, vertex.color; \n"
	"END \n" );

	return buf;
}


const char *BuildFP( int multitexture, int alphatest, int fogMode )
{
	static char buf[1024];

	strcpy( buf, "!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"TEMP base; \n" );

	if ( fogMode == FP_FOG_ONLY ) {
		strcat( buf, "TEX base, fragment.texcoord[4], texture[2], 2D; \n" );
		strcat( buf, "MUL result.color, base, program.local[0]; \n" );
		strcat( buf, "END \n" );
		return buf;
	}

	if ( alphatest || multitexture == GL_ADD  || multitexture == GL_MODULATE ) {
		strcat( buf, "TEMP t; \n" );
	}

	switch ( multitexture ) {
		case 0:
			strcat( buf, "TEX base, fragment.texcoord[0], texture[0], 2D; \n" );
			break;
		case GL_ADD:
			strcat( buf, "TEX base, fragment.texcoord[0], texture[0], 2D; \n" );
			strcat( buf, "TEX t,    fragment.texcoord[1], texture[1], 2D; \n"
			"ADD base, base, t; \n" );
			break;
		case GL_MODULATE:
			strcat( buf, "TEX base, fragment.texcoord[0], texture[0], 2D; \n" );
			strcat( buf, "TEX t,    fragment.texcoord[1], texture[1], 2D; \n" );
			strcat( buf, "MUL base, base, t; \n" );
			break;
		case GL_REPLACE:
			strcat( buf, "TEX base, fragment.texcoord[1], texture[1], 2D; \n" );
			break;
		default:
			ri.Error( ERR_DROP, "Invalid multitexture mode %04x", multitexture );
			break;
	}

	if ( fogMode == FP_FOG_BLEND ) {
		strcat( buf, "MUL base, base, fragment.color; \n" );
		strcat( buf, genATestFP( alphatest ) );
		strcat( buf, "TEMP fog; \n"
		"TEX fog, fragment.texcoord[4], texture[2], 2D; \n"
		"MUL fog, fog, program.local[0]; \n"
		"LRP_SAT result.color, fog.a, fog, base; \n"
		"END \n" );
	} else {
		if ( alphatest ) {
			strcat( buf, "MUL base, base, fragment.color; \n" );
			strcat( buf, genATestFP( alphatest ) );
			strcat( buf,
			"MOV result.color, base; \n"
			"END \n" );
		} else {
			strcat( buf,
			"MUL result.color, base, fragment.color; \n"
			"END \n" );
		}
	}

	return buf;
}


// multitexture modes: single, mt-add, mt-modulate, mt-replace
// environment mapping: none, tx0, tx1, tx0 + tx1
// fog modes: disabled, eye-in, eye-out, fog-only
static GLuint vbo_vp[4*4*4+1];

// multitexture modes: single, mt-add, mt-modulate, mt-replace
// alpha test modes: disabled, GT0, LT80, GE80
// fog modes: disabled, enabled, fog-only, unused
static GLuint vbo_fp[4*4*4+1];

static int getVPindex( int multitexture, int fogmode, int texgen )
{
	int index;

	switch ( multitexture )
	{
		default:			index = 0; break;
		case GL_ADD:		index = 1; break;
		case GL_MODULATE:	index = 2; break;
		case GL_REPLACE:	index = 3; break;
	}

	index <<= 2;  // reserve bits for texgen
	index |= texgen & 3; // environment mapping: none, tx0, tx1, tx0 + tx1

	index <<= 2;  // reserve bits for fogmode
	index |= fogmode & 3; // disabled, eye-in, eye-out, fog-only

	return index + 1;
}


static int getFPindex( int multitexture, int atest, int fogmode )
{
	int index;

	switch( multitexture )
	{
		default:			index = 0; break;
		case GL_ADD:		index = 1; break;
		case GL_MODULATE:	index = 2; break;
		case GL_REPLACE:	index = 3; break;
	}

	index <<= 2; // reserve bits for atest
	switch ( atest )
	{
		case GLS_ATEST_GT_0:  index |= 1; break;
		case GLS_ATEST_LT_80: index |= 2; break;
		case GLS_ATEST_GE_80: index |= 3; break;
		default: break;
	}

	index <<= 2; // reserve bits for fog mode
	index |= fogmode & 3; // disabled, blend, fog-only

	return index + 1;
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


static qboolean isStaticTCmod( const textureBundle_t *bundle )
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
			return qfalse;
		}
	}

	return qtrue;
}


static qboolean isStaticTCgen( shaderStage_t *stage, int bundle )
{
	switch ( stage->bundle[bundle].tcGen )
	{
		case TCGEN_BAD:
		case TCGEN_IDENTITY:	// clear to 0,0
		case TCGEN_LIGHTMAP:
		case TCGEN_TEXTURE:
			return qtrue;
		case TCGEN_ENVIRONMENT_MAPPED:
			if ( bundle == 0 && stage->bundle[bundle].numTexMods == 0 ) {
				stage->tessFlags |= TESS_ENV0 << bundle;
				stage->tessFlags &= ~( TESS_ST0 << bundle );
				return qtrue;
			} else {
				stage->tessFlags |= TESS_ST0 << bundle;
				stage->tessFlags &= ~( TESS_ENV0 << bundle );
				return qfalse;
			}
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


static void CompileVertexProgram( int VPindex, int mtx, int fogMode, int texgen )
{
	if ( vbo_vp[ VPindex ] == 0 )
	{
		// generate vertex program
		qglGenProgramsARB( 1, &vbo_vp[ VPindex ] );
		ARB_CompileProgram( Vertex, BuildVP( mtx, fogMode, texgen ), vbo_vp[ VPindex ] );
	}
}


static void CompileFragmentProgram( int FPindex, int mtx, int atestBits, int fogMode )
{
	if ( vbo_fp[ FPindex ] == 0 )
	{
		// generate fragment program
		qglGenProgramsARB( 1, &vbo_fp[ FPindex ] );
		ARB_CompileProgram( Fragment, BuildFP( mtx, atestBits, fogMode ), vbo_fp[ FPindex ] );
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
	shaderStage_t* stage;
	int i, svarsSize, mtx;
	GLbitfield atestBits;

	if ( shader->isStaticShader )
		return qtrue;

	if ( shader->isSky || shader->remappedShader )
		return qfalse;

	if ( shader->numDeforms || shader->numUnfoggedPasses > MAX_VBO_STAGES )
		return qfalse;

	svarsSize = 0;

	for ( i = 0; i < shader->numUnfoggedPasses; i++ )
	{
		stage = shader->stages[ i ];
		if ( !stage || !stage->active )
			break;
		if ( stage->depthFragment )
			return qfalse;
		if ( stage->adjustColorsForFog != ACFF_NONE )
			return qfalse;
		if ( !isStaticTCmod( &stage->bundle[0] ) || !isStaticTCmod( &stage->bundle[1] ) )
			return qfalse;
		if ( !isStaticRGBgen( stage->rgbGen ) )
			return qfalse;
		if ( !isStaticTCgen( stage, 0 ) )
			return qfalse;
		if ( !isStaticTCgen( stage, 1 ) )
			return qfalse;
		if ( !isStaticAgen( stage->alphaGen ) )
			return qfalse;
		svarsSize += sizeof( tess.svars.colors[0] );
		if ( stage->tessFlags & TESS_ST0 )
			svarsSize += sizeof( tess.svars.texcoords[0][0] );
		if ( stage->tessFlags & TESS_ST1 )
			svarsSize += sizeof( tess.svars.texcoords[1][0] );
	}

	if ( i == 0 )
		return qfalse;

	shader->isStaticShader = qtrue;

	// TODO: alloc separate structure?
	shader->svarsSize = svarsSize;
	shader->iboOffset = -1;
	shader->vboOffset = -1;
	shader->curIndexes = 0;
	shader->curVertexes = 0;
	shader->numIndexes = 0;
	shader->numVertexes = 0;

	for ( i = 0; i < shader->numUnfoggedPasses; i++ )
	{
		int texgen;
		stage = shader->stages[ i ];
		if ( !stage || !stage->active )
			break;
		
		mtx = stage->mtEnv;
		atestBits = stage->stateBits & GLS_ATEST_BITS;
		texgen = 0;
		if ( stage->tessFlags & TESS_ENV0 ) {
			texgen |= 1;
		}
		if ( stage->tessFlags & TESS_ENV1 ) {
			texgen |= 2;
		}

		if ( texgen || shader->numUnfoggedPasses == 1 ) {
			stage->vboVPindex[1] = getVPindex( mtx, VP_FOG_EYE_IN, texgen );
			stage->vboVPindex[2] = getVPindex( mtx, VP_FOG_EYE_OUT, texgen );
			stage->vboFPindex[1] = getFPindex( mtx, atestBits, FP_FOG_BLEND );
			CompileVertexProgram( stage->vboVPindex[1], mtx, VP_FOG_EYE_IN, texgen );
			CompileVertexProgram( stage->vboVPindex[2], mtx, VP_FOG_EYE_OUT, texgen );
			CompileFragmentProgram( stage->vboFPindex[1], mtx, atestBits, FP_FOG_BLEND );
			if ( texgen ) {
				stage->vboVPindex[0] = getVPindex( mtx, VP_FOG_NONE, texgen );
				stage->vboFPindex[0] = getFPindex( mtx, atestBits, FP_FOG_NONE );
				CompileVertexProgram( stage->vboVPindex[0], mtx, VP_FOG_NONE, texgen );
				CompileFragmentProgram( stage->vboFPindex[0], mtx, atestBits, FP_FOG_NONE );
			}
		}
	}

	world_vbo.fogVPindex[0] = getVPindex( 0, VP_FOG_EYE_IN, 0 );
	world_vbo.fogVPindex[1] = getVPindex( 0, VP_FOG_EYE_OUT, 0 );

	world_vbo.fogFPindex = getFPindex( 0, 0, FP_FOG_ONLY );

	CompileVertexProgram( world_vbo.fogVPindex[0], 0, VP_FOG_EYE_IN, 0 );
	CompileVertexProgram( world_vbo.fogVPindex[0], 0, VP_FOG_EYE_OUT, 0 );

	CompileFragmentProgram( world_vbo.fogFPindex, 0 /*mtx*/, 0 /*atest*/, FP_FOG_ONLY );

	return qtrue;
}


static void VBO_AddGeometry( vbo_t *vbo, vbo_item_t *vi, shaderCommands_t *input )
{
	uint32_t size, offs;
	int i;

	if ( input->shader->iboOffset == -1 || input->shader->vboOffset == -1 ) {

		// allocate indexes
		input->shader->iboOffset = vbo->ibo_offset;
		vbo->ibo_offset += input->shader->numIndexes * sizeof( input->indexes[0] );

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
			pStage->color_offset = offs; offs += input->shader->numVertexes * sizeof( tess.svars.colors[0] );
			if ( pStage->tessFlags & TESS_ST0 )
			{
				pStage->tex_offset[0] = offs; offs += input->shader->numVertexes * sizeof( tess.svars.texcoords[0][0] );
			}
			if ( pStage->tessFlags & TESS_ST1 )
			{
				pStage->tex_offset[1] = offs; offs += input->shader->numVertexes * sizeof( tess.svars.texcoords[1][0] );
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
		//vi->soft_offset = input->shader->iboOffset;
		vi->soft_offset = input->shader->iboOffset + input->shader->curIndexes * sizeof( input->indexes[0] );
	}

	size = input->numIndexes * sizeof( input->indexes[ 0 ] );
	offs = input->shader->iboOffset + input->shader->curIndexes * sizeof( input->indexes[0] );
	if ( offs + size > vbo->ibo_size ) {
		ri.Error( ERR_DROP, "Index0 overflow" );
	}
	memcpy( vbo->ibo_buffer + offs, input->indexes, size );
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


static void VBO_AddStageColors( vbo_t *vbo, int stage, const shaderCommands_t *input )
{
	const int offs = input->xstages[ stage ]->color_offset + input->shader->curVertexes * sizeof( input->svars.colors[0] );
	const int size = input->numVertexes * sizeof( input->svars.colors[ 0 ] );

	memcpy( vbo->vbo_buffer + offs, input->svars.colors, size );
}


static void VBO_AddStageTxCoords( vbo_t *vbo, int stage, const shaderCommands_t *input, int unit )
{
	const int offs = input->xstages[ stage ]->tex_offset[ unit ] + input->shader->curVertexes * sizeof( input->svars.texcoords[unit][0] );
	const int size = input->numVertexes * sizeof( input->svars.texcoords[unit][0] );

	memcpy( vbo->vbo_buffer + offs, input->svars.texcoordPtr[ unit ], size );
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
		R_ComputeColors( pStage );
		VBO_AddStageColors( vbo, i, input );
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
	}

	input->shader->curVertexes += input->numVertexes;
	input->shader->curIndexes += input->numIndexes;

	//Com_Printf( "%s: vert %i (of %i), ind %i (of %i)\n", input->shader->name, 
	//	input->shader->curVertexes, input->shader->numVertexes,
	//	input->shader->curIndexes, input->shader->numIndexes );
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
	GLenum err;

	int numStaticSurfaces = 0;
	int numStaticIndexes = 0;
	int numStaticVertexes = 0;

	if ( !qglBindBufferARB || !r_vbo->integer )
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
	vbo->vbo_buffer = ri.Hunk_AllocateTempMemory( vbo_size );
	vbo->vbo_offset = 0;
	vbo->vbo_size = vbo_size;

	// index buffer
	vbo->ibo_buffer = ri.Hunk_Alloc( ibo_size, h_low );	
	vbo->ibo_offset = 0;
	vbo->ibo_size = ibo_size;

	// soft index buffer
	vbo->soft_buffer = ri.Hunk_Alloc( ibo_size, h_low );
	vbo->soft_buffer_indexes = 0;

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
		tess.allowVBO = qfalse; // block execution of VBO path as we need to tesselate geometry
#ifdef USE_TESS_NEEDS_NORMAL
		tess.needsNormal = qtrue;
#endif
#ifdef USE_TESS_NEEDS_ST2
		tess.needsST2 = qtrue;
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

	// reset error state
	qglGetError();

	if ( !VBO_world_data ) {
		qglGenBuffersARB( 1, &VBO_world_data );
		if ( (err = qglGetError()) != GL_NO_ERROR )
			goto __fail;
	}

	// upload vertex array & colors & textures
	if ( VBO_world_data ) {
		VBO_BindData();
		qglBufferDataARB( GL_ARRAY_BUFFER_ARB, vbo->vbo_size, vbo->vbo_buffer, GL_STATIC_DRAW_ARB );
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
		qglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, vbo->ibo_size, vbo->ibo_buffer, GL_STATIC_DRAW_ARB );
		if ( (err = qglGetError()) != GL_NO_ERROR )
			goto __fail;
	}

	VBO_UnBind();
	ri.Hunk_FreeTempMemory( vbo->vbo_buffer );
	vbo->vbo_buffer = NULL;
	return;

__fail:

	if ( err == GL_OUT_OF_MEMORY )
		ri.Printf( PRINT_WARNING, "%s: out of memory\n", __func__ );
	else
		ri.Printf( PRINT_ERROR, "%s: error %i\n", __func__, err );

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

	VBO_UnBind();

	// release host memory
	ri.Hunk_FreeTempMemory( vbo->vbo_buffer );
	vbo->vbo_buffer = NULL;

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

	for ( i = 0; i < tr.numShaders; i++ )
	{
		tr.shaders[ i ]->isStaticShader = qfalse;
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
	VBO_UnBind();
}


static void VBO_AddItemDataToSoftBuffer( int itemIndex )
{
	vbo_t *vbo = &world_vbo;
	const vbo_item_t *vi = vbo->items + itemIndex;

	memcpy( &vbo->soft_buffer[ vbo->soft_buffer_indexes ], vbo->ibo_buffer + vi->soft_offset, vi->num_indexes * sizeof( glIndex_t ) );

	vbo->soft_buffer_indexes += vi->num_indexes;
}


static void VBO_AddItemRangeToIBOBuffer( int offset, int length )
{
	vbo_t *vbo = &world_vbo;
	ibo_item_t *it;

	it = vbo->ibo_items + vbo->ibo_items_count++;

	it->offset = offset * sizeof( glIndex_t ) + tess.shader->iboOffset;
	it->length = length;
}


static void VBO_RenderIBOItems( void )
{
	const vbo_t *vbo = &world_vbo;
	int i;

	// from device-local memory
	if ( vbo->ibo_items_count )
	{
		VBO_BindIndex( qtrue );
		for ( i = 0; i < vbo->ibo_items_count; i++ )
		{
			qglDrawElements( GL_TRIANGLES, vbo->ibo_items[ i ].length, GL_INDEX_TYPE, (const GLvoid *)(intptr_t) vbo->ibo_items[ i ].offset );
		}
	}
}


static void VBO_RenderSoftItems( void )
{
	const vbo_t *vbo = &world_vbo;

	if ( vbo->soft_buffer_indexes )
	{
		VBO_BindIndex( qfalse );
		qglDrawElements( GL_TRIANGLES, vbo->soft_buffer_indexes, GL_INDEX_TYPE, vbo->soft_buffer );
	}
}


static void VBO_RenderIndexes( void )
{
	if ( curr_index_bind )
	{
		VBO_RenderIBOItems();
		VBO_RenderSoftItems();
	}
	else
	{
		VBO_RenderSoftItems();
		VBO_RenderIBOItems();
	}
}


static void VBO_PrepareQueues( void )
{
	vbo_t *vbo = &world_vbo;
	int i, item_run, index_run, n;
	const vbo_item_t *vi;
	const int *a;
	
	vbo->items_queue[ vbo->items_queue_count ] = 0; // terminate run

	// sort items so we can scan for longest runs
	if ( vbo->items_queue_count > 1 )
		qsort_int( vbo->items_queue, vbo->items_queue_count-1 );
	
	vbo->soft_buffer_indexes = 0;
	vbo->ibo_items_count = 0;

	vbo->items_queue_vertexes = 0;
	vbo->items_queue_indexes = 0;

	for ( i = 0; i < vbo->items_queue_count; i++ ) {
		vi = &vbo->items[ vbo->items_queue[ i ] ];
		vbo->items_queue_vertexes += vi->num_vertexes;
		vbo->items_queue_indexes += vi->num_indexes;
	}

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


static const fogProgramParms_t *VBO_SetupFog( int VPindex, int FPindex, GLuint *pvp, GLuint *pfp )
{
	const fogProgramParms_t *fparm;
	GLuint vp, fp;

	GL_BindTexture( 2, tr.fogImage->texnum );
	GL_SelectTexture( 0 );

	fparm = RB_CalcFogProgramParms();
	if ( fparm->eyeOutside )
		vp = vbo_vp[ VPindex + 1 ];
	else
		vp = vbo_vp[ VPindex + 0 ];

	fp = vbo_fp[ FPindex ];

	ARB_ProgramEnableExt( vp, fp );

	qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 2, fparm->fogDistanceVector );
	qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 3, fparm->fogDepthVector );
	qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 4, fparm->eyeT, 0.0f, 0.0f, 0.0f );
	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, fparm->fogColor );

	*pvp = vp;
	*pfp = fp;

	return fparm;
}


/*
** RB_IterateStagesVBO
*/
static void RB_IterateStagesVBO( const shaderCommands_t *input )
{
	const shaderStage_t *pStage;
	const fogProgramParms_t *fparm;
	int i;
	GLbitfield stateBits, normalMask;
	qboolean fogPass;
	GLuint vp, fp;

	fogPass = ( tess.fogNum && tess.shader->fogPass );

	if ( fogPass && tess.shader->numUnfoggedPasses == 1 ) {
		// combined fog + single stage program
		pStage = input->xstages[ 0 ];
		fparm = VBO_SetupFog( pStage->vboVPindex[1], pStage->vboFPindex[1], &vp, &fp ); 
	} else {
		fparm = NULL;
		vp = fp = 0;
	}

	VBO_PrepareQueues();

	VBO_BindData();

	qglVertexPointer( 3, GL_FLOAT, 16, (const GLvoid *)(intptr_t)tess.shader->vboOffset );

	if ( qglLockArraysEXT ) {
		// FIXME: not needed for VBOs?
		qglLockArraysEXT( 0, world_vbo.items_queue_vertexes );
	}

	for ( i = 0; i < MAX_VBO_STAGES; i++ ) {
		if ( !input->xstages[i] )
			break;

		pStage = input->xstages[i];

		stateBits = pStage->stateBits;

		if ( fparm == NULL ) {
			vp = vbo_vp[ pStage->vboVPindex[0] ];
			fp = vbo_fp[ pStage->vboFPindex[0] ];
			ARB_ProgramEnableExt( vp, fp );
		}

		if ( fp ) {
			stateBits &= ~GLS_ATEST_BITS; // done in shaders
		}

		GL_State( stateBits );

		if ( pStage->tessFlags & ( TESS_ENV0 | TESS_ENV1 ) ) {
			// setup viewpos needed for environment mapping program
			qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 0,
				backEnd.or.viewOrigin[0],
				backEnd.or.viewOrigin[1],
				backEnd.or.viewOrigin[2],
				0.0 );
			normalMask = CLS_NORMAL_ARRAY;
		} else {
			normalMask = 0;
		}

		if ( pStage->tessFlags & TESS_ENV0 ) {
			GL_ClientState( 0, normalMask | CLS_COLOR_ARRAY );
		} else {
			GL_ClientState( 0, normalMask | CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
			// bind colors and first texture array
			qglTexCoordPointer( 2, GL_FLOAT, 0, (const GLvoid *)(intptr_t)pStage->tex_offset[0] );
		}

		if ( normalMask ) {
			// bind normals
			qglNormalPointer( GL_FLOAT, 16, (const GLvoid *)(intptr_t)tess.shader->normalOffset );
		}

		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, (const GLvoid *)(intptr_t)pStage->color_offset );

		GL_SelectTexture( 0 );
		R_BindAnimatedImage( &pStage->bundle[0] );

		if ( pStage->mtEnv ) { // multitexture
			if ( fp == 0 ) {
				// bind second texture array
				GL_ClientState( 1, CLS_TEXCOORD_ARRAY );
				qglTexCoordPointer( 2, GL_FLOAT, 0, (const GLvoid *)(intptr_t)pStage->tex_offset[1] );
				GL_SelectTexture( 1 );
				qglEnable( GL_TEXTURE_2D );
				R_BindAnimatedImage( &pStage->bundle[1] );
				if ( fparm == NULL ) {
					if ( r_lightmap->integer )
						GL_TexEnv( GL_REPLACE );
					else
						GL_TexEnv( pStage->mtEnv );
				}
				VBO_RenderIndexes();
				qglDisable( GL_TEXTURE_2D );
				GL_SelectTexture( 0 );
			} else {
				if ( pStage->tessFlags & TESS_ENV1 ) {
					GL_ClientState( 1, CLS_NONE );
				} else {
					GL_ClientState( 1, CLS_TEXCOORD_ARRAY );
					qglTexCoordPointer( 2, GL_FLOAT, 0, (const GLvoid *)(intptr_t)pStage->tex_offset[1] );
				}
				GL_SelectTexture( 1 );
				// fragment programs have explicit control over texturing
				// so no need to enable/disable GL_TEXTURE_2D
				R_BindAnimatedImage( &pStage->bundle[1] );
				VBO_RenderIndexes();
				GL_SelectTexture( 0 );
			}
		} else {
			GL_ClientState( 1, CLS_NONE );
			VBO_RenderIndexes();
		}
	}

	GL_ClientState( 1, CLS_NONE );

	//fog-only pass
	if ( fogPass && fparm == NULL ) {

		GL_ClientState( 0, CLS_NONE );

		VBO_SetupFog( world_vbo.fogVPindex[0], world_vbo.fogFPindex, &vp, &fp );

		if ( tess.shader->fogPass == FP_EQUAL ) {
			GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
		} else {
			GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
		}

		VBO_RenderIndexes();
	}

	ARB_ProgramEnableExt( 0, 0 );

	if ( r_showtris->integer ) {

		if ( r_showtris->integer == 1 && backEnd.drawConsole )
			return;

		ARB_ProgramEnableExt( 0, 0 );

		GL_ClientState( 0, CLS_NONE );

		GL_SelectTexture( 0 );
		qglDisable( GL_TEXTURE_2D );

		GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
		qglDepthRange( 0, 0 );

		// green for IBO items
		qglColor4f( 0.25f, 1.0f, 0.25f, 1.0f );
		VBO_RenderIBOItems();
		
		// cyan for soft-index items
		qglColor4f( 0.25f, 1.0f, 0.55f, 1.0f );
		VBO_RenderSoftItems();

		qglDepthRange( 0, 1 );

		qglEnable( GL_TEXTURE_2D );
	}

	if ( qglUnlockArraysEXT ) {
		qglUnlockArraysEXT();
	}

	if ( r_speeds->integer == 1 ) {
		// update performance stats
		backEnd.pc.c_totalIndexes += world_vbo.items_queue_indexes;
		backEnd.pc.c_indexes += world_vbo.items_queue_indexes;
		backEnd.pc.c_vertexes += world_vbo.items_queue_vertexes;
		backEnd.pc.c_shaders++;
	}
	//VBO_UnBind();
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

	tess.vboIndex = 0;
	VBO_ClearQueue();
}

#endif // USE_VBO
