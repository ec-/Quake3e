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
// tr_font.c
// 
//
// The font system uses FreeType 2.x to render TrueType fonts for use within the game.
// As of this writing ( Nov, 2000 ) Team Arena uses these fonts for all of the ui and 
// about 90% of the cgame presentation. A few areas of the CGAME were left uses the old 
// fonts since the code is shared with standard Q3A.
//
// If you include this font rendering code in a commercial product you MUST include the
// following somewhere with your product, see www.freetype.org for specifics or changes.
// The Freetype code also uses some hinting techniques that MIGHT infringe on patents 
// held by apple so be aware of that also.
//
// As of Q3A 1.25+ and Team Arena, we are shipping the game with the font rendering code
// disabled. This removes any potential patent issues and it keeps us from having to 
// distribute an actual TrueTrype font which is 1. expensive to do and 2. seems to require
// an act of god to accomplish. 
//
// What we did was pre-render the fonts using FreeType ( which is why we leave the FreeType
// credit in the credits ) and then saved off the glyph data and then hand touched up the 
// font bitmaps so they scale a bit better in GL.
//
// There are limitations in the way fonts are saved and reloaded in that it is based on 
// point size and not name. So if you pre-render Helvetica in 18 point and Impact in 18 point
// you will end up with a single 18 point data file and image set. Typically you will want to 
// choose 3 sizes to best approximate the scaling you will be doing in the ui scripting system
// 
// In the UI Scripting code, a scale of 1.0 is equal to a 48 point font. In Team Arena, we
// use three or four scales, most of them exactly equaling the specific rendered size. We 
// rendered three sizes in Team Arena, 12, 16, and 20. 
//
// To generate new font data you need to go through the following steps.
// 1. delete the fontImage_x_xx.tga files and fontImage_xx.dat files from the fonts path.
// 2. in a ui script, specificy a font, smallFont, and bigFont keyword with font name and 
//    point size. the original TrueType fonts must exist in fonts at this point.
// 3. run the game, you should see things normally.
// 4. Exit the game and there will be three dat files and at least three tga files. The 
//    tga's are in 256x256 pages so if it takes three images to render a 24 point font you 
//    will end up with fontImage_0_24.tga through fontImage_2_24.tga
// 5. In future runs of the game, the system looks for these images and data files when a
//    specific point sized font is rendered and loads them for use. 
// 6. Because of the original beta nature of the FreeType code you will probably want to hand
//    touch the font bitmaps.
// 
// Currently a define in the project turns on or off the FreeType code which is currently 
// defined out. To pre-render new fonts you need enable the define ( BUILD_FREETYPE ) and 
// uncheck the exclude from build check box in the FreeType2 area of the Renderer project. 


#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderercommon/tr_public.h"

extern void R_IssuePendingRenderCommands( void );
extern qhandle_t RE_RegisterShaderNoMip( const char *name );

#ifdef BUILD_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ERRORS_H
#include FT_SYSTEM_H
#include FT_IMAGE_H
#include FT_OUTLINE_H

#define _FLOOR(x)  ((x) & -64)
#define _CEIL(x)   (((x)+63) & -64)
#define _TRUNC(x)  ((x) >> 6)

FT_Library ftLibrary = NULL;

/* Provided by the active renderer (GL / VK). Not available for renderer2's different R_CreateImage. */
typedef struct image_s image_t;
extern cvar_t *r_saveFontData;
image_t *R_CreateImage( const char *name, const char *name2, byte *pic, int width, int height, int flags );
qhandle_t RE_RegisterShaderFromImage( const char *name, int lightmapIndex, image_t *image, qboolean mipRawImage );
#ifndef IMGFLAG_CLAMPTOEDGE
#define IMGFLAG_CLAMPTOEDGE 0x0004
#endif
#ifndef LIGHTMAP_2D
#define LIGHTMAP_2D -4
#endif

/* 4x linear size vs original 256 — needed for high-DPI / large point FreeType bakes. */
#define FONT_ATLAS_SIZE 1024
/* 4x of Team Arena's 72 DPI — bake sharper glyphs; glyphScale compensates for TA UI. */
#define FONT_RENDER_DPI 144

/* Scratch glyph bitmap — reused across R_RenderGlyph calls during a bake. */
#define FONT_GLYPH_SCRATCH_MIN ( 64 * 64 )
static FT_Bitmap glyphScratchBitmap;
static unsigned char *glyphScratchBuffer = NULL;
static int glyphScratchCapacity = 0;

/* One loaded TTF face reused across point-size RegisterFontAtlas misses. */
typedef struct {
	char path[MAX_QPATH];
	void *faceData;
	int len;
	FT_Face face;
} fontFaceCache_t;
static fontFaceCache_t cachedFace;
#endif

