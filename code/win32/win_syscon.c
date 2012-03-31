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

#define ERRORBOX_ID		10
#define ERRORTEXT_ID	11

#define EDIT_ID			100
#define INPUT_ID		101

#define BORDERW			3
#define BORDERH			3
#define MAX_CONSIZE		65536

#define EDIT_COLOR		RGB(0x00,0x00,0x10)
#define TEXT_COLOR		RGB(0x40,0xEE,0x20)

field_t console;

typedef struct
{
	HWND		hWnd;
	HWND		hwndBuffer;

	HWND		hwndButtonClear;
	HWND		hwndButtonCopy;
	HWND		hwndButtonQuit;

	HWND		hwndErrorBox;
	HWND		hwndErrorText;

	HBITMAP		hbmLogo;
	HBITMAP		hbmClearBitmap;

	HBRUSH		hbrEditBackground;
	HBRUSH		hbrErrorBackground;

	HFONT		hfBufferFont;
	HFONT		hfButtonFont;

	HWND		hwndInputLine;

	char		errorString[80];

	char		consoleText[512], returnedText[512];
	int			visLevel;
	qboolean	quitOnClose;
	int			windowWidth, windowHeight;
	
	WNDPROC		SysInputLineWndProc;

} WinConData;

static WinConData s_wcd;

static int maxConSize; // up to MAX_CONSIZE
static int curConSize; // up to MAX_CONSIZE
static int conCache = 0;

void Conbuf_BeginPrint( void );
void Conbuf_EndPrint( void );

static void ConClear() 
{
	SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, -1 );
	SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, ( LPARAM ) "" );
	UpdateWindow( s_wcd.hwndBuffer );
	curConSize = 0;
}

static LONG WINAPI ConWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	char *cmdString;
	static qboolean s_timePolarity;

	switch (uMsg)
	{
	case WM_ACTIVATE:
		if ( LOWORD( wParam ) != WA_INACTIVE )
		{
			SetFocus( s_wcd.hwndInputLine );
		}

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

	case WM_CLOSE:
		if ( ( com_dedicated && com_dedicated->integer ) )
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

#if 0	// this draws a background in the edit box, but there are issues with this
			if ( ( hdcScaled = CreateCompatibleDC( ( HDC ) wParam ) ) != 0 )
			{
				if ( SelectObject( ( HDC ) hdcScaled, s_wcd.hbmLogo ) )
				{
					StretchBlt( ( HDC ) wParam, 0, 0, 512, 384, 
							hdcScaled, 0, 0, 512, 384,
							SRCCOPY );
				}
				DeleteDC( hdcScaled );
			}
#endif
			return ( long ) s_wcd.hbrEditBackground;
		}
		else if ( ( HWND ) lParam == s_wcd.hwndErrorBox )
		{
			if ( s_timePolarity & 1 )
			{
				SetBkColor( ( HDC ) wParam, RGB( 0x80, 0x80, 0x80 ) );
				SetTextColor( ( HDC ) wParam, RGB( 0xff, 0x00, 0x00 ) );
			}
			else
			{
				SetBkColor( ( HDC ) wParam, RGB( 0x80, 0x80, 0x80 ) );
				SetTextColor( ( HDC ) wParam, RGB( 0x00, 0x00, 0x00 ) );
			}
			return ( long ) s_wcd.hbrErrorBackground;
		}
		break;

	case WM_COMMAND:
		if ( wParam == COPY_ID )
		{
			SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, -1 );
			SendMessage( s_wcd.hwndBuffer, WM_COPY, 0, 0 );
			SetFocus( s_wcd.hwndInputLine );
		}
		else if ( wParam == QUIT_ID )
		{
			if ( s_wcd.quitOnClose )
			{
				PostQuitMessage( 0 );
			}
			else
			{
				cmdString = CopyString( "quit" );
				Sys_QueEvent( 0, SE_CONSOLE, 0, 0, strlen( cmdString ) + 1, cmdString );
			}
		}
		else if ( wParam == CLEAR_ID )
		{
			ConClear();
			SetFocus( s_wcd.hwndInputLine );
		}
		break;
	case WM_CREATE:
//		s_wcd.hbmLogo = LoadBitmap( g_wv.hInstance, MAKEINTRESOURCE( IDB_BITMAP1 ) );
//		s_wcd.hbmClearBitmap = LoadBitmap( g_wv.hInstance, MAKEINTRESOURCE( IDB_BITMAP2 ) );
		s_wcd.hbrEditBackground = CreateSolidBrush( EDIT_COLOR );
		s_wcd.hbrErrorBackground = CreateSolidBrush( RGB( 0x80, 0x80, 0x80 ) );
		SetTimer( hWnd, 1, 1000, NULL );
		break;
	case WM_ERASEBKGND:
