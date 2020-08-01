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
#elif defined( __linux__ ) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined( __sun )
#include <GL/gl.h>
#include <GL/glx.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
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

#else

#define GL_MAX_ACTIVE_TEXTURES_ARB          0x84E2

#endif /* defined(__sun) */

// anisotropic filtering constants
#define GL_TEXTURE_MAX_ANISOTROPY_EXT       0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT   0x84FF

#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER                  0x812D
#endif

// define for skyboxes without black seams
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE                    0x812F
#endif

/*
** extension constants
*/
// S3TC compression constants
#define GL_RGB_S3TC                         0x83A0
#define GL_RGB4_S3TC                        0x83A1

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT     0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT    0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT    0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT    0x83F3

#ifndef GL_VERSION_1_2
#define GL_VERSION_1_2 1
#define GL_UNSIGNED_SHORT_4_4_4_4           0x8033
#define GL_UNSIGNED_INT_8_8_8_8             0x8035
#define GL_UNSIGNED_INT_10_10_10_2          0x8036
#define GL_UNSIGNED_SHORT_4_4_4_4_REV       0x8365
#define GL_UNSIGNED_INT_8_8_8_8_REV         0x8367
#define GL_UNSIGNED_INT_2_10_10_10_REV      0x8368
#define GL_BGR                              0x80E0
#define GL_BGRA                             0x80E1
#endif

#ifndef GL_DEPTH_COMPONENT32
#define GL_DEPTH_COMPONENT32                0x81A7
#endif

#ifndef GL_ARB_vertex_buffer_object
#define GL_ARB_vertex_buffer_object 1
typedef ptrdiff_t GLsizeiptrARB;
typedef ptrdiff_t GLintptrARB;
#define GL_ARRAY_BUFFER_ARB                 0x8892
#define GL_ELEMENT_ARRAY_BUFFER_ARB         0x8893
#define GL_STATIC_DRAW_ARB                  0x88E4
#endif

#ifndef GL_ARB_vertex_program
#define GL_ARB_vertex_program 1
#define GL_VERTEX_PROGRAM_ARB               0x8620
#endif

#ifndef GL_ARB_fragment_program
#define GL_ARB_fragment_program 1
#define GL_FRAGMENT_PROGRAM_ARB             0x8804
#define GL_PROGRAM_FORMAT_ASCII_ARB         0x8875
#define GL_PROGRAM_STRING_ARB               0x8628
#define GL_PROGRAM_ERROR_POSITION_ARB       0x864B
#define GL_PROGRAM_ERROR_STRING_ARB         0x8874
#endif

#ifndef GL_VERSION_2_0
#define GL_VERSION_2_0 1
typedef char GLchar;
#define GL_MAX_TEXTURE_IMAGE_UNITS          0x8872
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#endif

#ifndef GL_VERSION_3_0
#define GL_VERSION_3_0 1
#define GL_R11F_G11F_B10F                   0x8C3A
#define GL_DEPTH_STENCIL_ATTACHMENT         0x821A
#define GL_DEPTH24_STENCIL8                 0x88F0
#define GL_DEPTH_STENCIL                    0x84F9
#define GL_UNSIGNED_INT_24_8                0x84FA
#define GL_UNSIGNED_NORMALIZED              0x8C17
#define GL_READ_FRAMEBUFFER                 0x8CA8
#define GL_DRAW_FRAMEBUFFER                 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE             0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER 0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER 0x8CDC
#define GL_FRAMEBUFFER_UNSUPPORTED          0x8CDD
#define GL_MAX_COLOR_ATTACHMENTS            0x8CDF
#define GL_COLOR_ATTACHMENT0                0x8CE0
#define GL_DEPTH_ATTACHMENT                 0x8D00
#define GL_STENCIL_ATTACHMENT               0x8D20
#define GL_FRAMEBUFFER                      0x8D40
#define GL_RENDERBUFFER                     0x8D41
#endif

//===========================================================================