#define MAX_FONTS 128
static int registeredFontCount = 0;
static int registeredFontRover = 0;
static fontInfo_t registeredFont[MAX_FONTS];

/*
==================
RE_FontCacheStore

Append or round-robin replace when the cache is full.
==================
*/
static void RE_FontCacheStore( fontInfo_t *font ) {
	int slot;

	if ( registeredFontCount < MAX_FONTS ) {
		slot = registeredFontCount++;
	} else {
		slot = registeredFontRover;
		registeredFontRover = ( registeredFontRover + 1 ) % MAX_FONTS;
	}
	Com_Memcpy( &registeredFont[slot], font, sizeof( fontInfo_t ) );
}

/*
==================
RE_FontCacheKey

Build a unique cache/dat key from font path + point size.
Falls back to legacy fonts/fontImage_<size>.dat naming when fontName is empty.
==================
*/
static void RE_FontCacheKey( const char *fontName, int pointSize, char *out, int outSize ) {
	char base[MAX_QPATH];
	const char *skip;

	if ( !fontName || !fontName[0] ) {
		Com_sprintf( out, outSize, "fonts/fontImage_%i.dat", pointSize );
		return;
	}

	skip = COM_SkipPath( (char *)fontName );
	COM_StripExtension( skip, base, sizeof( base ) );
	if ( !base[0] ) {
		Com_sprintf( out, outSize, "fonts/fontImage_%i.dat", pointSize );
		return;
	}

	Com_sprintf( out, outSize, "fonts/%s_%i.dat", base, pointSize );
}

#ifdef BUILD_FREETYPE
static void RE_FontGlyphScratchEnsure( int size ) {
	if ( size < 1 ) {
		size = 1;
	}
	if ( size <= glyphScratchCapacity && glyphScratchBuffer ) {
		return;
	}
	if ( glyphScratchBuffer ) {
		ri.Free( glyphScratchBuffer );
	}
	if ( size < FONT_GLYPH_SCRATCH_MIN ) {
		size = FONT_GLYPH_SCRATCH_MIN;
	}
	glyphScratchCapacity = size;
	glyphScratchBuffer = ri.Malloc( glyphScratchCapacity );
	glyphScratchBitmap.buffer = glyphScratchBuffer;
}

static void RE_FontGlyphScratchClear( void ) {
	if ( glyphScratchBuffer ) {
		ri.Free( glyphScratchBuffer );
		glyphScratchBuffer = NULL;
	}
	glyphScratchCapacity = 0;
	Com_Memset( &glyphScratchBitmap, 0, sizeof( glyphScratchBitmap ) );
}

static void RE_FontFaceCacheClear( void ) {
	if ( cachedFace.face ) {
		FT_Done_Face( cachedFace.face );
		cachedFace.face = NULL;
	}
	if ( cachedFace.faceData ) {
		ri.FS_FreeFile( cachedFace.faceData );
		cachedFace.faceData = NULL;
	}
	cachedFace.path[0] = '\0';
	cachedFace.len = 0;
}

/*
==================
RE_FontFaceCacheGet

Reuse FS + FT_Face for the same TTF path across point-size misses.
==================
*/
static qboolean RE_FontFaceCacheGet( const char *fontName, FT_Face *outFace ) {
	int len;
	void *faceData;
	FT_Face face;

	if ( !fontName || !fontName[0] || !outFace ) {
		return qfalse;
	}
	if ( cachedFace.face && !Q_stricmp( cachedFace.path, fontName ) ) {
		*outFace = cachedFace.face;
		return qtrue;
	}

	RE_FontFaceCacheClear();

	len = ri.FS_ReadFile( fontName, &faceData );
	if ( len <= 0 ) {
		return qfalse;
	}
	if ( FT_New_Memory_Face( ftLibrary, faceData, len, 0, &face ) ) {
		ri.FS_FreeFile( faceData );
		return qfalse;
	}

	Q_strncpyz( cachedFace.path, fontName, sizeof( cachedFace.path ) );
	cachedFace.faceData = faceData;
	cachedFace.len = len;
	cachedFace.face = face;
	*outFace = face;
	return qtrue;
}

void R_GetGlyphInfo(FT_GlyphSlot glyph, int *left, int *right, int *width, int *top, int *bottom, int *height, int *pitch) {
	*left  = _FLOOR( glyph->metrics.horiBearingX );
	*right = _CEIL( glyph->metrics.horiBearingX + glyph->metrics.width );
	*width = _TRUNC(*right - *left);

	*top    = _CEIL( glyph->metrics.horiBearingY );
	*bottom = _FLOOR( glyph->metrics.horiBearingY - glyph->metrics.height );
	*height = _TRUNC( *top - *bottom );
	*pitch  = ( qtrue ? (*width+3) & -4 : (*width+7) >> 3 );
}


