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

backEndData_t	*backEndData;
backEndState_t	backEnd;

#ifndef USE_VULKAN
static const float s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};
#endif

/*
** GL_Bind
*/
void GL_Bind( image_t *image ) {
#ifdef USE_VULKAN
	if ( !image ) {
		ri.Printf( PRINT_WARNING, "GL_Bind: NULL image\n" );
		image = tr.defaultImage;
	}

	if ( r_nobind->integer && tr.dlightImage ) {		// performance evaluation option
		image = tr.dlightImage;
	}

	//if ( glState.currenttextures[glState.currenttmu] != texnum ) {
		image->frameUsed = tr.frameCount;
		vk_update_descriptor( glState.currenttmu + 1, image->descriptor );

	//}
#else
	GLuint texnum;

	if ( !image ) {
		ri.Printf( PRINT_WARNING, "GL_Bind: NULL image\n" );
		texnum = tr.defaultImage->texnum;
	} else {
		texnum = image->texnum;
	}

	if ( r_nobind->integer && tr.dlightImage ) {		// performance evaluation option
		texnum = tr.dlightImage->texnum;
	}

	if ( glState.currenttextures[glState.currenttmu] != texnum ) {
		if ( image ) {
			image->frameUsed = tr.frameCount;
		}
		glState.currenttextures[glState.currenttmu] = texnum;
		qglBindTexture (GL_TEXTURE_2D, texnum);
	}
#endif
}


/*
** GL_SelectTexture
*/
void GL_SelectTexture( int unit )
{
#ifndef USE_VULKAN
	if ( glState.currenttmu == unit )
	{
		return;
	}
#endif

	if ( unit >= glConfig.numTextureUnits )
	{
		ri.Error( ERR_DROP, "GL_SelectTexture: unit = %i", unit );
	}
#ifndef USE_VULKAN
	qglActiveTextureARB( GL_TEXTURE0_ARB + unit );
#endif
	glState.currenttmu = unit;
}


/*
** GL_SelectClientTexture
*/
#ifndef USE_VULKAN
static void GL_SelectClientTexture( int unit )
{
	if ( glState.currentArray == unit )
	{
		return;
	}

	if ( unit >= glConfig.numTextureUnits )
	{
		ri.Error( ERR_DROP, "GL_SelectClientTexture: unit = %i", unit );
	}

	qglClientActiveTextureARB( GL_TEXTURE0_ARB + unit );

	glState.currentArray = unit;
}
#endif


/*
** GL_Cull
*/
void GL_Cull( cullType_t cullType ) {
	if ( glState.faceCulling == cullType ) {
		return;
	}

	glState.faceCulling = cullType;
#ifndef USE_VULKAN
	if ( cullType == CT_TWO_SIDED ) 
	{
		qglDisable( GL_CULL_FACE );
	} 
	else 
	{
		qboolean cullFront;
		qglEnable( GL_CULL_FACE );

		cullFront = (cullType == CT_FRONT_SIDED);
		if ( backEnd.viewParms.portalView == PV_MIRROR )
		{
			cullFront = !cullFront;
		}

		qglCullFace( cullFront ? GL_FRONT : GL_BACK );
	}
#endif
}


/*
** GL_TexEnv
*/
void GL_TexEnv( GLint env )
{
#ifndef USE_VULKAN
	if ( env == glState.texEnv[ glState.currenttmu ] )
		return;

	glState.texEnv[ glState.currenttmu ] = env;

	switch ( env )
	{
	case GL_MODULATE:
	case GL_REPLACE:
	case GL_DECAL:
	case GL_ADD:
		qglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env );
		break;
	default:
		ri.Error( ERR_DROP, "GL_TexEnv: invalid env '%d' passed", env );
		break;
	}
#endif
}


