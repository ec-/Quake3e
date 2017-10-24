/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// tr_bloom.c: 2D lighting post process effect

#include "tr_local.h"

static cvar_t *r_bloom_sample_size;
static cvar_t *r_bloom_alpha;
static cvar_t *r_bloom_darken;
static cvar_t *r_bloom_intensity;
static cvar_t *r_bloom_diamond_size;
static cvar_t *r_bloom_cascade;
static cvar_t *r_bloom_cascade_blur;
static cvar_t *r_bloom_cascade_intensity;
static cvar_t *r_bloom_cascade_alpha;
static cvar_t *r_bloom_cascade_dry;
static cvar_t *r_bloom_dry;
static cvar_t *r_bloom_reflection;
extern cvar_t *r_bloom;

/*
==============================================================================

				LIGHT BLOOMS

==============================================================================
*/

static float Diamond8x[8][8] =
{
	{ 0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f, },
	{ 0.0f, 0.0f, 0.2f, 0.3f, 0.3f, 0.2f, 0.0f, 0.0f, },
	{ 0.0f, 0.2f, 0.4f, 0.6f, 0.6f, 0.4f, 0.2f, 0.0f, },
	{ 0.1f, 0.3f, 0.6f, 0.9f, 0.9f, 0.6f, 0.3f, 0.1f, },
	{ 0.1f, 0.3f, 0.6f, 0.9f, 0.9f, 0.6f, 0.3f, 0.1f, },
	{ 0.0f, 0.2f, 0.4f, 0.6f, 0.6f, 0.4f, 0.2f, 0.0f, },
	{ 0.0f, 0.0f, 0.2f, 0.3f, 0.3f, 0.2f, 0.0f, 0.0f, },
	{ 0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f  }
};

static float Diamond6x[6][6] =
{
	{ 0.0f, 0.0f, 0.1f, 0.1f, 0.0f, 0.0f, },
	{ 0.0f, 0.3f, 0.5f, 0.5f, 0.3f, 0.0f, }, 
	{ 0.1f, 0.5f, 0.9f, 0.9f, 0.5f, 0.1f, },
	{ 0.1f, 0.5f, 0.9f, 0.9f, 0.5f, 0.1f, },
	{ 0.0f, 0.3f, 0.5f, 0.5f, 0.3f, 0.0f, },
	{ 0.0f, 0.0f, 0.1f, 0.1f, 0.0f, 0.0f  }
};

static float Diamond4x[4][4] =
{
	{ 0.3f, 0.4f, 0.4f, 0.3f, },
	{ 0.4f, 0.9f, 0.9f, 0.4f, },
	{ 0.4f, 0.9f, 0.9f, 0.4f, },
	{ 0.3f, 0.4f, 0.4f, 0.3f  }
};

static struct {
	struct {
		image_t	*texture;
		int		width, height;
		float	readW, readH;
	} effect;
	struct {
		image_t	*texture;
		int		width, height;
		float	readW, readH;
	} effect2;
	struct {
		image_t	*texture;
		int		width, height;
		float	readW, readH;
	} screen;
	struct {
		int		width, height;
	} work;
	qboolean started;
	qboolean disabled;
} bloom;


static void ID_INLINE R_Bloom_Quad( int width, int height, float texX, float texY, float texWidth, float texHeight ) {
	int x = 0;
	int y = 0;
	x = 0;
	y += glConfig.vidHeight - height;
	width += x;
	height += y;
	
	texWidth += texX;
	texHeight += texY;

	qglBegin( GL_QUADS );							
	qglTexCoord2f(	texX,						texHeight	);	
	qglVertex2f(	x,							y	);

	qglTexCoord2f(	texX,						texY	);				
	qglVertex2f(	x,							height	);	

	qglTexCoord2f(	texWidth,					texY	);				
	qglVertex2f(	width,						height	);	

	qglTexCoord2f(	texWidth,					texHeight	);	
	qglVertex2f(	width,						y	);				
	qglEnd ();
}


