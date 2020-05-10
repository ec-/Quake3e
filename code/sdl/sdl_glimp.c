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

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#ifdef USE_VULKAN_API
#	include "SDL_vulkan.h"
#endif
#else
#	include <SDL.h>
#ifdef USE_VULKAN_API
#	include <SDL_vulkan.h>
#endif
#endif

#include "../client/client.h"
#include "../renderercommon/tr_public.h"
#include "sdl_glw.h"
#include "sdl_icon.h"

typedef enum {
	RSERR_OK,
	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,
	RSERR_UNKNOWN
} rserr_t;

glwstate_t glw_state;

SDL_Window *SDL_window = NULL;
static SDL_GLContext SDL_glContext = NULL;
#ifdef USE_VULKAN_API
static PFN_vkGetInstanceProcAddr qvkGetInstanceProcAddr;
#endif

cvar_t *r_stereoEnabled;
cvar_t *in_nograb;

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( qboolean unloadDLL )
{
	IN_Shutdown();

	SDL_DestroyWindow( SDL_window );
	SDL_window = NULL;

	if ( unloadDLL )
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
}


/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void GLimp_Minimize( void )
{
	SDL_MinimizeWindow( SDL_window );
}


/*
===============
GLimp_LogComment
===============
*/
void GLimp_LogComment( char *comment )
{
}


static int FindNearestDisplay( int *x, int *y, int w, int h )
{
	const int cx = *x + w / 2;
	const int cy = *y + h / 2;
	int i, index, numDisplays;
	SDL_Rect *list, *m;

	index = -1; // selected display index

	numDisplays = SDL_GetNumVideoDisplays();
	if ( numDisplays <= 0 )
		return -1;

	list = Z_Malloc( numDisplays * sizeof( list[0] ) );

	for ( i = 0; i < numDisplays; i++ )
	{
		SDL_GetDisplayBounds( i, list + i );
		//Com_Printf( "[%i]: x=%i, y=%i, w=%i, h=%i\n", i, list[i].x, list[i].y, list[i].w, list[i].h );
	}

	// select display by window center intersection
	for ( i = 0; i < numDisplays; i++ )
	{
		m = list + i;
		if ( cx >= m->x && cx < (m->x + m->w) && cy >= m->y && cy < (m->y + m->h) )
		{
			index = i;
			break;
		}
	}

	// select display by nearest distance between window center and display center
	if ( index == -1 )
	{
		unsigned long nearest, dist;
		int dx, dy;
		nearest = ~0UL;
		for ( i = 0; i < numDisplays; i++ )
		{
			m = list + i;
			dx = (m->x + m->w/2) - cx;
			dy = (m->y + m->h/2) - cy;
			dist = ( dx * dx ) + ( dy * dy );
			if ( dist < nearest )
			{
				nearest = dist;
				index = i;
			}
		}
	}

	// adjust x and y coordinates if needed
	if ( index >= 0 )
	{
		m = list + index;
		if ( *x < m->x )
			*x = m->x;

		if ( *y < m->y )
			*y = m->y;
	}

	Z_Free( list );

	return index;
}


static SDL_HitTestResult SDL_HitTestFunc( SDL_Window *win, const SDL_Point *area, void *data )
{
	if ( Key_GetCatcher() & KEYCATCH_CONSOLE && keys[ K_ALT ].down )
		return SDL_HITTEST_DRAGGABLE;

	return SDL_HITTEST_NORMAL;
}