FT_Bitmap *R_RenderGlyph(FT_GlyphSlot glyph, glyphInfo_t* glyphOut) {
	FT_Bitmap  *bit2;
	int left, right, width, top, bottom, height, pitch, size;

	R_GetGlyphInfo(glyph, &left, &right, &width, &top, &bottom, &height, &pitch);

	if ( glyph->format == ft_glyph_format_outline ) {
		size = pitch * height;
		RE_FontGlyphScratchEnsure( size );

		bit2 = &glyphScratchBitmap;
		bit2->width      = width;
		bit2->rows       = height;
		bit2->pitch      = pitch;
		bit2->pixel_mode = ft_pixel_mode_grays;
		bit2->buffer     = glyphScratchBuffer;
		bit2->num_grays  = 256;

		Com_Memset( bit2->buffer, 0, size );

		FT_Outline_Translate( &glyph->outline, -left, -bottom );

		FT_Outline_Get_Bitmap( ftLibrary, &glyph->outline, bit2 );

		glyphOut->height = height;
		glyphOut->pitch = pitch;
		glyphOut->top = (glyph->metrics.horiBearingY >> 6) + 1;
		glyphOut->bottom = bottom;

		return bit2;
	} else {
		ri.Printf(PRINT_ALL, "Non-outline fonts are not supported\n");
	}
	return NULL;
}

static void WriteTGA (const char *filename, byte *data, int width, int height) {
	byte			*buffer;
	int				i, c;
	int             row;
	unsigned char  *flip;
	unsigned char  *src, *dst;

	buffer = ri.Malloc(width*height*4 + 18);
	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width&255;
	buffer[13] = width>>8;
	buffer[14] = height&255;
	buffer[15] = height>>8;
	buffer[16] = 32;	// pixel size

	// swap rgb to bgr
	c = 18 + width * height * 4;
	for (i=18 ; i<c ; i+=4)
	{
		buffer[i] = data[i-18+2];		// blue
		buffer[i+1] = data[i-18+1];		// green
		buffer[i+2] = data[i-18+0];		// red
		buffer[i+3] = data[i-18+3];		// alpha
	}

	// flip upside down
	flip = (unsigned char *)ri.Malloc(width*4);
	for(row = 0; row < height/2; row++)
	{
		src = buffer + 18 + row * 4 * width;
		dst = buffer + 18 + (height - row - 1) * 4 * width;

		Com_Memcpy(flip, src, width*4);
		Com_Memcpy(src, dst, width*4);
		Com_Memcpy(dst, flip, width*4);
	}
	ri.Free(flip);

	ri.FS_WriteFile(filename, buffer, c);

	ri.Free (buffer);
}

/*
==================
RE_FontExpandPageRGBA

Convert used grayscale rows to RGBA. Unused rows stay zero (transparent).
==================
*/
static void RE_FontExpandPageRGBA( unsigned char *gray, unsigned char *rgba, int usedH ) {
	int k, usedPixels, fullPixels;
	float max, scale;

	if ( usedH < 1 ) {
		usedH = 1;
	}
	if ( usedH > FONT_ATLAS_SIZE ) {
		usedH = FONT_ATLAS_SIZE;
	}

	usedPixels = FONT_ATLAS_SIZE * usedH;
	fullPixels = FONT_ATLAS_SIZE * FONT_ATLAS_SIZE;

	max = 0;
	for ( k = 0; k < usedPixels; k++ ) {
		if ( max < gray[k] ) {
			max = gray[k];
		}
	}
	scale = ( max > 0 ) ? ( 255.0f / max ) : 0.0f;

	for ( k = 0; k < usedPixels; k++ ) {
		rgba[k * 4 + 0] = 255;
		rgba[k * 4 + 1] = 255;
		rgba[k * 4 + 2] = 255;
		rgba[k * 4 + 3] = (unsigned char)( (float)gray[k] * scale );
	}
	if ( usedPixels < fullPixels ) {
		Com_Memset( rgba + usedPixels * 4, 0, ( fullPixels - usedPixels ) * 4 );
	}
}

