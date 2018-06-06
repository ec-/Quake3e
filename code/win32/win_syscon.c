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
// win_syscon.h
#include "../client/client.h"
#include "win_local.h"
#include "resource.h"

#define COPY_ID			1
#define QUIT_ID			2
#define CLEAR_ID		3
#define ERROR_TIMER_ID	4

#define CON_TIMER_ID	5
#define BUF_TIMER_ID	6
#define TEX_TIMER_ID	7

#define ERRORBOX_ID		10
#define ERRORTEXT_ID	11

#define EDIT_ID			100
#define INPUT_ID		101
#define STATUS_ID       102

#define DEFAULT_WIDTH   600
#define DEFAULT_HEIGHT  434

#define BORDERW			1
#define BORDERH			2

#define INPUT_HEIGHT    16
#define ERROR_HEIGHT    27

#define MAX_CONSIZE		65536

#define T TEXT

#define EDIT_COLOR		RGB(0x00,0x00,0x10)
#define TEXT_COLOR		RGB(0x40,0xEE,0x20)

#define ERROR_BG_COLOR	RGB(0x90,0x80,0x80)

#define ERROR_COLOR_1   RGB(0xFF,0xFF,0x00)
#define ERROR_COLOR_2   RGB(0xF0,0x00,0x00)

field_t console;

typedef struct
{
	HWND		hWnd;
	HWND		hwndBuffer;

	HWND		hwndInputLine;

	HWND		hwndStatusBar;
	HWND		hwndButtonClear;
	HWND		hwndButtonCopy;

	HWND		hwndErrorBox;

	HBRUSH		hbrEditBackground;
	HBRUSH		hbrErrorBackground;

	HFONT		hfBufferFont;
	HFONT		hfStatusFont;

	char		consoleText[512];
	char		returnedText[512];

	int			visLevel;
	qboolean	quitOnClose;
	int			windowWidth, windowHeight;
	
	LONG_PTR	SysInputLineWndProc;
	LONG_PTR	SysStatusWndProc;
	LONG_PTR	SysBufferWndProc;

	qboolean	newline;

} WinConData;

static WinConData s_wcd;

static int maxConSize; // up to MAX_CONSIZE
static int curConSize; // up to MAX_CONSIZE

static UINT texTimerID; // for flushing text in buffer

static char conBuffer[ MAXPRINTMSG ];
static int  conBufPos;

static void AddBufferText( const char *text, int textLength );

static void ConClear( void ) 
{
	//SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, -1 );
	//SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, ( LPARAM ) "" );
	SetWindowText( s_wcd.hwndBuffer, T("") );
	UpdateWindow( s_wcd.hwndBuffer );
	s_wcd.newline = qfalse;
	curConSize = 0;
	conBufPos = 0;
}

static int GetStatusBarHeight( void ) 
{
	RECT rect;

	if ( !s_wcd.hwndStatusBar )
		return 22;

	GetClientRect( s_wcd.hwndStatusBar, &rect );

	return (rect.bottom-rect.top+1);
}


static int GetTimerMsec( void )
{
	int msec;
	if ( !com_sv_running || !com_sv_running->integer ) {
		msec = 50; // 20fps
	} else {
		msec = 1000 / Cvar_VariableIntegerValue( "sv_fps" );
	}
#ifndef DEDICATED
	if ( com_cl_running && com_cl_running->integer ) {
		if ( com_maxfps->integer ) {
			msec = 1000 / com_maxfps->integer;
		}
		if ( Cvar_VariableIntegerValue( "com_maxfpsUnfocused" ) ) {
			msec = 1000 / Cvar_VariableIntegerValue( "com_maxfpsUnfocused" );
		}
		if ( gw_minimized || CL_VideoRecording() ) {
			return 0;
		}
	}
#endif
	return msec;
}