/*
** GL_State
**
** This routine is responsible for setting the most commonly changed state
** in Q3.
*/
void GL_State( unsigned stateBits )
{
#ifndef USE_VULKAN
	unsigned diff = stateBits ^ glState.glStateBits;

	if ( !diff )
	{
		return;
	}

	//
	// check depthFunc bits
	//
	if ( diff & GLS_DEPTHFUNC_EQUAL )
	{
		if ( stateBits & GLS_DEPTHFUNC_EQUAL )
		{
			qglDepthFunc( GL_EQUAL );
		}
		else
		{
			qglDepthFunc( GL_LEQUAL );
		}
	}

	//
	// check blend bits
	//
	if ( diff & GLS_BLEND_BITS )
	{
		GLenum srcFactor = GL_ONE, dstFactor = GL_ONE;

		if ( stateBits & GLS_BLEND_BITS )
		{
			switch ( stateBits & GLS_SRCBLEND_BITS )
			{
			case GLS_SRCBLEND_ZERO:
				srcFactor = GL_ZERO;
				break;
			case GLS_SRCBLEND_ONE:
				srcFactor = GL_ONE;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				srcFactor = GL_DST_COLOR;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				srcFactor = GL_ONE_MINUS_DST_COLOR;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				srcFactor = GL_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				srcFactor = GL_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				srcFactor = GL_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				srcFactor = GL_ONE_MINUS_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ALPHA_SATURATE:
				srcFactor = GL_SRC_ALPHA_SATURATE;
				break;
			default:
				ri.Error( ERR_DROP, "GL_State: invalid src blend state bits" );
				break;
			}

			switch ( stateBits & GLS_DSTBLEND_BITS )
			{
			case GLS_DSTBLEND_ZERO:
				dstFactor = GL_ZERO;
				break;
			case GLS_DSTBLEND_ONE:
				dstFactor = GL_ONE;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				dstFactor = GL_SRC_COLOR;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				dstFactor = GL_ONE_MINUS_SRC_COLOR;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				dstFactor = GL_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				dstFactor = GL_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				dstFactor = GL_DST_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				dstFactor = GL_ONE_MINUS_DST_ALPHA;
				break;
			default:
				ri.Error( ERR_DROP, "GL_State: invalid dst blend state bits" );
				break;
			}

			qglEnable( GL_BLEND );
			qglBlendFunc( srcFactor, dstFactor );
		}
		else
		{
			qglDisable( GL_BLEND );
		}
	}

	//
	// check depthmask
	//
	if ( diff & GLS_DEPTHMASK_TRUE )
	{
		if ( stateBits & GLS_DEPTHMASK_TRUE )
		{
			qglDepthMask( GL_TRUE );
		}
		else
		{
			qglDepthMask( GL_FALSE );
		}
	}

	//
	// fill/line mode
	//
	if ( diff & GLS_POLYMODE_LINE )
	{
		if ( stateBits & GLS_POLYMODE_LINE )
		{
			qglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
		else
		{
			qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		}
	}

	//
	// depthtest
	//
	if ( diff & GLS_DEPTHTEST_DISABLE )
	{
		if ( stateBits & GLS_DEPTHTEST_DISABLE )
		{
			qglDisable( GL_DEPTH_TEST );
		}
		else
		{
			qglEnable( GL_DEPTH_TEST );
		}
	}

	//
	// alpha test
	//
	if ( diff & GLS_ATEST_BITS )
	{
		switch ( stateBits & GLS_ATEST_BITS )
		{
		case 0:
			qglDisable( GL_ALPHA_TEST );
			break;
		case GLS_ATEST_GT_0:
			qglEnable( GL_ALPHA_TEST );
			qglAlphaFunc( GL_GREATER, 0.0f );
			break;
		case GLS_ATEST_LT_80:
			qglEnable( GL_ALPHA_TEST );
			qglAlphaFunc( GL_LESS, 0.5f );
			break;
		case GLS_ATEST_GE_80:
			qglEnable( GL_ALPHA_TEST );
			qglAlphaFunc( GL_GEQUAL, 0.5f );
			break;
		default:
			ri.Error( ERR_DROP, "GL_State: invalid alpha test bits" );
			break;
		}
	}

	glState.glStateBits = stateBits;
#endif // USE_VULKAN
}


#ifndef USE_VULKAN
void GL_ClientState( int unit, unsigned stateBits )
{
	unsigned diff = stateBits ^ glState.glClientStateBits[ unit ];

	if ( diff == 0 )
	{
		if ( stateBits )
		{
			GL_SelectClientTexture( unit );
		}
		return;
	}

	GL_SelectClientTexture( unit );

	if ( diff & CLS_COLOR_ARRAY )
	{
		if ( stateBits & CLS_COLOR_ARRAY )
			qglEnableClientState( GL_COLOR_ARRAY );
		else
			qglDisableClientState( GL_COLOR_ARRAY );
	}

	if ( diff & CLS_NORMAL_ARRAY )
	{
		if ( stateBits & CLS_NORMAL_ARRAY )
			qglEnableClientState( GL_NORMAL_ARRAY );
		else
			qglDisableClientState( GL_NORMAL_ARRAY );
	}

	if ( diff & CLS_TEXCOORD_ARRAY )
	{
		if ( stateBits & CLS_TEXCOORD_ARRAY )
			qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		else
			qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}

	glState.glClientStateBits[ unit ] = stateBits;
}
#endif


/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace( void ) {
	float		c;

	if ( !backEnd.isHyperspace ) {
		// do initialization shit
	}

#ifdef USE_VULKAN
	{
		vec4_t color;
		c = ( backEnd.refdef.time & 255 ) / 255.0f;
		color[0] = color[1] = color[2] = c;
		color[3] = 1.0;
		vk_clear_color( color );
	}
#else
	c = ( backEnd.refdef.time & 255 ) / 255.0f;
	qglClearColor( c, c, c, 1 );
	qglClear( GL_COLOR_BUFFER_BIT );
#endif

	backEnd.isHyperspace = qtrue;
}


static void SetViewportAndScissor( void ) {
#ifdef USE_VULKAN
	//Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	//vk_update_mvp();
	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
#else
	qglMatrixMode(GL_PROJECTION);
	qglLoadMatrixf( backEnd.viewParms.projectionMatrix );
	qglMatrixMode(GL_MODELVIEW);

	// set the window clipping
	qglViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY, 
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
	qglScissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY, 
		backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight );
#endif
}


/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
static void RB_BeginDrawingView( void ) {
#ifndef USE_VULKAN
	int clearBits = 0;

	// sync with gl if needed
	if ( r_finish->integer == 1 && !glState.finishCalled ) {
		qglFinish();
		glState.finishCalled = qtrue;
	} else if ( r_finish->integer == 0 ) {
		glState.finishCalled = qtrue;
	}
#endif

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	//
	// set the modelview matrix for the viewer
	//
	SetViewportAndScissor();

#ifdef USE_VULKAN
	vk_clear_depth( qtrue );
#else
	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );

	// clear relevant buffers
	clearBits = GL_DEPTH_BUFFER_BIT;

	if ( r_shadows->integer == 2 )
	{
		clearBits |= GL_STENCIL_BUFFER_BIT;
	}
	if ( 0 && r_fastsky->integer && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		clearBits |= GL_COLOR_BUFFER_BIT;	// FIXME: only if sky shaders have been used
#ifdef _DEBUG
		qglClearColor( 0.8f, 0.7f, 0.4f, 1.0f );	// FIXME: get color of sky
#else
		qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );	// FIXME: get color of sky
#endif
	}
	qglClear( clearBits );
#endif

	if ( backEnd.refdef.rdflags & RDF_HYPERSPACE )
	{
		RB_Hyperspace();
		return;
	}
	else
	{
		backEnd.isHyperspace = qfalse;
	}

	glState.faceCulling = -1;		// force face culling to set next time

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