static glyphInfo_t *RE_ConstructGlyphInfo( unsigned char *imageOut, int *xOut, int *yOut, int *maxHeight, FT_Face face, const unsigned char c, qboolean calcHeight, int *usedBottom ) {
	int i;
	static glyphInfo_t glyph;
	unsigned char *src, *dst;
	float scaled_width, scaled_height;
	FT_Bitmap *bitmap = NULL;
	int left, right, width, top, bottom, height, pitch;
	int glyphBottom;

	Com_Memset( &glyph, 0, sizeof( glyphInfo_t ) );
	if ( face == NULL ) {
		return &glyph;
	}

	FT_Load_Glyph( face, FT_Get_Char_Index( face, c ), FT_LOAD_TARGET_NORMAL );

	/* Metrics-only pass: no outline rasterize / scratch fill. */
	if ( calcHeight ) {
		R_GetGlyphInfo( face->glyph, &left, &right, &width, &top, &bottom, &height, &pitch );
		glyph.height = height;
		glyph.pitch = pitch;
		glyph.top = ( face->glyph->metrics.horiBearingY >> 6 ) + 1;
		glyph.bottom = bottom;
		glyph.xSkip = ( face->glyph->metrics.horiAdvance >> 6 ) + 1;
		if ( glyph.height > *maxHeight ) {
			*maxHeight = glyph.height;
		}
		return &glyph;
	}

	bitmap = R_RenderGlyph( face->glyph, &glyph );
	if ( !bitmap ) {
		return &glyph;
	}
	glyph.xSkip = ( face->glyph->metrics.horiAdvance >> 6 ) + 1;

	if ( glyph.height > *maxHeight ) {
		*maxHeight = glyph.height;
	}

	scaled_width = glyph.pitch;
	scaled_height = glyph.height;

	/* we need to make sure we fit */
	if ( *xOut + scaled_width + 1 >= ( FONT_ATLAS_SIZE - 1 ) ) {
		*xOut = 0;
		*yOut += *maxHeight + 1;
	}

	if ( *yOut + *maxHeight + 1 >= ( FONT_ATLAS_SIZE - 1 ) ) {
		*yOut = -1;
		*xOut = -1;
		return &glyph;
	}

	src = bitmap->buffer;
	dst = imageOut + ( *yOut * FONT_ATLAS_SIZE ) + *xOut;

	if ( bitmap->pixel_mode == ft_pixel_mode_mono ) {
		for ( i = 0; i < glyph.height; i++ ) {
			int j;
			unsigned char *_src = src;
			unsigned char *_dst = dst;
			unsigned char mask = 0x80;
			unsigned char val = *_src;
			for ( j = 0; j < glyph.pitch; j++ ) {
				if ( mask == 0x80 ) {
					val = *_src++;
				}
				if ( val & mask ) {
					*_dst = 0xff;
				}
				mask >>= 1;

				if ( mask == 0 ) {
					mask = 0x80;
				}
				_dst++;
			}

			src += glyph.pitch;
			dst += FONT_ATLAS_SIZE;
		}
	} else {
		for ( i = 0; i < glyph.height; i++ ) {
			Com_Memcpy( dst, src, glyph.pitch );
			src += glyph.pitch;
			dst += FONT_ATLAS_SIZE;
		}
	}

	glyph.imageHeight = scaled_height;
	glyph.imageWidth = scaled_width;
	glyph.s = (float)*xOut / (float)FONT_ATLAS_SIZE;
	glyph.t = (float)*yOut / (float)FONT_ATLAS_SIZE;
	glyph.s2 = glyph.s + (float)scaled_width / (float)FONT_ATLAS_SIZE;
	glyph.t2 = glyph.t + (float)scaled_height / (float)FONT_ATLAS_SIZE;

	if ( usedBottom ) {
		glyphBottom = *yOut + (int)scaled_height;
		if ( glyphBottom > *usedBottom ) {
			*usedBottom = glyphBottom;
		}
	}

	*xOut += scaled_width + 1;

	return &glyph;
}
#endif

static int fdOffset;
static byte	*fdFile;

static int readInt( void ) {
	int i = ((unsigned int)fdFile[fdOffset] | ((unsigned int)fdFile[fdOffset+1]<<8) | ((unsigned int)fdFile[fdOffset+2]<<16) | ((unsigned int)fdFile[fdOffset+3]<<24));
	fdOffset += 4;
	return i;
}

typedef union {
	byte	fred[4];
	float	ffred;
} poor;

float readFloat( void ) {
	poor	me;
#if defined Q3_BIG_ENDIAN
	me.fred[0] = fdFile[fdOffset+3];
	me.fred[1] = fdFile[fdOffset+2];
	me.fred[2] = fdFile[fdOffset+1];
	me.fred[3] = fdFile[fdOffset+0];
#elif defined Q3_LITTLE_ENDIAN
	me.fred[0] = fdFile[fdOffset+0];
	me.fred[1] = fdFile[fdOffset+1];
	me.fred[2] = fdFile[fdOffset+2];
	me.fred[3] = fdFile[fdOffset+3];
#endif
	fdOffset += 4;
	return me.ffred;
}

