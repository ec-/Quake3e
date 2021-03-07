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

#include <unistd.h>
#include <sys/types.h>
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/qgl.h"
#include "../renderercommon/tr_types.h"
#include "unix_glw.h"

#include <dlfcn.h>

#define GLE( ret, name, ... ) ret ( APIENTRY * q##name )( __VA_ARGS__ );
QGL_LinX11_PROCS;
QGL_Swp_PROCS;
#undef GLE

/*
** QGL_Shutdown
**
** Unloads the specified DLL then nulls out all the proc pointers.
*/
void QGL_Shutdown( qboolean unloadDLL )
{
	Com_Printf( "...shutting down QGL\n" );

	if ( glw_state.OpenGLLib && unloadDLL )
	{
		Com_Printf( "...unloading OpenGL DLL\n" );
		// 25/09/05 Tim Angus <tim@ngus.net>
		// Certain combinations of hardware and software, specifically
		// Linux/SMP/Nvidia/agpgart (OK, OK. MY combination of hardware and
		// software), seem to cause a catastrophic (hard reboot required) crash
		// when libGL is dynamically unloaded. I'm unsure of the precise cause,
		// suffice to say I don't see anything in the Q3 code that could cause it.
		// I suspect it's an Nvidia driver bug, but without the source or means to
		// debug I obviously can't prove (or disprove) this. Interestingly (though
		// perhaps not surprisingly), Enemy Territory and Doom 3 both exhibit the
		// same problem.
		//
		// After many, many reboots and prodding here and there, it seems that a
		// placing a short delay before libGL is unloaded works around the problem.
		// This delay is changeable via the r_GLlibCoolDownMsec cvar (nice name
		// huh?), and it defaults to 0. For me, 500 seems to work.
		//if( r_GLlibCoolDownMsec->integer )
		//	usleep( r_GLlibCoolDownMsec->integer * 1000 );
		usleep( 250 * 1000 );

		dlclose( glw_state.OpenGLLib );

		glw_state.OpenGLLib = NULL;
	}

#define GLE( ret, name, ... ) q##name = NULL;
	QGL_LinX11_PROCS;
	QGL_Swp_PROCS;
#undef GLE
}

static int glErrorCount = 0;

void *GL_GetProcAddress( const char *symbol )
{
	void *sym;

	sym = dlsym( glw_state.OpenGLLib, symbol );
	if ( !sym )
	{
		glErrorCount++;
	}

	return sym;
}

//char *do_dlerror( void );


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
	Com_Printf( "...initializing QGL\n" );

	if ( glw_state.OpenGLLib == NULL )
	{
		Com_Printf( "...loading '%s' : ", dllname );

		glw_state.OpenGLLib = dlopen( dllname, RTLD_NOW | RTLD_GLOBAL );

		if ( glw_state.OpenGLLib == NULL )
		{
#if 0
			char fn[1024];

			Com_Printf( "\n...loading '%s' : ", dllname );
			// if we are not setuid, try current directory
			if ( dllname != NULL )
			{
				if ( getcwd( fn, sizeof( fn ) ) )
				{
					Q_strcat( fn, sizeof( fn ), "/" );
					Q_strcat( fn, sizeof( fn ), dllname );
					glw_state.OpenGLLib = dlopen( fn, RTLD_NOW );
				}

				if ( glw_state.OpenGLLib == NULL )
				{
					Com_Printf( "failed\n" );
					Com_Printf( "QGL_Init: Can't load %s from /etc/ld.so.conf or current dir: %s\n", dllname, do_dlerror() );
					return qfalse;
				}
			}
			else
#endif
			{
				Com_Printf( "failed\n" );
				//Com_Printf( "QGL_Init: Can't load %s from /etc/ld.so.conf: %s\n", dllname, do_dlerror() );
				return qfalse;
			}
		}

		Com_Printf( "succeeded\n" );
	}

	glErrorCount = 0;

#define GLE( ret, name, ... ) q##name = GL_GetProcAddress( XSTRING( name ) ); if ( !q##name ) { Com_Printf( "Error resolving core X11 functions\n" ); return qfalse; }
	QGL_LinX11_PROCS;
#undef GLE

	return qtrue;
}
