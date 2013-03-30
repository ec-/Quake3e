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

#include "../client/client.h"
#include "win_local.h"
#include "glw_win.h"
#include "../renderer/tr_local.h"

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL (WM_MOUSELAST+1)  // message that will be supported by the OS 
#endif

static UINT MSH_MOUSEWHEEL;

// Console variables that we need to access from this module
cvar_t		*vid_xpos;			// X coordinate of window position
cvar_t		*vid_ypos;			// Y coordinate of window position
cvar_t		*r_fullscreen;

#define VID_NUM_MODES ( sizeof( vid_modes ) / sizeof( vid_modes[0] ) )

static HHOOK WinHook;

static LRESULT CALLBACK WinKeyHook( int code, WPARAM wParam, LPARAM lParam )
{
	PKBDLLHOOKSTRUCT key = (PKBDLLHOOKSTRUCT)lParam;
	switch( wParam )
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if ( key->vkCode == VK_LWIN || key->vkCode == VK_RWIN ) { 
			Sys_QueEvent( 0, SE_KEY, K_SUPER, qtrue, 0, NULL );
			return 1;
		}
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if ( key->vkCode == VK_LWIN || key->vkCode == VK_RWIN ) { 
			Sys_QueEvent( 0, SE_KEY, K_SUPER, qfalse, 0, NULL );
			return 1;
		}
  }
  return CallNextHookEx( NULL, code, wParam, lParam );
}

void WIN_DisableHook( void  ) 
{
	if ( WinHook ) {
		UnhookWindowsHookEx( WinHook );
		WinHook = NULL;
	}
}

void WIN_EnableHook( void  ) 
{
	if ( !WinHook ) {
		WinHook = SetWindowsHookEx( WH_KEYBOARD_LL, WinKeyHook, g_wv.hInstance, 0 );
	}
}

static qboolean s_alttab_disabled;

void WIN_DisableAltTab( void )
{
	BOOL old;

	if ( s_alttab_disabled )
		return;

	if ( !Q_stricmp( Cvar_VariableString( "arch" ), "winnt" ) )
		RegisterHotKey( NULL, 0, MOD_ALT, VK_TAB );
	else
		SystemParametersInfo( SPI_SETSCREENSAVERRUNNING, 1, &old, 0 );

	s_alttab_disabled = qtrue;
}

void WIN_EnableAltTab( void )
{
	BOOL old;

	if ( !s_alttab_disabled )
		return;

	if ( !Q_stricmp( Cvar_VariableString( "arch" ), "winnt" ) ) 
		UnregisterHotKey( NULL, 0 );
	else 
		SystemParametersInfo( SPI_SETSCREENSAVERRUNNING, 0, &old, 0 );

	s_alttab_disabled = qfalse;
}

/*
==================
VID_AppActivate
==================
*/
static void VID_AppActivate( BOOL fActive, BOOL minimize )
{
	g_wv.isMinimized = minimize;

	Com_DPrintf( "VID_AppActivate: %i %i\n", fActive, minimize );

	Key_ClearStates();	// FIXME!!!

	// we don't want to act like we're active if we're minimized
	if ( fActive && !g_wv.isMinimized )
	{
		g_wv.activeApp = qtrue;
	}
	else
	{
		g_wv.activeApp = qfalse;
	}

	// minimize/restore mouse-capture on demand
	if ( !g_wv.activeApp )
	{
		WIN_DisableHook();
		IN_Activate( qfalse );
	}
	else
	{
		WIN_EnableHook();
		IN_Activate( qtrue );
	}
}

//==========================================================================

static int s_scantokey[ 128 ] = 
{ 
//  0        1       2       3       4       5       6       7 
//  8        9       A       B       C       D       E       F 
	0  , K_ESCAPE,  '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',K_BACKSPACE, K_TAB, // 0 
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',  K_ENTER, K_CTRL,	'a',	's',	// 1 
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'',	'`',  K_SHIFT,  '\\',   'z',    'x',    'c',    'v',	// 2 
	'b',    'n',    'm',    ',',    '.',    '/',  K_SHIFT,  '*', 
	K_ALT,  ' ',K_CAPSLOCK, K_F1,   K_F2,   K_F3,   K_F4,  K_F5,    // 3 
	K_F6, K_F7,  K_F8,   K_F9,  K_F10, K_PAUSE, K_SCROLLOCK, K_HOME, 
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW,K_KP_PLUS,K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL, 0,      0,      0,    K_F11, 
	K_F12,  0  ,    0  ,    0  ,    0  ,  K_MENU,   0  ,    0,     // 5
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,     // 6 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0      // 7 
}; 