void RE_RegisterFont(const char *fontName, int pointSize, fontInfo_t *font) {
#ifdef BUILD_FREETYPE
	FT_Face face;
	int j, xOut, yOut, lastStart, imageNumber;
	int maxHeight, pageUsedH;
	unsigned char *out, *imageBuff;
	glyphInfo_t *glyph = NULL;
	image_t *image;
	qhandle_t h;
	float dpi = FONT_RENDER_DPI;
	float glyphScale;
	char imgName[MAX_QPATH];
	char fontBase[MAX_QPATH];
#endif
	void *faceData;
	int i, len;
	char name[MAX_QPATH];
	char legacyName[MAX_QPATH];

	if (!font) {
		return;
	}

	Com_Memset(font, 0, sizeof(*font));

	if (!fontName) {
		ri.Printf(PRINT_ALL, "RE_RegisterFont: called with empty name\n");
		return;
	}

	if (pointSize <= 0) {
		pointSize = 12;
	}

	//R_IssuePendingRenderCommands();

	RE_FontCacheKey(fontName, pointSize, name, sizeof(name));

	/* Always search cache first — do not fail on full cache for already-known faces. */
	for (i = 0; i < registeredFontCount; i++) {
		if (Q_stricmp(name, registeredFont[i].name) == 0) {
			Com_Memcpy(font, &registeredFont[i], sizeof(fontInfo_t));
			return;
		}
	}

	/* New face: if table is full, overwrite oldest slot (glyph shaders stay alive). */
	if (registeredFontCount >= MAX_FONTS) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: cache full (%i), replacing slot %i for '%s'\n",
			MAX_FONTS, registeredFontRover, name);
	}

	len = ri.FS_ReadFile(name, NULL);
	if (len != sizeof(fontInfo_t)) {
		/* Team Arena compatibility: fonts/fontImage_<pointSize>.dat */
		Com_sprintf(legacyName, sizeof(legacyName), "fonts/fontImage_%i.dat", pointSize);
		if (Q_stricmp(name, legacyName) != 0) {
			len = ri.FS_ReadFile(legacyName, NULL);
			if (len == sizeof(fontInfo_t)) {
				Q_strncpyz(name, legacyName, sizeof(name));
			}
		}
	}

	if (len == sizeof(fontInfo_t)) {
		ri.FS_ReadFile(name, &faceData);
		fdOffset = 0;
		fdFile = faceData;
		for(i=0; i<GLYPHS_PER_FONT; i++) {
			font->glyphs[i].height		= readInt();
			font->glyphs[i].top			= readInt();
			font->glyphs[i].bottom		= readInt();
			font->glyphs[i].pitch		= readInt();
			font->glyphs[i].xSkip		= readInt();
			font->glyphs[i].imageWidth	= readInt();
			font->glyphs[i].imageHeight = readInt();
			font->glyphs[i].s			= readFloat();
			font->glyphs[i].t			= readFloat();
			font->glyphs[i].s2			= readFloat();
			font->glyphs[i].t2			= readFloat();
			font->glyphs[i].glyph		= readInt();
			Q_strncpyz(font->glyphs[i].shaderName, (const char *)&fdFile[fdOffset], sizeof(font->glyphs[i].shaderName));
			fdOffset += sizeof(font->glyphs[i].shaderName);
		}
		font->glyphScale = readFloat();
		Com_Memcpy(font->name, &fdFile[fdOffset], MAX_QPATH);

		Q_strncpyz(font->name, name, sizeof(font->name));
		for (i = GLYPH_START; i <= GLYPH_END; i++) {
			font->glyphs[i].glyph = RE_RegisterShaderNoMip(font->glyphs[i].shaderName);
		}
		RE_FontCacheStore(font);
		ri.FS_FreeFile(faceData);
		return;
	}

#ifndef BUILD_FREETYPE
	ri.Printf(PRINT_WARNING, "RE_RegisterFont: FreeType code not available\n");