static LRESULT WINAPI ConWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	char *cmdString;
	static qboolean s_timePolarity;
	static UINT conTimerID;
	int v;

	switch ( uMsg )
	{

	case WM_SETFOCUS:
		if ( s_wcd.hwndInputLine ) 
		{
			SetFocus( s_wcd.hwndInputLine );
		}
		break;

	case WM_ACTIVATE:

		if ( com_viewlog && ( com_dedicated && !com_dedicated->integer ) )
		{
			// if the viewlog is open, check to see if it's being minimized
			if ( com_viewlog->integer == 1 )
			{
				if ( HIWORD( wParam ) )		// minimized flag
				{
					Cvar_Set( "viewlog", "2" );
				}
			}
			else if ( com_viewlog->integer == 2 )
			{
				if ( !HIWORD( wParam ) )		// minimized flag
				{
					Cvar_Set( "viewlog", "1" );
				}
			}
		}
		break;

	case WM_QUERYENDSESSION:
		if ( com_dedicated && com_dedicated->integer && !com_errorEntered )
		{
			cmdString = CopyString( "quit" );
			Sys_QueEvent( 0, SE_CONSOLE, 0, 0, strlen( cmdString ) + 1, cmdString );
		}
		else
		{
			PostQuitMessage( 0 );
		}
		return TRUE;

	case WM_CLOSE:
		if ( com_dedicated && com_dedicated->integer && !com_errorEntered )
		{
			cmdString = CopyString( "quit" );
			Sys_QueEvent( 0, SE_CONSOLE, 0, 0, strlen( cmdString ) + 1, cmdString );
		}
		else if ( s_wcd.quitOnClose )
		{
			PostQuitMessage( 0 );
		}
		else
		{
			Sys_ShowConsole( 0, qfalse );
			Cvar_Set( "viewlog", "0" );
		}
		return 0;

	case WM_CTLCOLORSTATIC:
		if ( ( HWND ) lParam == s_wcd.hwndBuffer )
		{
			SetBkColor( ( HDC ) wParam, EDIT_COLOR );
			SetTextColor( ( HDC ) wParam, TEXT_COLOR );
			return ( LRESULT ) s_wcd.hbrEditBackground;
		}
		else if ( ( HWND ) lParam == s_wcd.hwndErrorBox )
		{
			if ( s_timePolarity & 1 )
			{
				SetBkColor( ( HDC ) wParam, ERROR_BG_COLOR );
				SetTextColor( ( HDC ) wParam, ERROR_COLOR_1 );
			}
			else
			{
				SetBkColor( ( HDC ) wParam, ERROR_BG_COLOR );
				SetTextColor( ( HDC ) wParam, ERROR_COLOR_2 );
			}
			return ( LRESULT ) s_wcd.hbrErrorBackground;
		}
		break;

	case WM_CREATE:
		s_wcd.hbrEditBackground = CreateSolidBrush( EDIT_COLOR );
		GetWindowRect( hWnd, &g_wv.conRect );
		break;

	case WM_MOVE:
		GetWindowRect( hWnd, &g_wv.conRect );
		break;

	case WM_SIZE:
		{
			RECT rect;
			int sth;

			sth = GetStatusBarHeight();
			GetClientRect( hWnd, &rect );

			s_wcd.windowWidth = rect.right - rect.left + 1;
			s_wcd.windowHeight = rect.bottom - rect.top + 1;

			if ( s_wcd.hwndErrorBox ) {
				SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, ERROR_HEIGHT + BORDERH*2, rect.right - BORDERW*2, rect.bottom - sth - ERROR_HEIGHT - BORDERH*3 + 1, SWP_NOZORDER );
			} else {
				SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, BORDERH, rect.right - BORDERW*2, rect.bottom - sth - INPUT_HEIGHT - BORDERH*3 - 2, SWP_NOZORDER );
			}

			if ( s_wcd.hwndErrorBox ) {
				SetWindowPos( s_wcd.hwndErrorBox, HWND_TOP, BORDERW, BORDERH, rect.right - BORDERW*2, ERROR_HEIGHT, SWP_NOZORDER );
				InvalidateRect( s_wcd.hwndErrorBox, NULL, FALSE );
			}

			if ( s_wcd.hwndInputLine ) {
				SetWindowPos( s_wcd.hwndInputLine, HWND_TOP, BORDERW, rect.bottom - sth - INPUT_HEIGHT - BORDERH, rect.right - BORDERW*2, INPUT_HEIGHT, SWP_NOZORDER );
				InvalidateRect( s_wcd.hwndInputLine, NULL, FALSE );
			}

			if ( s_wcd.hwndStatusBar ) {
				SetWindowPos( s_wcd.hwndStatusBar, HWND_TOP, BORDERW, rect.bottom, rect.right - BORDERW*2, 26, SWP_NOZORDER );
				InvalidateRect( s_wcd.hwndStatusBar, NULL, FALSE );
			}

			GetWindowRect( hWnd, &g_wv.conRect );

			return 0;
		}

	case WM_SIZING:
		{
			int w, h;
			RECT *r;
			r = (LPRECT) lParam;
			w = r->right - r->left - 280+BORDERW*2 + 1;
			h = r->bottom - r->top - 155+BORDERH*3 + 1;
			if ( w < 0 ) {
				if ( wParam == WMSZ_RIGHT || wParam == WMSZ_TOPRIGHT || wParam == WMSZ_BOTTOMRIGHT ) {
					r->right -= w;
				}
				if ( wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT ) {
					r->left += w;
				}
			}
			if ( h < 0 ) {
				if ( wParam == WMSZ_BOTTOM || wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_BOTTOMRIGHT ) {
					r->bottom -= h;
				}
				if ( wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT ) {
					r->top += h;
				}
			}
			return TRUE;
		}

	case WM_SYSCOMMAND:
		// Prevent Alt+Letter commands from hanging the application temporarily
		if ( wParam == SC_KEYMENU || wParam == SC_MOUSEMENU + HTSYSMENU || wParam == SC_CLOSE + HTSYSMENU )
			return 0;

		// simulate drag move to avoid ~500ms delay between DefWindowProc() and further WM_ENTERSIZEMOVE
		if ( wParam == SC_MOVE + HTCAPTION )
		{
			mouse_event( MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, 7, 0, 0, 0 );
			mouse_event( MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, (DWORD)-7, 0, 0, 0 );
		}
		break;

	case WM_ENTERSIZEMOVE: 
		if ( conTimerID == 0 && (v = GetTimerMsec()) > 0 ) {
			conTimerID = SetTimer( s_wcd.hWnd, CON_TIMER_ID, v, NULL );
		}
		break;

	case WM_EXITSIZEMOVE:
		if ( conTimerID != 0 ) {
			KillTimer( s_wcd.hWnd, conTimerID );
			conTimerID = 0;
		}
		break;

	case WM_TIMER:
		if ( wParam == ERROR_TIMER_ID )
		{
			s_timePolarity = !s_timePolarity;
			if ( s_wcd.hwndErrorBox )
			{
				InvalidateRect( s_wcd.hwndErrorBox, NULL, FALSE );
			}
		} 
		else if ( wParam == CON_TIMER_ID && conTimerID != 0 && !com_errorEntered )
		{
#ifdef DEDICATED
			Com_Frame( qfalse );
#else
			Com_Frame( CL_NoDelay() );
#endif
		}
		break;

	case WM_CONTEXTMENU:
			return 0;
    }
	
    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}