#ifndef USE_VULKAN
	// clip to the plane of the portal
	if ( backEnd.viewParms.portalView != PV_NONE ) {
		float	plane[4];
		GLdouble plane2[4];

		plane[0] = backEnd.viewParms.portalPlane.normal[0];
		plane[1] = backEnd.viewParms.portalPlane.normal[1];
		plane[2] = backEnd.viewParms.portalPlane.normal[2];
		plane[3] = backEnd.viewParms.portalPlane.dist;

		plane2[0] = DotProduct( backEnd.viewParms.or.axis[0], plane );
		plane2[1] = DotProduct( backEnd.viewParms.or.axis[1], plane );
		plane2[2] = DotProduct( backEnd.viewParms.or.axis[2], plane );
		plane2[3] = DotProduct( plane, backEnd.viewParms.or.origin) - plane[3];

		qglLoadMatrixf( s_flipMatrix );
		qglClipPlane( GL_CLIP_PLANE0, plane2 );
		qglEnable( GL_CLIP_PLANE0 );
	} else {
		qglDisable( GL_CLIP_PLANE0 );
	}
#endif
}

#ifdef USE_PMLIGHT
static void RB_LightingPass( qboolean skipWeapon );
#endif

/*
==================
RB_RenderDrawSurfList
==================
*/
static void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs, qboolean skipWeapon ) {
	shader_t		*shader, *oldShader;
	int				fogNum;
	int				entityNum, oldEntityNum;
	int				dlighted;
	qboolean		depthRange, isCrosshair;
#ifndef USE_VULKAN
	qboolean		oldDepthRange, wasCrosshair;
#endif
	int				i;
	drawSurf_t		*drawSurf;
	unsigned int	oldSort;
	float			oldShaderSort;
	double			originalTime; // -EC-

	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
#ifndef USE_VULKAN
	oldDepthRange = qfalse;
	wasCrosshair = qfalse;
#endif
	oldSort = MAX_UINT;
	oldShaderSort = -1;
	depthRange = qfalse;

	backEnd.pc.c_surfaces += numDrawSurfs;

	for (i = 0, drawSurf = drawSurfs ; i < numDrawSurfs ; i++, drawSurf++) {
		if ( drawSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
			continue;
		}

		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );

		if ( skipWeapon && entityNum != REFENTITYNUM_WORLD && backEnd.refdef.entities[ entityNum ].e.renderfx & RF_DEPTHHACK ) {
			continue;
		}

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( ( (oldSort ^ drawSurfs->sort ) & ~QSORT_REFENTITYNUM_MASK ) || !shader->entityMergable ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
			}
#ifdef USE_PMLIGHT
			#define INSERT_POINT SS_FOG
			if ( backEnd.refdef.numLitSurfs && oldShaderSort < INSERT_POINT && shader->sort >= INSERT_POINT ) {
				//RB_BeginDrawingLitSurfs(); // no need, already setup in RB_BeginDrawingView()
#ifdef USE_VULKAN
				RB_LightingPass( skipWeapon );
#else
				if ( depthRange ) {
					qglDepthRange( 0, 1 );
					RB_LightingPass();
					qglDepthRange( 0, 0.3 );
				} else {
					RB_LightingPass();
				}
#endif
				oldEntityNum = -1; // force matrix setup
			}
			oldShaderSort = shader->sort;
#endif
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
		}

		oldSort = drawSurf->sort;

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum ) {
			depthRange = isCrosshair = qfalse;

			if ( entityNum != REFENTITYNUM_WORLD ) {
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				if ( backEnd.currentEntity->intShaderTime )
					backEnd.refdef.floatTime = originalTime - (double)(backEnd.currentEntity->e.shaderTime.i) * 0.001;
				else
					backEnd.refdef.floatTime = originalTime - (double)backEnd.currentEntity->e.shaderTime.f;

				// set up the transformation matrix
				R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.or );
				// set up the dynamic lighting if needed
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
				if ( !r_dlightMode->integer )
#endif 
				if ( backEnd.currentEntity->needDlights ) {
					R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.or );
				}
#endif // USE_LEGACY_DLIGHTS
				if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) {
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
					
					if(backEnd.currentEntity->e.renderfx & RF_CROSSHAIR)
						isCrosshair = qtrue;
				}
			} else {
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
				if ( !r_dlightMode->integer )
#endif
				R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.or );
#endif // USE_LEGACY_DLIGHTS
			}

			// we have to reset the shaderTime as well otherwise image animations on
			// the world (like water) continue with the wrong frame
			tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

#ifdef USE_VULKAN
			Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
			tess.depthRange = depthRange ? DEPTH_RANGE_WEAPON : DEPTH_RANGE_NORMAL;
			vk_update_mvp( NULL );
#else
			qglLoadMatrixf( backEnd.or.modelMatrix );
#endif

			//
			// change depthrange. Also change projection matrix so first person weapon does not look like coming
			// out of the screen.
			//
#ifndef USE_VULKAN
			if (oldDepthRange != depthRange || wasCrosshair != isCrosshair)
			{
				if (depthRange)
				{
					if(backEnd.viewParms.stereoFrame != STEREO_CENTER)
					{
						if(isCrosshair)
						{
							if(oldDepthRange)
							{
								// was not a crosshair but now is, change back proj matrix
								qglMatrixMode(GL_PROJECTION);
								qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
								qglMatrixMode(GL_MODELVIEW);
							}
						}
						else
						{
							viewParms_t temp = backEnd.viewParms;

							R_SetupProjection(&temp, r_znear->value, qfalse);

							qglMatrixMode(GL_PROJECTION);
							qglLoadMatrixf(temp.projectionMatrix);
							qglMatrixMode(GL_MODELVIEW);
						}
					}

					if(!oldDepthRange)
						qglDepthRange (0, 0.3);
				}
				else
				{
					if(!wasCrosshair && backEnd.viewParms.stereoFrame != STEREO_CENTER)
					{
						qglMatrixMode(GL_PROJECTION);
						qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
						qglMatrixMode(GL_MODELVIEW);
					}

					qglDepthRange (0, 1);
				}
				oldDepthRange = depthRange;
				wasCrosshair = isCrosshair;
			}
