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
** QVK_WIN.C
**
** This file implements the operating system binding of Vulkan to QVulkan function
** pointers.  When doing a port of Quake3 you must implement the following
** two functions:
**
** QVK_Init() - loads libraries, assigns function pointers, etc.
** QVK_Shutdown() - unloads libraries, NULLs function pointers
*/
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/qgl.h"
#include "../renderercommon/tr_types.h"
#include "glw_win.h"
#include "win_local.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include "../renderercommon/vulkan/vulkan.h"

static PFN_vkGetInstanceProcAddr qvkGetInstanceProcAddr;
static PFN_vkCreateWin32SurfaceKHR qvkCreateWin32SurfaceKHR;

/*
** QVK_Shutdown
**
** Unloads the specified DLL then nulls out all the proc pointers.  This
** is only called during a hard shutdown of the Vulkan subsystem (e.g. vid_restart).
*/
void QVK_Shutdown( qboolean unloadDLL )
{
	Com_Printf( "...shutting down QVK\n" );

	if ( glw_state.VulkanLib && unloadDLL )
	{
		Com_Printf( "...unloading Vulkan DLL\n" );
		Sys_UnloadLibrary( glw_state.VulkanLib );
		glw_state.VulkanLib = NULL;
	}
	
	qvkGetInstanceProcAddr = NULL;
	qvkCreateWin32SurfaceKHR = NULL;
}


void *VK_GetInstanceProcAddr( VkInstance instance, const char *name )
{
	return qvkGetInstanceProcAddr( instance, name );
}


qboolean VK_CreateSurface( VkInstance instance, VkSurfaceKHR *pSurface )
{
	VkWin32SurfaceCreateInfoKHR desc;

	qvkCreateWin32SurfaceKHR = /*(PFN_vkCreateWin32SurfaceKHR)*/ VK_GetInstanceProcAddr( instance, "vkCreateWin32SurfaceKHR" );
	if ( !qvkCreateWin32SurfaceKHR )
		return qfalse;

	desc.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.hinstance = GetModuleHandle( NULL );
	desc.hwnd = g_wv.hWnd;

	if ( qvkCreateWin32SurfaceKHR( instance, &desc, NULL, pSurface ) == VK_SUCCESS )
		return qtrue;
	else
		return qfalse;
} 


/*
** QVK_Init
**
** This is responsible for binding our qvk function pointers to 
** the appropriate Vulkan stuff.  In Windows this means doing a 
** LoadLibrary and a bunch of calls to GetProcAddress.  On other
** operating systems we need to do the right thing, whatever that
** might be.
*/
qboolean QVK_Init( const char *dllname )
{
	char libName[1024];
#if 0
	char systemDir[1024];

#ifdef UNICODE
	TCHAR buffer[1024];
	GetSystemDirectory( buffer, ARRAYSIZE( buffer ) );
	strcpy( systemDir, WtoA( buffer ) );
#else
	GetSystemDirectory( systemDir, sizeof( systemDir ) );
#endif
#endif

	Com_Printf( "...initializing QVK\n" );

	if ( glw_state.VulkanLib == NULL )
	{
		// NOTE: this assumes that 'dllname' is lower case (and it should be)!
		Q_strncpyz( libName, dllname, sizeof( libName ) );
		Q_strcat( libName, sizeof( libName ), ".dll" );
		glw_state.VulkanLib = Sys_LoadLibrary( libName );
		if ( glw_state.VulkanLib == NULL )
		{
			Com_Printf( "...loading '%s' : " S_COLOR_YELLOW "failed\n", libName );
			return qfalse;
		}

		// get exact loaded module name
		GetModuleFileNameA( glw_state.VulkanLib, libName, sizeof( libName ) );
		Com_Printf( "...loading '%s' : succeeded\n", libName );

	}

	qvkGetInstanceProcAddr = /*(PFN_vkGetInstanceProcAddr)*/ Sys_LoadFunction( glw_state.VulkanLib, "vkGetInstanceProcAddr" );
	if ( qvkGetInstanceProcAddr == NULL )
	{

		Sys_UnloadLibrary( glw_state.VulkanLib );
		glw_state.VulkanLib = NULL;

		Com_Printf( "...loading '%s' : " S_COLOR_YELLOW "failed\n", libName );
		return qfalse;
	}

	Sys_LoadFunctionErrors(); // reset error count

	return qtrue;
}
