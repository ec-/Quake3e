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

#define GLE( ret, name, ... ) ret ( APIENTRY * q##name )( __VA_ARGS__ );
QGL_Core_PROCS;
QGL_Win32_PROCS;
QGL_Ext_PROCS;
QGL_Swp_PROCS;
#undef GLE


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

#define GLE( ret, name, ... ) q##name = NULL;
	QGL_Core_PROCS;
	QGL_Ext_PROCS;
	QGL_Win32_PROCS;
	QGL_Swp_PROCS;
#undef GLE
}


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
	glw_state.OpenGLLib = Sys_LoadLibrary( va("%s.dll", dllname) );
#endif

	if ( glw_state.OpenGLLib == NULL )
	{
		ri.Printf( PRINT_ALL, "failed\n" );
		return qfalse;
	}

	ri.Printf( PRINT_ALL, "succeeded\n" );

	Sys_LoadFunctionErrors(); // reset error count

#define GLE( ret, name, ... ) q##name = Sys_LoadFunction( glw_state.OpenGLLib, XSTRING( name ) );
	QGL_Core_PROCS;

	if ( Sys_LoadFunctionErrors() ) 
	{
		ri.Printf( PRINT_ALL, "core OpenGL functions resolve error\n" );
		return qfalse;
	}

	QGL_Win32_PROCS;
#undef GLE

	if ( Sys_LoadFunctionErrors() ) 
	{
		ri.Printf( PRINT_ALL, "wgl functions resolve error\n" );
		return qfalse;
	}

#define GLE( ret, name, ... ) q##name = NULL;
	QGL_Swp_PROCS;
	QGL_Ext_PROCS;
#undef GLE

	return qtrue;
}