#endif

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
	}

	// draw the contents of the last shader batch
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}

	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
#ifdef USE_VULKAN
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	//vk_update_mvp();
#else
	qglLoadMatrixf( backEnd.viewParms.world.modelMatrix );
	if ( depthRange ) {
		qglDepthRange(0, 1);
	}
#endif
}


#ifdef USE_PMLIGHT
/*
=================
RB_BeginDrawingLitView
=================
*/
static void RB_BeginDrawingLitSurfs( void )
{
	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

	//
	// set the modelview matrix for the viewer
	//
	SetViewportAndScissor();

#ifndef USE_VULKAN

	glState.faceCulling = -1;		// force face culling to set next time

	// clip to the plane of the portal
	if ( backEnd.viewParms.portalView != PV_NONE ) {
		float	plane[4];
		GLdouble plane2[4];

		plane[0] = backEnd.viewParms.portalPlane.normal[0];
		plane[1] = backEnd.viewParms.portalPlane.normal[1];
		plane[2] = backEnd.viewParms.portalPlane.normal[2];
		plane[3] = backEnd.viewParms.portalPlane.dist;

		plane2[0] = DotProduct( backEnd.viewParms.or.axis[0], plane );
		plane2[1] = DotProduct( backEnd.viewParms.or.axis[1], plane );
		plane2[2] = DotProduct( backEnd.viewParms.or.axis[2], plane );
		plane2[3] = DotProduct( plane, backEnd.viewParms.or.origin ) - plane[3];

		qglLoadMatrixf( s_flipMatrix );
		qglClipPlane( GL_CLIP_PLANE0, plane2 );
		qglEnable( GL_CLIP_PLANE0 );
	} else {
		qglDisable( GL_CLIP_PLANE0 );
	}
#endif
}


/*
==================
RB_RenderLitSurfList
==================
*/
static void RB_RenderLitSurfList( dlight_t* dl, qboolean skipWeapon ) {
	shader_t		*shader, *oldShader;
	int				fogNum;
	int				entityNum, oldEntityNum;
#ifndef USE_VULKAN
	qboolean		oldDepthRange, wasCrosshair;
#endif
	qboolean		depthRange, isCrosshair;
	const litSurf_t	*litSurf;
	unsigned int	oldSort;
	double			originalTime; // -EC- 

	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
#ifndef USE_VULKAN
	oldDepthRange = qfalse;
	wasCrosshair = qfalse;
#endif
	oldSort = MAX_UINT;
	depthRange = qfalse;

	tess.dlightUpdateParams = qtrue;

	for ( litSurf = dl->head; litSurf; litSurf = litSurf->next ) {
		//if ( litSurf->sort == sort ) {
		if ( litSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *litSurf->surface ]( litSurf->surface );
			continue;
		}

		R_DecomposeLitSort( litSurf->sort, &entityNum, &shader, &fogNum );

		if ( skipWeapon && entityNum != REFENTITYNUM_WORLD && backEnd.refdef.entities[ entityNum ].e.renderfx & RF_DEPTHHACK ) {
			continue;
		}

		// anything BEFORE opaque is sky/portal, anything AFTER it should never have been added
		//assert( shader->sort == SS_OPAQUE );
		// !!! but MIRRORS can trip that assert, so just do this for now
		//if ( shader->sort < SS_OPAQUE )
		//	continue;

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( ( (oldSort ^ litSurf->sort) & ~QSORT_REFENTITYNUM_MASK ) || !shader->entityMergable ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
			}
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
		}

		oldSort = litSurf->sort;

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum ) {
			depthRange = isCrosshair = qfalse;

			if ( entityNum != REFENTITYNUM_WORLD ) {
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];

				if ( backEnd.currentEntity->intShaderTime )
					backEnd.refdef.floatTime = originalTime - (double)(backEnd.currentEntity->e.shaderTime.i) * 0.001;
				else
					backEnd.refdef.floatTime = originalTime - (double)backEnd.currentEntity->e.shaderTime.f;

				// set up the transformation matrix
				R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.or );

				if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) {
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
					
					if(backEnd.currentEntity->e.renderfx & RF_CROSSHAIR)
						isCrosshair = qtrue;
				}
			} else {
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;
			}

			// we have to reset the shaderTime as well otherwise image animations on
			// the world (like water) continue with the wrong frame
			tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

			// set up the dynamic lighting
			R_TransformDlights( 1, dl, &backEnd.or );
			tess.dlightUpdateParams = qtrue;

#ifdef USE_VULKAN
			tess.depthRange = depthRange ? DEPTH_RANGE_WEAPON : DEPTH_RANGE_NORMAL;
			Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
			vk_update_mvp( NULL );
#else
			qglLoadMatrixf( backEnd.or.modelMatrix );

			//
			// change depthrange. Also change projection matrix so first person weapon does not look like coming
			// out of the screen.
			//

			if (oldDepthRange != depthRange || wasCrosshair != isCrosshair)
			{
				if (depthRange)
				{
					if(backEnd.viewParms.stereoFrame != STEREO_CENTER)
					{
						if(isCrosshair)
						{
							if(oldDepthRange)
							{
								// was not a crosshair but now is, change back proj matrix
								qglMatrixMode(GL_PROJECTION);
								qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
								qglMatrixMode(GL_MODELVIEW);
							}
						}
						else
						{
							viewParms_t temp = backEnd.viewParms;

							R_SetupProjection(&temp, r_znear->value, qfalse);

							qglMatrixMode(GL_PROJECTION);
							qglLoadMatrixf(temp.projectionMatrix);
							qglMatrixMode(GL_MODELVIEW);
						}
					}

					if(!oldDepthRange)
						qglDepthRange (0, 0.3);
				}
				else
				{
					if(!wasCrosshair && backEnd.viewParms.stereoFrame != STEREO_CENTER)
					{
						qglMatrixMode(GL_PROJECTION);
						qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
						qglMatrixMode(GL_MODELVIEW);
					}

					qglDepthRange (0, 1);
				}
				oldDepthRange = depthRange;
				wasCrosshair = isCrosshair;
			}
