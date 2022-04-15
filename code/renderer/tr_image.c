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
// tr_image.c
#include "tr_local.h"

static byte	s_intensitytable[256];
static byte	s_gammatable[256];

static byte	s_gammatable_linear[256];

GLint	gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
GLint	gl_filter_max = GL_LINEAR;

#define FILE_HASH_SIZE		1024
static	image_t*		hashTable[FILE_HASH_SIZE];

/*
================
return a hash value for the filename
================
*/
#define generateHashValue(fname) Com_GenerateHashValue((fname),FILE_HASH_SIZE)


/*
** R_GammaCorrect
*/
void R_GammaCorrect( byte *buffer, int bufSize ) {
	int i;
	if ( fboEnabled )
		return;
	if ( !gls.deviceSupportsGamma )
		return;
	for ( i = 0; i < bufSize; i++ ) {
		buffer[i] = s_gammatable[buffer[i]];
	}
}

typedef struct {
	const char *name;
	GLint minimize, maximize;
} textureMode_t;

static const textureMode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}
};

/*
===============
GL_TextureMode
===============
*/
void GL_TextureMode( const char *string ) {
	const textureMode_t *mode;
	image_t	*img;
	int		i;
	
	mode = NULL;
	for ( i = 0 ; i < ARRAY_LEN( modes ) ; i++ ) {
		if ( !Q_stricmp( modes[i].name, string ) ) {
			mode = &modes[i];
			break;
		}
	}

	if ( mode == NULL ) {
		ri.Printf( PRINT_ALL, "bad texture filter name '%s'\n", string );
		return;
	}

	gl_filter_min = mode->minimize;
	gl_filter_max = mode->maximize;

	// hack to prevent trilinear from being set on voodoo,
	// because their driver freaks...
	if ( glConfig.hardwareType == GLHW_3DFX_2D3D && gl_filter_max == GL_LINEAR &&
		gl_filter_min == GL_LINEAR_MIPMAP_LINEAR ) {
		gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
		ri.Printf( PRINT_ALL, "Refusing to set trilinear on a voodoo.\n" );
	}

	// change all the existing mipmap texture objects
	for ( i = 0; i < tr.numImages; i++ ) {
		img = tr.images[ i ];
		if ( img->flags & IMGFLAG_MIPMAP ) {
			GL_Bind( img );
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min );
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max );
		}
	}
}


/*
===============
R_SumOfUsedImages
===============
*/
int R_SumOfUsedImages( void ) {
	const image_t *img;
	int i, total = 0;

	for ( i = 0; i < tr.numImages; i++ ) {
		img = tr.images[ i ];
		if ( img->frameUsed == tr.frameCount ) {
			total += img->uploadWidth * img->uploadHeight;
		}
	}

	return total;
}


/*
===============
R_ImageList_f
===============
*/
void R_ImageList_f( void ) {
	const image_t *image;
	int i, estTotalSize = 0;
	char *name, buf[MAX_QPATH*2 + 5];

	ri.Printf( PRINT_ALL, "\n -n- --w-- --h-- type  -size- --name-------\n" );

	for ( i = 0; i < tr.numImages; i++ )
	{
		const char *format = "???? ";
		const char *sizeSuffix;
		int estSize;
		int displaySize;

		image = tr.images[ i ];
		estSize = image->uploadHeight * image->uploadWidth;

		switch ( image->internalFormat )
		{
			case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
				format = "DXT1 ";
				// 64 bits per 16 pixels, so 4 bits per pixel
				estSize /= 2;
				break;
			case GL_RGB4_S3TC:
				format = "S3TC ";
				// same as DXT1?
				estSize /= 2;
				break;
			case GL_RGBA4:
			case GL_RGBA8:
			case GL_RGBA:
				format = "RGBA ";
				// 4 bytes per pixel
				estSize *= 4;
				break;
			case GL_RGB5:
			case GL_RGB8:
			case GL_RGB:
				format = "RGB  ";
				// 3 bytes per pixel?
				estSize *= 3;
				break;
		}

		// mipmap adds about 50%
		if (image->flags & IMGFLAG_MIPMAP)
			estSize += estSize / 2;

		sizeSuffix = "b ";
		displaySize = estSize;

		if ( displaySize >= 2048 )
		{
			displaySize = ( displaySize + 1023 ) / 1024;
			sizeSuffix = "kb";
		}

		if ( displaySize >= 2048 )
		{
			displaySize = ( displaySize + 1023 ) / 1024;
			sizeSuffix = "Mb";
		}

		if ( displaySize >= 2048 )
		{
			displaySize = ( displaySize + 1023 ) / 1024;
			sizeSuffix = "Gb";
		}

		if ( Q_stricmp( image->imgName, image->imgName2 ) == 0 ) {
			name = image->imgName;
		} else {
			Com_sprintf( buf, sizeof( buf ), "%s => " S_COLOR_YELLOW "%s",
				image->imgName, image->imgName2 );
			name = buf;
		}

		ri.Printf( PRINT_ALL, " %3i %5i %5i %s %4i%s %s\n", i, image->uploadWidth, image->uploadHeight, format, displaySize, sizeSuffix, name );
		estTotalSize += estSize;
	}

	ri.Printf( PRINT_ALL, " -----------------------\n" );
	ri.Printf( PRINT_ALL, " approx %i kbytes\n", (estTotalSize + 1023) / 1024 );
	ri.Printf( PRINT_ALL, " %i total images\n\n", tr.numImages );
}

//=======================================================================