#else
	if (ftLibrary == NULL) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: FreeType not initialized.\n");
		return;
	}

	len = ri.FS_ReadFile(fontName, &faceData);
	if (len <= 0) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: Unable to read font file '%s'\n", fontName);
		return;
	}

	// allocate on the stack first in case we fail
	if (FT_New_Memory_Face( ftLibrary, faceData, len, 0, &face )) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: FreeType, unable to allocate new face.\n");
		ri.FS_FreeFile(faceData);
		return;
	}


	if (FT_Set_Char_Size( face, pointSize << 6, pointSize << 6, dpi, dpi)) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: FreeType, unable to set face char size.\n");
		FT_Done_Face(face);
		ri.FS_FreeFile(faceData);
		return;
	}

	//*font = &registeredFonts[registeredFontCount++];

	/* make an atlas image buffer; when full, register it and continue until all glyphs are rendered */
	out = ri.Malloc(FONT_ATLAS_SIZE * FONT_ATLAS_SIZE);
	if (out == NULL) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: ri.Malloc failure during output image creation.\n");
		FT_Done_Face(face);
		ri.FS_FreeFile(faceData);
		return;
	}
	Com_Memset(out, 0, FONT_ATLAS_SIZE * FONT_ATLAS_SIZE);

	maxHeight = 0;
	pageUsedH = 0;

	for (i = GLYPH_START; i <= GLYPH_END; i++) {
		RE_ConstructGlyphInfo(out, &xOut, &yOut, &maxHeight, face, (unsigned char)i, qtrue, NULL);
	}

	xOut = 0;
	yOut = 0;
	i = GLYPH_START;
	lastStart = i;
	imageNumber = 0;

	COM_StripExtension( COM_SkipPath( (char *)fontName ), fontBase, sizeof( fontBase ) );
	if ( !fontBase[0] ) {
		Q_strncpyz( fontBase, "fontImage", sizeof( fontBase ) );
	}

	while ( i <= GLYPH_END + 1 ) {

		if ( i == GLYPH_END + 1 ) {
			/* upload/save current image buffer */
			xOut = yOut = -1;
		} else {
			glyph = RE_ConstructGlyphInfo(out, &xOut, &yOut, &maxHeight, face, (unsigned char)i, qfalse, &pageUsedH);
		}

		if (xOut == -1 || yOut == -1)  {
			/* ran out of room */
			imageBuff = ri.Malloc(FONT_ATLAS_SIZE * FONT_ATLAS_SIZE * 4);
			RE_FontExpandPageRGBA(out, imageBuff, pageUsedH);

			Com_sprintf (imgName, sizeof(imgName), "fonts/%s_%i_%i.tga", fontBase, imageNumber++, pointSize);
			if (r_saveFontData->integer) { 
				WriteTGA(imgName, imageBuff, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE);
			}

			image = R_CreateImage(imgName, NULL, imageBuff, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, IMGFLAG_CLAMPTOEDGE );
			h = RE_RegisterShaderFromImage(imgName, LIGHTMAP_2D, image, qfalse);
			for (j = lastStart; j < i; j++) {
				font->glyphs[j].glyph = h;
				Q_strncpyz(font->glyphs[j].shaderName, imgName, sizeof(font->glyphs[j].shaderName));
			}
			lastStart = i;
			Com_Memset(out, 0, FONT_ATLAS_SIZE * FONT_ATLAS_SIZE);
			xOut = 0;
			yOut = 0;
			pageUsedH = 0;
			ri.Free(imageBuff);
			if ( i == GLYPH_END + 1 )
				i++;
		} else {
			Com_Memcpy(&font->glyphs[i], glyph, sizeof(glyphInfo_t));
			i++;
		}
	}

	/* change the scale to be relative to 1 based on 72 dpi ( so dpi of 144 means a scale of .5 ) */
	glyphScale = 72.0f / dpi;

	/* adjust vs 48pt UI scale (Team Arena) */
	glyphScale *= 48.0f / pointSize;

	font->glyphScale = glyphScale;
	Q_strncpyz(font->name, name, sizeof(font->name));
	RE_FontCacheStore(font);

	if (r_saveFontData->integer) {
		ri.FS_WriteFile(name, font, sizeof(fontInfo_t));
	}

	ri.Free(out);
	FT_Done_Face(face);
	ri.FS_FreeFile(faceData);
#endif
}

/*--------------------------------------------------------------------------
 * Extension atlas pool: bake selected alphabet, cache by face+size+alphabet
 *--------------------------------------------------------------------------*/

#define MAX_FONT_ATLASES 64
static fontAtlasInfo_t registeredAtlas[MAX_FONT_ATLASES];
static char registeredAtlasKey[MAX_FONT_ATLASES][MAX_QPATH];
static int registeredAtlasCount = 0;
static int registeredAtlasRover = 0;

static void RE_FontBuildCharset( const char *alphabet, byte charset[256] ) {
	int i;

	Com_Memset( charset, 0, 256 );
	if ( !alphabet || !alphabet[0] ) {
		for ( i = GLYPH_CHARSTART; i < GLYPH_CHAREND; i++ ) {
			charset[i] = 1;
		}
		return;
	}
	while ( *alphabet ) {
		charset[(unsigned char)*alphabet++] = 1;
	}
}

/* Order-independent: "abc" and "cba" share one key. */
static unsigned int RE_FontCharsetHash( const byte charset[256] ) {
	unsigned int h = 5381;
	int i;

	for ( i = 0; i < 256; i++ ) {
		h = ( ( h << 5 ) + h ) + charset[i];
	}
	return h;
}