/*
===============
GLimp_SetMode
===============
*/
static int GLW_SetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan )
{
	glconfig_t *config = glw_state.config;
	int perChannelColorBits;
	int colorBits, depthBits, stencilBits;
	int samples;
	int i;
	SDL_Surface *icon = NULL;
	SDL_DisplayMode desktopMode;
	int display;
	int x;
	int y;
	Uint32 flags = SDL_WINDOW_SHOWN;
#ifdef USE_VULKAN_API
	if ( vulkan ) {
		flags |= SDL_WINDOW_VULKAN;
		Com_Printf( "Initializing Vulkan display\n");
	} else
#endif
	{
		flags |= SDL_WINDOW_OPENGL;
		Com_Printf( "Initializing OpenGL display\n");
	}

#ifdef USE_ICON
	icon = SDL_CreateRGBSurfaceFrom(
			(void *)CLIENT_WINDOW_ICON.pixel_data,
			CLIENT_WINDOW_ICON.width,
			CLIENT_WINDOW_ICON.height,
			CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
			CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width,
#ifdef Q3_LITTLE_ENDIAN
			0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
			0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
			);
#endif

	// If a window exists, note its display index
	if ( SDL_window != NULL )
	{
		display = SDL_GetWindowDisplayIndex( SDL_window );
		if ( display < 0 )
		{
			Com_DPrintf( "SDL_GetWindowDisplayIndex() failed: %s\n", SDL_GetError() );
		}
	}
	else
	{
		x = vid_xpos->integer;
		y = vid_ypos->integer;

		// find out to which display our window belongs to
		// according to previously stored \vid_xpos and \vid_ypos coordinates
		display = FindNearestDisplay( &x, &y, 640, 480 );

		//Com_Printf("Selected display: %i\n", display );
	}

	if ( display >= 0 && SDL_GetDesktopDisplayMode( display, &desktopMode ) == 0 )
	{
		glw_state.desktop_width = desktopMode.w;
		glw_state.desktop_height = desktopMode.h;
	}
	else
	{
		glw_state.desktop_width = 640;
		glw_state.desktop_height = 480;
	}

	Com_Printf( "...setting mode %d:", mode );

	if ( !CL_GetModeInfo( &config->vidWidth, &config->vidHeight, &config->windowAspect, mode, modeFS, glw_state.desktop_width, glw_state.desktop_height, fullscreen ) )
	{
		Com_Printf( " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}

	Com_Printf( " %d %d\n", config->vidWidth, config->vidHeight );

	// Destroy existing state if it exists
	if ( SDL_glContext != NULL )
	{
		SDL_GL_DeleteContext( SDL_glContext );
		SDL_glContext = NULL;
	}

	if ( SDL_window != NULL )
	{
		SDL_GetWindowPosition( SDL_window, &x, &y );
		Com_DPrintf( "Existing window at %dx%d before being destroyed\n", x, y );
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}

	if ( fullscreen )
	{
#ifdef _WIN32
		flags |= SDL_WINDOW_FULLSCREEN;
#else
		flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS;
#endif
	}
	else if ( r_noborder->integer )
	{
		flags |= SDL_WINDOW_BORDERLESS;
	}

	config->isFullscreen = fullscreen;
	colorBits = r_colorbits->value;

	if ( colorBits == 0 || colorBits >= 32 )
		colorBits = 24;

	if ( cl_depthbits->integer == 0 )
		depthBits = 24;
	else
		depthBits = cl_depthbits->integer;

	stencilBits = cl_stencilbits->integer;
	samples = 0; // r_ext_multisample->integer;

	for ( i = 0; i < 16; i++ )
	{
		int testColorBits, testDepthBits, testStencilBits;
		int realColorBits[3];

		// 0 - default
		// 1 - minus colorBits
		// 2 - minus depthBits
		// 3 - minus stencil
		if ((i % 4) == 0 && i)
		{
			// one pass, reduce
			switch (i / 4)
			{
				case 2 :
					if (colorBits == 24)
						colorBits = 16;
					break;
				case 1 :
					if (depthBits == 24)
						depthBits = 16;
					else if (depthBits == 16)
						depthBits = 8;
				case 3 :
					if (stencilBits == 24)
						stencilBits = 16;
					else if (stencilBits == 16)
						stencilBits = 8;
			}
		}

		testColorBits = colorBits;
		testDepthBits = depthBits;
		testStencilBits = stencilBits;

		if ((i % 4) == 3)
		{ // reduce colorBits
			if (testColorBits == 24)
				testColorBits = 16;
		}

		if ((i % 4) == 2)
		{ // reduce depthBits
			if (testDepthBits == 24)
				testDepthBits = 16;
			else if (testDepthBits == 16)
				testDepthBits = 8;
		}

		if ((i % 4) == 1)
		{ // reduce stencilBits
			if (testStencilBits == 24)
				testStencilBits = 16;
			else if (testStencilBits == 16)
				testStencilBits = 8;
			else
				testStencilBits = 0;
		}

		if ( testColorBits == 24 )
			perChannelColorBits = 8;
		else
			perChannelColorBits = 4;

#ifdef USE_VULKAN_API
		if ( !vulkan )
#endif
		{
	
#ifdef __sgi /* Fix for SGIs grabbing too many bits of color */
			if (perChannelColorBits == 4)
				perChannelColorBits = 0; /* Use minimum size for 16-bit color */

			/* Need alpha or else SGIs choose 36+ bit RGB mode */
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 1 );
#endif

			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, testDepthBits );
			SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, testStencilBits );

			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, samples ? 1 : 0 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, samples );

			if ( r_stereoEnabled->integer )
			{
				config->stereoEnabled = qtrue;
				SDL_GL_SetAttribute( SDL_GL_STEREO, 1 );
			}
			else
			{
				config->stereoEnabled = qfalse;
				SDL_GL_SetAttribute( SDL_GL_STEREO, 0 );
			}
		
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

#if 1		// if multisampling is enabled on X11, this causes create window to fail.
			// If not allowing software GL, demand accelerated
			if ( !r_allowSoftwareGL->integer )
				SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );
#endif
		}

		if ( ( SDL_window = SDL_CreateWindow( CLIENT_WINDOW_TITLE, x, y, config->vidWidth, config->vidHeight, flags ) ) == NULL )
		{
			Com_DPrintf( "SDL_CreateWindow failed: %s\n", SDL_GetError() );
			continue;
		}

		if ( fullscreen )
		{
			SDL_DisplayMode mode;

			switch ( testColorBits )
			{
				case 16: mode.format = SDL_PIXELFORMAT_RGB565; break;
				case 24: mode.format = SDL_PIXELFORMAT_RGB24;  break;
				default: Com_DPrintf( "testColorBits is %d, can't fullscreen\n", testColorBits ); continue;
			}

			mode.w = config->vidWidth;
			mode.h = config->vidHeight;
			mode.refresh_rate = /* config->displayFrequency = */ Cvar_VariableIntegerValue( "r_displayRefresh" );
			mode.driverdata = NULL;

			if ( SDL_SetWindowDisplayMode( SDL_window, &mode ) < 0 )
			{
				Com_DPrintf( "SDL_SetWindowDisplayMode failed: %s\n", SDL_GetError( ) );
				continue;
			}

			if ( SDL_GetWindowDisplayMode( SDL_window, &mode ) >= 0 )
			{
				config->displayFrequency = mode.refresh_rate;
				config->vidWidth = mode.w;
				config->vidHeight = mode.h;
			}
		}

		SDL_SetWindowIcon( SDL_window, icon );

		SDL_glContext = NULL;

#ifdef USE_VULKAN_API
		if ( vulkan )
		{
			config->colorBits = testColorBits;
			config->depthBits = testDepthBits;
			config->stencilBits = testStencilBits;
		}
		else