/*
================
ResampleTexture

Used to resample images in a more general than quartering fashion.

This will only be filtered properly if the resampled size
is greater than half the original size.

If a larger shrinking is needed, use the mipmap function 
before or after.
================
*/
static void ResampleTexture( unsigned *in, int inwidth, int inheight, unsigned *out,  
							int outwidth, int outheight ) {
	int		i, j;
	unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	p1[MAX_TEXTURE_SIZE];
	unsigned	p2[MAX_TEXTURE_SIZE];
	byte		*pix1, *pix2, *pix3, *pix4;

	if ( outwidth > ARRAY_LEN( p1 ) )
		ri.Error( ERR_DROP, "ResampleTexture: max width" );
								
	fracstep = inwidth * 0x10000 / outwidth;

	frac = fracstep>>2;
	for ( i=0 ; i<outwidth ; i++ ) {
		p1[i] = 4*(frac>>16);
		frac += fracstep;
	}
	frac = 3*(fracstep>>2);
	for ( i=0 ; i<outwidth ; i++ ) {
		p2[i] = 4*(frac>>16);
		frac += fracstep;
	}

	for (i=0 ; i<outheight ; i++, out += outwidth) {
		inrow = in + inwidth*(int)((i+0.25)*inheight/outheight);
		inrow2 = in + inwidth*(int)((i+0.75)*inheight/outheight);
		for (j=0 ; j<outwidth ; j++) {
			pix1 = (byte *)inrow + p1[j];
			pix2 = (byte *)inrow + p2[j];
			pix3 = (byte *)inrow2 + p1[j];
			pix4 = (byte *)inrow2 + p2[j];
			((byte *)(out+j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0])>>2;
			((byte *)(out+j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1])>>2;
			((byte *)(out+j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2])>>2;
			((byte *)(out+j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3])>>2;
		}
	}
}


/*
================
R_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
static void R_LightScaleTexture( byte *in, int inwidth, int inheight, qboolean only_gamma )
{
	if ( in == NULL )
		return;

	if ( only_gamma )
	{
		if ( !glConfig.deviceSupportsGamma && !fboEnabled )
		{
			int		i, c;
			byte	*p;

			p = (byte *)in;

			c = inwidth*inheight;
			for (i=0 ; i<c ; i++, p+=4)
			{
				p[0] = s_gammatable[p[0]];
				p[1] = s_gammatable[p[1]];
				p[2] = s_gammatable[p[2]];
			}
		}
	}
	else
	{
		int		i, c;
		byte	*p;

		p = (byte *)in;

		c = inwidth*inheight;

		if ( glConfig.deviceSupportsGamma || fboEnabled )
		{
			for (i=0 ; i<c ; i++, p+=4)
			{
				p[0] = s_intensitytable[p[0]];
				p[1] = s_intensitytable[p[1]];
				p[2] = s_intensitytable[p[2]];
			}
		}
		else
		{
			for (i=0 ; i<c ; i++, p+=4)
			{
				p[0] = s_gammatable[s_intensitytable[p[0]]];
				p[1] = s_gammatable[s_intensitytable[p[1]]];
				p[2] = s_gammatable[s_intensitytable[p[2]]];
			}
		}
	}
}


/*
================
R_MipMap2

Operates in place, quartering the size of the texture
Proper linear filter
================
*/
static void R_MipMap2( unsigned * const out, unsigned * const in, int inWidth, int inHeight ) {
	int			i, j, k;
	byte		*outpix;
	int			inWidthMask, inHeightMask;
	int			total;
	int			outWidth, outHeight;
	unsigned	*temp;

	outWidth = inWidth >> 1;
	outHeight = inHeight >> 1;

	if ( out == in )
		temp = ri.Hunk_AllocateTempMemory( outWidth * outHeight * 4 );
	else
		temp = out;

	inWidthMask = inWidth - 1;
	inHeightMask = inHeight - 1;

	for ( i = 0 ; i < outHeight ; i++ ) {
		for ( j = 0 ; j < outWidth ; j++ ) {
			outpix = (byte *) ( temp + i * outWidth + j );
			for ( k = 0 ; k < 4 ; k++ ) {
				total = 
					1 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					1 * ((byte *)&in[ ((i*2-1)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					2 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					2 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					4 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+1)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k] +

					1 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2-1)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2)&inWidthMask) ])[k] +
					2 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2+1)&inWidthMask) ])[k] +
					1 * ((byte *)&in[ ((i*2+2)&inHeightMask)*inWidth + ((j*2+2)&inWidthMask) ])[k];
				outpix[k] = total / 36;
			}
		}
	}

	if ( out == in ) {
		Com_Memcpy( out, temp, outWidth * outHeight * 4 );
		ri.Hunk_FreeTempMemory( temp );
	}
}


/*
================
R_MipMap

Operates in place, quartering the size of the texture
================
*/
static void R_MipMap( byte *out, byte *in, int width, int height ) {
	int		i, j;
	int		row;

	if ( in == NULL )
		return;

	if ( !r_simpleMipMaps->integer ) {
		R_MipMap2( (unsigned *)out, (unsigned *)in, width, height );
		return;
	}

	if ( width == 1 && height == 1 ) {
		return;
	}

	row = width * 4;
	width >>= 1;
	height >>= 1;

	if ( width == 0 || height == 0 ) {
		width += height;	// get largest
		for (i=0 ; i<width ; i++, out+=4, in+=8 ) {
			out[0] = ( in[0] + in[4] )>>1;
			out[1] = ( in[1] + in[5] )>>1;
			out[2] = ( in[2] + in[6] )>>1;
			out[3] = ( in[3] + in[7] )>>1;
		}
		return;
	}

	for (i=0 ; i<height ; i++, in+=row) {
		for (j=0 ; j<width ; j++, out+=4, in+=8) {
			out[0] = (in[0] + in[4] + in[row+0] + in[row+4])>>2;
			out[1] = (in[1] + in[5] + in[row+1] + in[row+5])>>2;
			out[2] = (in[2] + in[6] + in[row+2] + in[row+6])>>2;
			out[3] = (in[3] + in[7] + in[row+3] + in[row+7])>>2;
		}
	}
}