static LRESULT WINAPI BufferWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	static UINT bufTimerID;
	int v;

	switch ( uMsg ) {

	case WM_VSCROLL:
		if ( (int)LOWORD(wParam) == SB_ENDSCROLL ) {
			if ( bufTimerID != 0 ) { 
				KillTimer( hWnd, bufTimerID );
				bufTimerID = 0;
			}
		} else {
			if ( bufTimerID == 0 && (v = GetTimerMsec()) > 0 ) {
				bufTimerID = SetTimer( hWnd, BUF_TIMER_ID, v, NULL );
			}
		}
		break;

	case WM_CAPTURECHANGED:
		if ( (HWND)lParam == hWnd ) {
			if ( bufTimerID == 0 && (v = GetTimerMsec()) > 0 )
				bufTimerID = SetTimer( hWnd, BUF_TIMER_ID, v, NULL );
		} else {
			if ( bufTimerID != 0 ) { 
				KillTimer( hWnd, bufTimerID );
				bufTimerID = 0;
			}
		}
		return 0;

	case WM_TIMER:
		if ( wParam == BUF_TIMER_ID && bufTimerID != 0 && !com_errorEntered )
		{
#ifdef DEDICATED
			Com_Frame( qfalse );
#else
			Com_Frame( CL_NoDelay() );
#endif
		}
		if ( wParam == TEX_TIMER_ID && texTimerID != 0 ) {
			if ( conBufPos ) { 
				// dump text
				AddBufferText( conBuffer, conBufPos );
				conBufPos = 0;
			} else {
				// kill timer
				KillTimer( hWnd, texTimerID );
				texTimerID = 0;
			}
		}
		return 0;
	
	case WM_CONTEXTMENU:
		return 0;
	}

	return CallWindowProc( (WNDPROC) s_wcd.SysBufferWndProc, hWnd, uMsg, wParam, lParam );
}


