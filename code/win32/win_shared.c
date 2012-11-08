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

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>

/*
================
Sys_Milliseconds
================
*/
int			sys_timeBase;
int Sys_Milliseconds (void)
{
	int			sys_curtime;
	static qboolean	initialized = qfalse;

	if (!initialized) {
		sys_timeBase = timeGetTime();
		initialized = qtrue;
	}
	sys_curtime = timeGetTime() - sys_timeBase;

	return sys_curtime;
}


qboolean Sys_RandomBytes( byte *string, int len )
{
	HCRYPTPROV  prov;

	if( !CryptAcquireContext( &prov, NULL, NULL,
		PROV_RSA_FULL, CRYPT_VERIFYCONTEXT ) )  {

		return qfalse;
	}

	if( !CryptGenRandom( prov, len, (BYTE *)string ) )  {
		CryptReleaseContext( prov, 0 );
		return qfalse;
	}
	CryptReleaseContext( prov, 0 );
	return qtrue;
}


#ifdef UNICODE

LPWSTR AtoW( const char *s ) 
{
	static WCHAR buffer[MAXPRINTMSG*2];
	MultiByteToWideChar( CP_OEMCP, 0, s, strlen( s ) + 1, (LPWSTR) buffer, ARRAYSIZE( buffer ) );
	return buffer;
}

const char *WtoA( const LPWSTR s ) 
{
	static char buffer[MAXPRINTMSG*2];
	WideCharToMultiByte( CP_OEMCP, 0, s, -1, buffer, ARRAYSIZE( buffer ), NULL, NULL );
	return buffer;
}

#endif

//============================================

char *Sys_GetCurrentUser( void )
{
	static char s_userName[256];

	TCHAR buffer[256];
	DWORD size = ARRAYSIZE( buffer );

	if ( !GetUserName( buffer, &size ) ) {
		strcpy( s_userName, "player" );
	} else {
		strcpy( s_userName, WtoA( buffer ) );
	}

	if ( !s_userName[0] )
	{
		strcpy( s_userName, "player" );
	}

	return s_userName;
}

char	*Sys_DefaultHomePath(void) {
#ifdef USE_PROFILES
	TCHAR szPath[MAX_PATH];
	static char path[MAX_OSPATH];
	FARPROC qSHGetFolderPath;
	HMODULE shfolder = LoadLibrary("shfolder.dll");
	
	if(shfolder == NULL) {
		Com_Printf("Unable to load SHFolder.dll\n");
		return NULL;
	}

	qSHGetFolderPath = GetProcAddress(shfolder, "SHGetFolderPathA");
	if(qSHGetFolderPath == NULL)
	{
		Com_Printf("Unable to find SHGetFolderPath in SHFolder.dll\n");
		FreeLibrary(shfolder);
		return NULL;
	}

	if( !SUCCEEDED( qSHGetFolderPath( NULL, CSIDL_APPDATA,
		NULL, 0, szPath ) ) )
	{
		Com_Printf("Unable to detect CSIDL_APPDATA\n");
		FreeLibrary(shfolder);
		return NULL;
	}
	Q_strncpyz( path, szPath, sizeof(path) );
	Q_strcat( path, sizeof(path), "\\Quake3" );
	FreeLibrary(shfolder);
	if( !CreateDirectory( path, NULL ) )
	{
		if( GetLastError() != ERROR_ALREADY_EXISTS )
		{
			Com_Printf("Unable to create directory \"%s\"\n", path);
			return NULL;
		}
	}
	return path;
#else
    return NULL;
#endif
}

char *Sys_DefaultInstallPath(void)
{
	return Sys_Cwd();
}