static void RE_FontAtlasCacheKey( const char *fontName, int pointSize, const byte charset[256], char *out, int outSize ) {
	char base[MAX_QPATH];
	const char *skip;
	unsigned int ah;

	ah = RE_FontCharsetHash( charset );
	if ( !fontName || !fontName[0] ) {
		Com_sprintf( out, outSize, "atlas_%i_%08x", pointSize, ah );
		return;
	}
	skip = COM_SkipPath( (char *)fontName );
	COM_StripExtension( skip, base, sizeof( base ) );
	if ( !base[0] ) {
		Q_strncpyz( base, "font", sizeof( base ) );
	}
	Com_sprintf( out, outSize, "%s_%i_%08x", base, pointSize, ah );
}

static void RE_FontAtlasCacheStore( const char *key, fontAtlasInfo_t *font ) {
	int slot;

	if ( registeredAtlasCount < MAX_FONT_ATLASES ) {
		slot = registeredAtlasCount++;
	} else {
		slot = registeredAtlasRover;
		registeredAtlasRover = ( registeredAtlasRover + 1 ) % MAX_FONT_ATLASES;
		ri.Printf( PRINT_WARNING, "RE_RegisterFontAtlas: cache full, replacing slot %i\n", slot );
	}
	Q_strncpyz( registeredAtlasKey[slot], key, sizeof( registeredAtlasKey[slot] ) );
	Com_Memcpy( &registeredAtlas[slot], font, sizeof( fontAtlasInfo_t ) );
}