static void ID_INLINE R_Bloom_Quad_Lens( float offsert, int width, int height, float texX, float texY, float texWidth, float texHeight )
{
	int x = 0;
	int y = 0;
	x = 0;
	y += glConfig.vidHeight - height;
	width += x;
	height += y;
	offsert = offsert * 9; // bah
	texWidth -= texX;
	texHeight -= texY;

	qglBegin( GL_QUADS );
	qglTexCoord2f(	texX,				texHeight );
	qglVertex2f(	width + offsert,	height + offsert );

	qglTexCoord2f(	texX,				texY );
	qglVertex2f(	width + offsert,	y	- offsert );

	qglTexCoord2f(	texWidth,			texY );
	qglVertex2f(	x - offsert,		y	- offsert );

	qglTexCoord2f(	texWidth,			texHeight );
	qglVertex2f(	x - offsert,		height	+ offsert );
	qglEnd ();
}


/*
=================
R_Bloom_InitTextures
=================
*/
static void R_Bloom_InitTextures( void )
{
	byte	*data;

	// find closer power of 2 to screen size 
	for (bloom.screen.width = 1;bloom.screen.width< glConfig.vidWidth;bloom.screen.width *= 2);
	for (bloom.screen.height = 1;bloom.screen.height < glConfig.vidHeight;bloom.screen.height *= 2);

	bloom.screen.readW = glConfig.vidWidth / (float)bloom.screen.width;
	bloom.screen.readH = glConfig.vidHeight / (float)bloom.screen.height;

	// find closer power of 2 to effect size 
	bloom.work.width = r_bloom_sample_size->integer;
	bloom.work.height = bloom.work.width * ( glConfig.vidWidth / glConfig.vidHeight );

	for (bloom.effect.width = 1;bloom.effect.width < bloom.work.width;bloom.effect.width *= 2);
	for (bloom.effect.height = 1;bloom.effect.height < bloom.work.height;bloom.effect.height *= 2);

	bloom.effect.readW = bloom.work.width / (float)bloom.effect.width;
	bloom.effect.readH = bloom.work.height / (float)bloom.effect.height;

	bloom.effect2.readW=bloom.effect.readW;
	bloom.effect2.readH=bloom.effect.readH;
	bloom.effect2.width=bloom.effect.width;
	bloom.effect2.height=bloom.effect.height;

	// disable blooms if we can't handle a texture of that size
	if( bloom.screen.width > glConfig.maxTextureSize ||
		bloom.screen.height > glConfig.maxTextureSize ||
		bloom.effect.width > glConfig.maxTextureSize ||
		bloom.effect.height > glConfig.maxTextureSize ||
		bloom.work.width > glConfig.vidWidth ||
		bloom.work.height > glConfig.vidHeight
	) {
		ri.Printf( PRINT_WARNING, "WARNING: 'R_InitBloomTextures' too high resolution for light bloom, effect disabled\n" );
		bloom.started = qfalse;
		bloom.disabled = qtrue;
		return;
	}

	data = ri.Hunk_AllocateTempMemory( bloom.screen.width * bloom.screen.height * 4 );
	Com_Memset( data, 0, bloom.screen.width * bloom.screen.height * 4 );
	bloom.screen.texture = R_CreateImage( "***bloom screen texture***", data, bloom.screen.width, bloom.screen.height, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE, 0 );
	ri.Hunk_FreeTempMemory( data );

	data = ri.Hunk_AllocateTempMemory( bloom.effect.width * bloom.effect.height * 4 );
	Com_Memset( data, 0, bloom.effect.width * bloom.effect.height * 4 );
	bloom.effect.texture = R_CreateImage( "***bloom effect texture***", data, bloom.effect.width, bloom.effect.height, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE, 0 );
	bloom.effect2.texture = R_CreateImage( "***bloom effect texture 2***", data, bloom.effect.width, bloom.effect.height, IMGTYPE_COLORALPHA, IMGFLAG_CLAMPTOEDGE, 0 );
	ri.Hunk_FreeTempMemory( data );

	bloom.started = qtrue;
}


/*
=================
R_InitBloomTextures
=================
*/
void R_InitBloomTextures( void )
{
	if ( r_bloom->integer != 1 )
		return;
	memset( &bloom, 0, sizeof( bloom ));
	R_Bloom_InitTextures ();
}