LRESULT WINAPI StatusWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	HGLOBAL hMem;
	TCHAR *text;
	int len;

	switch (uMsg)
	{

	case WM_COMMAND:
		if ( wParam == COPY_ID )
		{
			if ( OpenClipboard( s_wcd.hWnd ) )
			{
				EmptyClipboard();
				len = GetWindowTextLength( s_wcd.hwndBuffer );
				if ( len > 0 ) {
					hMem = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, 
						(len + 1) * sizeof( TCHAR ) );
					if ( hMem != NULL ) {
						text = ( TCHAR* )GlobalLock( hMem );
						if ( text != NULL ) {
							GetWindowText( s_wcd.hwndBuffer, text, len + 1 );
						}
						GlobalUnlock( hMem );
#ifdef UNICODE
						SetClipboardData( CF_UNICODETEXT, hMem );
#else
						SetClipboardData( CF_TEXT, hMem );
#endif
					}
				}
				CloseClipboard();
			}
			if ( s_wcd.hwndInputLine ) {
				SetFocus( s_wcd.hwndInputLine );
			}
		}
		else if ( wParam == CLEAR_ID )
		{
			ConClear();
			if ( s_wcd.hwndInputLine ) {
				SetFocus( s_wcd.hwndInputLine );
			}
		}
		break;
	case WM_LBUTTONDBLCLK:
	case WM_LBUTTONDOWN:
		if ( s_wcd.hwndInputLine ) {
			SetFocus( s_wcd.hwndInputLine );
		}
		break;
    }

	return CallWindowProc( (WNDPROC)s_wcd.SysStatusWndProc, hWnd, uMsg, wParam, lParam );
}


LRESULT WINAPI InputLineWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	TCHAR inputBuffer[ MAX_EDIT_LINE ];
	int zDelta, fwKeys, i;
	WPARAM scrollMsg;

	switch ( uMsg )
	{
	case WM_KILLFOCUS:
		if ( (HWND)wParam == s_wcd.hwndBuffer ) {
			SetFocus( s_wcd.hwndInputLine );
			return 0;
		}
		break;

	case WM_MOUSEWHEEL:
		zDelta = (short) HIWORD( wParam ) / WHEEL_DELTA;
		if ( zDelta ) {
			fwKeys = LOWORD( wParam );
			if ( zDelta > 0 ) {
				if ( fwKeys & MK_CONTROL )
					scrollMsg = SB_PAGEUP;
				else
					scrollMsg = SB_LINEUP;
			} else {
				zDelta = -zDelta;
				if ( fwKeys & MK_CONTROL )
					scrollMsg = SB_PAGEDOWN;
				else
					scrollMsg = SB_LINEDOWN;
			}
			for ( i = 0; i < zDelta; i++ ) {
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, scrollMsg, 0 );
			}
			return 0;
		}
		break;

	case WM_KEYDOWN:
	{
		if ( wParam == 'L' && ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 || GetAsyncKeyState( VK_RCONTROL ) & 0x8000 ) ) {
			ConClear();
			return 0;
		}

		if ( wParam == VK_PRIOR ) {
			if ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 || GetAsyncKeyState( VK_RCONTROL ) & 0x8000 )
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, (WPARAM)SB_PAGEUP, 0 );
			else
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, (WPARAM)SB_LINEUP, 0 );
			return 0;
		}

		if ( wParam == VK_NEXT ) {
			if ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 || GetAsyncKeyState( VK_RCONTROL ) & 0x8000 )
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, (WPARAM)SB_PAGEDOWN, 0 );
			else
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, (WPARAM)SB_LINEDOWN, 0 );
			return 0;
		}

		if ( wParam == VK_UP ) {
			Con_HistoryGetPrev( &console );
			SetWindowText( hWnd, AtoW( console.buffer ) );
			SendMessage( hWnd, EM_SETSEL, (WPARAM) console.cursor, console.cursor );
			return 0;
		}

		if ( wParam == VK_DOWN ) {
			Con_HistoryGetNext( &console );
			SetWindowText( hWnd, AtoW( console.buffer ) );
			SendMessage( hWnd, EM_SETSEL, (WPARAM) console.cursor, console.cursor );
			return 0;
		}

		break;
	}

	case WM_CHAR:
		if ( wParam > 255 )
			return 0;
		if ( wParam == VK_RETURN )
		{
			DWORD pos;
			char *s;

			GetWindowText( hWnd, inputBuffer, sizeof( inputBuffer ) );
			Q_strncpyz( console.buffer, WtoA( inputBuffer ), sizeof( console.buffer ) );
			SendMessage( hWnd, EM_GETSEL, (WPARAM) &pos, (LPARAM) 0 );
			console.cursor = pos;
			Con_SaveField( &console );

			s = console.buffer;

			while ( *s == '\\' || *s == '/' ) // skip leading slashes
				s++;
			
			strncat( s_wcd.consoleText, s, sizeof( s_wcd.consoleText ) - strlen( s_wcd.consoleText ) - 2 );
			strcat( s_wcd.consoleText, "\n" );
			
			SetWindowText( s_wcd.hwndInputLine, T("") );
			Field_Clear( &console );

			Sys_Print( va( "]%s\n", WtoA( inputBuffer ) ) );

			return 0;
		}

		if ( wParam == VK_TAB ) {
			DWORD pos;

			GetWindowText( hWnd, inputBuffer, sizeof( inputBuffer ) );
			Q_strncpyz( console.buffer, WtoA( inputBuffer ), sizeof( console.buffer ) );
			SendMessage( hWnd, EM_GETSEL, (WPARAM) &pos, (LPARAM) 0 );
			console.cursor = pos;
			
			Field_AutoComplete( &console );

			SetWindowText( hWnd, AtoW( console.buffer ) );
			SendMessage( hWnd, EM_SETSEL, console.cursor, console.cursor );
			return 0;
		}
		break;

	case WM_CONTEXTMENU:
		return 0;
	}

	return CallWindowProc( (WNDPROC)s_wcd.SysInputLineWndProc, hWnd, uMsg, wParam, lParam );
}


