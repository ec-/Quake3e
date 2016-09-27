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
** QGL_WIN.C
**
** This file implements the operating system binding of GL to QGL function
** pointers.  When doing a port of Quake3 you must implement the following
** two functions:
**
** QGL_Init() - loads libraries, assigns function pointers, etc.
** QGL_Shutdown() - unloads libraries, NULLs function pointers
*/
#include "../renderer/tr_local.h"
#include "glw_win.h"
#include "win_local.h"

#ifdef USE_STATIC_GL

#ifdef _MSC_VER
#pragma comment(lib, "opengl32.lib")
#endif

#else // !USE_STATIC_GL

void ( APIENTRY * qglAlphaFunc )(GLenum func, GLclampf ref);
void ( APIENTRY * qglArrayElement )(GLint i);
void ( APIENTRY * qglBegin )(GLenum mode);
void ( APIENTRY * qglBindTexture )(GLenum target, GLuint texture);
void ( APIENTRY * qglBlendFunc )(GLenum sfactor, GLenum dfactor);
void ( APIENTRY * qglClear )(GLbitfield mask);
void ( APIENTRY * qglClearColor )(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void ( APIENTRY * qglClearDepth )(GLclampd depth);
void ( APIENTRY * qglClearStencil )(GLint s);
void ( APIENTRY * qglClipPlane )(GLenum plane, const GLdouble *equation);
void ( APIENTRY * qglColor3f )(GLfloat red, GLfloat green, GLfloat blue);
void ( APIENTRY * qglColor3fv )(const GLfloat *v);
void ( APIENTRY * qglColor4f )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void ( APIENTRY * qglColor4ubv )(const GLubyte *v);
void ( APIENTRY * qglColorMask )(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void ( APIENTRY * qglColorPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglCopyPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
void ( APIENTRY * qglCopyTexImage2D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void ( APIENTRY * qglCopyTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void ( APIENTRY * qglCullFace )(GLenum mode);
void ( APIENTRY * qglDeleteTextures )(GLsizei n, const GLuint *textures);
void ( APIENTRY * qglDepthFunc )(GLenum func);
void ( APIENTRY * qglDepthMask )(GLboolean flag);
void ( APIENTRY * qglDepthRange )(GLclampd zNear, GLclampd zFar);
void ( APIENTRY * qglDisable )(GLenum cap);
void ( APIENTRY * qglDisableClientState )(GLenum array);
void ( APIENTRY * qglDrawBuffer )(GLenum mode);
void ( APIENTRY * qglDrawElements )(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void ( APIENTRY * qglEnable )(GLenum cap);
void ( APIENTRY * qglEnableClientState )(GLenum array);
void ( APIENTRY * qglEnd )(void);
void ( APIENTRY * qglFinish )(void);
void ( APIENTRY * qglGetBooleanv )(GLenum pname, GLboolean *params);
GLenum ( APIENTRY * qglGetError )(void);
void ( APIENTRY * qglGetIntegerv )(GLenum pname, GLint *params);
const GLubyte * ( APIENTRY * qglGetString )(GLenum name);
void ( APIENTRY * qglHint )(GLenum target, GLenum mode);
void ( APIENTRY * qglLineWidth )(GLfloat width);
void ( APIENTRY * qglLoadIdentity )(void);
void ( APIENTRY * qglLoadMatrixf )(const GLfloat *m);
void ( APIENTRY * qglMatrixMode )(GLenum mode);
void ( APIENTRY * qglNormalPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglOrtho )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void ( APIENTRY * qglPolygonMode )(GLenum face, GLenum mode);
void ( APIENTRY * qglPolygonOffset )(GLfloat factor, GLfloat units);
void ( APIENTRY * qglPopMatrix )(void);
void ( APIENTRY * qglPushMatrix )(void);
void ( APIENTRY * qglReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void ( APIENTRY * qglScissor )(GLint x, GLint y, GLsizei width, GLsizei height);
void ( APIENTRY * qglShadeModel )(GLenum mode);
void ( APIENTRY * qglStencilFunc )(GLenum func, GLint ref, GLuint mask);
void ( APIENTRY * qglStencilMask )(GLuint mask);
void ( APIENTRY * qglStencilOp )(GLenum fail, GLenum zfail, GLenum zpass);
void ( APIENTRY * qglTexCoord2f )(GLfloat s, GLfloat t);
void ( APIENTRY * qglTexCoord2fv )(const GLfloat *v);
void ( APIENTRY * qglTexCoordPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglTexEnvi )(GLenum target, GLenum pname, GLint param);
void ( APIENTRY * qglTexImage1D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTexImage2D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTexParameterf )(GLenum target, GLenum pname, GLfloat param);
void ( APIENTRY * qglTexParameteri )(GLenum target, GLenum pname, GLint param);
void ( APIENTRY * qglTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTranslatef )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglVertex2f )(GLfloat x, GLfloat y);
void ( APIENTRY * qglVertex3f )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglVertex3fv )(const GLfloat *v);
void ( APIENTRY * qglVertexPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglViewport )(GLint x, GLint y, GLsizei width, GLsizei height);

#endif

int ( WINAPI * qwglSwapIntervalEXT)( int interval );

HGLRC ( WINAPI * qwglCreateContext)(HDC);
BOOL  ( WINAPI * qwglDeleteContext)(HGLRC);
HGLRC ( WINAPI * qwglGetCurrentContext)(VOID);
HDC   ( WINAPI * qwglGetCurrentDC)(VOID);
PROC  ( WINAPI * qwglGetProcAddress)(LPCSTR);
BOOL  ( WINAPI * qwglMakeCurrent)(HDC, HGLRC);

void ( APIENTRY * qglMultiTexCoord2fARB )( GLenum texture, GLfloat s, GLfloat t );
void ( APIENTRY * qglActiveTextureARB )( GLenum texture );
void ( APIENTRY * qglClientActiveTextureARB )( GLenum texture );

void ( APIENTRY * qglLockArraysEXT)( GLint, GLint);
void ( APIENTRY * qglUnlockArraysEXT) ( void );

static const char * BooleanToString( GLboolean b )
{
	if ( b == GL_FALSE )
		return "GL_FALSE";
	else if ( b == GL_TRUE )
		return "GL_TRUE";
	else
		return "OUT OF RANGE FOR BOOLEAN";
}

static const char * FuncToString( GLenum f )
{
	switch ( f )
	{
	case GL_ALWAYS:
		return "GL_ALWAYS";
	case GL_NEVER:
		return "GL_NEVER";
	case GL_LEQUAL:
		return "GL_LEQUAL";
	case GL_LESS:
		return "GL_LESS";
	case GL_EQUAL:
		return "GL_EQUAL";
	case GL_GREATER:
		return "GL_GREATER";
	case GL_GEQUAL:
		return "GL_GEQUAL";
	case GL_NOTEQUAL:
		return "GL_NOTEQUAL";
	default:
		return "!!! UNKNOWN !!!";
	}
}

static const char * PrimToString( GLenum mode )
{
	static char prim[1024];

	if ( mode == GL_TRIANGLES )
		strcpy( prim, "GL_TRIANGLES" );
	else if ( mode == GL_TRIANGLE_STRIP )
		strcpy( prim, "GL_TRIANGLE_STRIP" );
	else if ( mode == GL_TRIANGLE_FAN )
		strcpy( prim, "GL_TRIANGLE_FAN" );
	else if ( mode == GL_QUADS )
		strcpy( prim, "GL_QUADS" );
	else if ( mode == GL_QUAD_STRIP )
		strcpy( prim, "GL_QUAD_STRIP" );
	else if ( mode == GL_POLYGON )
		strcpy( prim, "GL_POLYGON" );
	else if ( mode == GL_POINTS )
		strcpy( prim, "GL_POINTS" );
	else if ( mode == GL_LINES )
		strcpy( prim, "GL_LINES" );
	else if ( mode == GL_LINE_STRIP )
		strcpy( prim, "GL_LINE_STRIP" );
	else if ( mode == GL_LINE_LOOP )
		strcpy( prim, "GL_LINE_LOOP" );
	else
		sprintf( prim, "0x%x", mode );

	return prim;
}

static const char * CapToString( GLenum cap )
{
	static char buffer[1024];

	switch ( cap )
	{
	case GL_TEXTURE_2D:
		return "GL_TEXTURE_2D";
	case GL_BLEND:
		return "GL_BLEND";
	case GL_DEPTH_TEST:
		return "GL_DEPTH_TEST";
	case GL_CULL_FACE:
		return "GL_CULL_FACE";
	case GL_CLIP_PLANE0:
		return "GL_CLIP_PLANE0";
	case GL_COLOR_ARRAY:
		return "GL_COLOR_ARRAY";
	case GL_TEXTURE_COORD_ARRAY:
		return "GL_TEXTURE_COORD_ARRAY";
	case GL_VERTEX_ARRAY:
		return "GL_VERTEX_ARRAY";
	case GL_ALPHA_TEST:
		return "GL_ALPHA_TEST";
	case GL_STENCIL_TEST:
		return "GL_STENCIL_TEST";
	default:
		sprintf( buffer, "0x%x", cap );
	}

	return buffer;
}

static const char * TypeToString( GLenum t )
{
	switch ( t )
	{
	case GL_BYTE:
		return "GL_BYTE";
	case GL_UNSIGNED_BYTE:
		return "GL_UNSIGNED_BYTE";
	case GL_SHORT:
		return "GL_SHORT";
	case GL_UNSIGNED_SHORT:
		return "GL_UNSIGNED_SHORT";
	case GL_INT:
		return "GL_INT";
	case GL_UNSIGNED_INT:
		return "GL_UNSIGNED_INT";
	case GL_FLOAT:
		return "GL_FLOAT";
	case GL_DOUBLE:
		return "GL_DOUBLE";
	default:
		return "!!! UNKNOWN !!!";
	}
}


/*
** QGL_Shutdown
**
** Unloads the specified DLL then nulls out all the proc pointers.  This
** is only called during a hard shutdown of the OGL subsystem (e.g. vid_restart).
*/
void QGL_Shutdown( void )
{
	ri.Printf( PRINT_ALL, "...shutting down QGL\n" );

	if ( glw_state.OpenGLLib )
	{
		ri.Printf( PRINT_ALL, "...unloading OpenGL DLL\n" );
		Sys_UnloadLibrary( glw_state.OpenGLLib );
	}

	glw_state.OpenGLLib = NULL;

#ifndef USE_STATIC_GL
	qglAlphaFunc			= NULL;
	qglArrayElement			= NULL;
	qglBegin				= NULL;
	qglBindTexture			= NULL;
	qglBlendFunc			= NULL;
	qglClear				= NULL;
	qglClearColor			= NULL;
	qglClearDepth			= NULL;
	qglClearStencil			= NULL;
	qglClipPlane			= NULL;
	qglColor3f				= NULL;
	qglColor3fv				= NULL;
	qglColor4f				= NULL;
	qglColor4ubv			= NULL;
	qglColorMask			= NULL;
	qglColorPointer			= NULL;
	qglCopyPixels			= NULL;
	qglCopyTexImage2D		= NULL;
	qglCopyTexSubImage2D	= NULL;
	qglCullFace				= NULL;
	qglDeleteTextures		= NULL;
	qglDepthFunc			= NULL;
	qglDepthMask			= NULL;
	qglDepthRange			= NULL;
	qglDisable				= NULL;
	qglDisableClientState	= NULL;
	qglDrawBuffer			= NULL;
	qglDrawElements			= NULL;
	qglEnable				= NULL;
	qglEnableClientState	= NULL;
	qglEnd					= NULL;
	qglFinish				= NULL;
	qglGetBooleanv			= NULL;
	qglGetError				= NULL;
	qglGetIntegerv			= NULL;
	qglGetString			= NULL;
	qglHint					= NULL;
	qglLineWidth			= NULL;
	qglLoadIdentity			= NULL;
	qglLoadMatrixf			= NULL;
	qglMatrixMode			= NULL;
	qglNormalPointer		= NULL;
	qglOrtho				= NULL;
	qglPolygonMode			= NULL;
	qglPolygonOffset		= NULL;
	qglPopMatrix			= NULL;
	qglPushMatrix			= NULL;
	qglReadPixels			= NULL;
	qglScissor				= NULL;
	qglShadeModel			= NULL;
	qglStencilFunc			= NULL;
	qglStencilMask			= NULL;
	qglStencilOp			= NULL;
	qglTexCoord2f			= NULL;
	qglTexCoord2fv			= NULL;
	qglTexCoordPointer		= NULL;
	qglTexEnvi				= NULL;
	qglTexImage1D			= NULL;
	qglTexImage2D			= NULL;
	qglTexParameterf		= NULL;
	qglTexParameteri		= NULL;
	qglTexSubImage1D		= NULL;
	qglTexSubImage2D		= NULL;
	qglTranslatef			= NULL;
	qglVertex2f				= NULL;
	qglVertex3f				= NULL;
	qglVertex3fv			= NULL;
	qglVertexPointer		= NULL;
	qglViewport				= NULL;
#endif

	qwglCreateContext		= NULL;
	qwglDeleteContext		= NULL;
	qwglGetCurrentContext	= NULL;
	qwglGetCurrentDC		= NULL;
	qwglGetProcAddress		= NULL;
	qwglMakeCurrent			= NULL;
}


#ifdef _MSC_VER
#	pragma warning (disable : 4113 4133 4047 )
#	define GPA( a ) Sys_LoadFunction( glw_state.OpenGLLib, a )
#else
#	define GPA( a ) (void *)Sys_LoadFunction( glw_state.OpenGLLib, a )
#endif


/*
** QGL_Init
**
** This is responsible for binding our qgl function pointers to 
** the appropriate GL stuff.  In Windows this means doing a 
** LoadLibrary and a bunch of calls to GetProcAddress.  On other
** operating systems we need to do the right thing, whatever that
** might be.
*/
qboolean QGL_Init( const char *dllname )
{
#if 0
	char systemDir[1024];
	char libName[1024];

#ifdef UNICODE
	TCHAR buffer[1024];
	GetSystemDirectory( buffer, ARRAYSIZE( buffer ) );
	strcpy( systemDir, WtoA( buffer ) );
#else
	GetSystemDirectory( systemDir, sizeof( systemDir ) );
#endif
#endif

	assert( glw_state.OpenGLLib == 0 );

	ri.Printf( PRINT_ALL, "...initializing QGL\n" );

	// NOTE: this assumes that 'dllname' is lower case (and it should be)!
#if 0
	if ( dllname[0] != '!' )
		Com_sprintf( libName, sizeof( libName ), "%s\\%s", systemDir, dllname );
	else
		Q_strncpyz( libName, dllname+1, sizeof( libName ) );

	ri.Printf( PRINT_ALL, "...loading '%s.dll' : ", libName );
	glw_state.OpenGLLib = Sys_LoadLibrary( libName );
#else
	ri.Printf( PRINT_ALL, "...loading '%s.dll' : ", dllname );
	glw_state.OpenGLLib = Sys_LoadLibrary( dllname );
#endif

	if ( glw_state.OpenGLLib == NULL )
	{
		ri.Printf( PRINT_ALL, "failed\n" );
		return qfalse;
	}

	ri.Printf( PRINT_ALL, "succeeded\n" );

#ifndef USE_STATIC_GL
	Sys_LoadFunctionErrors(); // reset error count

	qglAlphaFunc			= GPA( "glAlphaFunc" );
	qglArrayElement			= GPA( "glArrayElement" );
	qglBegin				= GPA( "glBegin" );
	qglBindTexture			= GPA( "glBindTexture" );
	qglBlendFunc			= GPA( "glBlendFunc" );
	qglClear				= GPA( "glClear" );
	qglClearColor			= GPA( "glClearColor" );
	qglClearDepth			= GPA( "glClearDepth" );
	qglClearStencil			= GPA( "glClearStencil" );
	qglClipPlane			= GPA( "glClipPlane" );
	qglColor3f				= GPA( "glColor3f" );
	qglColor3fv				= GPA( "glColor3fv" );
	qglColor4f				= GPA( "glColor4f" );
	qglColor4ubv			= GPA( "glColor4ubv" );
	qglColorMask			= GPA( "glColorMask" );
	qglColorPointer			= GPA( "glColorPointer" );
	qglCopyPixels			= GPA( "glCopyPixels" );
	qglCopyTexImage2D		= GPA( "glCopyTexImage2D" );
	qglCopyTexSubImage2D	= GPA( "glCopyTexSubImage2D" );
	qglCullFace				= GPA( "glCullFace" );
	qglDeleteTextures		= GPA( "glDeleteTextures" );
	qglDepthFunc			= GPA( "glDepthFunc" );
	qglDepthMask			= GPA( "glDepthMask" );
	qglDepthRange			= GPA( "glDepthRange" );
	qglDisable				= GPA( "glDisable" );
	qglDisableClientState	= GPA( "glDisableClientState" );
	qglDrawBuffer			= GPA( "glDrawBuffer" );
	qglDrawElements			= GPA( "glDrawElements" );
	qglEnable				= GPA( "glEnable" );
	qglEnableClientState	= GPA( "glEnableClientState" );
	qglEnd					= GPA( "glEnd" );
	qglFinish				= GPA( "glFinish" );
	qglGetBooleanv			= GPA( "glGetBooleanv" );
	qglGetError				= GPA( "glGetError" );
	qglGetIntegerv			= GPA( "glGetIntegerv" );
	qglGetString			= GPA( "glGetString" );
	qglHint					= GPA( "glHint" );
	qglLineWidth			= GPA( "glLineWidth" );
	qglLoadIdentity			= GPA( "glLoadIdentity" );
	qglLoadMatrixf			= GPA( "glLoadMatrixf" );
	qglMatrixMode			= GPA( "glMatrixMode" );
	qglNormalPointer		= GPA( "glNormalPointer" );
	qglOrtho				= GPA( "glOrtho" );
	qglPolygonMode			= GPA( "glPolygonMode" );
	qglPolygonOffset		= GPA( "glPolygonOffset" );
	qglPopMatrix			= GPA( "glPopMatrix" );
	qglPushMatrix			= GPA( "glPushMatrix" );
	qglReadPixels			= GPA( "glReadPixels" );
	qglScissor				= GPA( "glScissor" );
	qglShadeModel			= GPA( "glShadeModel" );
	qglStencilFunc			= GPA( "glStencilFunc" );
	qglStencilMask			= GPA( "glStencilMask" );
	qglStencilOp			= GPA( "glStencilOp" );
	qglTexCoord2f			= GPA( "glTexCoord2f" );
	qglTexCoord2fv			= GPA( "glTexCoord2fv" );
	qglTexCoordPointer		= GPA( "glTexCoordPointer" );
	qglTexEnvi				= GPA( "glTexEnvi" );
	qglTexImage1D			= GPA( "glTexImage1D" );
	qglTexImage2D			= GPA( "glTexImage2D" );
	qglTexParameterf		= GPA( "glTexParameterf" );
	qglTexParameteri		= GPA( "glTexParameteri" );
	qglTexSubImage1D		= GPA( "glTexSubImage1D" );
	qglTexSubImage2D		= GPA( "glTexSubImage2D" );
	qglTranslatef			= GPA( "glTranslatef" );
	qglVertex2f				= GPA( "glVertex2f" );
	qglVertex3f				= GPA( "glVertex3f" );
	qglVertex3fv			= GPA( "glVertex3fv" );
	qglVertexPointer		= GPA( "glVertexPointer" );
	qglViewport				= GPA( "glViewport" );

	if ( Sys_LoadFunctionErrors() ) 
	{
		ri.Printf( PRINT_ALL, "resolve error\n" );
		return qfalse;
	}
#endif


	qwglCreateContext		= GPA( "wglCreateContext" );
	qwglDeleteContext		= GPA( "wglDeleteContext" );
	qwglGetCurrentContext	= GPA( "wglGetCurrentContext" );
	qwglGetCurrentDC		= GPA( "wglGetCurrentDC" );
	qwglGetProcAddress		= GPA( "wglGetProcAddress" );
	qwglMakeCurrent			= GPA( "wglMakeCurrent" );

	qwglSwapIntervalEXT		= NULL;
	qglActiveTextureARB		= NULL;
	qglClientActiveTextureARB = NULL;
	qglMultiTexCoord2fARB	= NULL;
	qglLockArraysEXT		= NULL;
	qglUnlockArraysEXT		= NULL;

	return qtrue;
}

#ifdef _MSC_VER
#pragma warning (default : 4113 4133 4047 )
#endif