/*
=================
R_Bloom_DrawEffect
=================
*/
static void R_Bloom_DrawEffect( void )
{
	float alpha;

	GL_Bind( bloom.effect.texture );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	if ( r_bloom_cascade->integer ) {
		alpha = r_bloom_cascade_alpha->value;
	} else {
		alpha = r_bloom_alpha->value;
	}
	qglColor4f( alpha, alpha, alpha, 1.0f );
	R_Bloom_Quad( glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
}


/*
=================
R_Bloom_LensEffect
LEILEI's Silly hack
=================
*/
static void R_Bloom_LensEffect( void )
{
	GL_Bind( bloom.effect.texture );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE );

	qglColor4f( 0.78f, 0.23f, 0.34f, 0.07f );
	R_Bloom_Quad_Lens(16, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.78f, 0.39f, 0.21f, 0.07f );
	R_Bloom_Quad_Lens(32, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.78f, 0.59f, 0.21f, 0.07f );
	R_Bloom_Quad_Lens(48, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.71f, 0.75f, 0.21f, 0.07f );
	R_Bloom_Quad_Lens(64, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.52f, 0.78f, 0.21f, 0.07f );
	R_Bloom_Quad_Lens(80, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.32f, 0.78f, 0.21f, 0.07f );
	R_Bloom_Quad_Lens(96, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.21f, 0.78f, 0.28f, 0.07f );
	R_Bloom_Quad_Lens(112, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.21f, 0.78f, 0.47f, 0.07f );
	R_Bloom_Quad_Lens(128, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.21f, 0.77f, 0.66f, 0.07f );
	R_Bloom_Quad_Lens(144, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.21f, 0.67f, 0.78f, 0.07f );
	R_Bloom_Quad_Lens(160, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.21f, 0.47f, 0.78f, 0.07f );
	R_Bloom_Quad_Lens(176, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.21f, 0.28f, 0.78f, 0.07f );
	R_Bloom_Quad_Lens(192, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.35f, 0.21f, 0.78f, 0.07f );
	R_Bloom_Quad_Lens(208, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.53f, 0.21f, 0.78f, 0.07f );
	R_Bloom_Quad_Lens(224, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.72f, 0.21f, 0.75f, 0.07f );
	R_Bloom_Quad_Lens(240, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );
	qglColor4f( 0.78f, 0.21f, 0.59f, 0.07f );
	R_Bloom_Quad_Lens(256, glConfig.vidWidth, glConfig.vidHeight, 0, 0, bloom.effect.readW, bloom.effect.readW );

}


/*
 =================
 R_Bloom_Cascaded
 =================
 Tcpp: sorry for my poor English skill.
 */
static void R_Bloom_Cascaded( void )
{
	int scale;
	int oldWorkW, oldWorkH;
	int newWorkW, newWorkH;
	float bloomShiftX = r_bloom_cascade_blur->value / (float)bloom.effect.width;
	float bloomShiftY = r_bloom_cascade_blur->value / (float)bloom.effect.height;
	float intensity = r_bloom_cascade_intensity->value;
	float intensity2;

	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	//Take the backup texture and downscale it
	GL_Bind( bloom.screen.texture );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	R_Bloom_Quad( bloom.work.width, bloom.work.height, 0, 0, bloom.screen.readW, bloom.screen.readH );
	//Copy downscaled framebuffer into a texture
	GL_Bind( bloom.effect.texture );
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, bloom.work.width, bloom.work.height );

	/* Copy the result to the effect texture */
	GL_Bind( bloom.effect2.texture );
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, bloom.work.width, bloom.work.height );

	// do blurs..
	scale = 32;
	while ( bloom.work.width < scale )
		scale >>= 1;
	while ( bloom.work.height < scale )
		scale >>= 1;

	// prepare the first level.
	newWorkW = bloom.work.width / scale;
	newWorkH = bloom.work.height / scale;

	GL_Bind( bloom.effect2.texture );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	intensity2 = intensity / (float)scale;
	qglColor4f( intensity2, intensity2, intensity2, 1.0f );
	R_Bloom_Quad( newWorkW, newWorkH, 0, 0, bloom.effect2.readW, bloom.effect2.readH );

	// go through levels.
	while ( scale > 1 ) {
		float oldScaleInv = 1.0f / (float)scale;
		scale >>= 1;
		oldWorkH = newWorkH;
		oldWorkW = newWorkW;
		newWorkW = bloom.work.width/scale;
		newWorkH = bloom.work.height/scale;

		// get effect texture.
		GL_Bind( bloom.effect.texture );
		qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, oldWorkW, oldWorkH );

		// maginfy the previous level.
		if ( r_bloom_cascade_blur->value < 0.01f ) {
			// don't blur.
			qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
			GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
			R_Bloom_Quad( newWorkW, newWorkH, 0, 0, bloom.effect.readW*oldScaleInv, bloom.effect.readH*oldScaleInv );
		}
		else {
			// blur.
			qglColor4f( 0.25f, 0.25f, 0.25f, 1.0f );
			GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
			R_Bloom_Quad( newWorkW, newWorkH, -bloomShiftX, -bloomShiftY, bloom.effect.readW*oldScaleInv, bloom.effect.readH*oldScaleInv );

			GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
			R_Bloom_Quad( newWorkW, newWorkH, bloomShiftX, -bloomShiftY, bloom.effect.readW*oldScaleInv, bloom.effect.readH*oldScaleInv );
			R_Bloom_Quad( newWorkW, newWorkH, -bloomShiftX, bloomShiftY, bloom.effect.readW*oldScaleInv, bloom.effect.readH*oldScaleInv );
			R_Bloom_Quad( newWorkW, newWorkH, bloomShiftX, bloomShiftY, bloom.effect.readW*oldScaleInv, bloom.effect.readH*oldScaleInv );
		}

		// add the input.
		intensity2 = intensity / (float)scale;
		qglColor4f( intensity2, intensity2, intensity2, 1.0f );

		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
		GL_Bind( bloom.effect2.texture );

		R_Bloom_Quad( newWorkW, newWorkH, 0, 0, bloom.effect2.readW, bloom.effect2.readH );
	}

	GL_Bind( bloom.effect.texture );
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, bloom.work.width, bloom.work.height );
}