/*
==================
R_BlendOverTexture

Apply a color blend over a set of pixels
==================
*/
static void R_BlendOverTexture( byte *data, int pixelCount, int mipLevel ) {

	static const byte blendColors[][4] = {
		{255,0,0,128},
		{255,255,0,128},
		{0,255,0,128},
		{0,255,255,128},
		{0,0,255,128},
		{255,0,255,128}
	};

	const byte *blend;
	int		i;
	int		inverseAlpha;
	int		premult[3];

	if ( data == NULL )
		return;

	if ( mipLevel <= 0 )
		return;

	blend = blendColors[ ( mipLevel - 1 ) % ARRAY_LEN( blendColors ) ];

	inverseAlpha = 255 - blend[3];
	premult[0] = blend[0] * blend[3];
	premult[1] = blend[1] * blend[3];
	premult[2] = blend[2] * blend[3];

	for ( i = 0 ; i < pixelCount ; i++, data+=4 ) {
		data[0] = ( data[0] * inverseAlpha + premult[0] ) >> 9;
		data[1] = ( data[1] * inverseAlpha + premult[1] ) >> 9;
		data[2] = ( data[2] * inverseAlpha + premult[2] ) >> 9;
	}
}


static qboolean RawImage_HasAlpha( const byte *scan, const int numPixels )
{
	int i;

	if ( !scan )
		return qtrue;

	for ( i = 0; i < numPixels; i++ )
	{
		if ( scan[i*4 + 3] != 255 )
		{
			return qtrue;
		}
	}

	return qfalse;
}


static GLint RawImage_GetInternalFormat( const byte *scan, int numPixels, qboolean lightMap, qboolean allowCompression )
{
	GLint internalFormat;

	if ( lightMap )
		return GL_RGB;

	if ( RawImage_HasAlpha( scan, numPixels ) )
	{
		if ( r_texturebits->integer == 16 )
		{
			internalFormat = GL_RGBA4;
		}
		else if ( r_texturebits->integer == 32 )
		{
			internalFormat = GL_RGBA8;
		}
		else
		{
			internalFormat = GL_RGBA;
		}
	}
	else
	{
		if ( allowCompression && glConfig.textureCompression == TC_S3TC_ARB )
		{
			internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
		}
		else if ( allowCompression && glConfig.textureCompression == TC_S3TC )
		{
			internalFormat = GL_RGB4_S3TC;
		}
		else if ( r_texturebits->integer == 16 )
		{
			internalFormat = GL_RGB5;
		}
		else if ( r_texturebits->integer == 32 )
		{
			internalFormat = GL_RGB8;
		}
		else
		{
			internalFormat = GL_RGB;
		}
	}

	return internalFormat;
}


static void LoadTexture( int miplevel, int x, int y, int width, int height, const byte *data, qboolean subImage, image_t *image )
{
	if ( subImage )
		qglTexSubImage2D( GL_TEXTURE_2D, miplevel, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data );
	else
		qglTexImage2D( GL_TEXTURE_2D, miplevel, image->internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
}


/*
===============
Upload32
===============
*/
static void Upload32( byte *data, int x, int y, int width, int height, image_t *image, qboolean subImage )
{
	qboolean allowCompression = !(image->flags & IMGFLAG_NO_COMPRESSION);
	qboolean lightMap = image->flags & IMGFLAG_LIGHTMAP;
	qboolean mipmap = image->flags & IMGFLAG_MIPMAP;
	qboolean picmip = image->flags & IMGFLAG_PICMIP;
	byte		*resampledBuffer = NULL;
	int			scaled_width, scaled_height;

	if ( image->flags & IMGFLAG_NOSCALE ) {
		//
		// keep original dimensions
		//
		scaled_width = width;
		scaled_height = height;
	} else {
		//
		// convert to exact power of 2 sizes
		//
		for (scaled_width = 1 ; scaled_width < width ; scaled_width<<=1)
			;
		for (scaled_height = 1 ; scaled_height < height ; scaled_height<<=1)
			;

		if ( r_roundImagesDown->integer && scaled_width > width )
			scaled_width >>= 1;
		if ( r_roundImagesDown->integer && scaled_height > height )
			scaled_height >>= 1;
	}

	//
	// clamp to the current texture size limit
	// scale both axis down equally so we don't have to
	// deal with a half mip resampling
	//
	while ( scaled_width > glConfig.maxTextureSize
		|| scaled_height > glConfig.maxTextureSize ) {
		scaled_width >>= 1;
		scaled_height >>= 1;
		x >>= 1;
		y >>= 1;
	}

	if ( scaled_width != width || scaled_height != height ) {
		if ( data ) {
			resampledBuffer = ri.Hunk_AllocateTempMemory( scaled_width * scaled_height * 4 );
			ResampleTexture( (unsigned*)data, width, height, (unsigned*)resampledBuffer, scaled_width, scaled_height );
			data = resampledBuffer;
		}
		width = scaled_width;
		height = scaled_height;
	}

	if ( image->flags & IMGFLAG_COLORSHIFT ) {
		byte *p = data;
		int i, n = width * height;
		for ( i = 0; i < n; i++, p+=4 ) {
			R_ColorShiftLightingBytes( p, p, qfalse );
		}
	}

	//
	// perform optional picmip operation
	//
	if ( picmip && ( tr.mapLoading || r_nomip->integer == 0 ) ) {
		scaled_width >>= r_picmip->integer;
		scaled_height >>= r_picmip->integer;
		x >>= r_picmip->integer;
		y >>= r_picmip->integer;
	}

	//
	// clamp to minimum size
	//
	if (scaled_width < 1) {
		scaled_width = 1;
	}
	if (scaled_height < 1) {
		scaled_height = 1;
	}

	if ( !subImage ) {
		// verify if the alpha channel is being used or not
		if ( image->internalFormat == 0 ) {
			image->internalFormat = RawImage_GetInternalFormat( data, width*height, lightMap, allowCompression );
		}
		image->uploadWidth = scaled_width;
		image->uploadHeight = scaled_height;
	}

	// copy or resample data as appropriate for first MIP level
	if ( ( scaled_width == width ) && ( scaled_height == height ) )
	{
		if ( !mipmap )
		{
			LoadTexture( 0, x, y, scaled_width, scaled_height, data, subImage, image );
			goto done;
		}
	}
	else
	{
		// use the normal mip-mapping function to go down from here
		while ( width > scaled_width || height > scaled_height ) {
			R_MipMap( data, data, width, height );
			width = MAX( 1, width >> 1 );
			height = MAX( 1, height >> 1 );
		}
	}

	if ( !(image->flags & IMGFLAG_NOLIGHTSCALE) )
		R_LightScaleTexture( data, scaled_width, scaled_height, !mipmap );

	LoadTexture( 0, x, y, scaled_width, scaled_height, data, subImage, image );

	if ( mipmap )
	{
		int	miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			R_MipMap( data, data, scaled_width, scaled_height );
			scaled_width = MAX( 1, scaled_width >> 1 );
			scaled_height = MAX( 1, scaled_height >> 1 );
			x >>= 1;
			y >>= 1;
			miplevel++;

			if ( r_colorMipLevels->integer ) {
				R_BlendOverTexture( data, scaled_width * scaled_height, miplevel );
			}

			LoadTexture( miplevel, x, y, scaled_width, scaled_height, data, subImage, image );
		}
	}
done:
	if ( resampledBuffer != NULL )
		ri.Hunk_FreeTempMemory( resampledBuffer );

	GL_CheckErrors();
}


