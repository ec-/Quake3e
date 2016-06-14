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

		dlclose ( glw_state.OpenGLLib );

		glw_state.OpenGLLib = NULL;
	}

	qglXChooseVisual             = NULL;
	qglXCreateContext            = NULL;
	qglXDestroyContext           = NULL;
	qglXMakeCurrent              = NULL;
	qglXCopyContext              = NULL;
	qglXSwapBuffers              = NULL;

	qwglGetProcAddress           =  NULL;

}


static void *glGetProcAddress( const char *symbol )
{
	if ( glw_state.OpenGLLib )
		return dlsym( glw_state.OpenGLLib, symbol );

	return NULL;
}

char *do_dlerror(void);


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
	if (glw_state.OpenGLLib == 0)
	{
		glw_state.OpenGLLib = dlopen( dllname, RTLD_LAZY|RTLD_GLOBAL );
	}

	if (glw_state.OpenGLLib == 0)
	{
		char	fn[1024];
		// FILE *fp; // bk001204 - unused

		// if we are not setuid, try current directory
		if (dllname != NULL) {
			getcwd(fn, sizeof(fn));
			Q_strcat(fn, sizeof(fn), "/");
			Q_strcat(fn, sizeof(fn), dllname);

			glw_state.OpenGLLib = dlopen( fn, RTLD_LAZY );

			if ( glw_state.OpenGLLib == 0 ) {
				ri.Printf(PRINT_ALL, "QGL_Init: Can't load %s from /etc/ld.so.conf or current dir: %s\n", dllname, do_dlerror());
				return qfalse;
			}
		} else {
			ri.Printf(PRINT_ALL, "QGL_Init: Can't load %s from /etc/ld.so.conf: %s\n", dllname, do_dlerror());
			return qfalse;
		}
	}

#define GPA( a ) qwglGetProcAddress( a )

	qwglGetProcAddress			 =  glGetProcAddress;

	qglXChooseVisual             =  GPA("glXChooseVisual");
	qglXCreateContext            =  GPA("glXCreateContext");
	qglXDestroyContext           =  GPA("glXDestroyContext");
	qglXMakeCurrent              =  GPA("glXMakeCurrent");
	qglXCopyContext              =  GPA("glXCopyContext");
	qglXSwapBuffers              =  GPA("glXSwapBuffers");

	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;
	qglPointParameterfEXT = NULL;
	qglPointParameterfvEXT = NULL;
	qglColorTableEXT = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	qglMultiTexCoord2fARB = NULL;

	return qtrue;
}