#if 0
	HDC hdcScaled;
	HGDIOBJ oldObject;

#if 1	// a single, large image
		hdcScaled = CreateCompatibleDC( ( HDC ) wParam );
		assert( hdcScaled != 0 );

		if ( hdcScaled )
		{
			oldObject = SelectObject( ( HDC ) hdcScaled, s_wcd.hbmLogo );
			assert( oldObject != 0 );
			if ( oldObject )
			{
				StretchBlt( ( HDC ) wParam, 0, 0, s_wcd.windowWidth, s_wcd.windowHeight, 
						hdcScaled, 0, 0, 512, 384,
						SRCCOPY );
			}
			DeleteDC( hdcScaled );
			hdcScaled = 0;
		}
#else	// a repeating brush
		{
			HBRUSH hbrClearBrush;
			RECT r;

			GetWindowRect( hWnd, &r );

			r.bottom = r.bottom - r.top + 1;
			r.right = r.right - r.left + 1;
			r.top = 0;
			r.left = 0;

			hbrClearBrush = CreatePatternBrush( s_wcd.hbmClearBitmap );

			assert( hbrClearBrush != 0 );

			if ( hbrClearBrush )
			{
				FillRect( ( HDC ) wParam, &r, hbrClearBrush );
				DeleteObject( hbrClearBrush );
			}
		}
#endif
		return 1;
#endif
	    return DefWindowProc( hWnd, uMsg, wParam, lParam );
	case WM_SIZE:
		{
			RECT rect;
			GetClientRect( hWnd, &rect );
			if ( s_wcd.hwndErrorBox )
				SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, BORDERH+31, rect.right - BORDERW*2, rect.bottom - 68 - BORDERH, SWP_NOZORDER );
			else
				SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, BORDERH, rect.right - BORDERW*2, rect.bottom - 61, SWP_NOZORDER );

			SetWindowPos( s_wcd.hwndButtonCopy, HWND_TOP, BORDERW, rect.bottom-24-BORDERH-2, 72, 24, SWP_NOZORDER );
			SetWindowPos( s_wcd.hwndButtonClear, HWND_TOP, BORDERW + 77, rect.bottom-24-BORDERH-2, 72, 24, SWP_NOZORDER );
			SetWindowPos( s_wcd.hwndButtonQuit, HWND_TOP, rect.right - 72 - BORDERW, rect.bottom-24-BORDERH-2, 72, 24, SWP_NOZORDER );

			if ( s_wcd.hwndErrorBox ) {
				SetWindowPos( s_wcd.hwndErrorBox, HWND_TOP, BORDERW, BORDERH+1, rect.right - BORDERW*2, 26, SWP_NOZORDER );
				InvalidateRect( s_wcd.hwndErrorBox, NULL, FALSE );
			}

			if ( s_wcd.hwndInputLine ) {
				SetWindowPos( s_wcd.hwndInputLine, HWND_TOP, BORDERW, rect.bottom - 50 - BORDERH, rect.right - BORDERW*2, 16, SWP_NOZORDER );
				InvalidateRect( s_wcd.hwndInputLine, NULL, FALSE );
			}

			return 0;
		}
	case WM_WINDOWPOSCHANGING:
		{
			LPWINDOWPOS w = (LPWINDOWPOS)lParam;
			if ( w->cy < 152 )
				w->cy = 152;
			if ( w->cx < 234 + BORDERW*2 )
				w->cx = 234 + BORDERW*2;
			return 0;
		}
	case WM_TIMER:
		if ( wParam == 1 )
		{
			s_timePolarity = !s_timePolarity;
			if ( s_wcd.hwndErrorBox )
			{
				InvalidateRect( s_wcd.hwndErrorBox, NULL, FALSE );
			}
		}
		break;
    }

    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

LONG WINAPI InputLineWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	char inputBuffer[1024];

	switch ( uMsg )
	{
	case WM_KILLFOCUS:
		if ( ( HWND ) wParam == s_wcd.hWnd ||
			 ( HWND ) wParam == s_wcd.hwndErrorBox )
		{
			SetFocus( hWnd );
			return 0;
		}
		break;

	case WM_CHAR:
		if ( wParam == VK_RETURN )
		{
			char *s;

			GetWindowText( hWnd, inputBuffer, sizeof( inputBuffer ) );
			s = inputBuffer;
			if ( *s == '\\' || *s == '/' )
				s++;

			strncat( s_wcd.consoleText, s, sizeof( s_wcd.consoleText ) - strlen( s_wcd.consoleText ) - 2 );
			strcat( s_wcd.consoleText, "\n" );
			
			SetWindowText( s_wcd.hwndInputLine, "" );
			Field_Clear( &console );

			Sys_Print( va( "]%s\n", inputBuffer ) );

			return 0;
		}

		if ( wParam == VK_TAB ) {
			DWORD pos;
			GetWindowText( hWnd, console.buffer, sizeof( console.buffer ) );
			SendMessage( hWnd, EM_GETSEL, (WPARAM) &pos, (LPARAM) 0 );
			console.cursor = pos;
			Sys_BeginPrint();
			Field_AutoComplete( &console );
			Sys_EndPrint();
			SetWindowText( hWnd, console.buffer );
			SendMessage( hWnd, EM_SETSEL, console.cursor, console.cursor );
			return 0;
		}
		break;
	}

	return CallWindowProc( s_wcd.SysInputLineWndProc, hWnd, uMsg, wParam, lParam );
}

