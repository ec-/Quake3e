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
** LINUX_QGL.C
**
** This file implements the operating system binding of GL to QGL function
** pointers.  When doing a port of Quake2 you must implement the following
** two functions:
**
** QGL_Init() - loads libraries, assigns function pointers, etc.
** QGL_Shutdown() - unloads libraries, NULLs function pointers
*/

// bk001204
#include <unistd.h>
#include <sys/types.h>


#include <float.h>
#include "../renderer/tr_local.h"
#include "unix_glw.h"

#include <dlfcn.h>

#ifndef USE_STATIC_GL
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
#endif // !USE_STATIC_GL

//GLX Functions
void* (*qwglGetProcAddress)( const char *symbol );
XVisualInfo * (*qglXChooseVisual)( Display *dpy, int screen, int *attribList );
GLXContext (*qglXCreateContext)( Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct );
void (*qglXDestroyContext)( Display *dpy, GLXContext ctx );
Bool (*qglXMakeCurrent)( Display *dpy, GLXDrawable drawable, GLXContext ctx);
void (*qglXCopyContext)( Display *dpy, GLXContext src, GLXContext dst, GLuint mask );
void (*qglXSwapBuffers)( Display *dpy, GLXDrawable drawable );

void ( APIENTRY * qglMultiTexCoord2fARB )( GLenum texture, GLfloat s, GLfloat t );
void ( APIENTRY * qglActiveTextureARB )( GLenum texture );
void ( APIENTRY * qglClientActiveTextureARB )( GLenum texture );

void ( APIENTRY * qglLockArraysEXT)( GLint, GLint);
void ( APIENTRY * qglUnlockArraysEXT) ( void );

void ( APIENTRY * qglPointParameterfEXT)( GLenum param, GLfloat value );
void ( APIENTRY * qglPointParameterfvEXT)( GLenum param, const GLfloat *value );
void ( APIENTRY * qglColorTableEXT)( int, int, int, int, int, const void * );

/*
** QGL_Shutdown
**
** Unloads the specified DLL then nulls out all the proc pointers.
*/
void QGL_Shutdown( void )
{
	if ( glw_state.OpenGLLib )
	{
		// 25/09/05 Tim Angus <tim@ngus.net>
		// Certain combinations of hardware and software, specifically
		// Linux/SMP/Nvidia/agpgart (OK, OK. MY combination of hardware and
		// software), seem to cause a catastrophic (hard reboot required) crash
		// when libGL is dynamically unloaded. I'm unsure of the precise cause,
		// suffice to say I don't see anything in the Q3 code that could cause it.
		// I suspect it's an Nvidia driver bug, but without the source or means to
		// debug I obviously can't prove (or disprove) this. Interestingly (though
		// perhaps not suprisingly), Enemy Territory and Doom 3 both exhibit the
		// same problem.
		//
		// After many, many reboots and prodding here and there, it seems that a
		// placing a short delay before libGL is unloaded works around the problem.
		// This delay is changable via the r_GLlibCoolDownMsec cvar (nice name
		// huh?), and it defaults to 0. For me, 500 seems to work.
		//if( r_GLlibCoolDownMsec->integer )
		//	usleep( r_GLlibCoolDownMsec->integer * 1000 );
		usleep( 250 * 1000 );

		dlclose( glw_state.OpenGLLib );

		glw_state.OpenGLLib = NULL;
	}

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
#endif // !USE_STATIC_GL

	qglXChooseVisual		= NULL;
	qglXCreateContext		= NULL;
	qglXDestroyContext		= NULL;
	qglXMakeCurrent			= NULL;
	qglXCopyContext			= NULL;
	qglXSwapBuffers			= NULL;

	qwglGetProcAddress		=  NULL;
}

static int glErrorCount = 0;

static void *glGetProcAddress( const char *symbol )
{
	void *sym;

	sym = dlsym( glw_state.OpenGLLib, symbol );
	if ( !sym )
	{
		glErrorCount++;
	}

	return sym;
}

char *do_dlerror( void );


/*
** QGL_Init
**
** This is responsible for binding our qgl function pointers to 
** the appropriate GL stuff.  In Windows this means doing a 
** LoadLibrary and a bunch of calls to GetProcAddress.  On other
** operating systems we need to do the right thing, whatever that
** might be.
** 
*/

qboolean QGL_Init( const char *dllname )
{
	ri.Printf( PRINT_ALL, "...initializing QGL\n" );

	ri.Printf( PRINT_ALL, "...loading '%s' : ", dllname );

	if ( glw_state.OpenGLLib == NULL )
	{
		glw_state.OpenGLLib = dlopen( dllname, RTLD_LAZY | RTLD_GLOBAL );
	}

	if ( glw_state.OpenGLLib == NULL )
	{
		// if we are not setuid, try current directory
		if ( dllname != NULL ) 
		{
			char fn[1024];

			getcwd( fn, sizeof( fn ) );
			Q_strcat( fn, sizeof( fn ), "/" );
			Q_strcat( fn, sizeof( fn ), dllname );

			glw_state.OpenGLLib = dlopen( fn, RTLD_LAZY );

			if ( glw_state.OpenGLLib == NULL ) 
			{
				ri.Printf( PRINT_ALL, "failed\n" );
				ri.Printf(PRINT_ALL, "QGL_Init: Can't load %s from /etc/ld.so.conf or current dir: %s\n", dllname, do_dlerror() );
				return qfalse;
			}
		}
		else 
		{	
			ri.Printf( PRINT_ALL, "failed\n" );
			ri.Printf( PRINT_ALL, "QGL_Init: Can't load %s from /etc/ld.so.conf: %s\n", dllname, do_dlerror() );
			return qfalse;
		}
	}

	ri.Printf( PRINT_ALL, "succeeded\n" );

#define GPA( a ) qwglGetProcAddress( a )
	qwglGetProcAddress		= glGetProcAddress;
	glErrorCount			= 0;

#ifndef USE_STATIC_GL
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
#endif // !USE_STATIC_GL

	if ( glErrorCount ) 
	{
		ri.Printf( PRINT_ALL, "QGL_Init: Can't resolve required symbols from %s\n", dllname );
		return qfalse;
	}

	qglXChooseVisual        = GPA( "glXChooseVisual" );
	qglXCreateContext       = GPA( "glXCreateContext" );
	qglXDestroyContext      = GPA( "glXDestroyContext" );
	qglXMakeCurrent         = GPA( "glXMakeCurrent" );
	qglXCopyContext         = GPA( "glXCopyContext" );
	qglXSwapBuffers         = GPA( "glXSwapBuffers" );

	qglLockArraysEXT		= NULL;
	qglUnlockArraysEXT		= NULL;
	qglPointParameterfEXT	= NULL;
	qglPointParameterfvEXT	= NULL;
	qglColorTableEXT		= NULL;
	qglActiveTextureARB		= NULL;
	qglClientActiveTextureARB = NULL;
	qglMultiTexCoord2fARB	= NULL;

	return qtrue;
}
