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
** WIN_GLIMP.C
**
** This file contains ALL Win32 specific stuff having to do with the
** OpenGL refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_LogComment
** GLimp_Shutdown
**
** Note that the GLW_xxx functions are Windows specific GL-subsystem
** related functions that are relevant ONLY to win_glimp.c
*/

#include "../client/client.h"
#include "resource.h"
#include "win_local.h"
#include "glw_win.h"
#include "../renderer/qgl_linked.h"

typedef enum {
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
} rserr_t;

#define TRY_PFD_SUCCESS		0
#define TRY_PFD_FAIL_SOFT	1
#define TRY_PFD_FAIL_HARD	2

#ifndef PFD_SUPPORT_COMPOSITION
#define PFD_SUPPORT_COMPOSITION 0x00008000
#endif

static rserr_t	GLW_SetMode( const char *drivername,
							 int mode,
							 const char *modeFS,
							 int colorbits,
							 qboolean cdsFullscreen );

static qboolean s_classRegistered = qfalse;

//
// function declaration
//
qboolean QGL_Init( const char *dllname );
void     QGL_Shutdown( qboolean unloadDLL );

//
// variable declarations
//
glwstate_t glw_state;

// GLimp-specific cvars
static cvar_t *r_maskMinidriver;		// allow a different dll name to be treated as if it were opengl32.dll
static cvar_t *r_stereoEnabled;
static cvar_t *r_verbose;				// used for verbose debug spew
static cvar_t *r_noborder;