/*
** Sys_CreateConsole
*/
void Sys_CreateConsole( char *title )
{
	HDC hDC;
	WNDCLASS wc;
	RECT rect;
	const char *DEDCLASS = "Q3 WinConsole";
	int nHeight;
	int swidth, sheight;
	int DEDSTYLE = WS_POPUPWINDOW | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;

	memset( &wc, 0, sizeof( wc ) );

	wc.style         = 0;
	wc.lpfnWndProc   = (WNDPROC) ConWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = g_wv.hInstance;
	wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = (void *)COLOR_WINDOW;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = DEDCLASS;

	if ( !RegisterClass (&wc) )
		return;

	rect.left = 0;
	rect.right = 540;
	rect.top = 0;
	rect.bottom = 452;

	AdjustWindowRect( &rect, DEDSTYLE, FALSE );

	hDC = GetDC( GetDesktopWindow() );
	swidth = GetDeviceCaps( hDC, HORZRES );
	sheight = GetDeviceCaps( hDC, VERTRES );
	ReleaseDC( GetDesktopWindow(), hDC );

	s_wcd.windowWidth = rect.right - rect.left + 1;
	s_wcd.windowHeight = rect.bottom - rect.top + 1;

	s_wcd.hWnd = CreateWindowEx( 0, DEDCLASS,
							   CONSOLE_WINDOW_TITLE, DEDSTYLE,
							   ( swidth - s_wcd.windowWidth ) / 2, 
							   ( sheight - s_wcd.windowHeight ) / 2 , 
							   s_wcd.windowWidth, s_wcd.windowHeight,
							   NULL, NULL, g_wv.hInstance, NULL );

	if ( s_wcd.hWnd == NULL )
	{
		return;
	}

	GetClientRect( s_wcd.hWnd, &rect );

	//
	// create fonts
	//
	hDC = GetDC( s_wcd.hWnd );
	nHeight = -MulDiv( 8, GetDeviceCaps( hDC, LOGPIXELSY), 72);

	s_wcd.hfBufferFont = CreateFont( 12,
									  8,
									  0,
									  0,
									  FW_NORMAL,
									  0,
									  0,
									  0,
									  DEFAULT_CHARSET,
									  OUT_DEFAULT_PRECIS,
									  CLIP_DEFAULT_PRECIS,
									  DEFAULT_QUALITY,
									  FIXED_PITCH,
									  "Terminal" );

	ReleaseDC( s_wcd.hWnd, hDC );

	//
	// create the input line
	//
	s_wcd.hwndInputLine = CreateWindow( "edit", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | 
												ES_LEFT | ES_AUTOHSCROLL,
												BORDERW, rect. bottom - 50 - BORDERH, rect.right - BORDERW*2, 16,
												s_wcd.hWnd, 
												( HMENU ) INPUT_ID,	// child window ID
												g_wv.hInstance, NULL );

	//
	// create the buttons
	//
	s_wcd.hwndButtonCopy = CreateWindow( "button", "copy", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
												BORDERW, rect.bottom-24-BORDERH-2, 72, 24,
												s_wcd.hWnd, 
												( HMENU ) COPY_ID,	// child window ID
												g_wv.hInstance, NULL );

	s_wcd.hwndButtonClear = CreateWindow( "button", "clear", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
												BORDERW + 77, rect.bottom-24-BORDERH-2, 72, 24,
												s_wcd.hWnd, 
												( HMENU ) CLEAR_ID,	// child window ID
												g_wv.hInstance, NULL );

	s_wcd.hwndButtonQuit = CreateWindow( "button", "quit", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
												rect.right - 72 - BORDERW, rect.bottom-24-BORDERH-2, 72, 24,
												s_wcd.hWnd, 
												( HMENU ) QUIT_ID,	// child window ID
												g_wv.hInstance, NULL );

	//
	// create the scrollbuffer
	//
	s_wcd.hwndBuffer = CreateWindow( "edit", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | 
												ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
												BORDERW, BORDERH/*+35*/, rect.right - 2*BORDERW, rect.bottom - 61,
												s_wcd.hWnd, 
												( HMENU ) EDIT_ID,	// child window ID
												g_wv.hInstance, NULL );

	SendMessage( s_wcd.hwndBuffer, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );

	s_wcd.SysInputLineWndProc = ( WNDPROC ) SetWindowLong( s_wcd.hwndInputLine, GWL_WNDPROC, ( long ) InputLineWndProc );
	SendMessage( s_wcd.hwndInputLine, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );

	if ( title && *title )
		SetWindowText( s_wcd.hWnd, title );

	ShowWindow( s_wcd.hWnd, SW_SHOWDEFAULT);
	UpdateWindow( s_wcd.hWnd );
	SetForegroundWindow( s_wcd.hWnd );
	
	SendMessage( s_wcd.hwndBuffer, EM_SETLIMITTEXT, MAX_CONSIZE, 0 );
	maxConSize = SendMessage( s_wcd.hwndBuffer, EM_GETLIMITTEXT, 0, 0 );
	ConClear();

	SendMessage( s_wcd.hwndInputLine, EM_SETLIMITTEXT, MAX_EDIT_LINE, 0 );
	SetFocus( s_wcd.hwndInputLine );
	Field_Clear( &console );

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
** Sys_ConsoleInput
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

char conBuffer[MAXPRINTMSG];
int  conBufPos;

/* 
 =================
 Conbuf_AppendText
 =================
*/
void Conbuf_AppendText( const char *msg )
{
	char buffer[MAXPRINTMSG*2]; // reserve space for CR-LF expansion
	char *b = buffer;
	int bufLen, n, pos;
	static qboolean newline = qfalse;

	n = strlen( msg );

	if ( conCache && n + conBufPos < MAXPRINTMSG-1 && msg != conBuffer ) {
		strcpy( conBuffer + conBufPos, msg );
		conBufPos += n;
		return;
	}

	if ( conBufPos ) {
		conBufPos = 0;
		Conbuf_AppendText( conBuffer );
	}

	// if the message is REALLY long, use just the last portion of it
	if ( n > (MAXPRINTMSG - 1) ) 
	{
		msg += n - (MAXPRINTMSG - 1);		
	}

	// insert skipped newline from previous message
	if ( newline ) {
		newline = qfalse;
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
	if ( b - buffer >= 2 && b[-1] == '\n' && b[-2] == '\r' ) {
		newline = qtrue;
		b -= 2;
	}

	*b = '\0';
	bufLen = b - buffer;

	// FIXME: check for maxConSize < MAX_PRINTMSG*2
	if ( bufLen + curConSize >= maxConSize ) {
		n = SendMessage( s_wcd.hwndBuffer, EM_GETLINECOUNT, 0, 0 );
		// cut off half from total lines count
		n = n / 2;
		if ( n <= 0 ) 
			n = 1;
		pos = SendMessage( s_wcd.hwndBuffer, EM_LINEINDEX, n, 0 );
		SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, pos );
		SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, (LPARAM) "" );
	}

	curConSize = GetWindowTextLength( s_wcd.hwndBuffer );	
	SendMessage( s_wcd.hwndBuffer, EM_SETSEL, curConSize, curConSize );

	// put this text into the windows console
	//SendMessage( s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff );
	SendMessage( s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0 );
	SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, 0, (LPARAM) buffer );
	curConSize += bufLen;
}

