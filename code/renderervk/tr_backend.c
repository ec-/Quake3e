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

/*
** GL_Bind
*/
void Bind( image_t *image ) {
	if ( !image ) {
		ri.Printf( PRINT_WARNING, "GL_Bind: NULL image\n" );
		image = tr.defaultImage;
	}

	if ( r_nobind->integer && tr.dlightImage ) {		// performance evaluation option
		image = tr.dlightImage;
	}

	//if ( glState.currenttextures[glState.currenttmu] != texnum ) {
		image->frameUsed = tr.frameCount;
		vk_update_descriptor( glState.currenttmu + VK_DESC_TEXTURE_BASE, image->descriptor );

	//}
}


/*
** GL_SelectTexture
*/
void SelectTexture( int unit )
{
	if ( unit >= glConfig.numTextureUnits )
	{
		ri.Error( ERR_DROP, "GL_SelectTexture: unit = %i", unit );
	}

	glState.currenttmu = unit;
}

/*
** GL_Cull
*/
void GL_Cull( cullType_t cullType ) {
	if ( glState.faceCulling == cullType ) {
		return;
	}

	glState.faceCulling = cullType;
}

static void RB_SetGL2D( void );

/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace( void ) {
	color4ub_t c;

	if ( !backEnd.isHyperspace ) {
		// do initialization shit
	}

	if ( tess.shader != tr.whiteShader ) {
		RB_EndSurface();
		RB_BeginSurface( tr.whiteShader, 0 );
	}

#ifdef USE_VBO
	VBO_UnBind();
#endif

	RB_SetGL2D();

	c.rgba[0] = c.rgba[1] = c.rgba[2] = (backEnd.refdef.time & 255);
	c.rgba[3] = 255;

	RB_AddQuadStamp2( backEnd.refdef.x, backEnd.refdef.y, backEnd.refdef.width, backEnd.refdef.height,
		0.0, 0.0, 0.0, 0.0, c );

	RB_EndSurface();

	tess.numIndexes = 0;
	tess.numVertexes = 0;

	backEnd.isHyperspace = true;
}


static void SetViewportAndScissor( void ) {
	//Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
	//vk_update_mvp();
	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
}


/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
static void RB_BeginDrawingView( void ) {
	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = false;

	//
	// set the modelview matrix for the viewer
	//
	SetViewportAndScissor();

	vk_clear_depth( true );

	if ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) {
		RB_Hyperspace();
		backEnd.projection2D = false;
		SetViewportAndScissor();
	} else {
		backEnd.isHyperspace = false;
	}

	glState.faceCulling = -1;		// force face culling to set next time

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = false;
}

#ifdef USE_PMLIGHT
static void RB_LightingPass( void );
#endif

/*
==================
RB_RenderDrawSurfList
==================
*/
static void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t		*shader, *oldShader;
	int				fogNum;
	int				entityNum, oldEntityNum;
	int				dlighted;
	bool		depthRange, isCrosshair;
	int				i;
	drawSurf_t		*drawSurf;
	unsigned int	oldSort;
#ifdef USE_PMLIGHT
	float			oldShaderSort;
#endif
	double			originalTime; // -EC-

	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldSort = MAX_UINT;
#ifdef USE_PMLIGHT
	oldShaderSort = -1;
#endif
	depthRange = false;

	backEnd.pc.c_surfaces += numDrawSurfs;

	for (i = 0, drawSurf = drawSurfs ; i < numDrawSurfs ; i++, drawSurf++) {
		if ( drawSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
			continue;
		}

		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
		if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP && entityNum != REFENTITYNUM_WORLD && backEnd.refdef.entities[ entityNum ].e.renderfx & RF_DEPTHHACK ) {
			continue;
		}
		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from separate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( ( (oldSort ^ drawSurfs->sort ) & ~QSORT_REFENTITYNUM_MASK ) || !shader->entityMergable ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
			}
#ifdef USE_PMLIGHT
			#define INSERT_POINT SS_FOG
			if ( backEnd.refdef.numLitSurfs && oldShaderSort < INSERT_POINT && shader->sort >= INSERT_POINT ) {
				//RB_BeginDrawingLitSurfs(); // no need, already setup in RB_BeginDrawingView()
				RB_LightingPass();
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
			depthRange = isCrosshair = false;

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
					depthRange = true;

					if(backEnd.currentEntity->e.renderfx & RF_CROSSHAIR)
						isCrosshair = true;
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

			Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
			tess.depthRange = depthRange ? DEPTH_RANGE_WEAPON : DEPTH_RANGE_NORMAL;
			vk_update_mvp( NULL );

			//
			// change depthrange. Also change projection matrix so first person weapon does not look like coming
			// out of the screen.
			//

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
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	//vk_update_mvp();
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
	backEnd.projection2D = false;

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = false;

	//
	// set the modelview matrix for the viewer
	//
	SetViewportAndScissor();

	glState.faceCulling = -1;		// force face culling to set next time
}