#define QGL_Core_PROCS \
	GLE( void, glAlphaFunc, GLenum func, GLclampf ref ) \
	GLE( void, glBindTexture, GLenum target, GLuint texture ) \
	GLE( void, glBlendFunc, GLenum sfactor, GLenum dfactor ) \
	GLE( void, glClear, GLbitfield mask ) \
	GLE( void, glClearColor, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha ) \
	GLE( void, glClearDepth, GLclampd depth ) \
	GLE( void, glColor4f, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha ) \
	GLE( void, glColorMask, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha ) \
	GLE( void, glColorPointer, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer ) \
	GLE( void, glCullFace, GLenum mode ) \
	GLE( void, glDeleteTextures, GLsizei n, const GLuint *textures ) \
	GLE( void, glDepthFunc, GLenum func ) \
	GLE( void, glDepthMask, GLboolean flag ) \
	GLE( void, glDepthRange, GLclampd zNear, GLclampd zFar ) \
	GLE( void, glDisable, GLenum cap ) \
	GLE( void, glDisableClientState, GLenum array ) \
	GLE( void, glDrawArrays, GLenum mode, GLint first, GLsizei count ) \
	GLE( void, glDrawBuffer, GLenum mode ) \
	GLE( void, glDrawElements, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices ) \
	GLE( void, glEnable, GLenum cap ) \
	GLE( void, glEnableClientState, GLenum array ) \
	GLE( void, glFinish, void ) \
	GLE( void, glGenTextures, GLsizei n, GLuint *textures ) \
	GLE( void, glGetBooleanv, GLenum pname, GLboolean *params ) \
	GLE( GLenum, glGetError, void ) \
	GLE( void, glGetIntegerv, GLenum pname, GLint *params ) \
	GLE( const GLubyte*, glGetString, GLenum name ) \
	GLE( void, glLineWidth, GLfloat width ) \
	GLE( void, glLoadIdentity, void ) \
	GLE( void, glLoadMatrixf, const GLfloat *m ) \
	GLE( void, glMatrixMode, GLenum mode ) \
	GLE( void, glNormalPointer, GLenum type, GLsizei stride, const GLvoid *pointer ) \
	GLE( void, glPolygonMode, GLenum face, GLenum mode ) \
	GLE( void, glPolygonOffset, GLfloat factor, GLfloat units ) \
	GLE( void, glPopMatrix, void ) \
	GLE( void, glPushMatrix, void ) \
	GLE( void, glReadPixels, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels ) \
	GLE( void, glScissor, GLint x, GLint y, GLsizei width, GLsizei height ) \
	GLE( void, glShadeModel, GLenum mode ) \
	GLE( void, glStencilFunc, GLenum func, GLint ref, GLuint mask ) \
	GLE( void, glStencilOp, GLenum fail, GLenum zfail, GLenum zpass ) \
	GLE( void, glTexCoordPointer, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer ) \
	GLE( void, glTexEnvi, GLenum target, GLenum pname, GLint param ) \
	GLE( void, glTexImage2D, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels ) \
	GLE( void, glTexParameteri, GLenum target, GLenum pname, GLint param ) \
	GLE( void, glTexSubImage2D, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels ) \
	GLE( void, glVertexPointer, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer ) \
	GLE( void, glViewport, GLint x, GLint y, GLsizei width, GLsizei height )

#define QGL_Ext_PROCS \
	GLE( void, glMultiTexCoord2fARB, GLenum texture, GLfloat s, GLfloat t ) \
	GLE( void, glActiveTextureARB, GLenum texture ) \
	GLE( void, glClientActiveTextureARB, GLenum texture ) \
	GLE( void, glLockArraysEXT, GLint, GLint) \
	GLE( void, glUnlockArraysEXT, void )

#define QGL_ARB_PROGRAM_PROCS \
	GLE( void, glGenProgramsARB, GLsizei n, GLuint *programs ) \
	GLE( void, glDeleteProgramsARB, GLsizei n, const GLuint *programs ) \
	GLE( void, glProgramStringARB, GLenum target, GLenum format, GLsizei len, const GLvoid *string ) \
	GLE( void, glBindProgramARB, GLenum target, GLuint program ) \
	GLE( void, glProgramLocalParameter4fARB, GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w ) \
	GLE( void, glProgramLocalParameter4fvARB, GLenum target, GLuint index, const GLfloat *params )