/*
** Sys_CreateConsole
*/
void Sys_CreateConsole( const char *title, int xPos, int yPos, qboolean useXYpos )
{
	HDC hDC;
	WNDCLASS wc;
	RECT rect;
	const TCHAR *DEDCLASS = T("Q3 WinConsole");

	int DEDSTYLE = WS_POPUPWINDOW | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;
	int	fontWidth, fontHeight, statusFontHeight;
	int widths[2] = { 140, -1 };
	int borders[3];
	int x, y, w, h, sth;
	int con_x, con_y;

	HMONITOR hMonitor;
	MONITORINFO mInfo;
	POINT		p;

	memset( &wc, 0, sizeof( wc ) );

	wc.style         = 0;
	wc.lpfnWndProc   = ConWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = g_wv.hInstance;
	wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(LRESULT)COLOR_WINDOW;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = DEDCLASS;

	if ( !RegisterClass (&wc) )
		return;

	rect.left = 0;
	rect.right = DEFAULT_WIDTH;
	rect.top = 0;
	rect.bottom = DEFAULT_HEIGHT;

	AdjustWindowRect( &rect, DEDSTYLE, FALSE );

	// try to use command line provided coodinates to locate primary monitor
	if ( useXYpos ) {
		p.x = xPos;
		p.y = yPos;
	} else {
		GetCursorPos( &p );
	}
	
	memset( &mInfo, 0, sizeof( mInfo ) );
	mInfo.cbSize = sizeof( MONITORINFO );
	// Query display dimensions
	hMonitor = MonitorFromPoint( p, MONITOR_DEFAULTTONEAREST );
	if ( hMonitor && GetMonitorInfo( hMonitor, &mInfo ) ) {
		// current monitor info
		w = mInfo.rcMonitor.right - mInfo.rcMonitor.left;
		h = mInfo.rcMonitor.bottom - mInfo.rcMonitor.top;
		x = mInfo.rcMonitor.left;
		y = mInfo.rcMonitor.top;
	} else {
		// primary display info
		hDC = GetDC( GetDesktopWindow() );
		w = GetDeviceCaps( hDC, HORZRES );
		h = GetDeviceCaps( hDC, VERTRES );
		x = 0;
		y = 0;
		ReleaseDC( GetDesktopWindow(), hDC );
	}

	fontWidth = -8;
	fontHeight = -12;
	statusFontHeight = -11;

	s_wcd.windowWidth = rect.right - rect.left + 1;
	s_wcd.windowHeight = rect.bottom - rect.top + 1;

#ifdef DEDICATED
	if ( useXYpos )
	{
		con_x = xPos;
		con_y = yPos;
	}
	else
#endif
	{
		con_x = x + ( w - s_wcd.windowWidth ) / 2;
		con_y = y + ( h - s_wcd.windowHeight ) / 2;
	}

	s_wcd.hWnd = CreateWindowEx( 0, DEDCLASS,
		T(CONSOLE_WINDOW_TITLE), DEDSTYLE, con_x, con_y,
		s_wcd.windowWidth, s_wcd.windowHeight,
		NULL, NULL, g_wv.hInstance, NULL );

	if ( s_wcd.hWnd == NULL )
		return;

	InitCommonControls();

	s_wcd.hfBufferFont = CreateFont( fontHeight, fontWidth,
		0,
		0,
		FW_DONTCARE,
		0,
		0,
		0,
		DEFAULT_CHARSET,
		OUT_RASTER_PRECIS,
		CLIP_DEFAULT_PRECIS,
		NONANTIALIASED_QUALITY,
		FIXED_PITCH | FF_MODERN,
		T("Terminal") );

	s_wcd.hfStatusFont = CreateFont( statusFontHeight, 0,
		0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY,
		DEFAULT_PITCH,
		T("Tahoma") );

	s_wcd.hwndStatusBar = CreateWindow( STATUSCLASSNAME, NULL, WS_VISIBLE | WS_CHILD,
		1,1,32,32, s_wcd.hWnd, NULL, g_wv.hInstance, NULL );

	// split statusbar into parts and set styles
	SendMessage( s_wcd.hwndStatusBar, WM_SETFONT, ( WPARAM ) s_wcd.hfStatusFont, 0 );
	SendMessage( s_wcd.hwndStatusBar, SB_GETBORDERS, 0, (LPARAM)&borders );
	widths[0] += borders[1]*2; // count vertical borders
	SendMessage( s_wcd.hwndStatusBar, SB_SETPARTS, 2, (LPARAM)&widths );
	SendMessage( s_wcd.hwndStatusBar, SB_SETTEXT, 0 | SBT_NOBORDERS, (LPARAM)"" );

	SendMessage( s_wcd.hwndStatusBar, SB_GETRECT, 0, (LPARAM)&rect );
	rect.left += borders[1];
	rect.right -= borders[1];
	x = rect.left;
	h = rect.bottom - rect.top - 1;
	w = (rect.right - rect.left - 4) / 2;

	// create the buttons 
	s_wcd.hwndButtonCopy = CreateWindow( T("button"), T("copy"), WS_VISIBLE | WS_CHILD,
		x, rect.top, w, h, s_wcd.hwndStatusBar, (HMENU)(LRESULT)COPY_ID, g_wv.hInstance, NULL );
	x += w + 4;

	s_wcd.hwndButtonClear = CreateWindow( T("button"), T("clear"), WS_VISIBLE | WS_CHILD,
		x, rect.top, w, h, s_wcd.hwndStatusBar, (HMENU)(LRESULT)CLEAR_ID, g_wv.hInstance, NULL );

	SendMessage( s_wcd.hwndButtonCopy, WM_SETFONT, ( WPARAM ) s_wcd.hfStatusFont, 0 );
	SendMessage( s_wcd.hwndButtonClear, WM_SETFONT, ( WPARAM ) s_wcd.hfStatusFont, 0 );

	sth = GetStatusBarHeight();
	GetClientRect( s_wcd.hWnd, &rect );

	// create fonts
	//hDC = GetDC( s_wcd.hWnd );
	//nHeight = -MulDiv( 8, GetDeviceCaps( hDC, LOGPIXELSY ), 72);
	//ReleaseDC( s_wcd.hWnd, hDC );

	// create the input line
	s_wcd.hwndInputLine = CreateWindow( T("edit"), NULL, WS_CHILD | WS_VISIBLE | WS_BORDER |
		ES_LEFT | ES_AUTOHSCROLL,
		BORDERW, rect.bottom - sth - INPUT_HEIGHT - BORDERH, rect.right - BORDERW*2, INPUT_HEIGHT,
		s_wcd.hWnd,
		(HMENU)(LRESULT)INPUT_ID,	// child window ID
		g_wv.hInstance, NULL );

	// create the scrollbuffer
	s_wcd.hwndBuffer = CreateWindow( T("edit"), NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
		ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
		BORDERW, BORDERH, rect.right - BORDERW*2, rect.bottom - sth - INPUT_HEIGHT - BORDERH*3 - 2,
		s_wcd.hWnd,
		(HMENU)(LRESULT)EDIT_ID,	// child window ID
		g_wv.hInstance, NULL );

	SendMessage( s_wcd.hwndBuffer, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );
	SendMessage( s_wcd.hwndInputLine, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );

	s_wcd.SysInputLineWndProc = SetWindowLongPtr( s_wcd.hwndInputLine, GWLP_WNDPROC, ( LONG_PTR ) InputLineWndProc );
	s_wcd.SysStatusWndProc = SetWindowLongPtr( s_wcd.hwndStatusBar, GWLP_WNDPROC, ( LONG_PTR ) StatusWndProc );
	s_wcd.SysBufferWndProc = SetWindowLongPtr( s_wcd.hwndBuffer, GWLP_WNDPROC, ( LONG_PTR ) BufferWndProc );

	if ( title && *title ) {
		SetWindowText( s_wcd.hWnd, AtoW( title ) );
	}

	ShowWindow( s_wcd.hWnd, SW_SHOWDEFAULT );
	UpdateWindow( s_wcd.hWnd );
	SetForegroundWindow( s_wcd.hWnd );
	
	SendMessage( s_wcd.hwndBuffer, EM_SETLIMITTEXT, MAX_CONSIZE, 0 );
	maxConSize = SendMessage( s_wcd.hwndBuffer, EM_GETLIMITTEXT, 0, 0 );

	SendMessage( s_wcd.hwndInputLine, EM_SETLIMITTEXT, MAX_EDIT_LINE, 0 );

	Field_Clear( &console );

	ConClear();

	Sys_SetStatus( "Server is not running" );

	s_wcd.visLevel = 1;
}