/*
================
R_UploadSubImage
================
*/
void R_UploadSubImage( byte *data, int x, int y, int width, int height, image_t *image )
{
	if ( image )
	{
		GL_Bind( image );
		Upload32( data, x, y, width, height, image, qtrue ); // subImage = qtrue
	}
}


/*
================
R_CreateImage

This is the only way any image_t are created
Picture data may be modified in-place during mipmap processing
================
*/
image_t *R_CreateImage( const char *name, const char *name2, byte *pic, int width, int height, imgFlags_t flags ) {
	image_t		*image;
	long		hash;
	GLint		glWrapClampMode;
	GLuint		currTexture;
	int			currTMU;
	int			namelen, namelen2;
	const char	*slash;

	namelen = (int)strlen( name ) + 1;
	if ( namelen > MAX_QPATH ) {
		ri.Error( ERR_DROP, "R_CreateImage: \"%s\" is too long", name );
	}

	if ( name2 && Q_stricmp( name, name2 ) != 0 ) {
		// leave only file name
		name2 = ( slash = strrchr( name2, '/' ) ) != NULL ? slash + 1 : name2;
		namelen2 = (int)strlen( name2 ) + 1;
	} else {
		namelen2 = 0;
	}

	if ( tr.numImages == MAX_DRAWIMAGES ) {
		ri.Error( ERR_DROP, "R_CreateImage: MAX_DRAWIMAGES hit" );
	}

	image = ri.Hunk_Alloc( sizeof( *image ) + namelen + namelen2, h_low );
	image->imgName = (char *)( image + 1 );
	strcpy( image->imgName, name );
	if ( namelen2 ) {
		image->imgName2 = image->imgName + namelen;
		strcpy( image->imgName2, name2 );
	} else {
		image->imgName2 = image->imgName; 
	}

	hash = generateHashValue( name );
	image->next = hashTable[ hash ];
	hashTable[ hash ] = image;

	tr.images[ tr.numImages++ ] = image;

	image->flags = flags;
	image->width = width;
	image->height = height;

	if ( namelen > 6 && Q_stristr( image->imgName, "maps/" ) == image->imgName && Q_stristr( image->imgName + 6, "/lm_" ) != NULL ) {
		// external lightmap atlases stored in maps/<mapname>/lm_XXXX textures
		//image->flags = IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOSCALE | IMGFLAG_COLORSHIFT;
		image->flags |= IMGFLAG_NO_COMPRESSION | IMGFLAG_NOSCALE;
	}

	if ( flags & IMGFLAG_RGB )
		image->internalFormat = GL_RGB;
	else
		image->internalFormat = 0; // autodetect

	if ( flags & IMGFLAG_CLAMPTOBORDER )
		glWrapClampMode = GL_CLAMP_TO_BORDER;
	else if ( flags & IMGFLAG_CLAMPTOEDGE )
		glWrapClampMode = gl_clamp_mode;
	else
		glWrapClampMode = GL_REPEAT;

	// save current state
	currTMU = glState.currenttmu;
	currTexture = glState.currenttextures[ glState.currenttmu ];

	qglGenTextures( 1, &image->texnum );

	// lightmaps are always allocated on TMU 1
	if ( qglActiveTextureARB && (flags & IMGFLAG_LIGHTMAP) ) {
		image->TMU = 1;
	} else {
		image->TMU = 0;
	}

	if ( qglActiveTextureARB ) {
		GL_SelectTexture( image->TMU );
	}

	GL_Bind( image );
	Upload32( pic, 0, 0, image->width, image->height, image, qfalse ); // subImage = qfalse

	if ( image->flags & IMGFLAG_MIPMAP )
	{
		if ( textureFilterAnisotropic ) {
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, (GLint) maxAnisotropy );
		}

		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max );
	}
	else
	{
		if ( textureFilterAnisotropic )
			qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );

		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}

	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrapClampMode );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrapClampMode );

	// restore original state
	GL_SelectTexture( currTMU );
	glState.currenttextures[ glState.currenttmu ] = currTexture;
	qglBindTexture( GL_TEXTURE_2D, currTexture );

	return image;
}

//===================================================================

typedef struct
{
	const char *ext;
	void (*ImageLoader)( const char *, unsigned char **, int *, int * );
} imageExtToLoaderMap_t;

// Note that the ordering indicates the order of preference used
// when there are multiple images of different formats available
static const imageExtToLoaderMap_t imageLoaders[] =
{
	{ "png",  R_LoadPNG },
	{ "tga",  R_LoadTGA },
	{ "jpg",  R_LoadJPG },
	{ "jpeg", R_LoadJPG },
	{ "pcx",  R_LoadPCX },
	{ "bmp",  R_LoadBMP }
};

static const int numImageLoaders = ARRAY_LEN( imageLoaders );