/*
==================
RB_RenderLitSurfList
==================
*/
static void RB_RenderLitSurfList( dlight_t* dl ) {
	shader_t		*shader, *oldShader;
	int				fogNum;
	int				entityNum, oldEntityNum;
	bool		depthRange, isCrosshair;
	const litSurf_t	*litSurf;
	unsigned int	oldSort;
	double			originalTime; // -EC-

	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldSort = MAX_UINT;
	depthRange = false;

	tess.dlightUpdateParams = true;

	for ( litSurf = dl->head; litSurf; litSurf = litSurf->next ) {
		//if ( litSurf->sort == sort ) {
		if ( litSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *litSurf->surface ]( litSurf->surface );
			continue;
		}

		R_DecomposeLitSort( litSurf->sort, &entityNum, &shader, &fogNum );

		if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP && entityNum != REFENTITYNUM_WORLD && backEnd.refdef.entities[ entityNum ].e.renderfx & RF_DEPTHHACK ) {
			continue;
		}

		// anything BEFORE opaque is sky/portal, anything AFTER it should never have been added
		//assert( shader->sort == SS_OPAQUE );
		// !!! but MIRRORS can trip that assert, so just do this for now
		//if ( shader->sort < SS_OPAQUE )
		//	continue;

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from separate
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
			depthRange = isCrosshair = false;

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
					depthRange = true;

					if(backEnd.currentEntity->e.renderfx & RF_CROSSHAIR)
						isCrosshair = true;
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
			tess.dlightUpdateParams = true;

			tess.depthRange = depthRange ? DEPTH_RANGE_WEAPON : DEPTH_RANGE_NORMAL;
			Com_Memcpy( vk_world.modelview_transform, backEnd.or.modelMatrix, 64 );
			vk_update_mvp( NULL );

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
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelMatrix, 64 );
	tess.depthRange = DEPTH_RANGE_NORMAL;
	//vk_update_mvp();
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
	backEnd.projection2D = true;

	vk_update_mvp( NULL );

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

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
void RE_StretchRaw( int x, int y, int w, int h, int cols, int rows, byte *data, int client, bool dirty ) {
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

	if ( ( 1 << i ) != cols || ( 1 << j ) != rows ) {
		ri.Error( ERR_DROP, "%s(): size not a power of 2: %i by %i", __func__, cols, rows );
	}

	RE_UploadCinematic( w, h, cols, rows, data, client, dirty );

	if ( r_speeds->integer ) {
		end = ri.Milliseconds();
		ri.Printf( PRINT_ALL, "RE_UploadCinematic( %i, %i ): %i msec\n", cols, rows, end - start );
	}

	tr.cinematicShader->stages[0]->bundle[0].image[0] = tr.scratchImage[client];
	RE_StretchPic( x, y, w, h, 0.5f / cols, 0.5f / rows, 1.0f - 0.5f / cols, 1.0f - 0.5 / rows, tr.cinematicShader->index );
}