/*
=================
R_Bloom_GeneratexDiamonds
=================
*/
static void R_Bloom_WarsowEffect( void )
{
	int		i, j, k;
	float	intensity, scale, *diamond;

	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	//Take the backup texture and downscale it
	GL_Bind( bloom.screen.texture );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	R_Bloom_Quad( bloom.work.width, bloom.work.height, 0, 0, bloom.screen.readW, bloom.screen.readH );
	//Copy downscaled framebuffer into a texture
	GL_Bind( bloom.effect.texture );
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, bloom.work.width, bloom.work.height );
	
	// darkening passes with repeated filter
	if ( r_bloom_darken->integer ) {
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO );
		for( i = 0; i < r_bloom_darken->integer; i++ ) {
			R_Bloom_Quad( bloom.work.width, bloom.work.height, 0, 0, bloom.effect.readW, bloom.effect.readH );
		}
		qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, bloom.work.width, bloom.work.height );
	}
	/* Copy the result to the effect texture */
	GL_Bind( bloom.effect.texture );
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, bloom.work.width, bloom.work.height );

	// bluring passes, warsow uses a repeated semi blend on a selectable diamond grid
	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR );
	if( r_bloom_diamond_size->integer > 7 || r_bloom_diamond_size->integer <= 3 ) {
		if( r_bloom_diamond_size->integer != 8 )
			ri.Cvar_Set( "r_bloom_diamond_size", "8" );
	} else if( r_bloom_diamond_size->integer > 5 ) {
		if( r_bloom_diamond_size->integer != 6 )
			ri.Cvar_Set( "r_bloom_diamond_size", "6" );
	} else if( r_bloom_diamond_size->integer > 3 ) {
		if( r_bloom_diamond_size->integer != 4 )
			ri.Cvar_Set( "r_bloom_diamond_size", "4" );
	}

	switch( r_bloom_diamond_size->integer ) {
		case 4:
			k = 2;
			diamond = &Diamond4x[0][0];
			scale = r_bloom_intensity->value * 0.8f;
			break;
		case 6:
			k = 3;
			diamond = &Diamond6x[0][0];
			scale = r_bloom_intensity->value * 0.5f;
			break;
		default:
//		case 8:
			k = 4;
			diamond = &Diamond8x[0][0];
			scale = r_bloom_intensity->value * 0.3f;
			break;
	}

	for( i = 0; i < r_bloom_diamond_size->integer; i++ ) {
		for( j = 0; j < r_bloom_diamond_size->integer; j++, diamond++ ) {
			float x, y;
			intensity =  *diamond * scale;
			if ( intensity < 0.01f )
				continue;
			qglColor4f( intensity, intensity, intensity, 1.0 );
			x = (i - k) * ( 2 / 640.0f ) * bloom.effect.readW;
			y = (j - k) * ( 2 / 480.0f ) * bloom.effect.readH;

			R_Bloom_Quad( bloom.work.width, bloom.work.height, x, y, bloom.effect.readW, bloom.effect.readH );
		}
	}
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, bloom.work.width, bloom.work.height );
}