/*
=======
MapKey

Map from windows to quake keynums
=======
*/
static int MapKey( int key )
{
	int result;
	int modified;
	qboolean is_extended;

	modified = ( key >> 16 ) & 255;

	//Com_Printf( "key: 0x%08x modified:%i extended:%i scan:%i\n", 
	//	key, modified, key & ( 1 << 24 )?1:0, s_scantokey[modified] );

	if ( modified > 127 )
		return 0;

	if ( key & ( 1 << 24 ) )
	{
		is_extended = qtrue;
	}
	else
	{
		is_extended = qfalse;
	}

	result = s_scantokey[modified];

	if ( !is_extended )
	{
		switch ( result )
		{
		case K_HOME:
			return K_KP_HOME;
		case K_UPARROW:
			return K_KP_UPARROW;
		case K_PGUP:
			return K_KP_PGUP;
		case K_LEFTARROW:
			return K_KP_LEFTARROW;
		case K_RIGHTARROW:
			return K_KP_RIGHTARROW;
		case K_END:
			return K_KP_END;
		case K_DOWNARROW:
			return K_KP_DOWNARROW;
		case K_PGDN:
			return K_KP_PGDN;
		case K_INS:
			return K_KP_INS;
		case K_DEL:
			return K_KP_DEL;
		case '`':
			return K_CONSOLE;
		default:
			return result;
		}
	}
	else
	{
		switch ( result )
		{
		case K_PAUSE:
			return K_KP_NUMLOCK;
		case 0x0D:
			return K_KP_ENTER;
		case 0x2F:
			return K_KP_SLASH;
		case 0xAF:
			return K_KP_PLUS;
		case '`':
			return K_CONSOLE;
		}
		return result;
	}
}


/*
====================
MainWndProc

main window procedure
====================
*/
extern cvar_t *in_mouse;
extern cvar_t *in_logitechbug;
extern cvar_t *in_minimize;