/*
** Sys_DestroyConsole
*/
void Sys_DestroyConsole( void )
{
	if ( s_wcd.hWnd )
	{
		ShowWindow( s_wcd.hWnd, SW_HIDE );
		CloseWindow( s_wcd.hWnd );
		DestroyWindow( s_wcd.hWnd );
		s_wcd.hWnd = NULL;
	}
}


/*
** Sys_ShowConsole
*/
void Sys_ShowConsole( int visLevel, qboolean quitOnClose )
{
	s_wcd.quitOnClose = quitOnClose;

	if ( visLevel == s_wcd.visLevel )
	{
		return;
	}

	s_wcd.visLevel = visLevel;

	if ( !s_wcd.hWnd )
		return;

	switch ( visLevel )
	{
	case 0:
		ShowWindow( s_wcd.hWnd, SW_HIDE );
		break;
	case 1:
		ShowWindow( s_wcd.hWnd, SW_SHOWNORMAL );
		curConSize = GetWindowTextLength( s_wcd.hwndBuffer );
		SendMessage( s_wcd.hwndBuffer, EM_SETSEL, curConSize, curConSize );
		SendMessage( s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0 );
		//SendMessage( s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff );
		break;
	case 2:
		ShowWindow( s_wcd.hWnd, SW_MINIMIZE );
		break;
	default:
		Sys_Error( "Invalid visLevel %d sent to Sys_ShowConsole\n", visLevel );
		break;
	}
}