/*
=================
R_LoadImage

Loads any of the supported image types into a cannonical
32 bit format.
=================
*/
static const char *R_LoadImage( const char *name, byte **pic, int *width, int *height )
{
	static char localName[ MAX_QPATH ];
	const char *altName, *ext;
	//qboolean orgNameFailed = qfalse;
	int orgLoader = -1;
	int i;

	*pic = NULL;
	*width = 0;
	*height = 0;

	Q_strncpyz( localName, name, sizeof( localName ) );

	ext = COM_GetExtension( localName );
	if ( *ext )
	{
		// Look for the correct loader and use it
		for ( i = 0; i < numImageLoaders; i++ )
		{
			if ( !Q_stricmp( ext, imageLoaders[ i ].ext ) )
			{
				// Load
				imageLoaders[ i ].ImageLoader( localName, pic, width, height );
				break;
			}
		}

		// A loader was found
		if ( i < numImageLoaders )
		{
			if ( *pic == NULL )
			{
				// Loader failed, most likely because the file isn't there;
				// try again without the extension
				//orgNameFailed = qtrue;
				orgLoader = i;
				COM_StripExtension( name, localName, MAX_QPATH );
			}
			else
			{
				// Something loaded
				return localName;
			}
		}
	}

	// Try and find a suitable match using all
	// the image formats supported
	for ( i = 0; i < numImageLoaders; i++ )
	{
		if ( i == orgLoader )
			continue;

		altName = va( "%s.%s", localName, imageLoaders[ i ].ext );

		// Load
		imageLoaders[ i ].ImageLoader( altName, pic, width, height );

		if ( *pic )
		{
#if 0
			if ( orgNameFailed )
			{
				ri.Printf( PRINT_DEVELOPER, S_COLOR_YELLOW "WARNING: %s not present, using %s instead\n",
						name, altName );
			}
#endif
			Q_strncpyz( localName, altName, sizeof( localName ) );
			break;
		}
	}

	return localName;
}


/*
===============
R_FindImageFile

Finds or loads the given image.
Returns NULL if it fails, not a default image.
==============
*/
image_t	*R_FindImageFile( const char *name, imgFlags_t flags )
{
	image_t	*image;
	const char *localName;
	char	strippedName[ MAX_QPATH ];
	int		width, height;
	byte	*pic;
	int		hash;

	if ( !name ) {
		return NULL;
	}

	hash = generateHashValue( name );

	//
	// see if the image is already loaded
	//
	for ( image = hashTable[ hash ]; image; image = image->next ) {
		if ( !Q_stricmp( name, image->imgName ) ) {
			// the white image can be used with any set of parms, but other mismatches are errors
			if ( strcmp( name, "*white" ) ) {
				if ( image->flags != flags ) {
					ri.Printf( PRINT_DEVELOPER, "WARNING: reused image %s with mixed flags (%i vs %i)\n", name, image->flags, flags );
				}
			}
			return image;
		}
	}

	if ( strrchr( name, '.' ) > name ) {
		// try with stripped extension
		COM_StripExtension( name, strippedName, sizeof( strippedName ) );
		for ( image = hashTable[ hash ]; image; image = image->next ) {
			if ( !Q_stricmp( strippedName, image->imgName ) ) {
				//if ( strcmp( strippedName, "*white" ) ) {
					if ( image->flags != flags ) {
						ri.Printf( PRINT_DEVELOPER, "WARNING: reused image %s with mixed flags (%i vs %i)\n", strippedName, image->flags, flags );
					}
				//}
				return image;
			}
		}
	}

	//
	// load the pic from disk
	//
	localName = R_LoadImage( name, &pic, &width, &height );
	if ( pic == NULL ) {
		return NULL;
	}

	if ( tr.mapLoading && r_mapGreyScale->value > 0 ) {
		byte *img;
		int i;
		for ( i = 0, img = pic; i < width * height; i++, img += 4 ) {
			if ( r_mapGreyScale->integer ) {
				byte luma = LUMA( img[0], img[1], img[2] );
				img[0] = luma;
				img[1] = luma;
				img[2] = luma;
			} else {
				float luma = LUMA( img[0], img[1], img[2] );
				img[0] = LERP( img[0], luma, r_mapGreyScale->value );
				img[1] = LERP( img[1], luma, r_mapGreyScale->value );
				img[2] = LERP( img[2], luma, r_mapGreyScale->value );
			}
		}
	}

	image = R_CreateImage( name, localName, pic, width, height, flags );
	ri.Free( pic );
	return image;
}


/*
================
R_CreateDlightImage
================
*/
#define	DLIGHT_SIZE	16
static void R_CreateDlightImage( void ) {
	int		x,y;
	byte	data[DLIGHT_SIZE][DLIGHT_SIZE][4];
	int		b;

	// make a centered inverse-square falloff blob for dynamic lighting
	for (x=0 ; x<DLIGHT_SIZE ; x++) {
		for (y=0 ; y<DLIGHT_SIZE ; y++) {
			float	d;

			d = ( DLIGHT_SIZE/2 - 0.5f - x ) * ( DLIGHT_SIZE/2 - 0.5f - x ) +
				( DLIGHT_SIZE/2 - 0.5f - y ) * ( DLIGHT_SIZE/2 - 0.5f - y );
			b = 4000 / d;
			if (b > 255) {
				b = 255;
			} else if ( b < 75 ) {
				b = 0;
			}
			data[y][x][0] = 
			data[y][x][1] = 
			data[y][x][2] = b;
			data[y][x][3] = 255;
		}
	}
	tr.dlightImage = R_CreateImage( "*dlight", NULL, (byte*)data, DLIGHT_SIZE, DLIGHT_SIZE, IMGFLAG_CLAMPTOEDGE );
}


/*
=================
R_InitFogTable
=================
*/
void R_InitFogTable( void ) {
	int		i;
	float	d;
	float	exp;

	exp = 0.5;

	for ( i = 0 ; i < FOG_TABLE_SIZE ; i++ ) {
		d = powf( (float)i/(FOG_TABLE_SIZE-1), exp );

		tr.fogTable[i] = d;
	}
}