void RE_UploadCinematic( int w, int h, int cols, int rows, byte *data, int client, bool dirty ) {

	image_t *image;

	if ( !tr.scratchImage[ client ] ) {
		tr.scratchImage[ client ] = R_CreateImage( va( "*scratch%i", client ), NULL, data, cols, rows, IMGFLAG_CLAMPTOEDGE | IMGFLAG_RGB | IMGFLAG_NOSCALE );
	}

	image = tr.scratchImage[ client ];

	Bind( image );

	// if the scratchImage isn't in the format we want, specify it as a new texture
	if ( cols != image->width || rows != image->height ) {
		image->width = image->uploadWidth = cols;
		image->height = image->uploadHeight = rows;
		vk_create_image( image, cols, rows, 1 );
		vk_upload_image_data( image, 0, 0, cols, rows, 1, data, cols * rows * 4, false );
	} else if ( dirty ) {
		// otherwise, just subimage upload it so that drivers can tell we are going to be changing
		// it and don't try and do a texture compression
		vk_upload_image_data( image, 0, 0, cols, rows, 1, data, cols * rows * 4, true );
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

	backEnd.color2D.rgba[0] = cmd->color[0] * 255;
	backEnd.color2D.rgba[1] = cmd->color[1] * 255;
	backEnd.color2D.rgba[2] = cmd->color[2] * 255;
	backEnd.color2D.rgba[3] = cmd->color[3] * 255;

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

	if ( r_bloom->integer ) {
		vk_bloom();
	}

	RB_AddQuadStamp2( cmd->x, cmd->y, cmd->w, cmd->h, cmd->s1, cmd->t1, cmd->s2, cmd->t2, backEnd.color2D );

	return (const void *)(cmd + 1);
}


#ifdef USE_PMLIGHT
static void RB_LightingPass( void )
{
	dlight_t	*dl;
	int	i;

#ifdef USE_VBO
	//VBO_Flush();
	//tess.allowVBO = false; // for now
#endif

	tess.dlightPass = true;

	for ( i = 0; i < backEnd.viewParms.num_dlights; i++ )
	{
		dl = &backEnd.viewParms.dlights[i];
		if ( dl->head )
		{
			tess.light = dl;
			RB_RenderLitSurfList( dl );
		}
	}

	tess.dlightPass = false;

	backEnd.viewParms.num_dlights = 0;
}
#endif


static void transform_to_eye_space( const vec3_t v, vec3_t v_eye )
{
	const float *m = backEnd.viewParms.world.modelMatrix;
	v_eye[0] = m[0]*v[0] + m[4]*v[1] + m[8 ]*v[2] + m[12];
	v_eye[1] = m[1]*v[0] + m[5]*v[1] + m[9 ]*v[2] + m[13];
	v_eye[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14];
};


/*
================
RB_DebugPolygon
================
*/
static void RB_DebugPolygon( int color, int numPoints, float *points ) {
	vec3_t pa;
	vec3_t pb;
	vec3_t p;
	vec3_t q;
	vec3_t n;
	int i;

	if ( numPoints < 3 ) {
		return;
	}

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

	if ( DotProduct( n, pa ) >= 0 ) {
		return; // discard backfacing polygon
	}

	// Solid shade.
	for (i = 0; i < numPoints; i++) {
		VectorCopy(&points[3*i], tess.xyz[i]);

		tess.svars.colors[0][i].rgba[0] = (color&1) ? 255 : 0;
		tess.svars.colors[0][i].rgba[1] = (color&2) ? 255 : 0;
		tess.svars.colors[0][i].rgba[2] = (color&4) ? 255 : 0;
		tess.svars.colors[0][i].rgba[3] = 255;
	}
	tess.numVertexes = numPoints;

	tess.numIndexes = 0;
	for (i = 1; i < numPoints - 1; i++) {
		tess.indexes[tess.numIndexes + 0] = 0;
		tess.indexes[tess.numIndexes + 1] = i;
		tess.indexes[tess.numIndexes + 2] = i + 1;
		tess.numIndexes += 3;
	}

	vk_bind_index();
	vk_bind_pipeline( vk.surface_debug_pipeline_solid );
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, true );

	// Outline.
	Com_Memset( tess.svars.colors[0], tr.identityLightByte, numPoints * 2 * sizeof( color4ub_t ) );

	for ( i = 0; i < numPoints; i++ ) {
		VectorCopy( &points[3*i], tess.xyz[2*i] );
		VectorCopy( &points[3*((i + 1) % numPoints)], tess.xyz[2*i + 1] );
	}
	tess.numVertexes = numPoints * 2;
	tess.numIndexes = 0;

	vk_bind_pipeline( vk.surface_debug_pipeline_outline );
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 );
	vk_draw_geometry( DEPTH_RANGE_ZERO, false );
	tess.numVertexes = 0;
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

	Bind( tr.whiteImage );
	vk_update_mvp( NULL );
	ri.CM_DrawDebugSurface( RB_DebugPolygon );
}


/*
=============
RB_DrawSurfs
=============
*/
static const void *RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t *cmd;

	// finish any 2D drawing if needed
	RB_EndSurface();

	cmd = (const drawSurfsCommand_t *)data;

	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;

#ifdef USE_VBO
	VBO_UnBind();
#endif

	// clear the z buffer, set the modelview, etc
	RB_BeginDrawingView();

	RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );

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
		RB_LightingPass();
	}
