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
/*
** QGL.H
*/

#ifndef __QGL_H__
#define __QGL_H__

#if defined( _WIN32 )
#if _MSC_VER
#pragma warning (disable: 4201)
#pragma warning (disable: 4214)
#pragma warning (disable: 4514)
#pragma warning (disable: 4032)
#pragma warning (disable: 4201)
#pragma warning (disable: 4214)
#endif
#include <windows.h>
#include <GL/gl.h>
#elif defined( __linux__ ) || defined(__FreeBSD__) || defined( __sun )
#include <GL/gl.h>
#include <GL/glx.h>
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

//===========================================================================
// <Timbo> I hate this section so much

/*
** multitexture extension definitions
*/
#if !defined(__sun)

#define GL_ACTIVE_TEXTURE_ARB               0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB        0x84E1
#define GL_MAX_ACTIVE_TEXTURES_ARB          0x84E2

#define GL_TEXTURE0_ARB                     0x84C0
#define GL_TEXTURE1_ARB                     0x84C1
#define GL_TEXTURE2_ARB                     0x84C2
#define GL_TEXTURE3_ARB                     0x84C3

#else

#define GL_MAX_ACTIVE_TEXTURES_ARB          0x84E2

#endif /* defined(__sun) */

// anisotropic filtering constants
#define GL_TEXTURE_MAX_ANISOTROPY_EXT       0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT   0x84FF

// define for skyboxes without black seams
#if !defined(GL_VERSION_1_2) && !defined(GL_CLAMP_TO_EDGE)
   #define GL_CLAMP_TO_EDGE                  0x812F
#endif

//WGL functions
#if defined (WIN32)

typedef void (APIENTRY * PFNGLMULTITEXCOORD2FARBPROC) (GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRY * PFNGLACTIVETEXTUREARBPROC) (GLenum target);
typedef void (APIENTRY * PFNGLCLIENTACTIVETEXTUREARBPROC) (GLenum target);

extern HGLRC ( WINAPI * qwglCreateContext)(HDC);
extern BOOL  ( WINAPI * qwglDeleteContext)(HGLRC);
extern HGLRC ( WINAPI * qwglGetCurrentContext)(VOID);
extern PROC  ( WINAPI * qwglGetProcAddress)(LPCSTR);
extern BOOL  ( WINAPI * qwglMakeCurrent)(HDC, HGLRC);
extern BOOL ( WINAPI * qwglSwapIntervalEXT)( int interval );

#endif // WIN32


//GLX Functions
#if ( (defined __linux__ )  || (defined __FreeBSD__ ) || (defined __sun) )

extern XVisualInfo * (*qglXChooseVisual)( Display *dpy, int screen, int *attribList );
extern GLXContext (*qglXCreateContext)( Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct );
extern void (*qglXDestroyContext)( Display *dpy, GLXContext ctx );
extern Bool (*qglXMakeCurrent)( Display *dpy, GLXDrawable drawable, GLXContext ctx);
extern void (*qglXCopyContext)( Display *dpy, GLXContext src, GLXContext dst, GLuint mask );
extern void (*qglXSwapBuffers)( Display *dpy, GLXDrawable drawable );

#endif // __linux__ || __FreeBSD__ || __sun


/*
** extension constants
*/
// S3TC compression constants
#define GL_RGB_S3TC							0x83A0
#define GL_RGB4_S3TC						0x83A1

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT  0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3

#ifndef GL_EXT_texture_sRGB
#define GL_EXT_texture_sRGB
#define GL_SRGB_EXT                                       0x8C40
#define GL_SRGB8_EXT                                      0x8C41
#define GL_SRGB_ALPHA_EXT                                 0x8C42
#define GL_SRGB8_ALPHA8_EXT                               0x8C43
#define GL_SLUMINANCE_ALPHA_EXT                           0x8C44
#define GL_SLUMINANCE8_ALPHA8_EXT                         0x8C45
#define GL_SLUMINANCE_EXT                                 0x8C46
#define GL_SLUMINANCE8_EXT                                0x8C47
#define GL_COMPRESSED_SRGB_EXT                            0x8C48
#define GL_COMPRESSED_SRGB_ALPHA_EXT                      0x8C49
#define GL_COMPRESSED_SLUMINANCE_EXT                      0x8C4A
#define GL_COMPRESSED_SLUMINANCE_ALPHA_EXT                0x8C4B
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT                  0x8C4C
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT            0x8C4D
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT            0x8C4E
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT            0x8C4F
#endif

#ifndef GL_EXT_framebuffer_sRGB
#define GL_EXT_framebuffer_sRGB
#define GL_FRAMEBUFFER_SRGB_EXT                         0x8DB9
#endif

#ifndef GL_EXT_texture_compression_latc
#define GL_EXT_texture_compression_latc
#define GL_COMPRESSED_LUMINANCE_LATC1_EXT                 0x8C70
#define GL_COMPRESSED_SIGNED_LUMINANCE_LATC1_EXT          0x8C71
#define GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT           0x8C72
#define GL_COMPRESSED_SIGNED_LUMINANCE_ALPHA_LATC2_EXT    0x8C73
#endif

#ifndef GL_ARB_texture_compression_bptc
#define GL_ARB_texture_compression_bptc
#define GL_COMPRESSED_RGBA_BPTC_UNORM_ARB                 0x8E8C
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB           0x8E8D
#define GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB           0x8E8E
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB         0x8E8F
#endif

// extensions will be function pointers on all platforms
extern	void ( APIENTRY * qglMultiTexCoord2fARB )( GLenum texture, GLfloat s, GLfloat t );
extern	void ( APIENTRY * qglActiveTextureARB )( GLenum texture );
extern	void ( APIENTRY * qglClientActiveTextureARB )( GLenum texture );

extern	void ( APIENTRY * qglLockArraysEXT) (GLint, GLint);
extern	void ( APIENTRY * qglUnlockArraysEXT) (void);

//===========================================================================

// non-dlopening systems will just redefine qgl* to gl*
#include "qgl_linked.h"

#endif