#endif

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[ *litSurf->surface ]( litSurf->surface );
	}

	// draw the contents of the last shader batch
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}

	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
#ifdef USE_VULKAN
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	//vk_update_mvp();
#else
	qglLoadMatrixf( backEnd.viewParms.world.modelMatrix );
	if ( depthRange ) {
		qglDepthRange (0, 1);
	}
#endif // !USE_VULKAN
}
#endif // USE_PMLIGHT


/*
============================================================================

RENDER BACK END FUNCTIONS

============================================================================
*/

/*
================
RB_SetGL2D
================
*/
static void RB_SetGL2D( void ) {
	backEnd.projection2D = qtrue;

#ifdef USE_VULKAN
	vk_update_mvp( NULL );

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
#else
	// set 2D virtual screen size
	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglMatrixMode( GL_PROJECTION );
	qglLoadIdentity();
	qglOrtho( 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1 );
	qglMatrixMode( GL_MODELVIEW );
	qglLoadIdentity();

	GL_State( GLS_DEPTHTEST_DISABLE |
		GLS_SRCBLEND_SRC_ALPHA |
		GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	GL_Cull( CT_TWO_SIDED );
	qglDisable( GL_CLIP_PLANE0 );
#endif

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds();
	backEnd.refdef.floatTime = (double)backEnd.refdef.time * 0.001; // -EC-: cast to double
}


/*
=============
RE_StretchRaw

FIXME: not exactly backend
Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle.
Used for cinematics.
=============
*/
void RE_StretchRaw( int x, int y, int w, int h, int cols, int rows, byte *data, int client, qboolean dirty ) {
	int			i, j;
	int			start, end;

	if ( !tr.registered ) {
		return;
	}

	start = 0;
	if ( r_speeds->integer ) {
		start = ri.Milliseconds();
	}

	// make sure rows and cols are powers of 2
	for ( i = 0 ; ( 1 << i ) < cols ; i++ ) {
	}
	for ( j = 0 ; ( 1 << j ) < rows ; j++ ) {
	}
	if ( ( 1 << i ) != cols || ( 1 << j ) != rows) {
		ri.Error (ERR_DROP, "Draw_StretchRaw: size not a power of 2: %i by %i", cols, rows);
	}

	RE_UploadCinematic( w, h, cols, rows, data, client, dirty );

	if ( r_speeds->integer ) {
		end = ri.Milliseconds();
		ri.Printf( PRINT_ALL, "qglTexSubImage2D %i, %i: %i msec\n", cols, rows, end - start );
	}

	tr.cinematicShader->stages[0]->bundle[0].image[0] = tr.scratchImage[client];
	RE_StretchPic( x, y, w, h, 0.5f / cols, 0.5f / rows, 1.0f - 0.5f / cols, 1.0f - 0.5 / rows, tr.cinematicShader->index );
}


void RE_UploadCinematic( int w, int h, int cols, int rows, byte *data, int client, qboolean dirty ) {

	image_t *image;

	if ( !tr.scratchImage[ client ] ) {
		tr.scratchImage[ client ] = R_CreateImage( va( "*scratch%i", client ), data, cols, rows, IMGFLAG_CLAMPTOEDGE | IMGFLAG_RGB | IMGFLAG_NOSCALE );
	}

	image = tr.scratchImage[ client ];

	GL_Bind( image );

	// if the scratchImage isn't in the format we want, specify it as a new texture
	if ( cols != image->width || rows != image->height ) {
		image->width = image->uploadWidth = cols;
		image->height = image->uploadHeight = rows;
#ifdef USE_VULKAN
		qvkDestroyImage( vk.device, image->handle, NULL );
		qvkDestroyImageView( vk.device, image->view, NULL );
		vk_create_image( cols, rows, VK_FORMAT_R8G8B8A8_UNORM, 1, image->wrapClampMode, image );
		vk_upload_image_data( image->handle, 0, 0, cols, rows, qfalse, data, 4 );
#else
		qglTexImage2D( GL_TEXTURE_2D, 0, image->internalFormat, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_clamp_mode );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_clamp_mode );
#endif
	} else if ( dirty ) {
		// otherwise, just subimage upload it so that drivers can tell we are going to be changing
		// it and don't try and do a texture compression
#ifdef USE_VULKAN
		vk_upload_image_data( image->handle, 0, 0, cols, rows, qfalse, data, 4 );
#else
		qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data );
#endif
	}
}


/*
=============
RB_SetColor
=============
*/
static const void *RB_SetColor( const void *data ) {
	const setColorCommand_t	*cmd;

	cmd = (const setColorCommand_t *)data;

	backEnd.color2D[0] = cmd->color[0] * 255;
	backEnd.color2D[1] = cmd->color[1] * 255;
	backEnd.color2D[2] = cmd->color[2] * 255;
	backEnd.color2D[3] = cmd->color[3] * 255;

	return (const void *)(cmd + 1);
}


/*
=============
RB_StretchPic
=============
*/
static const void *RB_StretchPic( const void *data ) {
	const stretchPicCommand_t	*cmd;
	shader_t *shader;
	int		numVerts, numIndexes;

	cmd = (const stretchPicCommand_t *)data;

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0 );
	}