void Conbuf_BeginPrint( void ) {
	conCache = 1;
}

void Conbuf_EndPrint( void ) {
	conCache = 0;
	if ( conBufPos ) {
		Conbuf_AppendText( "" );
	}
}


/*
** Sys_SetErrorText
*/
void Sys_SetErrorText( const char *buf )
{
	RECT rect;
	Q_strncpyz( s_wcd.errorString, buf, sizeof( s_wcd.errorString ) );

	Sys_EndPrint(); // flush any pending messages

	if ( !s_wcd.hwndErrorBox )
	{
		GetClientRect( s_wcd.hWnd, &rect );

		SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, BORDERH+31, rect.right - BORDERW*2, rect.bottom - 68 - BORDERH, SWP_NOZORDER );

		s_wcd.hwndErrorBox = CreateWindow( "static", NULL, WS_CHILD | WS_VISIBLE | SS_SUNKEN,
													BORDERW, BORDERH+1, rect.right - BORDERW*2, 26,
													s_wcd.hWnd, 
													( HMENU ) ERRORBOX_ID,	// child window ID
													g_wv.hInstance, NULL );
		SendMessage( s_wcd.hwndErrorBox, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );
		SetWindowText( s_wcd.hwndErrorBox, s_wcd.errorString );

		DestroyWindow( s_wcd.hwndInputLine );
		s_wcd.hwndInputLine = NULL;
	}
}