/*
** GLW_StartDriverAndSetMode
*/
static qboolean GLW_StartDriverAndSetMode( const char *drivername, 
										   int mode, 
										   const char *modeFS,
										   int colorbits,
										   qboolean cdsFullscreen )
{
	rserr_t err;

	err = GLW_SetMode( drivername, mode, modeFS, colorbits, cdsFullscreen );

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
** ChoosePFD
**
** Helper function that replaces ChoosePixelFormat.
*/
#define MAX_PFDS 256

static int GLW_ChoosePFD( HDC hDC, PIXELFORMATDESCRIPTOR *pPFD )
{
	PIXELFORMATDESCRIPTOR pfds[MAX_PFDS+1];
	int maxPFD, bestMatch;
	int i;

	Com_Printf( "...GLW_ChoosePFD( %d, %d, %d )\n", ( int ) pPFD->cColorBits, ( int ) pPFD->cDepthBits, ( int ) pPFD->cStencilBits );

	// count number of PFDs
	maxPFD = DescribePixelFormat( hDC, 1, sizeof( PIXELFORMATDESCRIPTOR ), &pfds[0] );

	if ( maxPFD > MAX_PFDS )
	{
		Com_Printf( "...numPFDs > MAX_PFDS (%d > %d)\n", maxPFD, MAX_PFDS );
		maxPFD = MAX_PFDS;
	}

	Com_Printf( "...%d PFDs found\n", maxPFD - 1 );

	// grab information
	for ( i = 1; i <= maxPFD; i++ )
	{
		DescribePixelFormat( hDC, i, sizeof( PIXELFORMATDESCRIPTOR ), &pfds[i] );
	}

__rescan:

	bestMatch = 0;

	// look for a best match
	for ( i = 1; i <= maxPFD; i++ )
	{
		//
		// make sure this has hardware acceleration
		//
		if ( ( pfds[i].dwFlags & PFD_GENERIC_FORMAT ) != 0 ) 
		{
			if ( !r_allowSoftwareGL->integer )
			{
				if ( r_verbose->integer )
				{
					Com_Printf( "...PFD %d rejected, software acceleration\n", i );
				}
				continue;
			}
		}

		// verify pixel type
		if ( pfds[i].iPixelType != PFD_TYPE_RGBA )
		{
			if ( r_verbose->integer )
			{
				Com_Printf( "...PFD %d rejected, not RGBA\n", i );
			}
			continue;
		}

		// verify proper flags
		if ( ( pfds[i].dwFlags & pPFD->dwFlags ) != pPFD->dwFlags ) 
		{
			if ( r_verbose->integer )
			{
				Com_Printf( "...PFD %d rejected, improper flags (%lx instead of %lx)\n", i, pfds[i].dwFlags, pPFD->dwFlags );
			}
			continue;
		}

		// verify enough bits
		if ( pfds[i].cDepthBits < 15 )
		{
			continue;
		}
		if ( ( pfds[i].cStencilBits < 4 ) && ( pPFD->cStencilBits > 0 ) )
		{
			continue;
		}

		//
		// selection criteria (in order of priority):
		// 
		//  PFD_STEREO
		//  colorBits
		//  depthBits
		//  stencilBits
		//
		if ( bestMatch )
		{
			// check stereo
			if ( ( pfds[i].dwFlags & PFD_STEREO ) && ( !( pfds[bestMatch].dwFlags & PFD_STEREO ) ) && ( pPFD->dwFlags & PFD_STEREO ) )
			{
				bestMatch = i;
				continue;
			}
			
			if ( !( pfds[i].dwFlags & PFD_STEREO ) && ( pfds[bestMatch].dwFlags & PFD_STEREO ) && ( pPFD->dwFlags & PFD_STEREO ) )
			{
				bestMatch = i;
				continue;
			}

			// check color
			if ( pfds[bestMatch].cColorBits != pPFD->cColorBits )
			{
				// prefer perfect match
				if ( pfds[i].cColorBits == pPFD->cColorBits )
				{
					bestMatch = i;
					continue;
				}
				// otherwise if this PFD has more bits than our best, use it
				else if ( pfds[i].cColorBits > pfds[bestMatch].cColorBits )
				{
					bestMatch = i;
					continue;
				}
			}

			// check depth
			if ( pfds[bestMatch].cDepthBits != pPFD->cDepthBits )
			{
				// prefer perfect match
				if ( pfds[i].cDepthBits == pPFD->cDepthBits )
				{
					bestMatch = i;
					continue;
				}
				// otherwise if this PFD has more bits than our best, use it
				else if ( pfds[i].cDepthBits > pfds[bestMatch].cDepthBits )
				{
					bestMatch = i;
					continue;
				}
			}

			// check stencil
			if ( pfds[bestMatch].cStencilBits != pPFD->cStencilBits )
			{
				// prefer perfect match
				if ( pfds[i].cStencilBits == pPFD->cStencilBits )
				{
					bestMatch = i;
					continue;
				}
				// otherwise if this PFD has more bits than our best, use it
				else if ( ( pfds[i].cStencilBits > pfds[bestMatch].cStencilBits ) && 
					 ( pPFD->cStencilBits > 0 ) )
				{
					bestMatch = i;
					continue;
				}
			}
		}
		else
		{
			bestMatch = i;
		}
	}
	
	if ( !bestMatch ) 
	{
		if ( pPFD->dwFlags & PFD_SUPPORT_COMPOSITION ) 
		{
			// this can be a problem if we are working via RDP for example
			pPFD->dwFlags &= ~PFD_SUPPORT_COMPOSITION;
			goto __rescan;
		}
		return 0;
	}

	if ( ( pfds[bestMatch].dwFlags & PFD_GENERIC_FORMAT ) != 0 )
	{
		if ( !r_allowSoftwareGL->integer )
		{
			Com_Printf( "...no hardware acceleration found\n" );
			return 0;
		}
		else
		{
			Com_Printf( "...using software emulation\n" );
		}
	}
	else if ( pfds[bestMatch].dwFlags & PFD_GENERIC_ACCELERATED )
	{
		Com_Printf( "...MCD acceleration found\n" );
	}
	else
	{
		Com_Printf( "...hardware acceleration found\n" );
	}

	*pPFD = pfds[bestMatch];

	return bestMatch;
}


/*
** void GLW_CreatePFD
**
** Helper function zeros out then fills in a PFD
*/
static void GLW_CreatePFD( PIXELFORMATDESCRIPTOR *pPFD, int colorbits, int depthbits, int stencilbits, qboolean stereo )
{
    PIXELFORMATDESCRIPTOR src = 
	{
		sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
		1,								// version number
		PFD_DRAW_TO_WINDOW |			// support window
		PFD_SUPPORT_OPENGL |			// support OpenGL
		PFD_DOUBLEBUFFER,				// double buffered
		PFD_TYPE_RGBA,					// RGBA type
		24,								// 24-bit color depth
		0, 0, 0, 0, 0, 0,				// color bits ignored
		0,								// no alpha buffer
		0,								// shift bit ignored
		0,								// no accumulation buffer
		0, 0, 0, 0, 					// accum bits ignored
		24,								// 24-bit z-buffer	
		8,								// 8-bit stencil buffer
		0,								// no auxiliary buffer
		PFD_MAIN_PLANE,					// main layer
		0,								// reserved
		0, 0, 0							// layer masks ignored
    };

	src.cColorBits = colorbits;
	src.cDepthBits = depthbits;
	src.cStencilBits = stencilbits;

	if ( !glw_state.cdsFullscreen )
	{
		src.dwFlags |= PFD_SUPPORT_COMPOSITION;
	}

	if ( stereo )
	{
		Com_Printf( "...attempting to use stereo\n" );
		src.dwFlags |= PFD_STEREO;
		glw_state.config->stereoEnabled = qtrue;
	}
	else
	{
		glw_state.config->stereoEnabled = qfalse;
	}

	*pPFD = src;
}


/*
** GLW_MakeContext
*/
static int GLW_MakeContext( PIXELFORMATDESCRIPTOR *pPFD )
{
	int pixelformat;

	//
	// don't putz around with pixelformat if it's already set (e.g. this is a soft
	// reset of the graphics system)
	//
	if ( !glw_state.pixelFormatSet )
	{
		//
		// choose, set, and describe our desired pixel format.  If we're
		// using a minidriver then we need to bypass the GDI functions,
		// otherwise use the GDI functions.
		//
		if ( ( pixelformat = GLW_ChoosePFD( glw_state.hDC, pPFD ) ) == 0 )
		{
			Com_Printf( "...GLW_ChoosePFD failed\n" );
			return TRY_PFD_FAIL_SOFT;
		}
		Com_Printf( "...PIXELFORMAT %d selected\n", pixelformat );

		DescribePixelFormat( glw_state.hDC, pixelformat, sizeof( *pPFD ), pPFD );

		if ( SetPixelFormat( glw_state.hDC, pixelformat, pPFD ) == FALSE )
		{
			Com_Printf( "...SetPixelFormat failed\n" );
			return TRY_PFD_FAIL_SOFT;
		}

		glw_state.pixelFormatSet = qtrue;
	}

	//
	// startup the OpenGL subsystem by creating a context and making it current
	//
	if ( !glw_state.hGLRC )
	{
		Com_Printf( "...creating GL context: " );
		if ( ( glw_state.hGLRC = qwglCreateContext( glw_state.hDC ) ) == 0 )
		{
			Com_Printf( "failed\n" );

			return TRY_PFD_FAIL_HARD;
		}
		Com_Printf( "succeeded\n" );

		Com_Printf( "...making context current: " );
		if ( !qwglMakeCurrent( glw_state.hDC, glw_state.hGLRC ) )
		{
			qwglDeleteContext( glw_state.hGLRC );
			glw_state.hGLRC = NULL;
			Com_Printf( "failed\n" );
			return TRY_PFD_FAIL_HARD;
		}
		Com_Printf( "succeeded\n" );
	}

	return TRY_PFD_SUCCESS;
}


/*
** GLW_InitDriver
**
** - get a DC if one doesn't exist
** - create an HGLRC if one doesn't exist
*/
static qboolean GLW_InitDriver( const char *drivername, int colorbits )
{
	int		tpfd;
	int		depthbits, stencilbits;
	static PIXELFORMATDESCRIPTOR pfd;	// save between frames since 'tr' gets cleared

	Com_Printf( "Initializing OpenGL driver\n" );

	//
	// get a DC for our window if we don't already have one allocated
	//
	if ( glw_state.hDC == NULL )
	{
		Com_Printf( "...getting DC: " );

		if ( ( glw_state.hDC = GetDC( g_wv.hWnd ) ) == NULL )
		{
			Com_Printf( "failed\n" );
			return qfalse;
		}
		Com_Printf( "succeeded\n" );
	}

	if ( colorbits == 0 )
	{
		colorbits = glw_state.desktopBitsPixel;
	}

	//
	// implicitly assume Z-buffer depth == desktop color depth
	//
	if ( r_depthbits->integer == 0 ) {
		if ( colorbits > 16 ) {
			depthbits = 24;
		} else {
			depthbits = 16;
		}
	} else {
		depthbits = r_depthbits->integer;
	}

	//
	// do not allow stencil if Z-buffer depth likely won't contain it
	//
	stencilbits = r_stencilbits->integer;
	if ( depthbits < 24 )
	{
		stencilbits = 0;
	}

	//
	// make two attempts to set the PIXELFORMAT
	//

	//
	// first attempt: r_colorbits, depthbits, and r_stencilbits
	//
	if ( !glw_state.pixelFormatSet )
	{
		GLW_CreatePFD( &pfd, colorbits, depthbits, stencilbits, r_stereoEnabled->integer != 0 );
		if ( ( tpfd = GLW_MakeContext( &pfd ) ) != TRY_PFD_SUCCESS )
		{
			if ( tpfd == TRY_PFD_FAIL_HARD )
			{
				Com_Printf( S_COLOR_YELLOW "...failed hard\n" );
				return qfalse;
			}

			//
			// punt if we've already tried the desktop bit depth and no stencil bits
			//
			if ( ( r_colorbits->integer == glw_state.desktopBitsPixel ) &&
				 ( stencilbits == 0 ) )
			{
				ReleaseDC( g_wv.hWnd, glw_state.hDC );
				glw_state.hDC = NULL;

				Com_Printf( "...failed to find an appropriate PIXELFORMAT\n" );

				return qfalse;
			}

			//
			// second attempt: desktop's color bits and no stencil
			//
			if ( colorbits > glw_state.desktopBitsPixel )
			{
				colorbits = glw_state.desktopBitsPixel;
			}
			GLW_CreatePFD( &pfd, colorbits, depthbits, 0, r_stereoEnabled->integer != 0 );
			if ( GLW_MakeContext( &pfd ) != TRY_PFD_SUCCESS )
			{
				if ( glw_state.hDC )
				{
					ReleaseDC( g_wv.hWnd, glw_state.hDC );
					glw_state.hDC = NULL;
				}

				Com_Printf( "...failed to find an appropriate PIXELFORMAT\n" );

				return qfalse;
			}
		}

		/*
		** report if stereo is desired but unavailable
		*/
		if ( !( pfd.dwFlags & PFD_STEREO ) && ( r_stereoEnabled->integer != 0 ) ) 
		{
			Com_Printf( "...failed to select stereo pixel format\n" );
			glw_state.config->stereoEnabled = qfalse;
		}
	}

	/*
	** store PFD specifics 
	*/

	glw_state.config->colorBits = ( int ) pfd.cColorBits;
	glw_state.config->depthBits = ( int ) pfd.cDepthBits;
	glw_state.config->stencilBits = ( int ) pfd.cStencilBits;

	return qtrue;
}


/*
** GLW_CreateWindow
**
** Responsible for creating the Win32 window and initializing the OpenGL driver.
*/
static qboolean GLW_CreateWindow( const char *drivername, int width, int height, int colorbits, qboolean cdsFullscreen )
{
	RECT			r;
	int				stylebits;
	int				x, y, w, h;
	int				exstyle;
	qboolean		oldFullscreen;

	//
	// register the window class if necessary
	//
	if ( !s_classRegistered )
	{
		WNDCLASS wc;

		memset( &wc, 0, sizeof( wc ) );

		wc.style         = 0;
		wc.lpfnWndProc   = (WNDPROC) glw_state.wndproc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = 0;
		wc.hInstance     = g_wv.hInstance;
		wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
		wc.hCursor       = LoadCursor( NULL, IDC_ARROW );
		wc.hbrBackground = (HBRUSH)(LRESULT)COLOR_GRAYTEXT;
		wc.lpszMenuName  = 0;
		wc.lpszClassName = T(CLIENT_WINDOW_TITLE);

		if ( !RegisterClass( &wc ) )
		{
			Com_Error( ERR_FATAL, "GLW_CreateWindow: could not register window class" );
			return qfalse;
		}
		s_classRegistered = qtrue;
		Com_Printf( "...registered window class\n" );
	}

	r.left = vid_xpos->integer;
	r.top = vid_ypos->integer;
	r.right = r.left + width;
	r.bottom = r.top + height;

	UpdateMonitorInfo( &r );
	
	//
	// create the HWND if one does not already exist
	//
	if ( !g_wv.hWnd )
	{
		//
		// compute width and height
		//
		//r.left = 0;
		//r.top = 0;
		//r.right  = width;
		//r.bottom = height;
		
		g_wv.borderless = 0;

		if ( cdsFullscreen )
		{
			exstyle = WINDOW_ESTYLE_FULLSCREEN;
			stylebits = WINDOW_STYLE_FULLSCREEN;
		}
		else
		{
			exstyle = WINDOW_ESTYLE_NORMAL;
			if ( r_noborder->integer ) {
				stylebits = WINDOW_STYLE_NORMAL_NB;
				g_wv.borderless = r_noborder->integer;
			} else {
				stylebits = WINDOW_STYLE_NORMAL;
			}
			AdjustWindowRect( &r, stylebits, FALSE );
		}

		w = r.right - r.left;
		h = r.bottom - r.top;

		// select monitor from window rect
		r.left = vid_xpos->integer;
		r.top = vid_ypos->integer;
		r.right = r.left + w;
		r.bottom = r.top + h;
		UpdateMonitorInfo( &r );

		if ( cdsFullscreen )
		{
			x = glw_state.desktopX;
			y = glw_state.desktopY;
		}
		else
		{
			x = vid_xpos->integer;
			y = vid_ypos->integer;

			// adjust window coordinates if necessary 
			// so that the window is completely on screen
			if ( w < glw_state.desktopWidth && (x + w) > glw_state.desktopWidth + glw_state.desktopX )
				x = ( glw_state.desktopWidth + glw_state.desktopX - w );
			if ( h < glw_state.desktopHeight && (y + h) > glw_state.desktopHeight + glw_state.desktopY )
				y = ( glw_state.desktopHeight + glw_state.desktopY - h );

			if ( x < glw_state.desktopX )
				x = glw_state.desktopX;
			if ( y < glw_state.desktopY )
				y = glw_state.desktopY;
		}

		stylebits &= ~WS_VISIBLE; // show window only after successive OpenGL initialization
			
		oldFullscreen = glw_state.cdsFullscreen;
		glw_state.cdsFullscreen = cdsFullscreen;

		g_wv.hWnd = CreateWindowEx( exstyle, TEXT(CLIENT_WINDOW_TITLE), TEXT(CLIENT_WINDOW_TITLE),
			 stylebits, x, y, w, h, NULL, NULL, g_wv.hInstance,  NULL );

		if ( !g_wv.hWnd )
		{
			glw_state.cdsFullscreen = oldFullscreen;
			Com_Error( ERR_FATAL, "GLW_CreateWindow() - Couldn't create window" );
			return qfalse;
		}

		Com_Printf( "...created window@%d,%d (%dx%d)\n", x, y, w, h );
	}
	else
	{
		Com_Printf( "...window already present, CreateWindowEx skipped\n" );
	}

	if ( !GLW_InitDriver( drivername, colorbits ) )
	{
		//ShowWindow( g_wv.hWnd, SW_HIDE );
		DestroyWindow( g_wv.hWnd );
		g_wv.hWnd = NULL;
		return qfalse;
	}

	//SetForegroundWindow( g_wv.hWnd );
	//SetFocus( g_wv.hWnd );

	//ShowWindow( g_wv.hWnd, SW_SHOW );
	//UpdateWindow( g_wv.hWnd );

	return qtrue;
}


static void PrintCDSError( int value )
{
	switch ( value )
	{
	case DISP_CHANGE_RESTART:
		Com_Printf( "restart required\n" );
		break;
	case DISP_CHANGE_BADPARAM:
		Com_Printf( "bad param\n" );
		break;
	case DISP_CHANGE_BADFLAGS:
		Com_Printf( "bad flags\n" );
		break;
	case DISP_CHANGE_FAILED:
		Com_Printf( "DISP_CHANGE_FAILED\n" );
		break;
	case DISP_CHANGE_BADMODE:
		Com_Printf( "bad mode\n" );
		break;
	case DISP_CHANGE_NOTUPDATED:
		Com_Printf( "not updated\n" );
		break;
	default:
		Com_Printf( "unknown error %d\n", value );
		break;
	}
}

static DEVMODE dm_desktop, dm_current;


static void ResetDisplaySettings( void )
{
	if ( glw_state.displayName[0] )
		ChangeDisplaySettingsEx( glw_state.displayName, NULL, NULL, 0, NULL );
	else
		ChangeDisplaySettingsEx( NULL, NULL, NULL, 0, NULL );
}


static LONG ApplyDisplaySettings( DEVMODE *dm )
{
	DEVMODE curr;
	LONG result;

	// Get current display mode on current monitor
	if ( !EnumDisplaySettings( glw_state.displayName, ENUM_CURRENT_SETTINGS, &curr ) )
		return DISP_CHANGE_FAILED;

	// Check if current resolution is the same as we want to set
	if ( curr.dmDisplayFrequency &&
		curr.dmPelsWidth == dm->dmPelsWidth &&
		curr.dmPelsHeight == dm->dmPelsHeight &&
		(curr.dmBitsPerPel == dm->dmBitsPerPel || dm->dmBitsPerPel == 0 ) &&
		(curr.dmDisplayFrequency == dm->dmDisplayFrequency || dm->dmDisplayFrequency ==0)) 
	{
		memcpy( &dm_current, &curr, sizeof( dm_current ) );
		return DISP_CHANGE_SUCCESSFUL; // simulate success
	}

	// Uninitialized?
	if ( dm->dmDisplayFrequency == 0 && dm->dmPelsWidth == 0 && 
		dm->dmPelsHeight == 0 && dm->dmBitsPerPel == 0 ) {
		if ( dm_desktop.dmPelsWidth && dm_desktop.dmPelsHeight ) {
			return ApplyDisplaySettings( &dm_desktop );
		}
	}

	// Apply requested mode
	result = ChangeDisplaySettingsEx( glw_state.displayName, dm, NULL, CDS_FULLSCREEN, NULL );
	if ( result == DISP_CHANGE_SUCCESSFUL ) {
		memcpy( &dm_current, dm, sizeof( dm_current ) );
	}

	return result;
}


void SetGameDisplaySettings( void ) 
{
	ApplyDisplaySettings( &dm_current );
}


void SetDesktopDisplaySettings( void ) 
{
	ResetDisplaySettings();
	memset( &dm_desktop, 0, sizeof( dm_desktop ) );
	dm_desktop.dmSize = sizeof( DEVMODE );
	if ( glw_state.displayName[0] )
		EnumDisplaySettings( glw_state.displayName, ENUM_CURRENT_SETTINGS, &dm_desktop );
	else
		EnumDisplaySettings( NULL, ENUM_CURRENT_SETTINGS, &dm_desktop );
}


void UpdateMonitorInfo( const RECT *target ) 
{
	MONITORINFOEX mInfo;
	DEVMODE	devMode;
	HMONITOR hMon;
	const RECT *Rect;
	int w, h, x ,y;

	glw_state.monitorCount = GetSystemMetrics( SM_CMONITORS );

	if ( target )
		Rect = target;
	else if ( g_wv.winRectValid )
		Rect = &g_wv.winRect;
	else
		Rect = &g_wv.conRect;

	// try to get more correct data
	hMon = MonitorFromRect( Rect, MONITOR_DEFAULTTONEAREST );
	memset( &mInfo, 0, sizeof( mInfo ) );
	mInfo.cbSize = sizeof( MONITORINFOEX );

	memset( &devMode, 0, sizeof( devMode ) );
	devMode.dmSize = sizeof( DEVMODE );

	if ( GetMonitorInfo( hMon, (LPMONITORINFO)&mInfo ) && EnumDisplaySettings( mInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode ) ) {
		w = mInfo.rcMonitor.right - mInfo.rcMonitor.left;
		h = mInfo.rcMonitor.bottom - mInfo.rcMonitor.top;
		x = mInfo.rcMonitor.left;
		y = mInfo.rcMonitor.top;

		// try to detect DPI scale
		// we can't properly handle it but at least detect monitor resolution 
		// and inform user in console
		if ( devMode.dmPelsWidth > w || devMode.dmPelsHeight > h ) {
			int scaleX, scaleY;
			scaleX = (devMode.dmPelsWidth * 100) / w;
			scaleY = (devMode.dmPelsHeight * 100) / h;
			if ( scaleX == scaleY ) {
				Com_Printf( S_COLOR_YELLOW "...detected DPI scale: %i%%\n", scaleX );
				w = devMode.dmPelsWidth;
				h = devMode.dmPelsHeight;
			}
		}

		if ( glw_state.desktopWidth != w || glw_state.desktopHeight != h || 
			glw_state.desktopX != x || glw_state.desktopY != y || 
			glw_state.hMonitor != hMon ) {
				// track monitor and gamma change
				qboolean gammaSet = glw_state.gammaSet;
				if ( gammaSet ) {
					GLW_RestoreGamma();
				}
				glw_state.desktopWidth = w;
				glw_state.desktopHeight = h;
				glw_state.desktopX = x;
				glw_state.desktopY = y;
				glw_state.hMonitor = hMon;
				memcpy( glw_state.displayName, mInfo.szDevice, sizeof( glw_state.displayName ) );

				glw_state.desktopBitsPixel = devMode.dmBitsPerPel;

				Com_Printf( "...current monitor: %ix%i@%i,%i %s\n", 
					w, h, x, y, WtoA( mInfo.szDevice ) );

				if ( gammaSet && re.SetColorMappings ) {
					re.SetColorMappings();
				}
		}
	} else {
		// no information about current monitor, get desktop settings
		HDC hDC = GetDC( GetDesktopWindow() );
		glw_state.desktopX = 0;
		glw_state.desktopY = 0;
		glw_state.desktopWidth = GetDeviceCaps( hDC, HORZRES );
		glw_state.desktopHeight = GetDeviceCaps( hDC, VERTRES );
		ReleaseDC( GetDesktopWindow(), hDC );
		glw_state.displayName[0] = '\0';
	}
}


/*
** GLW_SetMode
*/
static rserr_t GLW_SetMode( const char *drivername, int mode, const char *modeFS, int colorbits, qboolean cdsFullscreen )
{
	//HDC hDC;
	RECT r;
	const char *win_fs[] = { "W", "FS" };
	glconfig_t *config = glw_state.config;
	int		cdsRet;
	DEVMODE dm;

	vid_xpos = Cvar_Get( "vid_xpos", "3", CVAR_ARCHIVE );
	vid_ypos = Cvar_Get( "vid_ypos", "22", CVAR_ARCHIVE );

	r.left = vid_xpos->integer;
	r.top = vid_ypos->integer;
	r.right = r.left + 320;
	r.bottom = r.top + 240;

	UpdateMonitorInfo( &r );

	if ( dm_desktop.dmSize == 0 )
	{
		SetDesktopDisplaySettings();
	}

	//
	// print out informational messages
	//
	Com_Printf( "...setting mode %d:", mode );
	if ( !CL_GetModeInfo( &config->vidWidth, &config->vidHeight, &config->windowAspect,
		mode, modeFS, glw_state.desktopWidth, glw_state.desktopHeight, cdsFullscreen ) )
	{
		Com_Printf( " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}
	Com_Printf( " %d %d %s\n", config->vidWidth, config->vidHeight, win_fs[ cdsFullscreen ] );

	//
	// verify desktop bit depth
	//
	if ( glw_state.desktopBitsPixel < 15 || glw_state.desktopBitsPixel == 24 )
	{
		if ( colorbits == 0 || ( !cdsFullscreen && colorbits >= 15 ) )
		{
			if ( MessageBox( NULL,
						T("It is highly unlikely that a correct\n") \
						T("windowed display can be initialized with\n") \
						T("the current desktop display depth.  Select\n") \
						T("'OK' to try anyway.  Press 'Cancel' if you\n") \
						T("have a 3Dfx Voodoo, Voodoo-2, or Voodoo Rush\n") \
						T("3D accelerator installed, or if you otherwise\n") \
						T("wish to quit."),	T("Low Desktop Color Depth"),
						MB_OKCANCEL | MB_ICONEXCLAMATION ) != IDOK )
			{
				return RSERR_INVALID_MODE;
			}
		}
	}

	// do a CDS if needed
	if ( cdsFullscreen )
	{
		memset( &dm, 0, sizeof( dm ) );
		
		dm.dmSize = sizeof( dm );
		
		dm.dmPelsWidth  = config->vidWidth;
		dm.dmPelsHeight = config->vidHeight;
		dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

		if ( Cvar_VariableIntegerValue( "r_displayRefresh" ) )
		{
			dm.dmDisplayFrequency = Cvar_VariableIntegerValue( "r_displayRefresh" );
			dm.dmFields |= DM_DISPLAYFREQUENCY;
		}
		else // try to set at least desktop refresh rate?
		if ( (dm_desktop.dmDisplayFrequency 
				&& dm.dmPelsWidth <= dm_desktop.dmPelsWidth 
				&& dm.dmPelsHeight <= dm_desktop.dmPelsWidth) 
				|| (dm_current.dmDisplayFrequency 
				&& dm.dmPelsWidth <= dm_current.dmPelsWidth 
				&& dm.dmPelsHeight <= dm_current.dmPelsWidth)) {
			//dm.dmDisplayFrequency = dm_desktop.dmDisplayFrequency;
			//dm.dmFields |= DM_DISPLAYFREQUENCY;
			//Com_Printf("...using display refresh rate: %iHz\n", 
			//	dm_desktop.dmDisplayFrequency );
		}
		
		// try to change color depth if possible
		if ( colorbits != 0 )
		{
			dm.dmBitsPerPel = colorbits;
			dm.dmFields |= DM_BITSPERPEL;
			Com_Printf( "...using colorsbits of %d\n", colorbits );
		}
		else
		{
			Com_Printf( "...using desktop display depth of %d\n", glw_state.desktopBitsPixel );
		}

		//
		// if we're already in fullscreen then just create the window
		//
		if ( glw_state.cdsFullscreen )
		{
			Com_Printf( "...already fullscreen, avoiding redundant CDS\n" );

			if ( !GLW_CreateWindow( drivername, config->vidWidth, config->vidHeight, colorbits, qtrue ) )
			{
				Com_Printf( "...restoring display settings\n" );
				ResetDisplaySettings();
				return RSERR_INVALID_MODE;
			}
		}
		//
		// need to call CDS
		//
		else
		{
			Com_Printf( "...calling CDS: " );
			
			// try setting the exact mode requested, because some drivers don't report
			// the low res modes in EnumDisplaySettings, but still work
			if ( ( cdsRet = ApplyDisplaySettings( &dm ) ) == DISP_CHANGE_SUCCESSFUL )
			{
				Com_Printf( "ok\n" );

				if ( !GLW_CreateWindow( drivername, config->vidWidth, config->vidHeight, colorbits, qtrue) )
				{
					Com_Printf( "...restoring display settings\n" );
					ResetDisplaySettings();
					return RSERR_INVALID_MODE;
				}
			}
			else
			{
				//
				// the exact mode failed, so scan EnumDisplaySettings for the next largest mode
				//
				DEVMODE		devmode;
				int			modeNum;

				Com_Printf( "failed, " );
				
				PrintCDSError( cdsRet );
			
				Com_Printf( "...trying next higher resolution:" );
				
				// we could do a better matching job here...
				for ( modeNum = 0 ; ; modeNum++ ) {
					if ( !EnumDisplaySettings( glw_state.displayName, modeNum, &devmode ) ) {
						modeNum = -1;
						break;
					}
					if ( devmode.dmPelsWidth >= config->vidWidth 
						&& devmode.dmPelsHeight >= config->vidHeight
						&& devmode.dmBitsPerPel >= 15 ) {
						break;
					}
				}

				if ( modeNum != -1 && ( cdsRet = ApplyDisplaySettings( &devmode ) ) == DISP_CHANGE_SUCCESSFUL )
				{
					Com_Printf( " ok\n" );
					if ( !GLW_CreateWindow( drivername, config->vidWidth, config->vidHeight, colorbits, qtrue) )
					{
						Com_Printf( "...restoring display settings\n" );
						ResetDisplaySettings();
						return RSERR_INVALID_MODE;
					}
				}
				else
				{
					Com_Printf( " failed, " );
					
					PrintCDSError( cdsRet );
					
					Com_Printf( "...restoring display settings\n" );
					ResetDisplaySettings();
					
					glw_state.config->isFullscreen = qfalse;
					if ( !GLW_CreateWindow( drivername, config->vidWidth, config->vidHeight, colorbits, qfalse) )
					{
						return RSERR_INVALID_MODE;
					}
					return RSERR_INVALID_FULLSCREEN;
				}
			}
		}
	}
	else // !cdsFullscreen
	{
		if ( glw_state.cdsFullscreen )
		{
			ResetDisplaySettings();
		}

		if ( !GLW_CreateWindow( drivername, config->vidWidth, config->vidHeight, colorbits, qfalse ) )
		{
			return RSERR_INVALID_MODE;
		}
	}

	//
	// success, now check display frequency, although this won't be valid on Voodoo(2)
	//
	memset( &dm, 0, sizeof( dm ) );
	dm.dmSize = sizeof( dm );
	if ( EnumDisplaySettings( glw_state.displayName, ENUM_CURRENT_SETTINGS, &dm ) ) 
	{
		glw_state.config->displayFrequency = dm.dmDisplayFrequency;
	}

	// NOTE: this is overridden later on standalone 3Dfx drivers
	glw_state.config->isFullscreen = cdsFullscreen;

	return RSERR_OK;
}


/*
** GLW_LoadOpenGL
**
** GLimp_win.c internal function that attempts to load and use 
** a specific OpenGL DLL.
*/
static qboolean GLW_LoadOpenGL( const char *drivername )
{
	char buffer[ 256 ];
	qboolean cdsFullscreen;

	glconfig_t *config = glw_state.config;

	Q_strncpyz( buffer, drivername, sizeof( buffer ) );
	Q_strlwr( buffer );

	if ( Q_stricmp( buffer, OPENGL_DRIVER_NAME ) == 0 || r_maskMinidriver->integer )
	{
		config->driverType = GLDRV_ICD;
	}
	else
	{
		config->driverType = GLDRV_STANDALONE;
		Com_Printf( "...assuming '%s' is a standalone driver\n", drivername );
	}

	//
	// load the driver and bind our function pointers to it
	// 
	if ( QGL_Init( buffer ) ) 
	{
		cdsFullscreen = (r_fullscreen->integer != 0);

		// create the window and set up the context
		if ( !GLW_StartDriverAndSetMode( drivername, r_mode->integer, r_modeFullscreen->string, r_colorbits->integer, cdsFullscreen ) )
		{
			// if we're on a 24/32-bit desktop try it again but with a 16-bit desktop
			if ( r_colorbits->integer != 16 || cdsFullscreen != qtrue || r_mode->integer != 3 )
			{
				if ( !GLW_StartDriverAndSetMode( drivername, 3, "", 16, qtrue ) )
				{
					goto fail;
				}
			}
		}
		return qtrue;
	}
fail:

	QGL_Shutdown( qtrue );

	return qfalse;
}


static void GLimp_SwapBuffers( void ) 
{
	if ( !SwapBuffers( glw_state.hDC ) ) 
	{
		Com_Error( ERR_FATAL, "GLimp_EndFrame() - SwapBuffers() failed!\n" );
	}
}


/*
** GLimp_EndFrame
*/
void GLimp_EndFrame( void )
{
	//
	// swapinterval stuff
	//
	if ( r_swapInterval->modified ) {
		r_swapInterval->modified = qfalse;

		//if ( !glConfig.stereoEnabled ) {	// why?
			if ( qwglSwapIntervalEXT ) {
				qwglSwapIntervalEXT( r_swapInterval->integer );
			}
		//}
	}

	// don't flip if drawing to front buffer
	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 ) {
		GLimp_SwapBuffers();
	}
}


static qboolean GLW_StartOpenGL( void )
{
	//
	// load and initialize the specific OpenGL driver
	//
	if ( !GLW_LoadOpenGL( r_glDriver->string ) )
	{
		if ( Q_stricmp( r_glDriver->string, OPENGL_DRIVER_NAME ) != 0 ) 
		{
			// try default driver
			if ( GLW_LoadOpenGL( OPENGL_DRIVER_NAME ) ) 
			{
				Cvar_Set( "r_glDriver", OPENGL_DRIVER_NAME );
				r_glDriver->modified = qfalse;
				return qtrue;
			}
		}

		Com_Error( ERR_FATAL, "GLW_StartOpenGL() - could not load OpenGL subsystem\n" );
		return qfalse;
	}

	return qtrue;
}


/*
** GLimp_Init
**
** This is the platform specific OpenGL initialization function.  It
** is responsible for loading OpenGL, initializing it, setting
** extensions, creating a window of the appropriate size, doing
** fullscreen manipulations, etc.  Its overall responsibility is
** to make sure that a functional OpenGL subsystem is operating
** when it returns to the ref.
*/
void GLimp_Init( glconfig_t *config )
{
	Com_Printf( "Initializing OpenGL subsystem\n" );

	// glimp-specific

	r_maskMinidriver = Cvar_Get( "r_maskMinidriver", "0", CVAR_LATCH );
	r_stereoEnabled = Cvar_Get( "r_stereoEnabled", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	r_verbose = Cvar_Get( "r_verbose", "0", 0 );
	r_noborder = Cvar_Get( "r_noborder", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( r_noborder, "0", "1", CV_INTEGER );

	// feedback to renderer configuration
	glw_state.config = config;

	// load appropriate DLL and initialize subsystem
	if ( !GLW_StartOpenGL() )
		return;

	//glConfig.driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	// optional
#define GLE( ret, name, ... ) q##name = GL_GetProcAddress( XSTRING( name ) )
	QGL_Swp_PROCS;
#undef GLE

	if ( qwglSwapIntervalEXT ) {
		Com_Printf( "...using WGL_EXT_swap_control\n" );
		r_swapInterval->modified = qtrue; // force a set next frame
	} else {
		Com_Printf( "...WGL_EXT_swap_control not found\n" );
	}

	// show main window after all initializations
	ShowWindow( g_wv.hWnd, SW_SHOW );
}


/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.
*/
void GLimp_Shutdown( qboolean unloadDLL )
{
	const char *success[] = { "failed", "success" };
	int retVal;

	// FIXME: Brian, we need better fallbacks from partially initialized failures
	if ( !qwglMakeCurrent ) {
		return;
	}

	Com_Printf( "Shutting down OpenGL subsystem\n" );

	// restore gamma.  We do this first because 3Dfx's extension needs a valid OGL subsystem
	if ( glw_state.gammaSet ) {
		GLW_RestoreGamma();
		glw_state.gammaSet = qfalse;
	}

	// set current context to NULL
	if ( qwglMakeCurrent )
	{
		retVal = qwglMakeCurrent( NULL, NULL ) != 0;

		Com_Printf( "...wglMakeCurrent( NULL, NULL ): %s\n", success[retVal] );
	}

	// delete HGLRC
	if ( glw_state.hGLRC )
	{
		retVal = qwglDeleteContext( glw_state.hGLRC ) != 0;
		Com_Printf( "...deleting GL context: %s\n", success[retVal] );
		glw_state.hGLRC = NULL;
	}

	// release DC
	if ( glw_state.hDC )
	{
		retVal = ReleaseDC( g_wv.hWnd, glw_state.hDC ) != 0;
		Com_Printf( "...releasing DC: %s\n", success[retVal] );
		glw_state.hDC   = NULL;
	}

	// destroy window
	if ( g_wv.hWnd )
	{
		Com_Printf( "...destroying window\n" );
		ShowWindow( g_wv.hWnd, SW_HIDE );
		DestroyWindow( g_wv.hWnd );
		g_wv.hWnd = NULL;
		glw_state.pixelFormatSet = qfalse;
	}

	// reset display settings
	if ( glw_state.cdsFullscreen )
	{
		Com_Printf( "...resetting display\n" );
		ResetDisplaySettings();
		glw_state.cdsFullscreen = qfalse;
	}

	// shutdown QGL subsystem
	QGL_Shutdown( unloadDLL );
}