#ifdef USE_VBO
	VBO_UnBind();
#endif

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	RB_CHECKOVERFLOW( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	*(int *)tess.vertexColors[ numVerts ] =
		*(int *)tess.vertexColors[ numVerts + 1 ] =
		*(int *)tess.vertexColors[ numVerts + 2 ] =
		*(int *)tess.vertexColors[ numVerts + 3 ] = *(int *)backEnd.color2D;

	tess.xyz[ numVerts ][0] = cmd->x;
	tess.xyz[ numVerts ][1] = cmd->y;
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[0][ numVerts + 0][0] = cmd->s1;
	tess.texCoords[0][ numVerts + 0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 1 ][1] = cmd->y;
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[0][numVerts + 1][0] = cmd->s2;
	tess.texCoords[0][numVerts + 1][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 2 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[0][numVerts + 2][0] = cmd->s2;
	tess.texCoords[0][numVerts + 2][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = cmd->x;
	tess.xyz[ numVerts + 3 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[0][numVerts + 3][0] = cmd->s1;
	tess.texCoords[0][numVerts + 3][1] = cmd->t2;

	return (const void *)(cmd + 1);
}


#ifdef USE_PMLIGHT
static void RB_LightingPass( qboolean skipWeapon )
{
	dlight_t	*dl;
	int	i;

#ifdef USE_VBO
	//VBO_Flush();
	//tess.allowVBO = qfalse; // for now
#endif

	tess.dlightPass = qtrue;

	for ( i = 0; i < backEnd.viewParms.num_dlights; i++ )
	{
		dl = &backEnd.viewParms.dlights[i];
		if ( dl->head )
		{
			tess.light = dl;
			RB_RenderLitSurfList( dl, skipWeapon );
		}
	}

	tess.dlightPass = qfalse;

	backEnd.viewParms.num_dlights = 0;
}
#endif


#ifdef USE_VULKAN
static void transform_to_eye_space( const vec3_t v, vec3_t v_eye )
{
	const float *m = vk_world.modelview_transform;
	v_eye[0] = m[0]*v[0] + m[4]*v[1] + m[8 ]*v[2] + m[12];
	v_eye[1] = m[1]*v[0] + m[5]*v[1] + m[9 ]*v[2] + m[13];
	v_eye[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14];
};
#endif


/*
================
RB_DebugPolygon
================
*/
static void RB_DebugPolygon( int color, int numPoints, float *points ) {
#ifdef USE_VULKAN
	vec3_t pa;
	vec3_t pb;
	vec3_t p;
	vec3_t q;
	vec3_t n;
	int i;

	if ( numPoints < 3 )
		return;

	transform_to_eye_space( &points[0], pa );
	transform_to_eye_space( &points[3], pb );
	VectorSubtract( pb, pa, p );

	for ( i = 2; i < numPoints; i++ ) {
		transform_to_eye_space( &points[3*i], pb );
		VectorSubtract( pb, pa, q );
		CrossProduct( q, p, n );
		if ( VectorLength( n ) > 1e-5 ) {
			break;
		}
	}

	if ( DotProduct(n, pa) >= 0 ) {
		return; // discard backfacing polygon
	}

	// Solid shade.
	for (i = 0; i < numPoints; i++) {
		VectorCopy(&points[3*i], tess.xyz[i]);

		tess.svars.colors[i][0] = (color&1) ? 255 : 0;
		tess.svars.colors[i][1] = (color&2) ? 255 : 0;
		tess.svars.colors[i][2] = (color&4) ? 255 : 0;
		tess.svars.colors[i][3] = 255;
	}
	tess.numVertexes = numPoints;

	tess.numIndexes = 0;
	for (i = 1; i < numPoints - 1; i++) {
		tess.indexes[tess.numIndexes + 0] = 0;
		tess.indexes[tess.numIndexes + 1] = i;
		tess.indexes[tess.numIndexes + 2] = i + 1;
		tess.numIndexes += 3;
	}

	vk_bind_geometry_ext( TESS_IDX | TESS_XYZ | TESS_RGBA | TESS_ST0 );
	vk_draw_geometry( vk.surface_debug_pipeline_solid, DEPTH_RANGE_NORMAL, qtrue );

	// Outline.
	Com_Memset( tess.svars.colors, tr.identityLightByte, numPoints * 2 * sizeof(tess.svars.colors[0] ) );

	for ( i = 0; i < numPoints; i++ ) {
		VectorCopy( &points[3*i], tess.xyz[2*i] );
		VectorCopy( &points[3*((i + 1) % numPoints)], tess.xyz[2*i + 1] );
	}
	tess.numVertexes = numPoints * 2;
	tess.numIndexes = 0;

	vk_bind_geometry_ext( TESS_XYZ | TESS_RGBA );
	vk_draw_geometry( vk.surface_debug_pipeline_outline, DEPTH_RANGE_ZERO, qfalse );
	tess.numVertexes = 0;
#else
	int		i;

	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );

	// draw solid shade

	qglColor3f( color&1, (color>>1)&1, (color>>2)&1 );
	qglBegin( GL_POLYGON );
	for ( i = 0 ; i < numPoints ; i++ ) {
		qglVertex3fv( points + i * 3 );
	}
	qglEnd();

	// draw wireframe outline
	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	qglDepthRange( 0, 0 );
	qglColor3f( 1, 1, 1 );
	qglBegin( GL_POLYGON );
	for ( i = 0 ; i < numPoints ; i++ ) {
		qglVertex3fv( points + i * 3 );
	}
	qglEnd();
	qglDepthRange( 0, 1 );
#endif
}


/*
====================
RB_DebugGraphics

Visualization aid for movement clipping debugging
====================
*/
static void RB_DebugGraphics( void ) {

	if ( !r_debugSurface->integer ) {
		return;
	}

	GL_Bind( tr.whiteImage );
#ifdef USE_VULKAN
	vk_update_mvp( NULL );
#else
	GL_Cull( CT_FRONT_SIDED );
#endif
	ri.CM_DrawDebugSurface( RB_DebugPolygon );
}


/*
=============
RB_DrawSurfs
=============
*/
static const void *RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t *cmd;
	qboolean skipWeapon;

	// finish any 2D drawing if needed
	RB_EndSurface();

	cmd = (const drawSurfsCommand_t *)data;

	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;

	skipWeapon = (vk.renderPassIndex == RENDER_PASS_SCREENMAP) ? qtrue : qfalse;

__redraw:

#ifdef USE_VBO
	VBO_UnBind();
#endif

	// clear the z buffer, set the modelview, etc
	RB_BeginDrawingView();

	RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs, skipWeapon );

#ifdef USE_VBO
	VBO_UnBind();
#endif

	if ( r_drawSun->integer ) {
		RB_DrawSun( 0.1f, tr.sunShader );
	}

	// darken down any stencil shadows
	RB_ShadowFinish();

	// add light flares on lights that aren't obscured
	RB_RenderFlares();

#ifdef USE_PMLIGHT
	if ( backEnd.refdef.numLitSurfs ) {
		RB_BeginDrawingLitSurfs();
		RB_LightingPass( skipWeapon );
	}
#endif

	if ( skipWeapon ) {
		vk_end_render_pass();
		vk_begin_main_render_pass();

		backEnd.refdef = cmd->refdef;
		backEnd.viewParms = cmd->viewParms;

		skipWeapon = qfalse;

		goto __redraw;
	}

	// draw main system development information (surface outlines, etc)
	RB_DebugGraphics();

	//TODO Maybe check for rdf_noworld stuff but q3mme has full 3d ui
	backEnd.doneSurfaces = qtrue; // for bloom

	return (const void *)(cmd + 1);
}