// raw input externals
extern UINT (WINAPI *GRID)(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
extern int raw_activated;

int			HotKey = 0;
int			hkinstalled = 0;

extern void	WG_RestoreGamma( void );
extern void	R_SetColorMappings( void );
extern void	SetGameDisplaySettings( void );
extern void SetDesktopDisplaySettings( void );

void Win_AddHotkey( ) {
	UINT modifiers, vk;
	ATOM atom;

	if ( !HotKey || !g_wv.hWnd || hkinstalled )
		return;

	modifiers = 0;
	vk = 0;

	if ( HotKey & HK_MOD_ALT )		modifiers |= MOD_ALT;
	if ( HotKey & HK_MOD_CONTROL )	modifiers |= MOD_CONTROL;
	if ( HotKey & HK_MOD_SHIFT )	modifiers |= MOD_SHIFT;
	if ( HotKey & HK_MOD_WIN )		modifiers |= MOD_WIN;

	vk = HotKey & 0xFF;

	atom = GlobalAddAtom( TEXT( "Q3MinimizeHotkey" ) );
	if ( !RegisterHotKey( g_wv.hWnd, atom, modifiers, vk ) ) {
		GlobalDeleteAtom( atom );
		return;
	}
	hkinstalled = 1;
}

void Win_RemoveHotkey( void ) {
	ATOM atom;

	if ( !g_wv.hWnd || !hkinstalled )
		return;

	atom = GlobalFindAtom( TEXT( "Q3MinimizeHotkey" ) );
	if ( atom ) {
		UnregisterHotKey( g_wv.hWnd, atom );
 		GlobalDeleteAtom( atom );
		hkinstalled = 0;
	}
}

BOOL Win_CheckHotkeyMod( void ) {

	if ( !(HotKey & HK_MOD_XMASK) )
 		return TRUE;

 	if ((HotKey&HK_MOD_LALT) && !GetAsyncKeyState(VK_LMENU)) return FALSE;
 	if ((HotKey&HK_MOD_RALT) && !GetAsyncKeyState(VK_RMENU)) return FALSE;
 	if ((HotKey&HK_MOD_LSHIFT) && !GetAsyncKeyState(VK_LSHIFT)) return FALSE;
 	if ((HotKey&HK_MOD_RSHIFT) && !GetAsyncKeyState(VK_RSHIFT)) return FALSE;
 	if ((HotKey&HK_MOD_LCONTROL) && !GetAsyncKeyState(VK_LCONTROL)) return FALSE;
 	if ((HotKey&HK_MOD_RCONTROL) && !GetAsyncKeyState(VK_RCONTROL)) return FALSE;
 	if ((HotKey&HK_MOD_LWIN) && !GetAsyncKeyState(VK_LWIN)) return FALSE;
 	if ((HotKey&HK_MOD_RWIN) && !GetAsyncKeyState(VK_RWIN)) return FALSE;

 	return TRUE;
}


LRESULT WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM  wParam, LPARAM lParam )
{
	static qboolean flip = qtrue;
	int zDelta, i;
	BOOL fActive;
	BOOL fMinimized;

	// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/winui/winui/windowsuserinterface/userinput/mouseinput/aboutmouseinput.asp
	// Windows 95, Windows NT 3.51 - uses MSH_MOUSEWHEEL
	// only relevant for non-DI input
	//
	// NOTE: not sure how reliable this is anymore, might trigger double wheel events
	if (in_mouse->integer == -1)
	{
		if ( uMsg == MSH_MOUSEWHEEL )
		{
			if ( ( ( int ) wParam ) > 0 )
			{
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
			}
			else
			{
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
			}
			return DefWindowProc( hWnd, uMsg, wParam, lParam );
		}
	}

	switch (uMsg)
	{
	case WM_MOUSEWHEEL:
		// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/winui/winui/windowsuserinterface/userinput/mouseinput/aboutmouseinput.asp
		// Windows 98/Me, Windows NT 4.0 and later - uses WM_MOUSEWHEEL
		// only relevant for non-DI input and when console is toggled in window mode
		//   if console is toggled in window mode (KEYCATCH_CONSOLE) then mouse is released and DI doesn't see any mouse wheel
		if (in_mouse->integer == -1 || (!glw_state.cdsFullscreen && (Key_GetCatcher( ) & KEYCATCH_CONSOLE)))
		{
			// 120 increments, might be 240 and multiples if wheel goes too fast
			// NOTE Logitech: logitech drivers are screwed and send the message twice?
			//   could add a cvar to interpret the message as successive press/release events
			zDelta = ( short ) HIWORD( wParam ) / WHEEL_DELTA;
			if ( zDelta > 0 )
			{
				for(i=0; i<zDelta; i++)
				{
					if (!in_logitechbug->integer)
					{
						Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
						Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
					}
					else
					{
						Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, flip, 0, NULL );
						flip = !flip;
					}
				}
			}
			else
			{
				for(i=0; i<-zDelta; i++)
				{
					if (!in_logitechbug->integer)
					{
						Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
						Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
					}
					else
					{
						Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, flip, 0, NULL );
						flip = !flip;
					}
				}
			}
			// when an application processes the WM_MOUSEWHEEL message, it must return zero
			return 0;
		}
		break;

	case WM_CREATE:

		g_wv.hWnd = hWnd;

		vid_xpos = Cvar_Get ("vid_xpos", "3", CVAR_ARCHIVE);
		vid_ypos = Cvar_Get ("vid_ypos", "22", CVAR_ARCHIVE);
		r_fullscreen = Cvar_Get ("r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );

		MSH_MOUSEWHEEL = RegisterWindowMessage( TEXT( "MSWHEEL_ROLLMSG" ) ); 

		if ( glw_state.cdsFullscreen ) {
			WIN_DisableAltTab();
		} else {
			WIN_EnableAltTab();
		}

		WIN_EnableHook();

		GetWindowRect( hWnd, &g_wv.winRect );
		g_wv.winRectValid = qtrue;

		break;
#if 0
	case WM_DISPLAYCHANGE:
		Com_DPrintf( "WM_DISPLAYCHANGE\n" );
		// we need to force a vid_restart if the user has changed
		// their desktop resolution while the game is running,
		// but don't do anything if the message is a result of
		// our own calling of ChangeDisplaySettings
		if ( com_insideVidInit ) {
			break;		// we did this on purpose
		}
		// something else forced a mode change, so restart all our gl stuff
		Cbuf_AddText( "vid_restart\n" );
		break;
#endif
	case WM_DESTROY:
		// let sound and input know about this?
		Win_RemoveHotkey();
		g_wv.hWnd = NULL;
		g_wv.isMinimized = qfalse;
		WIN_EnableAltTab();
		break;

	case WM_CLOSE:
		Cbuf_ExecuteText( EXEC_APPEND, "quit" );
		break;

	case WM_ACTIVATE:
		fActive = (LOWORD( wParam ) != WA_INACTIVE) ? TRUE : FALSE;
		fMinimized = (BOOL)HIWORD( wParam ) ? TRUE : FALSE;
		// sometimes we can recieve fActive with fMinimized
		if ( !( fActive && fMinimized )  )
			S_MuteClient( fMinimized );
		break;
	
	// WM_KILLFOCUS goes first and without correct window status
	// WM_SETFOCUS goes (almost) last with correct window status
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		{
			WINDOWPLACEMENT wp;
		
			memset( &wp, 0, sizeof( wp ) );
			wp.length = sizeof( WINDOWPLACEMENT );
			GetWindowPlacement( hWnd, &wp );

			fActive = ( uMsg == WM_SETFOCUS );
			//Com_DPrintf( "%s\n", fActive ? "WM_SETFOCUS" : "WM_KILLFOCUS" );

			Win_AddHotkey();

			// We can't get correct minimized status on WM_KILLFOCUS
			VID_AppActivate( fActive, FALSE ); 

			if ( glw_state.cdsFullscreen ) {
				if ( fActive ) {
					SetGameDisplaySettings();
					R_SetColorMappings();
				} else {
					WG_RestoreGamma();
					// Minimize if there only one monitor
					if ( glw_state.monitorCount <= 1 )
						ShowWindow( hWnd, SW_MINIMIZE );
					SetDesktopDisplaySettings();
				}
			} else {
				if ( fActive ) {
					R_SetColorMappings();
				} else {
					WG_RestoreGamma();
				}
			}

			if ( fActive ) {
				WIN_DisableAltTab();
			} else {
				WIN_EnableAltTab();
			}

			SNDDMA_Activate();
		}
		break;

	case WM_MOVE:
		{
			RECT	r;

			GetWindowRect( hWnd, &g_wv.winRect );
			g_wv.winRectValid = qtrue;
			UpdateMonitorInfo();
			IN_UpdateWindow( &r, qtrue );

			if ( !glw_state.cdsFullscreen )
			{
				Cvar_SetValue( "vid_xpos", r.left );
				Cvar_SetValue( "vid_ypos", r.top );

				vid_xpos->modified = qfalse;
				vid_ypos->modified = qfalse;
			}

			if ( g_wv.activeApp ) 
			{
				IN_Activate( qtrue );
			}

		}
		break;

// this is complicated because Win32 seems to pack multiple mouse events into
// one update sometimes, so we always check all states and look for events
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		{
			int	temp;

            if ( raw_activated ) 
                break;

			temp = (wParam & (MK_LBUTTON|MK_RBUTTON)) + ((wParam & (MK_MBUTTON|MK_XBUTTON1|MK_XBUTTON2)) >> 2);

			IN_MouseEvent( temp );
		}
		break;

	case WM_INPUT:
        {
			union {
				BYTE lpb[40];
				RAWINPUT raw;
			} u;

			UINT dwSize;
			UINT err;
			short data;

			if ( !raw_activated /* || !s_wmv.mouseInitialized */ )
                break;

			dwSize = sizeof( u.raw );

			err = GRID( (HRAWINPUT)lParam, RID_INPUT, &u.raw, &dwSize, sizeof( RAWINPUTHEADER ) );
			if ( err == -1 )
				break;

			if ( u.raw.header.dwType != RIM_TYPEMOUSE || u.raw.data.mouse.usFlags != MOUSE_MOVE_RELATIVE )
				break;

			if ( u.raw.data.mouse.lLastX || u.raw.data.mouse.lLastY )
				Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, u.raw.data.mouse.lLastX,
					u.raw.data.mouse.lLastY, 0, NULL );

			if ( !u.raw.data.mouse.usButtonFlags )
				break;

			#define CHECK_RAW_BUTTON(button) \
				if ( u.raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_##button##_DOWN ) \
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button##, qtrue, 0, NULL ); \
				if ( u.raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_##button##_UP ) \
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button##, qfalse, 0, NULL )

			CHECK_RAW_BUTTON(1);
			CHECK_RAW_BUTTON(2);
			CHECK_RAW_BUTTON(3);
			CHECK_RAW_BUTTON(4);
			CHECK_RAW_BUTTON(5);

			if ( !(u.raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) )
				break;

			data = u.raw.data.mouse.usButtonData;

			data = data / 120;
			if ( data > 0 )
			{
				while( data > 0 )
				{
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
					data--;
				}
			}
			else
			{
				while( data < 0 )
				{
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
					data++;
				}
           }
		}
		break;

	case WM_SYSCOMMAND:
		if ( wParam == SC_SCREENSAVE )
			return 0;
		break;

	case WM_HOTKEY:
		// check for left/right modifiers
		if ( Win_CheckHotkeyMod() )
		{
			if ( g_wv.activeApp )
			{
				ShowWindow( hWnd, SW_MINIMIZE );
			}
			else
			{
				SetForegroundWindow( hWnd );
				SetFocus( hWnd );
				ShowWindow( hWnd, SW_RESTORE );
			}
		}
		break;

	case WM_SYSKEYDOWN:
		if ( wParam == VK_RETURN )
		{
			Cvar_SetValue( "r_fullscreen", glw_state.cdsFullscreen? 0 : 1 );
				Cbuf_AddText( "vid_restart\n" );
			return 0;
		}
		// fall through
	case WM_KEYDOWN:
		//Com_Printf( "^2k+^7 %08x\n", lParam );
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, MapKey( lParam ), qtrue, 0, NULL );
		break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
		//Com_Printf( "^5k-^7 %08x\n", lParam );
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, MapKey( lParam ), qfalse, 0, NULL );
		break;

	case WM_CHAR:
		if ( (lParam & 0xFF0000) != 0x290000 /* '`' */ )
		Sys_QueEvent( g_wv.sysMsgTime, SE_CHAR, wParam, 0, 0, NULL );
		break;

	case WM_SIZE:
		GetWindowRect( hWnd, &g_wv.winRect );
		g_wv.winRectValid = qtrue;
		UpdateMonitorInfo();
		IN_UpdateWindow( NULL, qtrue );
		break;

