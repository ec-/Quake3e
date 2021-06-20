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

//static UINT MSH_MOUSEWHEEL;

// Console variables that we need to access from this module
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

Capture PrintScreen and Win* keys
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
static void VID_AppActivate( qboolean active )
{
	Key_ClearStates();

	IN_Activate( active );

	if ( active ) {
		WIN_EnableHook();
		SetWindowPos( g_wv.hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
	} else {
		WIN_DisableHook();
		SetWindowPos( g_wv.hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
	}
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
		//case '*':
		//	return K_KP_STAR;
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
		case 'n'-'a'+1:
		case 'p'-'a'+1:
		case 'l'-'a'+1: // CTRL+L
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


#if 0
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
#endif


static HWINEVENTHOOK hWinEventHook;

static VOID CALLBACK WinEventProc( HWINEVENTHOOK h_WinEventHook, DWORD dwEvent, HWND hWnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime )
{
	if ( gw_active )
	{
		if ( glw_state.cdsFullscreen )// disable topmost window style
		{
			SetWindowPos( g_wv.hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
		}
		SetForegroundWindow( hWnd );
	}
}

#define TIMER_M 11
#define TIMER_T 12
static UINT uTimerM;
static UINT uTimerT;

void WIN_Minimize( void ) {
	static int minimize = 0;

	if ( minimize )
		return;

	minimize = 1;

#ifdef FAST_MODE_SWITCH
	// move game window to background
	if ( glw_state.cdsFullscreen ) {
		if ( gw_active )
			SetForegroundWindow( GetDesktopWindow() );
		// and wait some time before minimizing
		if ( !uTimerM )
			uTimerM = SetTimer( g_wv.hWnd, TIMER_M, 50, NULL );
	} else {
		ShowWindow( g_wv.hWnd, SW_MINIMIZE );
	}
#else
	ShowWindow( g_wv.hWnd, SW_MINIMIZE );
#endif

	minimize = 0;
}


LRESULT WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM  wParam, LPARAM lParam )
{
	#define TIMER_ID 10
	//static UINT uTimerID;
	static qboolean flip = qtrue;
	static qboolean focused = qfalse;
	qboolean active;
	qboolean minimized;
	int zDelta, i;

	// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/winui/winui/windowsuserinterface/userinput/mouseinput/aboutmouseinput.asp
	// Windows 95, Windows NT 3.51 - uses MSH_MOUSEWHEEL
	// only relevant for non-DI input
	//
	// NOTE: not sure how reliable this is anymore, might trigger double wheel events
	/* if (in_mouse->integer == -1)
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
	} */

	switch (uMsg)
	{
	case WM_MOUSEWHEEL:
		// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/winui/winui/windowsuserinterface/userinput/mouseinput/aboutmouseinput.asp
		// Windows 98/Me, Windows NT 4.0 and later - uses WM_MOUSEWHEEL
		// only relevant for non-DI input and when console is toggled in window mode
		//   if console is toggled in window mode (KEYCATCH_CONSOLE) then mouse is released and DI doesn't see any mouse wheel
		if ( in_mouse->integer == -1 || ((!glw_state.cdsFullscreen || glw_state.monitorCount > 1) && (Key_GetCatcher() & KEYCATCH_CONSOLE)) )
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

		//MSH_MOUSEWHEEL = RegisterWindowMessage( TEXT( "MSWHEEL_ROLLMSG" ) ); 

		WIN_EnableHook(); // for PrintScreen and Win* keys

		hWinEventHook = SetWinEventHook( EVENT_SYSTEM_SWITCHSTART, EVENT_SYSTEM_SWITCHSTART, NULL, WinEventProc, 
			0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS );
		g_wv.hWnd = hWnd;
		GetWindowRect( hWnd, &g_wv.winRect );
		g_wv.winRectValid = qtrue;
		gw_minimized = qfalse;
		uTimerM = 0;
		uTimerT = 0;

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
		Win_RemoveHotkey();
		if ( hWinEventHook ) {
			UnhookWinEvent( hWinEventHook );
		}
		if ( uTimerM ) {
			KillTimer( g_wv.hWnd, uTimerM ); uTimerM = 0;
		}
		if ( uTimerT ) {
			KillTimer( g_wv.hWnd, uTimerT ); uTimerT = 0;
		}
		hWinEventHook = NULL;
		g_wv.hWnd = NULL;
		g_wv.winRectValid = qfalse;
		//gw_minimized = qfalse;
		gw_active = qfalse;
		//WIN_EnableAltTab();
		return 0;

	case WM_CLOSE:
		Cbuf_ExecuteText( EXEC_APPEND, "quit\n" );
		// filter this message or we may lose window before renderer shutdown ?
		return 0;

	/*
		on minimize:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWMINIMIZED
			WM_KILLFOCUS
			WM_MOVE (x:garbage y:garbage)
			WM_SIZE (SIZE_MINIMIZED w=0 h=0)
			WM_ACTIVATE (active=0 minimized=1)

		on restore:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_ACTIVATE (active=1 minimized=1)
			WM_MOVE (x, y)
			WM_SIZE (SIZE_RESTORED width height)
			WM_SETFOCUS
			WM_ACTIVATE (active=1 minimized=0)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL

		on click in:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_ACTIVATE (active=1 minimized=0)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_SETFOCUS

		on click out, destroy:
			WM_ACTIVATE (active=0 minimized=0)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_KILLFOCUS

		on create:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_ACTIVATE (active=1 minimized=0)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_SETFOCUS
			WM_SIZE (SIZE_RESTORED width height)
			WM_MOVE (x, y)

		on win+d:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWMINIMIZED
			WM_MOVE (x:garbage, y:garbage)
			WM_SIZE (SIZE_MINIMIZED)
			WM_ACTIVATE (active=0 minimized=1)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWMINIMIZED
			WM_KILLFOCUS
			
	*/

	case WM_ACTIVATE:
		active = (LOWORD( wParam ) != WA_INACTIVE) ? qtrue : qfalse;
		minimized = (BOOL)HIWORD( wParam ) ? qtrue : qfalse;

		// We can recieve Active & Minimized when restoring from minimized state
		if ( active && minimized ) {
			gw_minimized = qtrue;
			break;
		}

		gw_active = active;
		gw_minimized = minimized;

		VID_AppActivate( gw_active );
		Win_AddHotkey();

		if ( glw_state.cdsFullscreen ) {
			if ( gw_active ) {
				SetGameDisplaySettings();
				if ( re.SetColorMappings )
					re.SetColorMappings();
			} else {
				// don't restore gamma if we have multiple monitors
				if ( glw_state.monitorCount <= 1 || gw_minimized )
					GLW_RestoreGamma();
				// minimize if there is only one monitor
				if ( glw_state.monitorCount <= 1 ) {
					if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) ) {
						if ( !gw_minimized ) {
							WIN_Minimize();
						}
						SetDesktopDisplaySettings();
					}
				}
			}
		} else {
			if ( gw_active ) {
				if ( re.SetColorMappings )
					re.SetColorMappings();
			} else {
				GLW_RestoreGamma();
			}
		}

		// after ALT+TAB, even if we selected other window we may receive WM_ACTIVATE 1 and then WM_ACTIVATE 0
		// if we set HWND_TOPMOST in VID_AppActivate() other window will be not visible despite obtained input focus
		// so delay HWND_TOPMOST setup to make sure we have no such bogus activation
		if ( gw_active && glw_state.cdsFullscreen ) {
			if ( uTimerT ) {
				KillTimer( g_wv.hWnd, uTimerT );
			}
			uTimerT = SetTimer( g_wv.hWnd, TIMER_T, 20, NULL );
		}

		SNDDMA_Activate();
		break;

	case WM_SETFOCUS:
		focused = qtrue;
		break;

	case WM_KILLFOCUS:
		//gw_active = qfalse;
		focused = qfalse;
		break;

	case WM_MOVE:
		if ( !gw_active || gw_minimized || !focused )
			break;

		GetWindowRect( hWnd, &g_wv.winRect );
		g_wv.winRectValid = qtrue;
		UpdateMonitorInfo( &g_wv.winRect );
		IN_UpdateWindow( NULL, qtrue );
		IN_Activate( gw_active );

		if ( !glw_state.cdsFullscreen )	{
			Cvar_SetIntegerValue( "vid_xpos", g_wv.winRect.left );
			Cvar_SetIntegerValue( "vid_ypos", g_wv.winRect.top );
			vid_xpos->modified = qfalse;
			vid_ypos->modified = qfalse;
		}
		break;

	case WM_SIZE:
		if ( gw_active && focused && !gw_minimized ) {
			GetWindowRect( hWnd, &g_wv.winRect );
			g_wv.winRectValid = qtrue;
			UpdateMonitorInfo( &g_wv.winRect );
			IN_UpdateWindow( NULL, qtrue );
		}
		break;

	case WM_TIMER:
		//if ( wParam == TIMER_ID && uTimerID != 0 && !CL_VideoRecording() ) {
		//	Com_Frame( CL_NoDelay() );
		//	return 0;
		//}
		if ( wParam == TIMER_M ) {
			KillTimer( g_wv.hWnd, uTimerM ); uTimerM = 0;
			ShowWindow( hWnd, SW_MINIMIZE );
			return 0;
		}
		if ( wParam == TIMER_T ) {
			KillTimer( g_wv.hWnd, uTimerT ); uTimerT = 0;
			if ( gw_active && glw_state.cdsFullscreen ) {
				// set TOPMOST style to avoid losing input focus because of other underlying topmost windows
				// such as on-screen keyboard
				SetWindowPos( g_wv.hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
			}
			return 0;
		}
		break;

	case WM_WINDOWPOSCHANGING:
		{
			WINDOWPLACEMENT wp;

			// set minimized flag as early as possible
			if ( GetWindowPlacement( hWnd, &wp ) && wp.showCmd == SW_SHOWMINIMIZED )
				gw_minimized = qtrue;

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

					// snap window to current monitor borders
					if ( pos->x >= ( r->left - threshold ) && pos->x <= ( r->left + threshold ) )
						pos->x = r->left;
					else if ( ( pos->x + pos->cx ) >= ( r->right - threshold ) && ( pos->x + pos->cx ) <= ( r->right + threshold ) )
						pos->x = ( r->right - pos->cx );

					if ( pos->y >= ( r->top - threshold ) && pos->y <= ( r->top + threshold ) )
						pos->y = r->top;
					else if ( ( pos->y + pos->cy ) >= ( r->bottom - threshold ) && ( pos->y + pos->cy ) <= ( r->bottom + threshold ) )
						pos->y = ( r->bottom - pos->cy );

					return 0;
				}
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
		if ( IN_MouseActive() ) {
			int mstate = (wParam & (MK_LBUTTON|MK_RBUTTON)) + ((wParam & (MK_MBUTTON|MK_XBUTTON1|MK_XBUTTON2)) >> 2);
			IN_Win32MouseEvent( LOWORD(lParam), HIWORD(lParam), mstate );
			return 0;
		}
		break;

	case WM_INPUT:
		if ( IN_MouseActive() ) {
			IN_RawMouseEvent( lParam );
			return 0;
		}
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
		// disable context menus to avoid blocking message loop
		return 0;

	case WM_HOTKEY:
		// check for left/right modifiers
		if ( Win_CheckHotkeyMod() )
		{
			if ( gw_active )
			{
				if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) )
					WIN_Minimize();
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

	case WM_NCHITTEST:
		// in borderless mode - drag using client area when holding ALT
		if ( g_wv.borderless && GetKeyState( VK_MENU ) & (1<<15) )
			return HTCAPTION;
		break;

	case WM_ERASEBKGND: 
		// avoid GDI clearing the OpenGL window background in Vista/7
		return 1;
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}


/*
================
HandleEvents
================
*/
void HandleEvents( void ) {
	MSG msg;

	// pump the message loop
	while ( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) ) {
		if ( GetMessage( &msg, NULL, 0, 0 ) <= 0 ) {
			Cmd_Clear();
			Com_Quit_f();
		}

		// save the msg time, because wndprocs don't have access to the timestamp
		//g_wv.sysMsgTime = msg.time;
		g_wv.sysMsgTime = Sys_Milliseconds();

		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}
}


/*
================
Sys_GetClipboardData
================
*/
char *Sys_GetClipboardData( void ) {
	char *data = NULL;
	char *cliptext;

	if ( OpenClipboard( NULL ) ) {
		HANDLE hClipboardData;
		DWORD size;

		// GetClipboardData performs implicit CF_UNICODETEXT => CF_TEXT conversion
		if ( ( hClipboardData = GetClipboardData( CF_TEXT ) ) != 0 ) {
			if ( ( cliptext = GlobalLock( hClipboardData ) ) != 0 ) {
				size = GlobalSize( hClipboardData ) + 1;
				data = Z_Malloc( size );
				Q_strncpyz( data, cliptext, size );
				GlobalUnlock( hClipboardData );
				
				strtok( data, "\n\r\b" );
			}
		}
		CloseClipboard();
	}
	return data;
}


/*
================
Sys_SetClipboardBitmap
================
*/
void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{
	HGLOBAL hMem;
	byte *ptr;

	if ( !g_wv.hWnd || !OpenClipboard( g_wv.hWnd ) )
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
}
