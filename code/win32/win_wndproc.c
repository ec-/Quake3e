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

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL (WM_MOUSELAST+1)  // message that will be supported by the OS 
#endif

static UINT MSH_MOUSEWHEEL;

// Console variables that we need to access from this module
cvar_t		*vid_xpos;			// X coordinate of window position
cvar_t		*vid_ypos;			// Y coordinate of window position
cvar_t		*in_forceCharset;

static HHOOK WinHook;

/*
==================
WinKeyHook
==================
*/
static LRESULT CALLBACK WinKeyHook( int code, WPARAM wParam, LPARAM lParam )
{
	PKBDLLHOOKSTRUCT key = (PKBDLLHOOKSTRUCT)lParam;
	switch( wParam )
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if ( ( key->vkCode == VK_LWIN || key->vkCode == VK_RWIN ) && !(Key_GetCatcher() & KEYCATCH_CONSOLE) ) {
			Sys_QueEvent( 0, SE_KEY, K_SUPER, qtrue, 0, NULL );
			return 1;
		}
		if ( key->vkCode == VK_SNAPSHOT ) {
			Sys_QueEvent( 0, SE_KEY, K_PRINT, qtrue, 0, NULL );
			return 1;
		}
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if ( ( key->vkCode == VK_LWIN || key->vkCode == VK_RWIN ) && !(Key_GetCatcher() & KEYCATCH_CONSOLE) ) {
			Sys_QueEvent( 0, SE_KEY, K_SUPER, qfalse, 0, NULL );
			return 1;
		}
		if ( key->vkCode == VK_SNAPSHOT ) {
			Sys_QueEvent( 0, SE_KEY, K_PRINT, qfalse, 0, NULL );
			return 1;
		}
	}
	return CallNextHookEx( NULL, code, wParam, lParam );
}


/*
==================
WIN_DisableHook
==================
*/
void WIN_DisableHook( void  ) 
{
	if ( WinHook ) {
		UnhookWindowsHookEx( WinHook );
		WinHook = NULL;
	}
}


/*
==================
WIN_EnableHook
==================
*/
void WIN_EnableHook( void  ) 
{
	if ( !WinHook )
	{
		WinHook = SetWindowsHookEx( WH_KEYBOARD_LL, WinKeyHook, g_wv.hInstance, 0 );
	}
}


static qboolean s_alttab_disabled;

/*
==================
WIN_DisableAltTab
==================
*/
void WIN_DisableAltTab( void )
{
	BOOL old;

	if ( s_alttab_disabled )
		return;

#if 0
	if ( g_wv.hWnd && glw_state.cdsFullscreen && glw_state.monitorCount > 1 ) {
		// topmost window
		SetWindowLong( g_wv.hWnd, GWL_EXSTYLE, WINDOW_ESTYLE_FULLSCREEN );
		SetWindowLong( g_wv.hWnd, GWL_STYLE, WINDOW_STYLE_FULLSCREEN );
	}
#endif

	if ( !Q_stricmp( Cvar_VariableString( "arch" ), "winnt" ) )
		RegisterHotKey( NULL, 0, MOD_ALT, VK_TAB );
	else
		SystemParametersInfo( SPI_SETSCREENSAVERRUNNING, 1, &old, 0 );

	s_alttab_disabled = qtrue;
}