#endif
		{
			if ( !SDL_glContext )
			{
				if ( ( SDL_glContext = SDL_GL_CreateContext( SDL_window ) ) == NULL )
				{
					Com_DPrintf( "SDL_GL_CreateContext failed: %s\n", SDL_GetError( ) );
					SDL_DestroyWindow( SDL_window );
					SDL_window = NULL;
					continue;
				}
			}

			if ( SDL_GL_SetSwapInterval( r_swapInterval->integer ) == -1 )
			{
				Com_DPrintf( "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError( ) );
			}

			SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &realColorBits[0] );
			SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &realColorBits[1] );
			SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &realColorBits[2] );
			SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &config->depthBits );
			SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &config->stencilBits );

			config->colorBits = realColorBits[0] + realColorBits[1] + realColorBits[2];
		} // if ( !vulkan )


		Com_Printf( "Using %d color bits, %d depth, %d stencil display.\n",	config->colorBits, config->depthBits, config->stencilBits );

		break;
	}

	SDL_FreeSurface( icon );

	if ( !SDL_window )
	{
		Com_Printf( "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	if ( !config->isFullscreen && r_noborder->integer )
		SDL_SetWindowHitTest( SDL_window, SDL_HitTestFunc, NULL );

#ifdef USE_VULKAN_API
	if ( vulkan )
		SDL_Vulkan_GetDrawableSize( SDL_window, &config->vidWidth, &config->vidHeight );
	else
#endif
		SDL_GL_GetDrawableSize( SDL_window, &config->vidWidth, &config->vidHeight );

	glw_state.isFullscreen = config->isFullscreen;

	// save render dimensions as renderer may change it in advance
	glw_state.window_width = config->vidWidth;
	glw_state.window_height = config->vidHeight;

	return RSERR_OK;
}


/*
===============
GLimp_StartDriverAndSetMode
===============
*/
static qboolean GLimp_StartDriverAndSetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan )
{
	rserr_t err;

	if ( fullscreen && in_nograb->integer )
	{
		Com_Printf( "Fullscreen not allowed with \\in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		const char *driverName;

		if ( SDL_Init( SDL_INIT_VIDEO ) != 0 )
		{
			Com_Printf( "SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError() );
			return qfalse;
		}

		driverName = SDL_GetCurrentVideoDriver();

		Com_Printf( "SDL using driver \"%s\"\n", driverName );
	}

	err = GLW_SetMode( mode, modeFS, fullscreen, vulkan );

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			Com_Printf( "...WARNING: fullscreen unavailable in this mode\n" );
			return qfalse;
		case RSERR_INVALID_MODE:
			Com_Printf( "...WARNING: could not set the given mode (%d)\n", mode );
			return qfalse;
		default:
			break;
	}

	return qtrue;
}


/*
===============
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL
===============
*/
void GLimp_Init( glconfig_t *config )
{
#ifndef _WIN32
	InitSig();
#endif

	Com_DPrintf( "GLimp_Init()\n" );

	glw_state.config = config; // feedback renderer configuration

	in_nograb = Cvar_Get( "in_nograb", "0", CVAR_ARCHIVE );

	r_allowSoftwareGL = Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );

	r_swapInterval = Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_stereoEnabled = Cvar_Get( "r_stereoEnabled", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_ignorehwgamma = Cvar_Get( "r_ignorehwgamma", "0", CVAR_ARCHIVE | CVAR_LATCH );

	// Create the window and set up the context
	if ( !GLimp_StartDriverAndSetMode( r_mode->integer, r_modeFullscreen->string, r_fullscreen->integer, qfalse ) )
	{
		Com_Printf( "Setting r_mode %d failed, falling back on r_mode %d\n", r_mode->integer, 3 );
		if ( !GLimp_StartDriverAndSetMode( 3, "", r_fullscreen->integer, qfalse ) )
		{
			// Nothing worked, give up
			Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
			return;
		}
	}

	// These values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	glw_state.isFullscreen = config->isFullscreen;

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init();
}


/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame( void )
{
	// don't flip if drawing to front buffer
	if ( Q_stricmp( cl_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		SDL_GL_SwapWindow( SDL_window );
	}
}


/*
===============
GL_GetProcAddress

Used by opengl renderers to resolve all qgl* function pointers
===============
*/
void *GL_GetProcAddress( const char *symbol )
{
	return SDL_GL_GetProcAddress( symbol );
}


#ifdef USE_VULKAN_API
/*
===============
VKimp_Init

This routine is responsible for initializing the OS specific portions
of Vulkan
===============
*/
void VKimp_Init( glconfig_t *config )
{
#ifndef _WIN32
	InitSig();
#endif

	Com_DPrintf( "VKimp_Init()\n" );

	in_nograb = Cvar_Get( "in_nograb", "0", CVAR_ARCHIVE );

	r_swapInterval = Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_stereoEnabled = Cvar_Get( "r_stereoEnabled", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_ignorehwgamma = Cvar_Get( "r_ignorehwgamma", "0", CVAR_ARCHIVE | CVAR_LATCH );

	// feedback to renderer configuration
	glw_state.config = config;

	// Create the window and set up the context
	if ( !GLimp_StartDriverAndSetMode( r_mode->integer, r_modeFullscreen->string, r_fullscreen->integer, qtrue ) )
	{
		Com_Printf( "Setting r_mode %d failed, falling back on r_mode %d\n", r_mode->integer, 3 );
		if ( !GLimp_StartDriverAndSetMode( 3, "", r_fullscreen->integer, qtrue ) )
		{
			// Nothing worked, give up
			Com_Error( ERR_FATAL, "VKimp_Init() - could not load Vulkan subsystem" );
			return;
		}
	}

	qvkGetInstanceProcAddr = SDL_Vulkan_GetVkGetInstanceProcAddr();

	if ( qvkGetInstanceProcAddr == NULL )
	{
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		Com_Error( ERR_FATAL, "VKimp_Init: qvkGetInstanceProcAddr is NULL" );
	}

	// This values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init();
}


/*
===============
VK_GetInstanceProcAddr
===============
*/
void *VK_GetInstanceProcAddr( VkInstance instance, const char *name )
{
	return qvkGetInstanceProcAddr( instance, name );
}


/*
===============
VK_CreateSurface
===============
*/
qboolean VK_CreateSurface( VkInstance instance, VkSurfaceKHR *surface )
{
	if ( SDL_Vulkan_CreateSurface( SDL_window, instance, surface ) == SDL_TRUE )
		return qtrue;
	else
		return qfalse;
}


/*
===============
VKimp_Shutdown
===============
*/
void VKimp_Shutdown( qboolean unloadDLL )
{
	IN_Shutdown();

	SDL_DestroyWindow( SDL_window );
	SDL_window = NULL;

	if ( unloadDLL )
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
}
#endif // USE_VULKAN_API


/*
===============
Sys_GetClipboardData
===============
*/
char *Sys_GetClipboardData( void )
{
#ifdef DEDICATED
	return NULL;
#else
	char *data = NULL;
	char *cliptext;

	if ( ( cliptext = SDL_GetClipboardText() ) != NULL ) {
		if ( cliptext[0] != '\0' ) {
			size_t bufsize = strlen( cliptext ) + 1;

			data = Z_Malloc( bufsize );
			Q_strncpyz( data, cliptext, bufsize );

			// find first listed char and set to '\0'
			strtok( data, "\n\r\b" );
		}
		SDL_free( cliptext );
	}
	return data;
#endif
}


/*
===============
Sys_SetClipboardBitmap
===============
*/
void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{
#ifdef _WIN32
	HGLOBAL hMem;
	byte *ptr;

	if ( !OpenClipboard( NULL ) )
		return;

	EmptyClipboard();
	hMem = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, length );
	if ( hMem != NULL ) {
		ptr = ( byte* )GlobalLock( hMem );
		if ( ptr != NULL ) {
			memcpy( ptr, bitmap, length ); 
		}
		GlobalUnlock( hMem );
		SetClipboardData( CF_DIB, hMem );
	}
	CloseClipboard();
#endif
}