/*
=============
RB_DrawBuffer
=============
*/
static const void *RB_DrawBuffer( const void *data ) {
	const drawBufferCommand_t	*cmd;

	cmd = (const drawBufferCommand_t *)data;

#ifdef USE_VULKAN
	vk_begin_frame();

	tess.depthRange = DEPTH_RANGE_NORMAL;

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

	if ( r_clear->integer ) {
		const vec4_t color = {1, 0, 0.5, 1};
		backEnd.projection2D = qtrue; // to ensure we have viewport that occupies entire window
		vk_clear_color( color );
		backEnd.projection2D = qfalse;
	}
#else
	qglDrawBuffer( cmd->buffer );

	// clear screen for debugging
	if ( r_clear->integer ) {
		qglClearColor( 1, 0, 0.5, 1 );
		qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}
#endif

	return (const void *)(cmd + 1);
}


/*
===============
RB_ShowImages

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.

Also called by RE_EndRegistration
===============
*/
#ifdef USE_VULKAN
void RB_ShowImages( void )
{
	int i;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	vk_clear_color( colorBlack );

	for (i = 0 ; i < tr.numImages ; i++) {
		image_t *image = tr.images[i];

		float w = glConfig.vidWidth / 20;
		float h = glConfig.vidHeight / 15;
		float x = i % 20 * w;
		float y = i / 20 * h;

		// show in proportional size in mode 2
		if ( r_showImages->integer == 2 ) {
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		GL_Bind( image );

		Com_Memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * sizeof( tess.svars.colors[0] ) );

		tess.numIndexes = 6;
		tess.numVertexes = 4;

		tess.indexes[0] = 0;
		tess.indexes[1] = 1;
		tess.indexes[2] = 2;
		tess.indexes[3] = 0;
		tess.indexes[4] = 2;
		tess.indexes[5] = 3;

		tess.xyz[0][0] = x;
		tess.xyz[0][1] = y;
		tess.svars.texcoords[0][0][0] = 0;
		tess.svars.texcoords[0][0][1] = 0;

		tess.xyz[1][0] = x + w;
		tess.xyz[1][1] = y;
		tess.svars.texcoords[0][1][0] = 1;
		tess.svars.texcoords[0][1][1] = 0;

		tess.xyz[2][0] = x + w;
		tess.xyz[2][1] = y + h;
		tess.svars.texcoords[0][2][0] = 1;
		tess.svars.texcoords[0][2][1] = 1;

		tess.xyz[3][0] = x;
		tess.xyz[3][1] = y + h;
		tess.svars.texcoords[0][3][0] = 0;
		tess.svars.texcoords[0][3][1] = 1;

		tess.svars.texcoordPtr[0] = tess.svars.texcoords[0];

		vk_bind_geometry_ext( TESS_IDX | TESS_XYZ | TESS_RGBA | TESS_ST0 );
		vk_draw_geometry( vk.images_debug_pipeline, DEPTH_RANGE_NORMAL, qtrue );
	}

	tess.numIndexes = 0;
	tess.numVertexes = 0;
}
#else
void RB_ShowImages( void ) {
	int		i;
	image_t	*image;
	float	x, y, w, h;
	int		start, end;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	qglClear( GL_COLOR_BUFFER_BIT );

	qglFinish();

	start = ri.Milliseconds();

	for ( i = 0; i < tr.numImages; i++ ) {
		image = tr.images[ i ];
		w = glConfig.vidWidth / 20;
		h = glConfig.vidHeight / 15;
		x = i % 20 * w;
		y = i / 20 * h;

		// show in proportional size in mode 2
		if ( r_showImages->integer == 2 ) {
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		GL_Bind( image );
		qglBegin (GL_QUADS);
		qglTexCoord2f( 0, 0 );
		qglVertex2f( x, y );
		qglTexCoord2f( 1, 0 );
		qglVertex2f( x + w, y );
		qglTexCoord2f( 1, 1 );
		qglVertex2f( x + w, y + h );
		qglTexCoord2f( 0, 1 );
		qglVertex2f( x, y + h );
		qglEnd();
	}

	qglFinish();

	end = ri.Milliseconds();
	ri.Printf( PRINT_ALL, "%i msec to draw all images\n", end - start );
}
#endif


/*
=============
RB_ColorMask
=============
*/
static const void *RB_ColorMask( const void *data )
{
	const colorMaskCommand_t *cmd = data;
#ifdef USE_VULKAN
	// TODO: implement! ZZZZZZZZZZZ
#else
	qglColorMask( cmd->rgba[0], cmd->rgba[1], cmd->rgba[2], cmd->rgba[3] );
#endif
	
	return (const void *)(cmd + 1);
}


/*
=============
RB_ClearDepth
=============
*/
static const void *RB_ClearDepth( const void *data )
{
	const clearDepthCommand_t *cmd = data;
	
	RB_EndSurface();

	// texture swapping test
	//if ( r_showImages->integer )
	//	RB_ShowImages();

#ifdef USE_VULKAN
	vk_clear_depth( r_shadows->integer == 2 ? qtrue : qfalse );
#else
	qglClear( GL_DEPTH_BUFFER_BIT );
#endif
	
	return (const void *)(cmd + 1);
}


/*
=============
RB_ClearColor
=============
*/
static const void *RB_ClearColor( const void *data )
{
	const clearColorCommand_t *cmd = data;



#ifdef USE_VULKAN
	backEnd.projection2D = qtrue;
	vk_clear_color( colorBlack );
	backEnd.projection2D = qfalse;
#else
	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	qglClear( GL_COLOR_BUFFER_BIT );
#endif

	return (const void *)(cmd + 1);
}


/*
=============
RB_FinishBloom
=============
*/
static const void *RB_FinishBloom( const void *data )
{
	const finishBloomCommand_t *cmd = data;

	RB_EndSurface();

	backEnd.drawConsole = qtrue;

	return (const void *)(cmd + 1);
}


static const void *RB_SwapBuffers( const void *data ) {

	const swapBuffersCommand_t	*cmd;

	// finish any 2D drawing if needed
	RB_EndSurface();

	cmd = (const swapBuffersCommand_t *)data;

	// texture swapping test
	if ( r_showImages->integer ) {
		RB_ShowImages();
	}

	tr.needScreenMap = 0;

#ifdef USE_VULKAN
	vk_end_frame();
#else
	if ( backEnd.doneSurfaces && !glState.finishCalled ) {
		qglFinish();
	}
#endif

#ifdef USE_VULKAN
	if ( backEnd.screenshotMask && vk.cmd->waitForFence ) {
#else
	if ( backEnd.screenshotMask && tr.frameCount > 1 ) {
#endif
		if ( backEnd.screenshotMask & SCREENSHOT_TGA && backEnd.screenshotTGA[0] ) {
			RB_TakeScreenshot( 0, 0, glConfig.vidWidth, glConfig.vidHeight, backEnd.screenshotTGA );
			if ( !backEnd.screenShotTGAsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotTGA );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_JPG && backEnd.screenshotJPG[0] ) {
			RB_TakeScreenshotJPEG( 0, 0, glConfig.vidWidth, glConfig.vidHeight, backEnd.screenshotJPG );
			if ( !backEnd.screenShotJPGsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotJPG );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_BMP && ( backEnd.screenshotBMP[0] || ( backEnd.screenshotMask & SCREENSHOT_BMP_CLIPBOARD ) ) ) {
			RB_TakeScreenshotBMP( 0, 0, glConfig.vidWidth, glConfig.vidHeight, backEnd.screenshotBMP, backEnd.screenshotMask & SCREENSHOT_BMP_CLIPBOARD );
			if ( !backEnd.screenShotBMPsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotBMP );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_AVI ) {
			RB_TakeVideoFrameCmd( &backEnd.vcmd );
		}

		backEnd.screenshotJPG[0] = '\0';
		backEnd.screenshotTGA[0] = '\0';
		backEnd.screenshotBMP[0] = '\0';
		backEnd.screenshotMask = 0;
	}

#ifndef USE_VULKAN
	ri.GLimp_EndFrame();
#endif

	backEnd.projection2D = qfalse;
	backEnd.doneSurfaces = qfalse;
	backEnd.drawConsole = qfalse;

	return (const void *)(cmd + 1);
}


/*
====================
RB_ExecuteRenderCommands
====================
*/
void RB_ExecuteRenderCommands( const void *data ) {

	backEnd.pc.msec = ri.Milliseconds();

	while ( 1 ) {
		data = PADP(data, sizeof(void *));

		switch ( *(const int *)data ) {
		case RC_SET_COLOR:
			data = RB_SetColor( data );
			break;
		case RC_STRETCH_PIC:
			data = RB_StretchPic( data );
			break;
		case RC_DRAW_SURFS:
			data = RB_DrawSurfs( data );
			break;
		case RC_DRAW_BUFFER:
			data = RB_DrawBuffer( data );
			break;
		case RC_SWAP_BUFFERS:
			data = RB_SwapBuffers( data );
			break;
		case RC_FINISHBLOOM:
			data = RB_FinishBloom(data);
			break;
		case RC_COLORMASK:
			data = RB_ColorMask(data);
			break;
		case RC_CLEARDEPTH:
			data = RB_ClearDepth(data);
			break;
		case RC_CLEARCOLOR:
			data = RB_ClearColor(data);
			break;
		case RC_END_OF_LIST:
		default:
			// stop rendering
#ifdef USE_VULKAN
//			if (com_errorEntered && (begin_frame_called && !end_frame_called)) {
//				vk_end_frame();
//			}
#else
			backEnd.pc.msec = ri.Milliseconds() - backEnd.pc.msec;
#endif
			return;
		}
	}
}