/*
================
R_FogFactor

Returns a 0.0 to 1.0 fog density value
This is called for each texel of the fog texture on startup
and for each vertex of transparent shaders in fog dynamically
================
*/
float R_FogFactor( float s, float t ) {
	float	d;

	s -= 1.0/512;
	if ( s < 0 ) {
		return 0;
	}
	if ( t < 1.0/32 ) {
		return 0;
	}
	if ( t < 31.0/32 ) {
		s *= (t - 1.0f/32.0f) / (30.0f/32.0f);
	}

	// we need to leave a lot of clamp range
	s *= 8;

	if ( s > 1.0 ) {
		s = 1.0;
	}

	d = tr.fogTable[ (uint32_t)(s * (FOG_TABLE_SIZE-1)) ];

	return d;
}


/*
================
R_CreateFogImage
================
*/
#define	FOG_S	256
#define	FOG_T	32
static void R_CreateFogImage( void ) {
	int		x,y;
	byte	*data;
	float	d;

	data = ri.Hunk_AllocateTempMemory( FOG_S * FOG_T * 4 );

	// S is distance, T is depth
	for (x=0 ; x<FOG_S ; x++) {
		for (y=0 ; y<FOG_T ; y++) {
			d = R_FogFactor( ( x + 0.5f ) / FOG_S, ( y + 0.5f ) / FOG_T );

			data[(y*FOG_S+x)*4+0] = 
			data[(y*FOG_S+x)*4+1] = 
			data[(y*FOG_S+x)*4+2] = 255;
			data[(y*FOG_S+x)*4+3] = 255*d;
		}
	}
	tr.fogImage = R_CreateImage( "*fog", NULL, data, FOG_S, FOG_T, IMGFLAG_CLAMPTOEDGE );
	ri.Hunk_FreeTempMemory( data );
}


static int Hex( char c )
{
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'A' && c <= 'F' ) {
		return 10 + c - 'A';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return 10 + c - 'a';
	}

	return -1;
}