/*
=============
Sys_SetStatus
=============
*/
void QDECL Sys_SetStatus( const char *format, ... )
{
	va_list		argptr;
	char		text[256];

	if ( s_wcd.hwndStatusBar == NULL )
		return;

	text[0] = ' '; // add leading space for better look :P
	va_start( argptr, format );
	Q_vsnprintf( text + 1, sizeof( text ) - 1, format, argptr );
	va_end( argptr );

	SendMessage( s_wcd.hwndStatusBar, SB_SETTEXT, (WPARAM) 1 | 0, (LPARAM) AtoW( text ) );
}


/*
 =================
 Sys_ConsoleInput
 =================
*/
char *Sys_ConsoleInput( void )
{
	if ( s_wcd.consoleText[0] == '\0' )
	{
		return NULL;
	}

	strcpy( s_wcd.returnedText, s_wcd.consoleText );
	s_wcd.consoleText[0] = '\0';

	return s_wcd.returnedText;
}


/*
 =================
 Conbuf_AppendText
 =================
*/
void Conbuf_AppendText( const char *msg )
{
	char buffer[MAXPRINTMSG*2]; // reserve space for CR-LF expansion
	char *b = buffer;
	int bufLen, n;

	n = strlen( msg );

	// if the message is REALLY long, use just the last portion of it
	if ( n > (MAXPRINTMSG - 1) ) {
		msg += n - (MAXPRINTMSG - 1);
	}

	// insert skipped newline from previous message
	if ( s_wcd.newline ) {
		s_wcd.newline = qfalse;
		*b++ = '\r';
		*b++ = '\n';
	}

	// copy into an intermediate buffer
	while ( *msg )
	{
		if ( *msg == '\n' )
		{
			*b++ = '\r';
			*b++ = '\n';
			msg++;
		}
		else if ( *msg == '\r' )
		{
			*b++ = '\r';
			*b++ = '\n';
			msg++;
			if ( *msg == '\n' )
				msg++;
		}
		else if ( Q_IsColorString( msg ) )
		{
			msg += 2;
		}
		else
		{
			*b++ = *msg++;
		}
	}

	// try to skip ending newline to avoid inserting empty line in edit control
	if ( b - buffer >= 2 && *(b-1) == '\n' && *(b-2) == '\r' ) {
		s_wcd.newline = qtrue;
		b -= 2;
	}

	*b = '\0';
	bufLen = b - buffer;

	// not enough space in buffer -> flush
	if ( bufLen + conBufPos >= sizeof( conBuffer )-1 ) {
		AddBufferText( conBuffer, conBufPos );
		conBufPos = 0;
	} 

	// new message is too long -> flush
	if ( bufLen >= sizeof( conBuffer )-1 ) {
		if ( conBufPos ) {
			AddBufferText( conBuffer, conBufPos );
			conBufPos = 0;
		}
		AddBufferText( buffer, bufLen );
		return;
	}

	// accumulate
	memcpy( conBuffer + conBufPos, buffer, bufLen + 1 );
	conBufPos += bufLen;

	// set flush timer
	if ( texTimerID == 0 ) {
		texTimerID = SetTimer( s_wcd.hwndBuffer, TEX_TIMER_ID, 
			s_wcd.visLevel == 1 ? 25 : 100, NULL );
	}
}