/*
==================
WIN_EnableAltTab
==================
*/
void WIN_EnableAltTab( void )
{
	BOOL old;

	if ( !s_alttab_disabled )
		return;

#if 0
	if ( g_wv.hWnd && glw_state.cdsFullscreen && glw_state.monitorCount > 1 ) {
		// allow moving other windows on foreground
		SetWindowLong( g_wv.hWnd, GWL_EXSTYLE, WINDOW_ESTYLE_NORMAL );
		SetWindowLong( g_wv.hWnd, GWL_STYLE, WINDOW_STYLE_FULLSCREEN_MIN );
		SetWindowPos( g_wv.hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
	}
#endif

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
static void VID_AppActivate( BOOL fActive )
{
	Com_DPrintf( "VID_AppActivate: %i %i\n", fActive, gw_minimized );

	Key_ClearStates();	// FIXME!!!

	// we don't want to act like we're active if we're minimized
	if ( fActive && !gw_minimized )
		gw_active = qtrue;
	else
		gw_active = qfalse;

	// minimize/restore mouse-capture on demand
	IN_Activate( gw_active );

	if ( !gw_active )
		WIN_DisableHook();
	else
		WIN_EnableHook();
}

//==========================================================================

static const int s_scantokey[ 128 ] = 
{ 
//	0        1       2       3       4       5       6       7 
//	8        9       A       B       C       D       E       F 
	0  , K_ESCAPE,  '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',K_BACKSPACE,K_TAB,  // 0 
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',  K_ENTER, K_CTRL,	'a',	's',	// 1 
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'',K_CONSOLE,K_SHIFT, '\\',   'z',    'x',    'c',    'v',	// 2 
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
==================
MapKey

Map from windows to quake keynums
==================
*/
static int MapKey( int nVirtKey, int key )
{
	int result;
	int modified;
	qboolean is_extended;

	modified = ( key >> 16 ) & 255;

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

	//Com_Printf( "key: 0x%08x modified:%i extended:%i result:%i(%02x) vk=%i\n",
	//	key, modified, is_extended, result, result, nVirtKey );

	if ( !is_extended )
	{
		switch ( result )
		{
		case K_HOME:
			return K_KP_HOME;
		case K_UPARROW:
			if ( Key_GetCatcher() && nVirtKey == VK_NUMPAD8 )
				return 0;
			return K_KP_UPARROW;
		case K_DOWNARROW:
			if ( Key_GetCatcher() && nVirtKey == VK_NUMPAD2 )
				return 0;
			return K_KP_DOWNARROW;
		case K_PGUP:
			return K_KP_PGUP;
		case K_LEFTARROW:
			return K_KP_LEFTARROW;
		case K_RIGHTARROW:
			return K_KP_RIGHTARROW;
		case K_END:
			return K_KP_END;
		case K_PGDN:
			return K_KP_PGDN;
		case K_INS:
			return K_KP_INS;
		case K_DEL:
			return K_KP_DEL;
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
		case K_ENTER:
			return K_KP_ENTER;
		case '/':
			return K_KP_SLASH;
		case 0xAF:
			return K_KP_PLUS;
		case '*':
			return K_KP_STAR;
		}
		return result;
	}
}


static qboolean directMap( const WPARAM chr ) {

	if ( !in_forceCharset->integer )
		return qtrue;

	switch ( chr ) // edit control sequences
	{
		case 'c'-'a'+1:
		case 'v'-'a'+1:
		case 'h'-'a'+1:
		case 'a'-'a'+1:
		case 'e'-'a'+1:
		case 0xC: // CTRL+L
			return qtrue;
	}

	if ( chr < ' ' || chr > 127 || in_forceCharset->integer > 1 )
		return qfalse;
	else
		return qtrue;
}


/*
==================
MapChar

Map input to ASCII charset
==================
*/
static int MapChar( WPARAM wParam, byte scancode ) 
{
	static const int s_scantochar[ 128 ] = 
	{ 
//	0        1       2       3       4       5       6       7 
//	8        9       A       B       C       D       E       F 
 	 0,      0,     '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',    0x8,    0x9,	// 0
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',    0xD,     0,     'a',    's',	// 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'',    0,      0,     '\\',   'z',    'x',    'c',    'v',	// 2
	'b',    'n',    'm',    ',',    '.',    '/',     0,     '*', 
	 0,     ' ',     0,      0,      0,      0,      0,      0,     // 3

	 0,      0,     '!',    '@',    '#',    '$',    '%',    '^', 
	'&',    '*',    '(',    ')',    '_',    '+',    0x8,    0x9,	// 4
	'Q',    'W',    'E',    'R',    'T',    'Y',    'U',    'I', 
	'O',    'P',    '{',    '}',    0xD,     0,     'A',    'S',	// 5
	'D',    'F',    'G',    'H',    'J',    'K',    'L',    ':',
	'"',     0,      0,     '|',    'Z',    'X',    'C',    'V',	// 6
	'B',    'N',    'M',    '<',    '>',    '?',     0,     '*', 
 	 0,     ' ',     0,      0,      0,      0,      0,      0,     // 7
	}; 

	if ( scancode == 0x53 )
		return '.';

	if ( directMap( wParam ) || scancode > 0x39 )
	{
		return wParam;
	}
	else 
	{
		char ch = s_scantochar[ scancode ];
		int shift = (GetKeyState( VK_SHIFT ) >> 15) & 1;
		if ( ch >= 'a' && ch <= 'z' ) 
		{
			int  capital = GetKeyState( VK_CAPITAL ) & 1;
			if ( capital ^ shift ) 
			{
				ch = ch - 'a' + 'A';
			}
		} 
		else 
		{
			ch = s_scantochar[ scancode | (shift<<6) ];
		}

		return ch;
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

int			HotKey = 0;
int			hkinstalled = 0;

extern void SetGameDisplaySettings( void );
extern void SetDesktopDisplaySettings( void );

void Win_AddHotkey( void ) 
{
	UINT modifiers, vk;
	ATOM atom;

	if ( !HotKey || !g_wv.hWnd || hkinstalled )
		return;

	modifiers = 0;

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


void Win_RemoveHotkey( void ) 
{
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


static int GetTimerMsec( void ) {
	int msec;
	
	if ( gw_minimized || CL_VideoRecording() )
		return 0;

	if ( com_maxfps->integer > 0 ) {
		msec = 1000 / com_maxfps->integer;
		if ( msec < 1 )
			msec = 1;
	} else {
		msec = 16; // 62.5fps
	}

	return msec;
}


LRESULT WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM  wParam, LPARAM lParam )
{
	#define TIMER_ID 10
	#define TIMER_ID_1 11
	static UINT uTimerID;
	static UINT uTimerID_1;
	static qboolean flip = qtrue;
	int zDelta, i;
	static BOOL fActive = FALSE;
	static BOOL fMinimized = FALSE;

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
		if ( in_mouse->integer || (!glw_state.cdsFullscreen && (Key_GetCatcher() & KEYCATCH_CONSOLE)) )
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

		MSH_MOUSEWHEEL = RegisterWindowMessage( TEXT( "MSWHEEL_ROLLMSG" ) ); 

		WIN_EnableHook();

		g_wv.hWnd = hWnd;
		GetWindowRect( hWnd, &g_wv.winRect );
		g_wv.winRectValid = qtrue;

		in_forceCharset = Cvar_Get( "in_forceCharset", "1", CVAR_ARCHIVE_ND );

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
		g_wv.winRectValid = qfalse;
		gw_minimized = qfalse;
		gw_active = qfalse;
		WIN_EnableAltTab();
		break;

	case WM_CLOSE:
		Cbuf_ExecuteText( EXEC_APPEND, "quit" );
		// filter this message or we may lose window before renderer shutdown ?
		return 0;

	/*
		on minimize: WM_KILLFOCUS, WM_ACTIVATE A:0 M:1
		on restore: WM_ACTIVATE A:1 M:1, WM_SETFOCUS, WM_ACTIVATE A:1 M:0
		on click in: WM_ACTIVATE A:1 M:0, WM_SETFOCUS
		on click out: WM_ACTIVATE A:0 M:0, WM_KILLFOCUS
	*/

	case WM_ACTIVATE:
		fActive = (LOWORD( wParam ) != WA_INACTIVE) ? TRUE : FALSE;
		fMinimized = (BOOL)HIWORD( wParam ) ? TRUE : FALSE;
		//Com_DPrintf( S_COLOR_YELLOW "%WM_ACTIVATE active=%i minimized=%i\n", fActive, fMinimized  );
		// We can recieve Active & Minimized when restoring from minimized state
		if ( fActive && fMinimized )
			gw_minimized = qfalse;
		else
			gw_minimized = (fMinimized != FALSE);

		// focus/activate messages may come in different order
		// so process final result a bit later when we have all data set
		if ( uTimerID_1 == 0 )
			uTimerID_1 = SetTimer( g_wv.hWnd, TIMER_ID_1, 100, NULL );

		return 0;
	
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		fActive = ( uMsg == WM_SETFOCUS );

		if ( uTimerID_1 == 0 )
			uTimerID_1 = SetTimer( g_wv.hWnd, TIMER_ID_1, 100, NULL );

		Win_AddHotkey();

		// We can't get correct minimized status on WM_KILLFOCUS
		VID_AppActivate( fActive );

		if ( fActive ) {
			WIN_DisableAltTab();
		} else {
			WIN_EnableAltTab();
		}

		if ( glw_state.cdsFullscreen ) {
			if ( fActive ) {
				SetGameDisplaySettings();
				if ( re.SetColorMappings )
					re.SetColorMappings();
			} else {
				// don't restore gamma if we have multiple monitors
				if ( glw_state.monitorCount <= 1 || fMinimized )
					GLW_RestoreGamma();
				// minimize if there is only one monitor
				if ( glw_state.monitorCount <= 1 ) {
					if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) ) {
						ShowWindow( hWnd, SW_MINIMIZE );
						SetDesktopDisplaySettings();
					}
				}
			}
		} else {
			if ( fActive ) {
				if ( re.SetColorMappings )
					re.SetColorMappings();
			} else {
				GLW_RestoreGamma();
			}
		}

		SNDDMA_Activate();

		return 0;

	case WM_MOVE:
		{
			if ( !gw_active || gw_minimized )
				break;
			GetWindowRect( hWnd, &g_wv.winRect );
			g_wv.winRectValid = qtrue;
			UpdateMonitorInfo( &g_wv.winRect );
			IN_UpdateWindow( NULL, qtrue );
			IN_Activate( gw_active );
			if ( !gw_active )
				ClipCursor( NULL );

			if ( !glw_state.cdsFullscreen )
			{
				Cvar_SetIntegerValue( "vid_xpos", g_wv.winRect.left );
				Cvar_SetIntegerValue( "vid_ypos", g_wv.winRect.top );

				vid_xpos->modified = qfalse;
				vid_ypos->modified = qfalse;
			}
		}
		break;

	case WM_ENTERSIZEMOVE:
		if ( uTimerID == 0 && (i = GetTimerMsec()) > 0 ) {
			uTimerID = SetTimer( g_wv.hWnd, TIMER_ID, i, NULL );
		}
		break;

	case WM_EXITSIZEMOVE:
		if ( uTimerID != 0 ) {
			KillTimer( g_wv.hWnd, uTimerID );
			uTimerID = 0;
		}
		break;

	case WM_TIMER:
		if ( wParam == TIMER_ID && uTimerID != 0 && !CL_VideoRecording() ) {
			Com_Frame( CL_NoDelay() );
		}

		// delayed window minimize/deactivation
		if ( wParam == TIMER_ID_1 && uTimerID_1 != 0 ) {
			// we may not receive minimized flag with WM_ACTIVE
			// with another opened topmost window app like TaskManager
			if ( IsIconic( hWnd ) )
				gw_minimized = qtrue;
			VID_AppActivate( fActive );
			if ( fMinimized ) {
				GLW_RestoreGamma();
				SetDesktopDisplaySettings();
			}
			KillTimer( g_wv.hWnd, uTimerID_1 );
			uTimerID_1 = 0;
		}
		break;

	case WM_WINDOWPOSCHANGING:
		if ( g_wv.borderless )
		{
			WINDOWPOS *pos = (LPWINDOWPOS) lParam;
			const int threshold = 10;
			HMONITOR hMonitor;
			MONITORINFO mi;
			const RECT *r;
			RECT rr;

			rr.left = pos->x;
			rr.right = pos->x + pos->cx;
			rr.top = pos->y;
			rr.bottom = pos->y + pos->cy;
			hMonitor = MonitorFromRect( &rr, MONITOR_DEFAULTTONEAREST );

			if ( hMonitor )
			{
				mi.cbSize = sizeof( mi );
				GetMonitorInfo( hMonitor, &mi );
				r = &mi.rcWork;

				if ( pos->x >= (r->left - threshold) && pos->x <= (r->left + threshold ) )
					pos->x = r->left;
				else if( (pos->x + pos->cx) >= (r->right - threshold) && (pos->x + pos->cx) <= (r->right + threshold) )
					pos->x = (r->right - pos->cx);

				if ( pos->y >= (r->top - threshold) && pos->y <= (r->top + threshold ) )
					pos->y = r->top;
				else if( (pos->y + pos->cy) >= (r->bottom - threshold) && (pos->y + pos->cy) <= (r->bottom + threshold) )
					pos->y = (r->bottom - pos->cy);

				return 0;
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
			int mstate = (wParam & (MK_LBUTTON|MK_RBUTTON)) + ((wParam & (MK_MBUTTON|MK_XBUTTON1|MK_XBUTTON2)) >> 2);
			IN_Win32MouseEvent( LOWORD(lParam), HIWORD(lParam), mstate );
		}
		break;

	case WM_INPUT:
		IN_RawMouseEvent( lParam );
		break;

	case WM_SYSCOMMAND:
		// Prevent Alt+Letter commands from hanging the application temporarily
		if ( wParam == SC_KEYMENU || wParam == SC_MOUSEMENU + HTSYSMENU || wParam == SC_CLOSE + HTSYSMENU )
			return 0;

		if ( wParam == SC_SCREENSAVE || wParam == SC_MONITORPOWER )
			return 0;

		if ( wParam == SC_MINIMIZE && CL_VideoRecording() && !( re.CanMinimize && re.CanMinimize() ) )
			return 0;

		// simulate drag move to avoid ~500ms delay between DefWindowProc() and further WM_ENTERSIZEMOVE
		if ( wParam == SC_MOVE + HTCAPTION )
		{
			mouse_event( MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, 7, 0, 0, 0 );
			mouse_event( MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, (DWORD)-7, 0, 0, 0 );
		}
		break;

	case WM_CONTEXTMENU:
		return 0;

	case WM_HOTKEY:
		// check for left/right modifiers
		if ( Win_CheckHotkeyMod() )
		{
			if ( gw_active )
			{
				if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) )
					ShowWindow( hWnd, SW_MINIMIZE );
			}
			else
			{
				SetForegroundWindow( hWnd );
				SetFocus( hWnd );
				ShowWindow( hWnd, SW_RESTORE );
			}
			return 0;
		}
		break;

	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
		if ( wParam == VK_RETURN && ( uMsg == WM_SYSKEYDOWN || GetAsyncKeyState( VK_RMENU ) & 0x8000 ) ) {
			Cvar_SetIntegerValue( "r_fullscreen", glw_state.cdsFullscreen ? 0 : 1 );
				Cbuf_AddText( "vid_restart\n" );
			return 0;
		}
		//Com_Printf( "^2k+^7 wParam:%08x lParam:%08x\n", wParam, lParam );
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, MapKey( wParam, lParam ), qtrue, 0, NULL );
		break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
		//Com_Printf( "^5k-^7 wParam:%08x lParam:%08x\n", wParam, lParam );
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, MapKey( wParam, lParam ), qfalse, 0, NULL );
		break;

	case WM_CHAR:
		{
			byte scancode = ((lParam >> 16) & 0xFF);
			if ( wParam != VK_NUMPAD0 && scancode != 0x29 ) {
				Sys_QueEvent( g_wv.sysMsgTime, SE_CHAR, MapChar( wParam, scancode ), 0, 0, NULL );
			}
		}
		return 0;

	case WM_SIZE:
		if ( !gw_active || gw_minimized )
			break;
		GetWindowRect( hWnd, &g_wv.winRect );
		g_wv.winRectValid = qtrue;
		UpdateMonitorInfo( &g_wv.winRect );
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
	case WM_NCHITTEST:
		if ( g_wv.borderless && GetKeyState( VK_CONTROL ) & (1<<15) )
			return HTCAPTION;
		break;

	case WM_ERASEBKGND: 
		// avoid GDI clearing the OpenGL window background in Vista/7
		return 1;
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}