#endif

	// draw main system development information (surface outlines, etc)
	RB_DebugGraphics();

	if ( cmd->refdef.switchRenderPass ) {
		vk_end_render_pass();
		vk_begin_main_render_pass();
		backEnd.screenMapDone = true;
	}

	//TODO Maybe check for rdf_noworld stuff but q3mme has full 3d ui
	backEnd.doneSurfaces = true; // for bloom

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

	vk_begin_frame();

	tess.depthRange = DEPTH_RANGE_NORMAL;

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

	if ( r_clear->integer ) {
		const vec4_t color = {1, 0, 0.5, 1};
		backEnd.projection2D = true; // to ensure we have viewport that occupies entire window
		vk_clear_color( color );
		backEnd.projection2D = false;
	}
	
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
void RB_ShowImages( void )
{
	int i;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	vk_clear_color( colorBlack );

	for ( i = 0; i < tr.numImages; i++ ) {
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

		Bind( image );

		tess.svars.colors[0][0].u32 = ~0U; // 255-255-255-255
		tess.svars.colors[0][1].u32 = ~0U;
		tess.svars.colors[0][2].u32 = ~0U;
		tess.svars.colors[0][3].u32 = ~0U;

		tess.numVertexes = 4;

		tess.xyz[0][0] = x;
		tess.xyz[0][1] = y;
		tess.svars.texcoords[0][0][0] = 0;
		tess.svars.texcoords[0][0][1] = 0;

		tess.xyz[1][0] = x + w;
		tess.xyz[1][1] = y;
		tess.svars.texcoords[0][1][0] = 1;
		tess.svars.texcoords[0][1][1] = 0;

		tess.xyz[2][0] = x;
		tess.xyz[2][1] = y + h;
		tess.svars.texcoords[0][2][0] = 0;
		tess.svars.texcoords[0][2][1] = 1;

		tess.xyz[3][0] = x + w;
		tess.xyz[3][1] = y + h;
		tess.svars.texcoords[0][3][0] = 1;
		tess.svars.texcoords[0][3][1] = 1;

		tess.svars.texcoordPtr[0] = tess.svars.texcoords[0];

		vk_bind_pipeline( vk.images_debug_pipeline );
		vk_bind_geometry( TESS_XYZ | TESS_RGBA0 | TESS_ST0 );
		vk_draw_geometry( DEPTH_RANGE_NORMAL, false );
	}

	tess.numIndexes = 0;
	tess.numVertexes = 0;
}

/*
=============
RB_ColorMask
=============
*/
static const void *RB_ColorMask( const void *data )
{
	const colorMaskCommand_t *cmd = data;
	// TODO: implement! ZZZZZZZZZZZ  in gl qglColorMask( cmd->rgba[0], cmd->rgba[1], cmd->rgba[2], cmd->rgba[3] );

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

	vk_clear_depth( r_shadows->integer == 2 ? true : false );

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

	backEnd.projection2D = true;
	vk_clear_color( colorBlack );
	backEnd.projection2D = false;

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

	if ( r_bloom->integer ) {
		vk_bloom();
	}

	// texture swapping test
	if ( r_showImages->integer ) {
		RB_ShowImages();
	}

	backEnd.drawConsole = true;

	return (const void *)(cmd + 1);
}


static const void *RB_SwapBuffers( const void *data ) {

	const swapBuffersCommand_t	*cmd;

	// finish any 2D drawing if needed
	RB_EndSurface();

	// texture swapping test
	if ( r_showImages->integer && !backEnd.drawConsole ) {
		RB_ShowImages();
	}

	cmd = (const swapBuffersCommand_t *)data;

	tr.needScreenMap = 0;

	vk_end_frame();

	if ( backEnd.screenshotMask && vk.cmd->waitForFence ) {
		if ( backEnd.screenshotMask & SCREENSHOT_TGA && backEnd.screenshotTGA[0] ) {
			RB_TakeScreenshot( 0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotTGA );
			if ( !backEnd.screenShotTGAsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotTGA );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_JPG && backEnd.screenshotJPG[0] ) {
			RB_TakeScreenshotJPEG( 0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotJPG );
			if ( !backEnd.screenShotJPGsilent ) {
				ri.Printf( PRINT_ALL, "Wrote %s\n", backEnd.screenshotJPG );
			}
		}
		if ( backEnd.screenshotMask & SCREENSHOT_BMP && ( backEnd.screenshotBMP[0] || ( backEnd.screenshotMask & SCREENSHOT_BMP_CLIPBOARD ) ) ) {
			RB_TakeScreenshotBMP( 0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotBMP, backEnd.screenshotMask & SCREENSHOT_BMP_CLIPBOARD );
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

	vk_present_frame();

	backEnd.projection2D = false;
	backEnd.doneSurfaces = false;
	backEnd.drawConsole = false;
	backEnd.doneBloom = false;

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
			if ( vk.frame_count ) {
				vk_end_frame();
			}
			return;
		}
	}
}