static void AddBufferText( const char *text, int textLength )
{
	int lineCount;
	int pos, n;

	if ( textLength + curConSize >= maxConSize ) {
		lineCount = SendMessage( s_wcd.hwndBuffer, EM_GETLINECOUNT, 0, 0 );
		// cut off half from total lines count
		lineCount /= 2;
		if ( lineCount <= 1 ) {
			SetWindowText( s_wcd.hwndBuffer, T("") );
		} else {
			pos = SendMessage( s_wcd.hwndBuffer, EM_LINEINDEX, lineCount, 0 );
			SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, pos );
			SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, (LPARAM) TEXT("") );
		}
		curConSize = 0;
	}

	if ( !curConSize )
		curConSize = GetWindowTextLength( s_wcd.hwndBuffer );

	SendMessage( s_wcd.hwndBuffer, EM_GETSEL, (WPARAM)(LPDWORD)&pos, (LPARAM)(LPDWORD)&n );
	if ( pos != curConSize || n != curConSize ) {
		SendMessage( s_wcd.hwndBuffer, EM_SETSEL, curConSize, curConSize );
	}

	// put this text into the windows console
	//SendMessage( s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff );
	SendMessage( s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0 );
	SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, 0, (LPARAM) AtoW( text ) );

	curConSize += textLength;
}



/*
** Sys_SetErrorText
*/
void Sys_SetErrorText( const char *buf )
{
	RECT rect;
	int sth;

	if ( s_wcd.hwndErrorBox ) // already created
		return;

	// remove input field
	DestroyWindow( s_wcd.hwndInputLine );
	s_wcd.hwndInputLine = NULL;

	EnableWindow( s_wcd.hwndButtonClear, FALSE );

	s_wcd.hbrErrorBackground = CreateSolidBrush( ERROR_BG_COLOR );
	SetTimer( s_wcd.hWnd, ERROR_TIMER_ID, 1000, NULL );

	sth = GetStatusBarHeight();
	GetClientRect( s_wcd.hWnd, &rect );

	// shift buffer position
	SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, ERROR_HEIGHT + BORDERH*2, rect.right - BORDERW*2, rect.bottom - sth - ERROR_HEIGHT - BORDERH*3+1, SWP_NOZORDER );

	s_wcd.hwndErrorBox = CreateWindow( T("static"), NULL, WS_CHILD | WS_VISIBLE | SS_SUNKEN,
		BORDERW, BORDERH, rect.right - BORDERW*2, ERROR_HEIGHT,
		s_wcd.hWnd,
		(HMENU)(LRESULT)ERRORBOX_ID,	// child window ID
		g_wv.hInstance, NULL );

	SendMessage( s_wcd.hwndErrorBox, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );
	SetWindowText( s_wcd.hwndErrorBox, AtoW( buf ) );

	Sys_SetStatus( "Fatal error occured" );
}
