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
// win_local.h: Win32-specific Quake3 header file

#define RAW_INPUT

#ifdef RAW_INPUT

#ifndef HID_USAGE_GENERIC_MOUSE
#define HID_USAGE_GENERIC_MOUSE        ((USHORT) 0x02)
#endif

#ifndef HID_USAGE_PAGE_GENERIC
#define HID_USAGE_PAGE_GENERIC         ((USHORT) 0x01)
#endif

#endif

#if defined (_MSC_VER) && (_MSC_VER >= 1200)
#pragma warning(disable : 4201)
#pragma warning( push )
#endif
#include <windows.h>
#if defined (_MSC_VER) && (_MSC_VER >= 1200)
#pragma warning( pop )
#endif

#define HK_MOD_ALT		0x00100
#define HK_MOD_CONTROL  0x00200
#define HK_MOD_SHIFT	0x00400
#define HK_MOD_WIN		0x00800
#define HK_MOD_MASK		0x00F00
#define HK_MOD_LALT		0x01000
#define HK_MOD_RALT		0x02000
#define HK_MOD_LCONTROL	0x04000
#define HK_MOD_RCONTROL	0x08000
#define HK_MOD_LSHIFT	0x10000
#define HK_MOD_RSHIFT	0x20000
#define HK_MOD_LWIN		0x40000
#define HK_MOD_RWIN		0x80000
#define HK_MOD_XMASK	0xFF000

#define	DIRECTSOUND_VERSION	0x0300
#define	DIRECTINPUT_VERSION	0x0300

#include <mmsystem.h>
#include <dinput.h>
#include <dsound.h>
#include <shlobj.h>

#undef open
#define open _open
#undef close
#define close _close
#undef write
#define write _write

#ifndef MK_XBUTTON1
#define MK_XBUTTON1         0x0020
#endif
#ifndef MK_XBUTTON2
#define MK_XBUTTON2         0x0040
#endif

#define	WINDOW_STYLE_NORMAL          (WS_VISIBLE|WS_CLIPCHILDREN|WS_SYSMENU|WS_CAPTION|WS_MINIMIZEBOX|WS_OVERLAPPED|WS_BORDER)
#define	WINDOW_STYLE_NORMAL_NB       (WS_VISIBLE|WS_POPUP)
#define	WINDOW_ESTYLE_NORMAL         (0)
#define	WINDOW_STYLE_FULLSCREEN      (WS_VISIBLE|WS_CLIPCHILDREN|WS_POPUP)
#define	WINDOW_ESTYLE_FULLSCREEN     (WS_EX_TOPMOST)
#define	WINDOW_STYLE_FULLSCREEN_MIN  (WS_VISIBLE|WS_CLIPCHILDREN)
#define	WINDOW_ESTYLE_FULLSCREEN_MIN (0)

#define T TEXT
#ifdef UNICODE
LPWSTR AtoW( const char *s );
const char *WtoA( const LPWSTR s ); 
#else
#define AtoW(S) (S)
#define WtoA(S) (S)
#endif


void	IN_Win32MouseEvent( int x, int y, int mstate );
void	IN_RawMouseEvent( LPARAM lParam );

void	Sys_CreateConsole( const char *title, int xPos, int yPos, qboolean usePos );
void	Sys_DestroyConsole( void );

// Input subsystem

void	IN_Init (void);
void	IN_Shutdown (void);
void	IN_JoystickCommands (void);

void	IN_Activate( qboolean active );
void	IN_Frame( void );

void	IN_UpdateWindow( RECT *window_rect, qboolean updateClipRegion );
void	UpdateMonitorInfo( const RECT *target );

// window procedure
LRESULT WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM  wParam, LPARAM  lParam );

void Conbuf_AppendText( const char *msg );
void Conbuf_BeginPrint( void );
void Conbuf_EndPrint( void );

void SNDDMA_Activate( void );
qboolean SNDDMA_InitDS( void );

typedef struct
{
	HWND			hWnd;
	HINSTANCE		hInstance;
	int				borderless;

	// when we get a windows message, we store the time off so keyboard processing
	// can know the exact time of an event
	unsigned		sysMsgTime;

	// Multi-monitor tracking
	RECT			conRect;
	RECT			winRect;
	qboolean		winRectValid;

	int	raw_mx;
	int raw_my;

	POINT mouse;
	
} WinVars_t;

extern WinVars_t	g_wv;

void WIN_DisableHook( void  );
void WIN_EnableHook( void  );

void WIN_DisableAltTab( void );
void WIN_EnableAltTab( void );
