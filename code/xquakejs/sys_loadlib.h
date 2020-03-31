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

#	ifdef USE_LOCAL_HEADERS
#		include "SDL.h"
#		include "SDL_loadso.h"
#	else
#		include <SDL.h>
#		include <SDL_loadso.h>
#	endif
extern void *Sys_LoadLibrary(const char *f);
extern void *Sys_UnloadLibrary(const char *h);
#	define Sys_LoadFunction(h,fn) SDL_LoadFunction(h,fn)
#	define Sys_LibraryError() SDL_GetError()

void IN_Init (void);
void IN_Frame (void);
void IN_Shutdown (void);