/*
==================
R_BuildDefaultImage

Create solid color texture from following input formats (hex):
#rgb
#rrggbb
==================
*/
#define	DEFAULT_SIZE 16
static qboolean R_BuildDefaultImage( const char *format ) {
	byte data[DEFAULT_SIZE][DEFAULT_SIZE][4];
	byte color[4];
	int i, len, hex[6];
	int x, y;

	if ( *format++ != '#' ) {
		return qfalse;
	}

	len = (int)strlen( format );
	if ( len <= 0 || len > 6 ) {
		return qfalse;
	}

	for ( i = 0; i < len; i++ ) {
		hex[i] = Hex( format[i] );
		if ( hex[i] == -1 ) {
			return qfalse;
		}
	}

	switch ( len ) {
		case 3: // #rgb
			color[0] = hex[0] << 4 | hex[0];
			color[1] = hex[1] << 4 | hex[1];
			color[2] = hex[2] << 4 | hex[2];
			color[3] = 255;
			break;
		case 6: // #rrggbb
			color[0] = hex[0] << 4 | hex[1];
			color[1] = hex[2] << 4 | hex[3];
			color[2] = hex[4] << 4 | hex[5];
			color[3] = 255;
			break;
		default: // unsupported format
			return qfalse;
	}

	for ( y = 0; y < DEFAULT_SIZE; y++ ) {
		for ( x = 0; x < DEFAULT_SIZE; x++ ) {
			data[x][y][0] = color[0];
			data[x][y][1] = color[1];
			data[x][y][2] = color[2];
			data[x][y][3] = color[3];
		}
	}

	tr.defaultImage = R_CreateImage( "*default", NULL, (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, IMGFLAG_MIPMAP );

	return qtrue;
}


/*
==================
R_CreateDefaultImage
==================
*/
static void R_CreateDefaultImage( void ) {
	int		x;
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	if ( r_defaultImage->string[0] )
	{
		// build from format
		if ( R_BuildDefaultImage( r_defaultImage->string ) )
			return;
		// load from external file
		tr.defaultImage = R_FindImageFile( r_defaultImage->string, IMGFLAG_MIPMAP | IMGFLAG_PICMIP );
		if ( tr.defaultImage )
			return;
	}

	// the default image will be a box, to allow you to see the mapping coordinates
	Com_Memset( data, 32, sizeof( data ) );
	for ( x = 0 ; x < DEFAULT_SIZE ; x++ ) {
		data[0][x][0] =
		data[0][x][1] =
		data[0][x][2] =
		data[0][x][3] = 255;

		data[x][0][0] =
		data[x][0][1] =
		data[x][0][2] =
		data[x][0][3] = 255;

		data[DEFAULT_SIZE-1][x][0] =
		data[DEFAULT_SIZE-1][x][1] =
		data[DEFAULT_SIZE-1][x][2] =
		data[DEFAULT_SIZE-1][x][3] = 255;

		data[x][DEFAULT_SIZE-1][0] =
		data[x][DEFAULT_SIZE-1][1] =
		data[x][DEFAULT_SIZE-1][2] =
		data[x][DEFAULT_SIZE-1][3] = 255;
	}

	tr.defaultImage = R_CreateImage( "*default", NULL, (byte *)data, DEFAULT_SIZE, DEFAULT_SIZE, IMGFLAG_MIPMAP );
}


/*
==================
R_CreateBuiltinImages
==================
*/
static void R_CreateBuiltinImages( void ) {
	int		x,y;
	byte	data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	R_CreateDefaultImage();

	// we use a solid white image instead of disabling texturing
	Com_Memset( data, 255, sizeof( data ) );
	tr.whiteImage = R_CreateImage( "*white", NULL, (byte *)data, 8, 8, IMGFLAG_NONE );

	// with overbright bits active, we need an image which is some fraction of full color,
	// for default lightmaps, etc
	for (x=0 ; x<DEFAULT_SIZE ; x++) {
		for (y=0 ; y<DEFAULT_SIZE ; y++) {
			data[y][x][0] = 
			data[y][x][1] = 
			data[y][x][2] = tr.identityLightByte;
			data[y][x][3] = 255;
		}
	}

	tr.identityLightImage = R_CreateImage( "*identityLight", NULL, (byte *)data, 8, 8, IMGFLAG_NONE );

	//for ( x = 0; x < ARRAY_LEN( tr.scratchImage ); x++ ) {
		// scratchimage is usually used for cinematic drawing
		//tr.scratchImage[x] = R_CreateImage( "*scratch", NULL, DEFAULT_SIZE, DEFAULT_SIZE,
		//	IMGFLAG_PICMIP | IMGFLAG_CLAMPTOEDGE | IMGFLAG_RGB );
	//}

	R_CreateDlightImage();
	R_CreateFogImage();
}


/*
===============
R_SetColorMappings
===============
*/
void R_SetColorMappings( void ) {
	int		i, j;
	float	g;
	int		inf;
	int		shift;
	qboolean applyGamma;

	if ( !tr.inited ) {
		// it may be called from window handling functions where gamma flags is now yet known/set
		return;
	}

	// setup the overbright lighting
	// negative value will force gamma in windowed mode
	tr.overbrightBits = abs( r_overBrightBits->integer );
	if ( !glConfig.deviceSupportsGamma && !fboEnabled )
		tr.overbrightBits = 0;		// need hardware gamma for overbright

	// never overbright in windowed mode
	if ( !glConfig.isFullscreen && r_overBrightBits->integer >= 0 && !fboEnabled ) {
		tr.overbrightBits = 0;
		applyGamma = qfalse;
	} else {
		applyGamma = qtrue;
	}

	// allow 2 overbright bits in 24 bit, but only 1 in 16 bit
	if ( glConfig.colorBits > 16 ) {
		if ( tr.overbrightBits > 2 ) {
			tr.overbrightBits = 2;
		}
	} else {
		if ( tr.overbrightBits > 1 ) {
			tr.overbrightBits = 1;
		}
	}
	if ( tr.overbrightBits < 0 ) {
		tr.overbrightBits = 0;
	}

	tr.identityLight = 1.0f / ( 1 << tr.overbrightBits );
	tr.identityLightByte = 255 * tr.identityLight;

	g = r_gamma->value;

	shift = tr.overbrightBits;

	for ( i = 0; i < ARRAY_LEN( s_gammatable ); i++ ) {
		if ( g == 1.0f ) {
			inf = i;
		} else {
			inf = 255 * powf( i/255.0f, 1.0f / g ) + 0.5f;
		}
		inf <<= shift;
		if (inf < 0) {
			inf = 0;
		}
		if (inf > 255) {
			inf = 255;
		}
		s_gammatable[i] = inf;
	}

	for ( i = 0; i < ARRAY_LEN( s_intensitytable ); i++ ) {
		j = i * r_intensity->value;
		if ( j > 255 ) {
			j = 255;
		}
		s_intensitytable[i] = j;
	}

	if ( gls.deviceSupportsGamma ) {
		if ( fboEnabled )
			ri.GLimp_SetGamma( s_gammatable_linear, s_gammatable_linear, s_gammatable_linear );
		else {
			if ( applyGamma ) {
				ri.GLimp_SetGamma( s_gammatable, s_gammatable, s_gammatable );
			}
		}
	}
}


/*
===============
R_InitImages
===============
*/
void R_InitImages( void ) {

	// initialize linear gamma table before setting color mappings for the first time
	int i;

	for ( i = 0; i < 256; i++ )
		s_gammatable_linear[i] = (unsigned char)i;

	Com_Memset( hashTable, 0, sizeof( hashTable ) );

	// build brightness translation tables
	R_SetColorMappings();

	// create default texture and white texture
	R_CreateBuiltinImages();
}


/*
===============
R_DeleteTextures
===============
*/
void R_DeleteTextures( void ) {
	image_t *img;
	int i;

	for ( i = 0; i < tr.numImages; i++ ) {
		img = tr.images[ i ];
		qglDeleteTextures( 1, &img->texnum );
	}

	if ( qglActiveTextureARB ) {
		for ( i = glConfig.numTextureUnits - 1; i >= 0; i-- ) {
			qglActiveTextureARB( GL_TEXTURE0_ARB + i );
			qglBindTexture( GL_TEXTURE_2D, 0 );
		}
	} else {
		qglBindTexture( GL_TEXTURE_2D, 0 );
	}

	Com_Memset( tr.images, 0, sizeof( tr.images ) );
	Com_Memset( tr.scratchImage, 0, sizeof( tr.scratchImage ) );
	tr.numImages = 0;

	Com_Memset( glState.currenttextures, 0, sizeof( glState.currenttextures ) );
}


/*
============================================================================

SKINS

============================================================================
*/

/*
==================
CommaParse

This is unfortunate, but the skin files aren't
compatable with our normal parsing rules.
==================
*/
static char *CommaParse( const char **data_p ) {
	int c, len;
	const char *data;
	static char com_token[ MAX_TOKEN_CHARS ];

	data = *data_p;
	com_token[0] = '\0';

	// make sure incoming data is valid
	if ( !data ) {
		*data_p = NULL;
		return com_token;
	}

	len = 0;

	while ( 1 ) {
		// skip whitespace
		while ( (c = *data) <= ' ' ) {
			if ( c == '\0' ) {
				break;
			}
			data++;
		}

		c = *data;

		// skip double slash comments
		if ( c == '/' && data[1] == '/' )
		{
			data += 2;
			while ( *data && *data != '\n' ) {
				data++;
			}
		}
		// skip /* */ comments
		else if ( c == '/' && data[1] == '*' ) 
		{
			data += 2;
			while ( *data && ( *data != '*' || data[1] != '/' ) ) 
			{
				data++;
			}
			if ( *data ) 
			{
				data += 2;
			}
		}
		else
		{
			break;
		}
	}

	if ( c == '\0' ) {
		return "";
	}

	// handle quoted strings
	if ( c == '\"' )
	{
		data++;
		while (1)
		{
			c = *data;
			if ( c == '\"' || c == '\0' )
			{
				if ( c == '\"' )
					data++;
				com_token[ len ] = '\0';
				*data_p = data;
				return com_token;
			}
			data++;
			if ( len < MAX_TOKEN_CHARS-1 )
			{
				com_token[ len ] = c;
				len++;
			}
		}
	}

	// parse a regular word
	do
	{
		if ( len < MAX_TOKEN_CHARS-1 )
		{
			com_token[ len ] = c;
			len++;
		}
		data++;
		c = *data;
	} while ( c > ' ' && c != ',' );

	com_token[ len ] = '\0';

	*data_p = data;
	return com_token;
}


/*
===============
RE_RegisterSkin
===============
*/
qhandle_t RE_RegisterSkin( const char *name ) {
	skinSurface_t parseSurfaces[MAX_SKIN_SURFACES];
	qhandle_t	hSkin;
	skin_t		*skin;
	skinSurface_t	*surf;
	union {
		char *c;
		void *v;
	} text;
	const char	*text_p;
	const char	*token;
	char		surfName[MAX_QPATH];
	int			totalSurfaces;

	if ( !name || !name[0] ) {
		ri.Printf( PRINT_DEVELOPER, "Empty name passed to RE_RegisterSkin\n" );
		return 0;
	}

	if ( strlen( name ) >= MAX_QPATH ) {
		ri.Printf( PRINT_DEVELOPER, "Skin name exceeds MAX_QPATH\n" );
		return 0;
	}


	// see if the skin is already loaded
	for ( hSkin = 1; hSkin < tr.numSkins ; hSkin++ ) {
		skin = tr.skins[hSkin];
		if ( !Q_stricmp( skin->name, name ) ) {
			if( skin->numSurfaces == 0 ) {
				return 0;		// default skin
			}
			return hSkin;
		}
	}

	// allocate a new skin
	if ( tr.numSkins == MAX_SKINS ) {
		ri.Printf( PRINT_WARNING, "WARNING: RE_RegisterSkin( '%s' ) MAX_SKINS hit\n", name );
		return 0;
	}
	tr.numSkins++;
	skin = ri.Hunk_Alloc( sizeof( skin_t ), h_low );
	tr.skins[hSkin] = skin;
	Q_strncpyz( skin->name, name, sizeof( skin->name ) );
	skin->numSurfaces = 0;

	//R_IssuePendingRenderCommands();

	// If not a .skin file, load as a single shader
	if ( strcmp( name + strlen( name ) - 5, ".skin" ) ) {
		skin->numSurfaces = 1;
		skin->surfaces = ri.Hunk_Alloc( sizeof( skinSurface_t ), h_low );
		skin->surfaces[0].shader = R_FindShader( name, LIGHTMAP_NONE, qtrue );
		return hSkin;
	}

	// load and parse the skin file
	ri.FS_ReadFile( name, &text.v );
	if ( !text.c ) {
		return 0;
	}

	totalSurfaces = 0;
	text_p = text.c;
	while ( text_p && *text_p ) {
		// get surface name
		token = CommaParse( &text_p );
		Q_strncpyz( surfName, token, sizeof( surfName ) );

		if ( !token[0] ) {
			break;
		}
		// lowercase the surface name so skin compares are faster
		Q_strlwr( surfName );

		if ( *text_p == ',' ) {
			text_p++;
		}

		if ( strstr( token, "tag_" ) ) {
			continue;
		}

		// parse the shader name
		token = CommaParse( &text_p );

		if ( skin->numSurfaces < MAX_SKIN_SURFACES ) {
			surf = &parseSurfaces[skin->numSurfaces];
			Q_strncpyz( surf->name, surfName, sizeof( surf->name ) );
			surf->shader = R_FindShader( token, LIGHTMAP_NONE, qtrue );
			skin->numSurfaces++;
		}

		totalSurfaces++;
	}

	ri.FS_FreeFile( text.v );

	if ( totalSurfaces > MAX_SKIN_SURFACES ) {
		ri.Printf( PRINT_WARNING, "WARNING: Ignoring excess surfaces (found %d, max is %d) in skin '%s'!\n",
					totalSurfaces, MAX_SKIN_SURFACES, name );
	}

	// never let a skin have 0 shaders
	if ( skin->numSurfaces == 0 ) {
		return 0;		// use default skin
	}

	// copy surfaces to skin
	skin->surfaces = ri.Hunk_Alloc( skin->numSurfaces * sizeof( skinSurface_t ), h_low );
	memcpy( skin->surfaces, parseSurfaces, skin->numSurfaces * sizeof( skinSurface_t ) );

	return hSkin;
}


/*
===============
R_InitSkins
===============
*/
void	R_InitSkins( void ) {
	skin_t		*skin;

	tr.numSkins = 1;

	// make the default skin have all default shaders
	skin = tr.skins[0] = ri.Hunk_Alloc( sizeof( skin_t ), h_low );
	Q_strncpyz( skin->name, "<default skin>", sizeof( skin->name )  );
	skin->numSurfaces = 1;
	skin->surfaces = ri.Hunk_Alloc( sizeof( skinSurface_t ), h_low );
	skin->surfaces[0].shader = tr.defaultShader;
}


/*
===============
R_GetSkinByHandle
===============
*/
skin_t	*R_GetSkinByHandle( qhandle_t hSkin ) {
	if ( hSkin < 1 || hSkin >= tr.numSkins ) {
		return tr.skins[0];
	}
	return tr.skins[ hSkin ];
}


/*
===============
R_SkinList_f
===============
*/
void	R_SkinList_f( void ) {
	int			i, j;
	skin_t		*skin;

	ri.Printf (PRINT_ALL, "------------------\n");

	for ( i = 0 ; i < tr.numSkins ; i++ ) {
		skin = tr.skins[i];

		ri.Printf( PRINT_ALL, "%3i:%s (%d surfaces)\n", i, skin->name, skin->numSurfaces );
		for ( j = 0 ; j < skin->numSurfaces ; j++ ) {
			ri.Printf( PRINT_ALL, "       %s = %s\n", 
				skin->surfaces[j].name, skin->surfaces[j].shader->name );
		}
	}
	ri.Printf (PRINT_ALL, "------------------\n");
}