#define QGL_VBO_PROCS \
	GLE( void, glGenBuffersARB, GLsizei n, GLuint *buffers ) \
	GLE( void, glDeleteBuffersARB, GLsizei n, const GLuint *buffers ) \
	GLE( void, glBindBufferARB, GLenum target, GLuint buffer ) \
	GLE( void, glBufferDataARB, GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage )

#define QGL_FBO_PROCS \
	GLE( void, glBindRenderbuffer, GLenum target, GLuint renderbuffer ) \
	GLE( void, glDeleteFramebuffers, GLsizei n, const GLuint *framebuffers ) \
	GLE( void, glDeleteRenderbuffers, GLsizei n, const GLuint *renderbuffers ) \
	GLE( void, glGenRenderbuffers, GLsizei n, GLuint *renderbuffers ) \
	GLE( void, glRenderbufferStorage, GLenum target, GLenum internalformat, GLsizei width, GLsizei height ) \
	GLE( void, glGetRenderbufferParameteriv, GLenum target, GLenum pname, GLint *params ) \
	GLE( GLboolean, glIsFramebuffer, GLuint framebuffer ) \
	GLE( void, glBindFramebuffer, GLenum target, GLuint framebuffer ) \
	GLE( void, glGenFramebuffers, GLsizei n, GLuint *framebuffers ) \
	GLE( GLenum, glCheckFramebufferStatus, GLenum target ) \
	GLE( void, glFramebufferTexture2D, GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level ) \
	GLE( void, glFramebufferRenderbuffer, GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer ) \
	GLE( void, glGetFramebufferAttachmentParameteriv, GLenum target, GLenum attachment, GLenum pname, GLint *params ) \
	GLE( void, glBlitFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter )

#define QGL_FBO_OPT_PROCS \
	GLE( void, glRenderbufferStorageMultisample, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height ) \
	GLE( void, glGetInternalformativ, GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint *params )

#define QGL_Win32_PROCS \
	GLE( HGLRC, wglCreateContext, HDC ) \
	GLE( BOOL,  wglDeleteContext ,HGLRC ) \
	GLE( HGLRC, wglGetCurrentContext, VOID ) \
	GLE( PROC,  wglGetProcAddress, LPCSTR ) \
	GLE( BOOL,  wglMakeCurrent, HDC, HGLRC )

#ifdef _WIN32
#define QGL_Swp_PROCS \
	GLE( BOOL,	wglSwapIntervalEXT, int interval )
#else
#define QGL_Swp_PROCS \
	GLE( void,	glXSwapIntervalEXT, Display *dpy, GLXDrawable drawable, int interval ) \
	GLE( int,	glXSwapIntervalMESA, unsigned interval ) \
	GLE( int,	glXSwapIntervalSGI, int interval )
#endif

#define QGL_LinX11_PROCS \
	GLE( XVisualInfo*, glXChooseVisual, Display *dpy, int screen, int *attribList ) \
	GLE( GLXContext, glXCreateContext, Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct ) \
	GLE( void, glXDestroyContext, Display *dpy, GLXContext ctx ) \
	GLE( Bool, glXMakeCurrent, Display *dpy, GLXDrawable drawable, GLXContext ctx) \
	GLE( void, glXCopyContext, Display *dpy, GLXContext src, GLXContext dst, GLuint mask ) \
	GLE( void, glXSwapBuffers, Display *dpy, GLXDrawable drawable )

#ifndef __APPLE__

#define GLE( ret, name, ... ) extern ret ( APIENTRY * q##name )( __VA_ARGS__ );
	QGL_Swp_PROCS;
#ifdef _WIN32
	QGL_Win32_PROCS;
#else // assume in opposition to win32
	QGL_LinX11_PROCS;
#endif
#undef GLE

#endif // !__APPLE__

#endif // __QGL_H__