#if 0 // looks like people have troubles with it
	case WM_SIZE:

		if ( LOWORD(lParam) > 0 && HIWORD(lParam) > 0 )
		if ( LOWORD(lParam) != glConfig.vidWidth || glConfig.vidHeight != HIWORD(lParam) ) {
			glConfig.vidWidth = LOWORD(lParam);
			glConfig.vidHeight = HIWORD(lParam);
			if ( r_customPixelAspect )
				glConfig.windowAspect = (float)glConfig.vidWidth / ( glConfig.vidHeight * r_customPixelAspect->value );
			else
				glConfig.windowAspect = (float)glConfig.vidWidth / glConfig.vidHeight;
			Cvar_Set( "r_customwidth", va( "%i", glConfig.vidWidth ) );
			Cvar_Set( "r_customheight", va( "%i", glConfig.vidHeight ) );
			Cvar_Set( "r_mode", "-1" );
			memcpy( &cls.glconfig, &glConfig, sizeof( cls.glconfig ) );
			g_consoleField.widthInChars = cls.glconfig.vidWidth / SMALLCHAR_WIDTH - 2;
			Con_CheckResize();
		}
		break;
#endif

	case WM_ERASEBKGND: 
		// avoid GDI clearing the OpenGL window background in Vista/7
		if ( g_wv.osversion.dwMajorVersion >= 6 )
			return 1;
	}

    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