/*
==================
RE_RegisterFontAtlas

Bake alphabet into atlas page(s), cache by face+size+alphabet.
Mod draws via StretchPic with glyph.glyph (page handle) + UV/spacing.
Returns qtrue on success.
==================
*/
qboolean RE_RegisterFontAtlas( const char *fontName, int pointSize, const char *alphabet, fontAtlasInfo_t *out ) {
	int i;
	char cacheKey[MAX_QPATH];
	byte charset[256];
#ifdef BUILD_FREETYPE
	FT_Face face;
	int j, xOut, yOut, lastStart, imageNumber;
	int maxHeight, pageUsedH;
	unsigned char *buf, *imageBuff;
	glyphInfo_t *glyph = NULL;
	image_t *image;
	qhandle_t h;
	float dpi = FONT_RENDER_DPI;
	char imgName[MAX_QPATH];
	char fontBase[MAX_QPATH];
	int charList[256];
	int charCount;
	int ci;
	float ascenderPx;
	float descenderPx;
	float heightPx;
#endif

	if ( !out ) {
		return qfalse;
	}
	Com_Memset( out, 0, sizeof( *out ) );

	if ( !fontName || !fontName[0] ) {
		ri.Printf( PRINT_ALL, "RE_RegisterFontAtlas: empty font name\n" );
		return qfalse;
	}
	if ( pointSize <= 0 ) {
		pointSize = 12;
	}

	RE_FontBuildCharset( alphabet, charset );
	RE_FontAtlasCacheKey( fontName, pointSize, charset, cacheKey, sizeof( cacheKey ) );

	for ( i = 0; i < registeredAtlasCount; i++ ) {
		if ( Q_stricmp( cacheKey, registeredAtlasKey[i] ) == 0 ) {
			Com_Memcpy( out, &registeredAtlas[i], sizeof( *out ) );
			return qtrue;
		}
	}

#ifndef BUILD_FREETYPE
	(void)alphabet;
	ri.Printf( PRINT_WARNING, "RE_RegisterFontAtlas: FreeType not available\n" );
	return qfalse;
#else
	if ( ftLibrary == NULL ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterFontAtlas: FreeType not initialized\n" );
		return qfalse;
	}

	charCount = 0;
	for ( i = 0; i < 256; i++ ) {
		if ( charset[i] ) {
			charList[charCount++] = i;
		}
	}
	if ( charCount <= 0 ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterFontAtlas: empty alphabet\n" );
		return qfalse;
	}

	if ( !RE_FontFaceCacheGet( fontName, &face ) ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterFontAtlas: Unable to read '%s'\n", fontName );
		return qfalse;
	}

	if ( FT_Set_Char_Size( face, pointSize << 6, pointSize << 6, dpi, dpi ) ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterFontAtlas: unable to set char size\n" );
		return qfalse;
	}

	ascenderPx = (float)( face->size->metrics.ascender >> 6 );
	descenderPx = (float)( -( face->size->metrics.descender >> 6 ) );
	heightPx = (float)( face->size->metrics.height >> 6 );
	if ( heightPx < 1.0f ) {
		heightPx = ascenderPx + descenderPx;
	}
	if ( heightPx < 1.0f ) {
		heightPx = (float)pointSize;
	}
	if ( ascenderPx < 1.0f ) {
		ascenderPx = heightPx * 0.8f;
	}

	buf = ri.Malloc( FONT_ATLAS_SIZE * FONT_ATLAS_SIZE );
	if ( !buf ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterFontAtlas: malloc failed\n" );
		return qfalse;
	}
	Com_Memset( buf, 0, FONT_ATLAS_SIZE * FONT_ATLAS_SIZE );

	maxHeight = 0;
	pageUsedH = 0;
	xOut = 0;
	yOut = 0;
	for ( ci = 0; ci < charCount; ci++ ) {
		RE_ConstructGlyphInfo( buf, &xOut, &yOut, &maxHeight, face, (unsigned char)charList[ci], qtrue, NULL );
	}
	if ( maxHeight < 1 ) {
		maxHeight = 1;
	}
	if ( heightPx < (float)maxHeight ) {
		/* Prefer actual glyph extent when FT height underestimates */
		heightPx = (float)maxHeight;
	}

	xOut = 0;
	yOut = 0;
	ci = 0;
	lastStart = 0;
	imageNumber = 0;
	pageUsedH = 0;

	COM_StripExtension( COM_SkipPath( (char *)fontName ), fontBase, sizeof( fontBase ) );
	if ( !fontBase[0] ) {
		Q_strncpyz( fontBase, "fontAtlas", sizeof( fontBase ) );
	}

	out->pointSize = pointSize;
	out->lineHeight = heightPx;
	out->ascender = ascenderPx;
	out->descender = descenderPx;
	Q_strncpyz( out->name, cacheKey, sizeof( out->name ) );

	while ( ci <= charCount ) {
		if ( ci == charCount ) {
			xOut = yOut = -1;
		} else {
			glyph = RE_ConstructGlyphInfo( buf, &xOut, &yOut, &maxHeight, face,
				(unsigned char)charList[ci], qfalse, &pageUsedH );
		}

		if ( xOut == -1 || yOut == -1 ) {
			imageBuff = ri.Malloc( FONT_ATLAS_SIZE * FONT_ATLAS_SIZE * 4 );
			RE_FontExpandPageRGBA( buf, imageBuff, pageUsedH );

			Com_sprintf( imgName, sizeof( imgName ), "*fontAtlas/%s_%i_%i", fontBase, imageNumber, pointSize );
			image = R_CreateImage( imgName, NULL, imageBuff, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, IMGFLAG_CLAMPTOEDGE );
			h = RE_RegisterShaderFromImage( imgName, LIGHTMAP_2D, image, qfalse );

			if ( out->atlasCount < FONT_ATLAS_MAX_PAGES ) {
				out->atlases[out->atlasCount++] = h;
			}

			for ( j = lastStart; j < ci; j++ ) {
				int ch = charList[j];
				out->glyphs[ch].glyph = h;
			}
			lastStart = ci;
			Com_Memset( buf, 0, FONT_ATLAS_SIZE * FONT_ATLAS_SIZE );
			xOut = 0;
			yOut = 0;
			pageUsedH = 0;
			ri.Free( imageBuff );
			imageNumber++;
			if ( ci == charCount ) {
				ci++;
			}
		} else {
			int ch = charList[ci];
			out->glyphs[ch].height = glyph->height;
			out->glyphs[ch].top = glyph->top;
			out->glyphs[ch].xSkip = glyph->xSkip;
			out->glyphs[ch].imageWidth = glyph->imageWidth;
			out->glyphs[ch].imageHeight = glyph->imageHeight;
			out->glyphs[ch].s = glyph->s;
			out->glyphs[ch].t = glyph->t;
			out->glyphs[ch].s2 = glyph->s2;
			out->glyphs[ch].t2 = glyph->t2;
			ci++;
		}
	}

	ri.Free( buf );
	/* face remains in cachedFace for next point-size bake */

	if ( out->atlasCount <= 0 ) {
		Com_Memset( out, 0, sizeof( *out ) );
		return qfalse;
	}

	RE_FontAtlasCacheStore( cacheKey, out );
	return qtrue;
#endif
}



void R_InitFreeType(void) {
#ifdef BUILD_FREETYPE
	if (FT_Init_FreeType( &ftLibrary )) {
		ri.Printf(PRINT_WARNING, "R_InitFreeType: Unable to initialize FreeType.\n");
	}
	RE_FontFaceCacheClear();
	RE_FontGlyphScratchClear();
#endif
	registeredFontCount = 0;
	registeredFontRover = 0;
	registeredAtlasCount = 0;
	registeredAtlasRover = 0;
}


void R_DoneFreeType(void) {
#ifdef BUILD_FREETYPE
	RE_FontFaceCacheClear();
	RE_FontGlyphScratchClear();
	if (ftLibrary) {
		FT_Done_FreeType( ftLibrary );
		ftLibrary = NULL;
	}
#endif
	registeredFontCount = 0;
	registeredFontRover = 0;
	registeredAtlasCount = 0;
	registeredAtlasRover = 0;
}