/*
=================
R_Bloom_BackupScreen
Backup the full original screen to a texture for downscaling and later restoration
=================
*/
static void R_Bloom_BackupScreen( void ) {
	GL_Bind( bloom.screen.texture );
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, glConfig.vidWidth, glConfig.vidHeight );
}


/*
=================
R_Bloom_RestoreScreen
Restore the temporary framebuffer section we used with the backup texture
=================
*/
static void R_Bloom_RestoreScreen( void ) {
	float dry = r_bloom_dry->value;
	if ( r_bloom_cascade->integer )
		dry = r_bloom_cascade_dry->value;
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_Bind( bloom.screen.texture );
	qglColor4f( dry, dry, dry, 1.0f );
	if ( dry < 0.99f ) {
		R_Bloom_Quad( bloom.screen.width, bloom.screen.height, 0, 0, 1.f, 1.f );
	}
	else {
		R_Bloom_Quad( bloom.work.width, bloom.work.height, 0, 0,
			bloom.work.width / (float)bloom.screen.width,
			bloom.work.height / (float)bloom.screen.height );
	}
}


extern void	RB_SetGL2D( void );

/*
=================
R_BloomScreen
=================
*/
void R_BloomScreen( void )
{
#ifdef USE_PMLIGHT
	if ( r_bloom->integer == 2 && fboEnabled )
	{
		if ( !backEnd.doneBloom2fbo && backEnd.doneSurfaces )
		{
			if ( !backEnd.projection2D )
				RB_SetGL2D();
			qglColor4f( 1, 1, 1, 1 );
			FBO_Bloom( 0, 0, qfalse );
		}
		return;
	}
#endif
	if ( r_bloom->integer != 1 )
		return;
	if ( backEnd.doneBloom )
		return;
	if ( !backEnd.doneSurfaces )
		return;
	backEnd.doneBloom = qtrue;

	if( !bloom.started ) {
		if ( bloom.disabled )
			return;
		R_Bloom_InitTextures();
		if( !bloom.started )
			return;
	}

	if ( !backEnd.projection2D )
		RB_SetGL2D();

	qglColor4f( 1, 1, 1, 1 );

	//Backup the old screen in a texture
	R_Bloom_BackupScreen();

	// create the bloom texture using one of a few methods
	if ( r_bloom_cascade->integer )
		R_Bloom_Cascaded();
	else
		R_Bloom_WarsowEffect();

	// restore the screen-backup to the screen
	R_Bloom_RestoreScreen();

	// Do the final pass using the bloom texture for the final effect
	R_Bloom_DrawEffect();

	if ( r_bloom_reflection->integer )
		R_Bloom_LensEffect();
}


void R_BloomInit( void )
{
	memset( &bloom, 0, sizeof( bloom ));

	r_bloom_alpha = ri.Cvar_Get( "r_bloom_alpha", "0.5", CVAR_ARCHIVE_ND );
	r_bloom_diamond_size = ri.Cvar_Get( "r_bloom_diamond_size", "8", CVAR_ARCHIVE_ND );
	r_bloom_intensity = ri.Cvar_Get( "r_bloom_intensity", "2.0", CVAR_ARCHIVE_ND );
	r_bloom_darken = ri.Cvar_Get( "r_bloom_darken", "2", CVAR_ARCHIVE_ND );
	r_bloom_sample_size = ri.Cvar_Get( "r_bloom_sample_size", "128", CVAR_ARCHIVE_ND | CVAR_LATCH );

	r_bloom_cascade = ri.Cvar_Get( "r_bloom_cascade", "0", CVAR_ARCHIVE );
	r_bloom_cascade_blur = ri.Cvar_Get( "r_bloom_cascade_blur", "0.4", CVAR_ARCHIVE_ND );
	r_bloom_cascade_intensity= ri.Cvar_Get( "r_bloom_cascade_intensity", "20", CVAR_ARCHIVE_ND );
	r_bloom_cascade_alpha = ri.Cvar_Get( "r_bloom_cascade_alpha", "0.15", CVAR_ARCHIVE_ND );
	r_bloom_cascade_dry = ri.Cvar_Get( "r_bloom_cascade_dry", "0.8", CVAR_ARCHIVE_ND );
	r_bloom_dry = ri.Cvar_Get( "r_bloom_dry", "1", CVAR_ARCHIVE_ND );
	r_bloom_reflection = ri.Cvar_Get( "r_bloom_reflection", "0", CVAR_ARCHIVE_ND );
}
