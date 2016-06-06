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

#if defined( __LINT__ )

#include <GL/gl.h>

#elif defined( _WIN32 )

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

#elif defined(MACOS_X)

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#ifndef GL_EXT_abgr
#include <OpenGL/glext.h>
#endif

// This can be defined to use the CGLMacro.h support which avoids looking up
// the current context.
//#define USE_CGLMACROS

#ifdef USE_CGLMACROS
#include "macosx_local.h"
#define cgl_ctx glw_state._cgl_ctx
#include <OpenGL/CGLMacro.h>
#endif

#elif defined( __linux__ ) || defined(__FreeBSD__)

#include <GL/gl.h>
#include <GL/glx.h>
// bk001129 - from cvs1.17 (mkv)
#if defined(__FX__)
#include <GL/fxmesa.h>
#endif

#elif defined( __sun )
#include <GL/gl.h>
#include <GL/glx.h>

#else

#include <gl.h>

#endif

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef WINAPI
#define WINAPI
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

//===========================================================================

// NOTE: some Linux platforms would need those prototypes
#if defined(MACOS_X) || ( defined(__sun) && defined(__sparc) )
typedef void (APIENTRY * PFNGLMULTITEXCOORD1DARBPROC) (GLenum target, GLdouble s);
typedef void (APIENTRY * PFNGLMULTITEXCOORD1DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD1FARBPROC) (GLenum target, GLfloat s);
typedef void (APIENTRY * PFNGLMULTITEXCOORD1FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD1IARBPROC) (GLenum target, GLint s);
typedef void (APIENTRY * PFNGLMULTITEXCOORD1IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD1SARBPROC) (GLenum target, GLshort s);
typedef void (APIENTRY * PFNGLMULTITEXCOORD1SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD2DARBPROC) (GLenum target, GLdouble s, GLdouble t);
typedef void (APIENTRY * PFNGLMULTITEXCOORD2DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD2FARBPROC) (GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRY * PFNGLMULTITEXCOORD2FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD2IARBPROC) (GLenum target, GLint s, GLint t);
typedef void (APIENTRY * PFNGLMULTITEXCOORD2IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD2SARBPROC) (GLenum target, GLshort s, GLshort t);
typedef void (APIENTRY * PFNGLMULTITEXCOORD2SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD3DARBPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r);
typedef void (APIENTRY * PFNGLMULTITEXCOORD3DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD3FARBPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r);
typedef void (APIENTRY * PFNGLMULTITEXCOORD3FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD3IARBPROC) (GLenum target, GLint s, GLint t, GLint r);
typedef void (APIENTRY * PFNGLMULTITEXCOORD3IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD3SARBPROC) (GLenum target, GLshort s, GLshort t, GLshort r);
typedef void (APIENTRY * PFNGLMULTITEXCOORD3SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD4DARBPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
typedef void (APIENTRY * PFNGLMULTITEXCOORD4DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD4FARBPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
typedef void (APIENTRY * PFNGLMULTITEXCOORD4FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD4IARBPROC) (GLenum target, GLint s, GLint t, GLint r, GLint q);
typedef void (APIENTRY * PFNGLMULTITEXCOORD4IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRY * PFNGLMULTITEXCOORD4SARBPROC) (GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
typedef void (APIENTRY * PFNGLMULTITEXCOORD4SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRY * PFNGLACTIVETEXTUREARBPROC) (GLenum target);
typedef void (APIENTRY * PFNGLCLIENTACTIVETEXTUREARBPROC) (GLenum target);
#endif

// TTimo - VC7 / XP ?
#ifdef WIN32
typedef void (APIENTRY * PFNGLMULTITEXCOORD2FARBPROC) (GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRY * PFNGLACTIVETEXTUREARBPROC) (GLenum target);
typedef void (APIENTRY * PFNGLCLIENTACTIVETEXTUREARBPROC) (GLenum target);
#endif

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
#if !defined( _WIN32 ) && !defined(MACOS_X) && !defined( __linux__ ) && !defined( __FreeBSD__ ) && !defined(__sun) // rb010123

#include "qgl_linked.h"

#elif (defined(MACOS_X))
// This includes #ifdefs for optional logging and GL error checking after every GL call as well as #defines to prevent incorrect usage of the non-'qgl' versions of the GL API.
#include "macosx_qgl.h"

#else

// windows systems use a function pointer for each call so we can load minidrivers

#define qglAccum		glAccum
#define qglAlphaFunc		glAlphaFunc
#define qglArrayElement		glArrayElement
#define qglBegin		glBegin
#define qglBindTexture		glBindTexture
#define qglBitmap		glBitmap
#define qglBlendFunc		glBlendFunc
#define qglCallList		glCallList
#define qglClear		glClear
#define qglClearColor		glClearColor
#define qglClearDepth		glClearDepth
#define qglClearStencil		glClearStencil
#define qglClipPlane		glClipPlane
#define qglColor3f		glColor3f
#define qglColor3fv		glColor3fv
#define qglColor4f		glColor4f
#define qglColor4ubv		glColor4ubv
#define qglColorMask		glColorMask
#define qglColorPointer		glColorPointer
#define qglCopyPixels		glCopyPixels
#define qglCopyTexSubImage2D		glCopyTexSubImage2D
#define qglCullFace		glCullFace
#define qglDeleteTextures		glDeleteTextures
#define qglDepthFunc		glDepthFunc
#define qglDepthMask		glDepthMask
#define qglDepthRange		glDepthRange
#define qglDisable		glDisable
#define qglDisableClientState		glDisableClientState
#define qglDrawArrays		glDrawArrays
#define qglDrawBuffer		glDrawBuffer
#define qglDrawElements		glDrawElements
#define qglDrawPixels		glDrawPixels
#define qglEnable		glEnable
#define qglEnableClientState		glEnableClientState
#define qglEnd		glEnd
#define qglFinish		glFinish
#define qglFlush		glFlush
#define qglFrontFace		glFrontFace
#define qglFrustum		glFrustum
#define qglGenLists		glGenLists
#define qglGenTextures		glGenTextures
#define qglGetBooleanv		glGetBooleanv
#define qglGetClipPlane		glGetClipPlane
#define qglGetError		glGetError
#define qglGetFloatv		glGetFloatv
#define qglGetIntegerv		glGetIntegerv
#define qglGetLightfv		glGetLightfv
#define qglGetLightiv		glGetLightiv
#define qglGetPointerv		glGetPointerv
#define qglGetPolygonStipple		glGetPolygonStipple
#define qglGetString		glGetString
#define qglGetTexEnvfv		glGetTexEnvfv
#define qglGetTexEnviv		glGetTexEnviv
#define qglGetTexGendv		glGetTexGendv
#define qglGetTexGenfv		glGetTexGenfv
#define qglGetTexGeniv		glGetTexGeniv
#define qglGetTexImage		glGetTexImage
#define qglGetTexLevelParameterfv		glGetTexLevelParameterfv
#define qglGetTexLevelParameteriv		glGetTexLevelParameteriv
#define qglGetTexParameterfv		glGetTexParameterfv
#define qglGetTexParameteriv		glGetTexParameteriv
#define qglHint		glHint
#define qglIndexMask		glIndexMask
#define qglIndexPointer		glIndexPointer
#define qglIndexf		glIndexf
#define qglIndexfv		glIndexfv
#define qglIndexi		glIndexi
#define qglIndexiv		glIndexiv
#define qglInitNames		glInitNames
#define qglInterleavedArrays		glInterleavedArrays
#define qglIsEnabled		glIsEnabled
#define qglLineWidth		glLineWidth
#define qglListBase		glListBase
#define qglLoadIdentity		glLoadIdentity
#define qglLoadMatrixd		glLoadMatrixd
#define qglLoadMatrixf		glLoadMatrixf
#define qglMaterialf		glMaterialf
#define qglMaterialfv		glMaterialfv
#define qglMateriali		glMateriali
#define qglMaterialiv		glMaterialiv
#define qglMatrixMode		glMatrixMode
#define qglMultMatrixd		glMultMatrixd
#define qglMultMatrixf		glMultMatrixf
#define qglNewList		glNewList
#define qglNormal3b		glNormal3b
#define qglNormal3bv		glNormal3bv
#define qglNormal3d		glNormal3d
#define qglNormal3dv		glNormal3dv
#define qglNormal3f		glNormal3f
#define qglNormal3fv		glNormal3fv
#define qglNormal3i		glNormal3i
#define qglNormal3iv		glNormal3iv
#define qglNormal3s		glNormal3s
#define qglNormal3sv		glNormal3sv
#define qglNormalPointer		glNormalPointer
#define qglOrtho		glOrtho
#define qglPassThrough		glPassThrough
#define qglPixelMapfv		glPixelMapfv
#define qglPixelMapuiv		glPixelMapuiv
#define qglPixelMapusv		glPixelMapusv
#define qglPixelStoref		glPixelStoref
#define qglPixelStorei		glPixelStorei
#define qglPixelTransferf		glPixelTransferf
#define qglPixelTransferi		glPixelTransferi
#define qglPixelZoom		glPixelZoom
#define qglPointSize		glPointSize
#define qglPolygonMode		glPolygonMode
#define qglPolygonOffset		glPolygonOffset
#define qglPolygonStipple		glPolygonStipple
#define qglPopAttrib		glPopAttrib
#define qglPopClientAttrib		glPopClientAttrib
#define qglPopMatrix		glPopMatrix
#define qglPushAttrib		glPushAttrib
#define qglPushMatrix		glPushMatrix
#define qglReadPixels		glReadPixels
#define qglRotatef		glRotatef
#define qglScissor		glScissor
#define qglSelectBuffer		glSelectBuffer
#define qglShadeModel		glShadeModel
#define qglStencilFunc		glStencilFunc
#define qglStencilMask		glStencilMask
#define qglStencilOp		glStencilOp
#define qglTexCoord2f		glTexCoord2f
#define qglTexCoord2fv		glTexCoord2fv
#define qglTexCoordPointer		glTexCoordPointer
#define qglTexEnvi		glTexEnvi
#define qglTexGenf		glTexGenf
#define qglTexGenfv		glTexGenfv
#define qglTexGeni		glTexGeni
#define qglTexImage1D		glTexImage1D
#define qglTexImage2D		glTexImage2D
#define qglTexParameterf		glTexParameterf
#define qglTexParameterfv		glTexParameterfv
#define qglTexParameteri		glTexParameteri
#define qglTexSubImage1D		glTexSubImage1D
#define qglTexSubImage2D		glTexSubImage2D
#define qglTranslatef		glTranslatef
#define qglVertex2f		glVertex2f
#define qglVertex2fv		glVertex2fv
#define qglVertex3f		glVertex3f
#define qglVertex3fv		glVertex3fv
#define qglVertex4f		glVertex4f
#define qglVertex4fv		glVertex4fv
#define qglVertexPointer		glVertexPointer
#define qglViewport		glViewport

#if defined( _WIN32 )

extern  int   ( WINAPI * qwglChoosePixelFormat )(HDC, CONST PIXELFORMATDESCRIPTOR *);
extern  int   ( WINAPI * qwglDescribePixelFormat) (HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
extern  int   ( WINAPI * qwglGetPixelFormat)(HDC);
extern  BOOL  ( WINAPI * qwglSetPixelFormat)(HDC, int, CONST PIXELFORMATDESCRIPTOR *);
extern  BOOL  ( WINAPI * qwglSwapBuffers)(HDC);

extern HGLRC ( WINAPI * qwglCreateContext)(HDC);
extern HGLRC ( WINAPI * qwglCreateLayerContext)(HDC, int);
extern BOOL  ( WINAPI * qwglDeleteContext)(HGLRC);
extern HGLRC ( WINAPI * qwglGetCurrentContext)(VOID);
extern HDC   ( WINAPI * qwglGetCurrentDC)(VOID);
extern PROC  ( WINAPI * qwglGetProcAddress)(LPCSTR);
extern BOOL  ( WINAPI * qwglMakeCurrent)(HDC, HGLRC);

extern BOOL ( WINAPI * qwglSwapLayerBuffers)(HDC, UINT);

extern BOOL ( WINAPI * qwglSwapIntervalEXT)( int interval );

#endif	// _WIN32

#if ( (defined __linux__ )  || (defined __FreeBSD__ ) || (defined __sun) ) // rb010123

//FX Mesa Functions
// bk001129 - from cvs1.17 (mkv)
#if defined (__FX__)
extern fxMesaContext (*qfxMesaCreateContext)(GLuint win, GrScreenResolution_t, GrScreenRefresh_t, const GLint attribList[]);
extern fxMesaContext (*qfxMesaCreateBestContext)(GLuint win, GLint width, GLint height, const GLint attribList[]);
extern void (*qfxMesaDestroyContext)(fxMesaContext ctx);
extern void (*qfxMesaMakeCurrent)(fxMesaContext ctx);
extern fxMesaContext (*qfxMesaGetCurrentContext)(void);
extern void (*qfxMesaSwapBuffers)(void);
#endif

//GLX Functions
extern XVisualInfo * (*qglXChooseVisual)( Display *dpy, int screen, int *attribList );
extern GLXContext (*qglXCreateContext)( Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct );
extern void (*qglXDestroyContext)( Display *dpy, GLXContext ctx );
extern Bool (*qglXMakeCurrent)( Display *dpy, GLXDrawable drawable, GLXContext ctx);
extern void (*qglXCopyContext)( Display *dpy, GLXContext src, GLXContext dst, GLuint mask );
extern void (*qglXSwapBuffers)( Display *dpy, GLXDrawable drawable );

#endif // __linux__ || __FreeBSD__ || __sun // rb010123

#endif	// _WIN32 && __linux__

#endif
