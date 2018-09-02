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
// win_input.c -- win32 mouse and joystick code
// 02/21/97 JCB Added extended DirectInput code to support external controllers.

#include "../client/client.h"
#include "win_local.h"
#include "glw_win.h"


typedef struct {
	int			oldButtonState;

	qboolean	mouseActive;
	qboolean	mouseInitialized;
	qboolean	mouseStartupDelayed; // delay mouse init to try again when we have a window
} WinMouseVars_t;

static WinMouseVars_t s_wmv;

static POINT window_center;
static POINT client_center;

#ifdef USE_MIDI
//
// MIDI definitions
//
static void IN_StartupMIDI( void );
static void IN_ShutdownMIDI( void );

#define MAX_MIDIIN_DEVICES	8

typedef struct {
	int			numDevices;
	MIDIINCAPS	caps[MAX_MIDIIN_DEVICES];

	HMIDIIN		hMidiIn;
} MidiInfo_t;

static MidiInfo_t s_midiInfo;
#endif

#ifdef USE_JOYSTICK
//
// Joystick definitions
//
#define	JOY_MAX_AXES		6				// X, Y, Z, R, U, V

typedef struct {
	qboolean	avail;
	int			id;			// joystick number
	JOYCAPS		jc;

	int			oldbuttonstate;
	int			oldpovstate;

	JOYINFOEX	ji;
} joystickInfo_t;

static	joystickInfo_t	joy;
#endif


#ifdef USE_MIDI
cvar_t	*in_midi;
cvar_t	*in_midiport;
cvar_t	*in_midichannel;
cvar_t	*in_mididevice;
#endif

cvar_t	*in_minimize;
cvar_t	*in_nograb;
cvar_t	*in_lagged;

cvar_t	*in_mouse;
cvar_t  *in_logitechbug;

#ifdef USE_JOYSTICK
cvar_t	*in_joystick;
cvar_t	*in_joyBallScale;
cvar_t	*in_debugJoystick;
cvar_t	*joy_threshold;
#endif

qboolean	in_appactive;

// forward-referenced functions
#ifdef USE_JOYSTICK
void IN_StartupJoystick (void);
void IN_JoyMove(void);
#endif

#ifdef USE_MIDI
static void MidiInfo_f( void );
#endif

/*
============================================================

WIN32 MOUSE CONTROL

============================================================
*/


/*
================
IN_MouseActive
================
*/
static qboolean IN_MouseActive( void )
{
	return ( in_nograb->integer == 0 && s_wmv.mouseActive );
}


/*
================
IN_UpdateWindow

Called when window gets resized/moved
Updates window center and clip region
================
*/
void IN_UpdateWindow( RECT *window_rect, qboolean updateClipRegion )
{
	RECT		rect, rc;

	if ( !window_rect ) 
		window_rect = &rect;

	if ( !GetWindowRect( g_wv.hWnd, window_rect ) )
		return;

	if ( GetClientRect( g_wv.hWnd, &rc ) ) {
		client_center.x = rc.right / 2;
		client_center.y = rc.bottom / 2;
		window_center = client_center;
		ClientToScreen( g_wv.hWnd, &window_center );
	} else {
		window_center.x = ( window_rect->right + window_rect->left )/2;
		window_center.y = ( window_rect->top + window_rect->bottom )/2;
		client_center = window_center;
		ScreenToClient( g_wv.hWnd, &client_center );
	}

	if ( updateClipRegion && s_wmv.mouseActive && gw_active ) {
		ClipCursor( window_rect );
	}
}


/*
================
IN_CaptureMouse
================
*/
static void IN_CaptureMouse( const RECT *clipRect )
{
	while( ShowCursor( FALSE ) >= 0 )
		;
	SetCursorPos( window_center.x, window_center.y );
	SetCapture( g_wv.hWnd );
	ClipCursor( clipRect );
}


/*
================
IN_ActivateWin32Mouse
================
*/
static void IN_ActivateWin32Mouse( void )
{
	RECT window_rect;
	IN_UpdateWindow( &window_rect, qfalse );
	IN_CaptureMouse( &window_rect );
}


/*
================
IN_DeactivateWin32Mouse
================
*/
static void IN_DeactivateWin32Mouse( void ) 
{
	IN_UpdateWindow( NULL, qfalse );
	ClipCursor( NULL );
	SetCursorPos( window_center.x, window_center.y );
	ReleaseCapture();
	while ( ShowCursor( TRUE ) < 0 )
		;
}


/*
================
IN_Win32Mouse
================
*/
static void IN_Win32Mouse( int *mx, int *my ) 
{
	POINT		current_pos;

	// find mouse movement
	GetCursorPos( &current_pos );

	*mx = current_pos.x - window_center.x;
	*my = current_pos.y - window_center.y;
}


/*
============================================================

RAW INPUT MOUSE CONTROL

============================================================
*/
#define ISWINXP(sys) (sys.dwPlatformId==VER_PLATFORM_WIN32_NT && \
	((sys.dwMajorVersion==5 && sys.dwMinorVersion>=1)||(sys.dwMajorVersion>5)))

typedef UINT (WINAPI *PGRRID)(PRAWINPUTDEVICE pRawInputDevices, PUINT puiNumDevices, UINT cbSize);
typedef BOOL (WINAPI *PRRID)(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize);
typedef UINT (WINAPI *PGRID)(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);

static	PGRRID	GRRID;
static	PRRID	RRID;
static	PGRID	GRID;

static	BOOL	raw_inited = FALSE;
static	BOOL	raw_activated = FALSE;


/*
================
IN_InitRawMouse
================
*/
static BOOL IN_InitRawMouse( void ) {

    HMODULE dll;

#ifndef idx64
	if ( !ISWINXP( g_wv.osversion ) ) {
		return FALSE; // operating system is not supported
	}
#endif

    if ( raw_inited ) {
		return TRUE; // already inited
    }

	GRRID = NULL;
	RRID  = NULL;
	GRID  = NULL;

	dll = GetModuleHandle( T("user32") ); // should always success
	if ( !dll ) {
		return FALSE;
	}

	GRRID = (PGRRID) GetProcAddress( dll, "GetRegisteredRawInputDevices" );
	RRID  = (PRRID) GetProcAddress( dll, "RegisterRawInputDevices" );
	GRID  = (PGRID) GetProcAddress( dll, "GetRawInputData" );

	//CloseHandle( dll );

	if ( !GRRID || !RRID || !GRID ) {
        return FALSE;
    }

	raw_inited = TRUE;

	return TRUE;
}


/*
================
IN_ActivateRawMouse
================
*/
static void IN_ActivateRawMouse( void )
{
	RECT		window_rect;
	RAWINPUTDEVICE Rid;
	UINT num;
	int cnt;

	if ( raw_activated ) 
	{
		return; // already activated
	}

	num = 1;
	cnt = GRRID( &Rid, &num, sizeof( Rid ) );
	if ( cnt < 0 || !g_wv.hWnd ) 
	{
		Com_Printf( S_COLOR_YELLOW "Error getting registered raw input devices\n" );
		return; // error getting registered raw input devices
	}

	IN_UpdateWindow( &window_rect, qfalse );

	if ( cnt >= 1 && Rid.hwndTarget == g_wv.hWnd ) 
	{
		// device already exists?
	}
	else 
	{
		Rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
		Rid.usUsage = HID_USAGE_GENERIC_MOUSE;
		Rid.dwFlags = RIDEV_NOLEGACY; // skip all WM_*BUTTON* and WM_MOUSEMOVE stuff
		Rid.hwndTarget = g_wv.hWnd;

		if( !RRID( &Rid, 1, sizeof( Rid ) ) ) 
		{
			Com_Printf( S_COLOR_YELLOW "Error registering raw input device\n" );
			return;
		}
	}

	IN_CaptureMouse( &window_rect );

	raw_activated = TRUE;
}


/*
================
IN_RawMouse
================
*/
static void IN_RawMouse( int *mx, int *my ) {

	*mx = g_wv.raw_mx;
	*my = g_wv.raw_my;
}


/*
================
IN_DeactivateRawMouse
================
*/
static void IN_DeactivateRawMouse( void ) 
{
	if ( raw_activated ) 
	{
		RAWINPUTDEVICE Rid;

		Rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
		Rid.usUsage = HID_USAGE_GENERIC_MOUSE;
		Rid.dwFlags = RIDEV_REMOVE;
		Rid.hwndTarget = NULL;
		if( !RRID( &Rid, 1, sizeof( Rid ) ) ) 
		{
			Com_Printf( S_COLOR_YELLOW "Error removing raw input device\n" );
			return;
		}
		
	}
	raw_activated = FALSE;
}


/*
============================================================

DIRECT INPUT MOUSE CONTROL

============================================================
*/

#undef DEFINE_GUID

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        const GUID name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUID(GUID_SysMouse,   0x6F1D2B60,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_XAxis,   0xA36D02E0,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_YAxis,   0xA36D02E1,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_ZAxis,   0xA36D02E2,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);


#define DINPUT_BUFFERSIZE           64
#define iDirectInputCreate(a,b,c,d)	pDirectInputCreate(a,b,c,d)

HRESULT (WINAPI *pDirectInputCreate)(HINSTANCE hinst, DWORD dwVersion,
	LPDIRECTINPUT * lplpDirectInput, LPUNKNOWN punkOuter);

static HINSTANCE hInstDI;

typedef struct MYDATA {
	LONG  lX;                   // X axis goes here
	LONG  lY;                   // Y axis goes here
	LONG  lZ;                   // Z axis goes here
	BYTE  bButtonA;             // One button goes here
	BYTE  bButtonB;             // Another button goes here
	BYTE  bButtonC;             // Another button goes here
	BYTE  bButtonD;             // Another button goes here
} MYDATA;

static DIOBJECTDATAFORMAT rgodf[] = {
  { &GUID_XAxis,    FIELD_OFFSET(MYDATA, lX),       DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { &GUID_YAxis,    FIELD_OFFSET(MYDATA, lY),       DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { &GUID_ZAxis,    FIELD_OFFSET(MYDATA, lZ),       0x80000000 | DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonA), DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonB), DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonC), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonD), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
};

#define NUM_OBJECTS (sizeof(rgodf) / sizeof(rgodf[0]))

// NOTE TTimo: would be easier using c_dfDIMouse or c_dfDIMouse2 
static DIDATAFORMAT	df = {
	sizeof(DIDATAFORMAT),       // this structure
	sizeof(DIOBJECTDATAFORMAT), // size of object data format
	DIDF_RELAXIS,               // absolute axis coordinates
	sizeof(MYDATA),             // device data size
	NUM_OBJECTS,                // number of objects
	rgodf,                      // and here they are
};

static LPDIRECTINPUT		g_pdi;
static LPDIRECTINPUTDEVICE	g_pMouse;

static void IN_DIMouse( int *mx, int *my );

/*
========================
IN_InitDIMouse
========================
*/
static qboolean IN_InitDIMouse( void ) {
    HRESULT		hr;
	int			x, y;
	DIPROPDWORD	dipdw = {
		{
			sizeof(DIPROPDWORD),        // diph.dwSize
			sizeof(DIPROPHEADER),       // diph.dwHeaderSize
			0,                          // diph.dwObj
			DIPH_DEVICE,                // diph.dwHow
		},
		DINPUT_BUFFERSIZE,              // dwData
	};

	Com_DPrintf( "Initializing DirectInput...\n");

	if (!hInstDI) {
		hInstDI = LoadLibrary( TEXT( "dinput.dll" ) );
		
		if (hInstDI == NULL) {
			Com_DPrintf ("Couldn't load dinput.dll\n");
			return qfalse;
		}
	}

	if (!pDirectInputCreate) {
		pDirectInputCreate = (HRESULT (WINAPI *)(HINSTANCE, DWORD, LPDIRECTINPUT *, LPUNKNOWN))
			GetProcAddress(hInstDI,"DirectInputCreateA");

		if (!pDirectInputCreate) {
			Com_DPrintf ("Couldn't get DI proc addr\n");
			return qfalse;
		}
	}

	// register with DirectInput and get an IDirectInput to play with.
	hr = iDirectInputCreate( g_wv.hInstance, DIRECTINPUT_VERSION, &g_pdi, NULL);

	if (FAILED(hr)) {
		Com_DPrintf ("iDirectInputCreate failed\n");
		return qfalse;
	}

	// obtain an interface to the system mouse device.
	hr = IDirectInput_CreateDevice(g_pdi, &GUID_SysMouse, &g_pMouse, NULL);

	if (FAILED(hr)) {
		Com_DPrintf ("Couldn't open DI mouse device\n");
		return qfalse;
	}

	// set the data format to "mouse format".
	hr = IDirectInputDevice_SetDataFormat(g_pMouse, &df);

	if (FAILED(hr)) 	{
		Com_DPrintf ("Couldn't set DI mouse format\n");
		return qfalse;
	}

	// set the cooperativity level.
	hr = IDirectInputDevice_SetCooperativeLevel(g_pMouse, g_wv.hWnd,
			DISCL_EXCLUSIVE | DISCL_FOREGROUND);

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=50
	if (FAILED(hr)) {
		Com_DPrintf ("Couldn't set DI coop level\n");
		return qfalse;
	}


	// set the buffer size to DINPUT_BUFFERSIZE elements.
	// the buffer size is a DWORD property associated with the device
	hr = IDirectInputDevice_SetProperty(g_pMouse, DIPROP_BUFFERSIZE, &dipdw.diph);

	if (FAILED(hr)) {
		Com_DPrintf ("Couldn't set DI buffersize\n");
		return qfalse;
	}

	// clear any pending samples
	IN_DIMouse( &x, &y );
	IN_DIMouse( &x, &y );

	Com_DPrintf( "DirectInput initialized.\n");
	return qtrue;
}


/*
==========================
IN_ShutdownDIMouse
==========================
*/
static void IN_ShutdownDIMouse( void ) {
    if (g_pMouse) {
		IDirectInputDevice_Release(g_pMouse);
		g_pMouse = NULL;
	}

    if (g_pdi) {
		IDirectInput_Release(g_pdi);
		g_pdi = NULL;
	}
}


/*
==========================
IN_ActivateDIMouse
==========================
*/
static void IN_ActivateDIMouse( void ) {
	HRESULT		hr;

	if (!g_pMouse) {
		return;
	}

	// we may fail to reacquire if the window has been recreated
	hr = IDirectInputDevice_Acquire( g_pMouse );
	if (FAILED(hr)) {
		if ( !IN_InitDIMouse() ) {
			Com_Printf ("Falling back to Win32 mouse support...\n");
			Cvar_Set( "in_mouse", "-1" );
		}
	}
	while (ShowCursor (FALSE) >= 0)
        ;
}


/*
==========================
IN_DeactivateDIMouse
==========================
*/
static void IN_DeactivateDIMouse( void ) {
	if (!g_pMouse) {
		return;
	}
	IDirectInputDevice_Unacquire( g_pMouse );
}


/*
===================
IN_DIMouse
===================
*/
static void IN_DIMouse( int *mx, int *my ) {
	DIDEVICEOBJECTDATA	od;
	DIMOUSESTATE		state;
	DWORD				dwElements;
	HRESULT				hr;
	int value;

	if ( !g_pMouse ) {
		return;
	}

	// fetch new events
	for (;;)
	{
		dwElements = 1;

		hr = IDirectInputDevice_GetDeviceData(g_pMouse,
				sizeof(DIDEVICEOBJECTDATA), &od, &dwElements, 0);
		if ((hr == DIERR_INPUTLOST) || (hr == DIERR_NOTACQUIRED)) {
			IDirectInputDevice_Acquire(g_pMouse);
			return;
		}

		/* Unable to read data or no data available */
		if ( FAILED(hr) ) {
			break;
		}

		if ( dwElements == 0 ) {
			break;
		}

		switch (od.dwOfs) {
		case DIMOFS_BUTTON0:
			if (od.dwData & 0x80)
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE1, qtrue, 0, NULL );
			else
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE1, qfalse, 0, NULL );
			break;

		case DIMOFS_BUTTON1:
			if (od.dwData & 0x80)
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE2, qtrue, 0, NULL );
			else
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE2, qfalse, 0, NULL );
			break;
			
		case DIMOFS_BUTTON2:
			if (od.dwData & 0x80)
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE3, qtrue, 0, NULL );
			else
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE3, qfalse, 0, NULL );
			break;

		case DIMOFS_BUTTON3:
			if (od.dwData & 0x80)
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE4, qtrue, 0, NULL );
			else
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE4, qfalse, 0, NULL );
			break;
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=50
		case DIMOFS_Z:
			value = od.dwData;
			if (value == 0) {

			} else if (value < 0) {
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
			} else {
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
			}
			break;
		}
	}

	// read the raw delta counter and ignore
	// the individual sample time / values
	hr = IDirectInputDevice_GetDeviceState(g_pMouse,
			sizeof(DIDEVICEOBJECTDATA), &state);
	if ( FAILED(hr) ) {
		*mx = *my = 0;
		return;
	}
	*mx = state.lX;
	*my = state.lY;
}

/*
============================================================

  MOUSE CONTROL

============================================================
*/

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
static void IN_ActivateMouse( void )
{
	if ( !s_wmv.mouseInitialized )
		return;

	if ( !in_mouse->integer ) {
		s_wmv.mouseActive = qfalse;
		return;
	}

	if ( s_wmv.mouseActive )
		return;

	s_wmv.mouseActive = qtrue;

	if ( in_mouse->integer == -1 ) {
		IN_ActivateWin32Mouse();
	} else {
		if ( raw_inited )
			IN_ActivateRawMouse();
        else
			IN_ActivateDIMouse();
	}
}


/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
static void IN_DeactivateMouse( void )
{
	if ( !s_wmv.mouseActive )
		return;

	if ( !s_wmv.mouseInitialized )
		return;

	s_wmv.oldButtonState = 0;
	s_wmv.mouseActive = qfalse;

	IN_DeactivateDIMouse();
	IN_DeactivateWin32Mouse();
	IN_DeactivateRawMouse();
}


/*
===========
IN_StartupMouse
===========
*/
static void IN_StartupMouse( void )
{
	s_wmv.mouseInitialized = qfalse;
	s_wmv.mouseStartupDelayed = qfalse;

	if ( in_mouse->integer == 0 ) {
		Com_DPrintf( "Mouse control not active.\n" );
		return;
	}

#ifndef idx64
	// nt4.0 direct input is screwed up
	if ( ( g_wv.osversion.dwPlatformId == VER_PLATFORM_WIN32_NT ) &&
		 ( g_wv.osversion.dwMajorVersion == 4 ) )
	{
		Com_DPrintf( "Disallowing DirectInput on NT 4.0\n" );
		Cvar_Set( "in_mouse", "-1" );
	}
#endif

	if ( in_mouse->integer == -1 ) {
		Com_DPrintf( "Skipping check for Raw/DirectInput\n" ); 
	} else {

		if ( !g_wv.hWnd ) {
			Com_DPrintf( "No window for mouse init, delaying\n" );
			s_wmv.mouseStartupDelayed = qtrue;
			return;
		}

		if ( IN_InitRawMouse() ) {
			s_wmv.mouseInitialized = qtrue;
			Com_Printf( "Raw mouse input initialized.\n" );
			return;
		}

		if ( IN_InitDIMouse() ) {
			s_wmv.mouseInitialized = qtrue;
			return;
		}
		Com_DPrintf( "Falling back to Win32 mouse support...\n" );
	}

	s_wmv.mouseInitialized = qtrue;
}


/*
===========
IN_Win32MouseEvent
===========
*/
void IN_Win32MouseEvent( int x, int y, int mstate )
{
	int dx, dy;

	if ( !IN_MouseActive() )
		return;

	if ( in_lagged->integer ) {
		
	} else {
		dx = x - g_wv.mouse.x;
		dy = y - g_wv.mouse.y;
		g_wv.mouse.x = x;
		g_wv.mouse.y = y;
		if ( dx || dy ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, dx, dy, 0, NULL );
		}
	}

#define CHECK_BUTTON(button) \
	if ( mstate & (1<<(button-1)) ) { \
		if ( !(s_wmv.oldButtonState & (1<<(button-1))) ) \
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button, qtrue, 0, NULL ); \
	} else { \
		if 	( s_wmv.oldButtonState & (1<<(button-1)) ) \
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button, qfalse, 0, NULL ); \
	}

	// perform button actions
	CHECK_BUTTON(1);
	CHECK_BUTTON(2);
	CHECK_BUTTON(3);
	CHECK_BUTTON(4);
	CHECK_BUTTON(5);

#undef CHECK_BUTTON

	s_wmv.oldButtonState = mstate;
}


/*
===========
IN_RawMouseEvent
===========
*/
void IN_RawMouseEvent( LPARAM lParam )
{
	UINT err, dwSize;
	union {
		BYTE lpb[40];
		RAWINPUT raw;
	} u;

	if ( !IN_MouseActive() )
		return;

	dwSize = sizeof( u.raw );

	err = GRID( (HRAWINPUT) lParam, RID_INPUT, &u.raw, &dwSize, sizeof( RAWINPUTHEADER ) );
	if ( err == -1 )
		return;

	if ( u.raw.header.dwType != RIM_TYPEMOUSE || u.raw.data.mouse.usFlags != MOUSE_MOVE_RELATIVE )
		return;

	if ( u.raw.data.mouse.lLastX || u.raw.data.mouse.lLastY ) {
		if ( in_lagged->integer ) {
			g_wv.raw_mx += u.raw.data.mouse.lLastX;
			g_wv.raw_my += u.raw.data.mouse.lLastY;
		} else {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, u.raw.data.mouse.lLastX,
				u.raw.data.mouse.lLastY, 0, NULL );
		}
	}

	if ( !u.raw.data.mouse.usButtonFlags )
		return;

#define CHECK_RAW_BUTTON(button) \
	if ( u.raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_##button##_DOWN ) \
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button, qtrue, 0, NULL ); \
	if ( u.raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_##button##_UP ) \
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button, qfalse, 0, NULL )

	CHECK_RAW_BUTTON(1);
	CHECK_RAW_BUTTON(2);
	CHECK_RAW_BUTTON(3);
	CHECK_RAW_BUTTON(4);
	CHECK_RAW_BUTTON(5);

#undef CHECK_RAW_BUTTON

	if ( u.raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL ) 
	{
		short data = u.raw.data.mouse.usButtonData;
		if ( data > 0 )
		{
			while( data > 0 )
			{
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
				data -= 120;
			}
		}
		else
		{
			while( data < 0 )
			{
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
				data += 120;
			}
		}
	}
}


/*
===========
IN_MouseMove
===========
*/
static void IN_MouseMove( void ) {
	int		mx = 0, my = 0;

	if ( g_pMouse ) {
		IN_DIMouse( &mx, &my );
	} else {
		if ( in_lagged->integer ) {
			if ( raw_activated ) {
				IN_RawMouse( &mx, &my );
			} else {
				IN_Win32Mouse( &mx, &my );
			}
		}
		g_wv.raw_mx = 0;
		g_wv.raw_my = 0;

		// force the mouse to the center, so there's room to move
		SetCursorPos( window_center.x, window_center.y );

		// reset delta base
		g_wv.mouse = client_center;
	}

	if ( !mx && !my ) {
		return;
	}

	Sys_QueEvent( 0, SE_MOUSE, mx, my, 0, NULL );
}


/*
	in_minimize processing
*/

extern int HotKey;
extern void Win_RemoveHotkey( void );
extern void Win_AddHotkey( void );

extern int Win32_GetKey( const char **s, char *buf, int buflen );

/*
=========================================================================

=========================================================================
*/
static void IN_GetHotkey( cvar_t *var, int *pHotKey ) {

	char	kset[256], buf[64];
	const char *s;
	int		i, code;

	if ( !pHotKey )
		return;

	*pHotKey = 0;

	if ( !var )
		return;

	s = var->string;

	if ( !s ) {
		Win_RemoveHotkey();
		return;
	}

	memset( kset, 0, sizeof( kset ) );

	for ( i = 0; i < 4; i++ ) 
	{
		code = Win32_GetKey( &s, buf, sizeof( buf ) );
		if ( code == 0 ) // no more tokens
			break;
		if ( code < 0 || kset[code & 0xFF ] ||
			(	code != VK_CONTROL && code != VK_LCONTROL && code != VK_RCONTROL
				&& code != VK_MENU && code != VK_LMENU && code != VK_RMENU
				&& code != VK_SHIFT && code != VK_LSHIFT && code != VK_RSHIFT
				&& code != (VK_LWIN|HK_MOD_LWIN) && code != (VK_RWIN|HK_MOD_RWIN)
				&& *pHotKey & 0xFF )) {
			Com_Printf( "%s:"S_COLOR_YELLOW" invalid token %s\n", var->name, buf );
			*pHotKey = 0;
			break;
		}
		kset[code & 0xFF] = 1;
		switch ( code ) {
			case VK_MENU:	 *pHotKey |= HK_MOD_ALT; break;
			case VK_LMENU:	 *pHotKey |= (HK_MOD_ALT|HK_MOD_LALT); break;
			case VK_RMENU:	 *pHotKey |= (HK_MOD_ALT|HK_MOD_RALT); break;
			case VK_CONTROL: *pHotKey |= HK_MOD_CONTROL; break;
			case VK_LCONTROL:*pHotKey |= (HK_MOD_CONTROL|HK_MOD_LCONTROL); break;
			case VK_RCONTROL:*pHotKey |= (HK_MOD_CONTROL|HK_MOD_RCONTROL); break;
			case VK_SHIFT:	 *pHotKey |= HK_MOD_SHIFT; break;
			case VK_LSHIFT:	 *pHotKey |= HK_MOD_SHIFT|HK_MOD_LSHIFT; break;
			case VK_RSHIFT:	 *pHotKey |= HK_MOD_SHIFT|HK_MOD_RSHIFT; break;
			case VK_LWIN:	 *pHotKey |= HK_MOD_WIN; break;
			case (VK_LWIN|HK_MOD_LWIN): *pHotKey |= (HK_MOD_WIN|HK_MOD_LWIN); break;
			case (VK_RWIN|HK_MOD_RWIN): *pHotKey |= (HK_MOD_WIN|HK_MOD_RWIN); break;
			default:		 *pHotKey |= (code & 0xFF); break;
		};
    }

	if ( i == 0 ) 
	{
		Win_RemoveHotkey();
		return;
	}

	if ( *pHotKey == VK_OEM_3 // '~'
			|| *pHotKey == VK_RETURN
			|| *pHotKey == HK_MOD_WIN
			|| *pHotKey == (HK_MOD_WIN|HK_MOD_LWIN)
			|| *pHotKey == (HK_MOD_WIN|HK_MOD_RWIN)
			|| *pHotKey == (VK_RETURN|HK_MOD_ALT)
			|| *pHotKey == (HK_MOD_CONTROL|VK_PAUSE)) {
		Com_Printf( "%s:"S_COLOR_YELLOW" invalid hotkey %s\n", var->name, var->string );
		*pHotKey = 0;
	}

	//Com_Printf("GetHotkey: %06X\n",*HotKey);
	Win_RemoveHotkey();
	Win_AddHotkey();
}


/*
===========
IN_Minimize
===========
*/
static void IN_Minimize( void )
{
	if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) )
		ShowWindow( g_wv.hWnd, SW_MINIMIZE );
}


/*
===========
IN_Startup
===========
*/
void IN_Startup( void ) {
	Com_DPrintf( "\n------- Input Initialization -------\n" );
	IN_StartupMouse();
#ifdef USE_JOYSTICK
	IN_StartupJoystick ();
#endif
#ifdef USE_MIDI
	IN_StartupMIDI();
#endif
	Com_DPrintf( "------------------------------------\n" );

	in_mouse->modified = qfalse;
#ifdef USE_JOYSTICK
	in_joystick->modified = qfalse;
#endif
}


/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void ) {
	IN_DeactivateMouse();
	IN_ShutdownDIMouse();
#ifdef USE_MIDI
	IN_ShutdownMIDI();
	Cmd_RemoveCommand( "midiinfo" );
#endif
	Cmd_RemoveCommand( "minimize" );
}


/*
===========
IN_Init
===========
*/
void IN_Init( void ) {

#ifdef USE_MIDI
	// MIDI input controler variables
	in_midi = Cvar_Get( "in_midi", "0", CVAR_ARCHIVE );
	in_midiport = Cvar_Get( "in_midiport", "1", CVAR_ARCHIVE );
	in_midichannel = Cvar_Get( "in_midichannel", "1", CVAR_ARCHIVE );
	in_mididevice = Cvar_Get( "in_mididevice", "0", CVAR_ARCHIVE );
	Cmd_AddCommand( "midiinfo", MidiInfo_f );
#endif

#ifdef USE_JOYSTICK
	// joystick variables
	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE | CVAR_LATCH );
	in_joyBallScale = Cvar_Get( "in_joyBallScale", "0.02", CVAR_ARCHIVE );
	in_debugJoystick = Cvar_Get( "in_debugjoystick", "0", CVAR_TEMP );
	joy_threshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE );
#endif

	// mouse variables
	in_mouse = Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE |CVAR_LATCH );
	Cvar_CheckRange( in_mouse, "-1", "1", CV_INTEGER );
	Cvar_SetDescription( in_mouse,
		"Mouse data input source:\n" \
		"  0 - disable mouse input\n" \
		"  1 - di/raw mouse\n" \
		" -1 - win32 mouse" );
		
	in_nograb = Cvar_Get( "in_nograb", "0", 0 );
	in_lagged = Cvar_Get( "in_lagged", "0", 0 );
	Cvar_SetDescription( in_lagged, 
		"Mouse movement processing order:\n" \
		" 0 - before rendering\n" \
		" 1 - before framerate limiter" );

	in_logitechbug = Cvar_Get( "in_logitechbug", "0", CVAR_ARCHIVE_ND );

	in_minimize	= Cvar_Get( "in_minimize", "", CVAR_ARCHIVE | CVAR_LATCH );
	IN_GetHotkey( in_minimize, &HotKey );

	Cmd_AddCommand( "minimize", IN_Minimize );

	IN_Startup();
}


/*
===========
IN_Activate

Called when the main window gains or loses focus.
The window may have been destroyed and recreated
between a deactivate and an activate.
===========
*/
void IN_Activate( qboolean active ) {
	in_appactive = active;

	if ( !active )
	{
		IN_DeactivateMouse();
	}
}


/*
==================
IN_Frame

Called every frame, even if not generating commands
==================
*/
void IN_Frame( void ) {
	// post joystick events
#ifdef USE_JOYSTICK
	IN_JoyMove();
#endif

	if ( !s_wmv.mouseInitialized ) {
		if ( s_wmv.mouseStartupDelayed && g_wv.hWnd ) {
			// some application may steal our keyboard input focus and foreground state
			// but windows will NOT send any WM_KILLFOCUS or WM_ACTIVATE messages to us
			// which will result in stuck mouse cursor in current foreground application
			if ( GetForegroundWindow() == g_wv.hWnd ) {
				Com_Printf( "Proceeding with delayed mouse init\n" );
				IN_StartupMouse();
				s_wmv.mouseStartupDelayed = qfalse;
			}
		}
		return;
	}

	if ( Key_GetCatcher() & KEYCATCH_CONSOLE ) {
		// temporarily deactivate if not in the game and
		// running on the desktop with multimonitor configuration
		if ( !glw_state.cdsFullscreen || glw_state.monitorCount > 1 )
		{
			IN_DeactivateMouse();
			WIN_EnableAltTab();
			//WIN_DisableHook();
			return;
		}
	}

	if ( !in_appactive || in_nograb->integer ) {
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse();

	WIN_DisableAltTab();
	WIN_EnableHook();

	// post events to the system que
	IN_MouseMove();
}


/*
=========================================================================

JOYSTICK

=========================================================================
*/

#ifdef USE_JOYSTICK
/* 
=============== 
IN_StartupJoystick 
=============== 
*/  
void IN_StartupJoystick (void) { 
	int			numdevs;
	MMRESULT	mmr;

	// assume no joystick
	joy.avail = qfalse; 

	if (! in_joystick->integer ) {
		Com_DPrintf ("Joystick is not active.\n");
		return;
	}

	// verify joystick driver is present
	if ((numdevs = joyGetNumDevs ()) == 0)
	{
		Com_DPrintf ("joystick not found -- driver not present\n");
		return;
	}

	// cycle through the joystick ids for the first valid one
	mmr = 0;
	for (joy.id=0 ; joy.id<numdevs ; joy.id++)
	{
		Com_Memset (&joy.ji, 0, sizeof(joy.ji));
		joy.ji.dwSize = sizeof(joy.ji);
		joy.ji.dwFlags = JOY_RETURNCENTERED;

		if ((mmr = joyGetPosEx (joy.id, &joy.ji)) == JOYERR_NOERROR)
			break;
	} 

	// abort startup if we didn't find a valid joystick
	if (mmr != JOYERR_NOERROR)
	{
		Com_DPrintf ("joystick not found -- no valid joysticks (%x)\n", mmr);
		return;
	}

	// get the capabilities of the selected joystick
	// abort startup if command fails
	Com_Memset (&joy.jc, 0, sizeof(joy.jc));
	if ((mmr = joyGetDevCaps (joy.id, &joy.jc, sizeof(joy.jc))) != JOYERR_NOERROR)
	{
		Com_DPrintf ("joystick not found -- invalid joystick capabilities (%x)\n", mmr); 
		return;
	}

	Com_DPrintf( "Joystick found.\n" );
	Com_DPrintf( "Pname: %s\n", joy.jc.szPname );
	Com_DPrintf( "OemVxD: %s\n", joy.jc.szOEMVxD );
	Com_DPrintf( "RegKey: %s\n", joy.jc.szRegKey );

	Com_DPrintf( "Numbuttons: %i / %i\n", joy.jc.wNumButtons, joy.jc.wMaxButtons );
	Com_DPrintf( "Axis: %i / %i\n", joy.jc.wNumAxes, joy.jc.wMaxAxes );
	Com_DPrintf( "Caps: 0x%x\n", joy.jc.wCaps );
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		Com_DPrintf( "HASPOV\n" );
	} else {
		Com_DPrintf( "no POV\n" );
	}

	// old button and POV states default to no buttons pressed
	joy.oldbuttonstate = 0;
	joy.oldpovstate = 0;

	// mark the joystick as available
	joy.avail = qtrue; 
}

/*
===========
JoyToF
===========
*/
float JoyToF( int value ) {
	float	fValue;

	// move centerpoint to zero
	value -= 32768;

	// convert range from -32768..32767 to -1..1 
	fValue = (float)value / 32768.0;

	if ( fValue < -1 ) {
		fValue = -1;
	}
	if ( fValue > 1 ) {
		fValue = 1;
	}
	return fValue;
}

int JoyToI( int value ) {
	// move centerpoint to zero
	value -= 32768;

	return value;
}

int	joyDirectionKeys[16] = {
	K_LEFTARROW, K_RIGHTARROW,
	K_UPARROW, K_DOWNARROW,
	K_JOY16, K_JOY17,
	K_JOY18, K_JOY19,
	K_JOY20, K_JOY21,
	K_JOY22, K_JOY23,

	K_JOY24, K_JOY25,
	K_JOY26, K_JOY27
};

/*
===========
IN_JoyMove
===========
*/
void IN_JoyMove( void ) {
	float	fAxisValue;
	int		i;
	DWORD	buttonstate, povstate;
	int		x, y;

	// verify joystick is available and that the user wants to use it
	if ( !joy.avail ) {
		return; 
	}

	// collect the joystick data, if possible
	Com_Memset (&joy.ji, 0, sizeof(joy.ji));
	joy.ji.dwSize = sizeof(joy.ji);
	joy.ji.dwFlags = JOY_RETURNALL;

	if ( joyGetPosEx (joy.id, &joy.ji) != JOYERR_NOERROR ) {
		// read error occurred
		// turning off the joystick seems too harsh for 1 read error,
		// but what should be done?
		// Com_Printf ("IN_ReadJoystick: no response\n");
		// joy.avail = false;
		return;
	}

	if ( in_debugJoystick->integer ) {
		Com_Printf( "%8x %5i %5.2f %5.2f %5.2f %5.2f %6i %6i\n", 
			JoyToI( joy.ji.dwButtons ),
			JoyToI( joy.ji.dwPOV ),
			JoyToF( joy.ji.dwXpos ), JoyToF( joy.ji.dwYpos ),
			JoyToF( joy.ji.dwZpos ), JoyToF( joy.ji.dwRpos ),
			JoyToI( joy.ji.dwUpos ), JoyToI( joy.ji.dwVpos ) );
	}

	// loop through the joystick buttons
	// key a joystick event or auxillary event for higher number buttons for each state change
	buttonstate = joy.ji.dwButtons;
	for ( i=0 ; i < joy.jc.wNumButtons ; i++ ) {
		if ( (buttonstate & (1<<i)) && !(joy.oldbuttonstate & (1<<i)) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qtrue, 0, NULL );
		}
		if ( !(buttonstate & (1<<i)) && (joy.oldbuttonstate & (1<<i)) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qfalse, 0, NULL );
		}
	}
	joy.oldbuttonstate = buttonstate;

	povstate = 0;

	// convert main joystick motion into 6 direction button bits
	for (i = 0; i < joy.jc.wNumAxes && i < 4 ; i++) {
		// get the floating point zero-centered, potentially-inverted data for the current axis
		fAxisValue = JoyToF( (&joy.ji.dwXpos)[i] );

		if ( fAxisValue < -joy_threshold->value ) {
			povstate |= (1<<(i*2));
		} else if ( fAxisValue > joy_threshold->value ) {
			povstate |= (1<<(i*2+1));
		}
	}

	// convert POV information from a direction into 4 button bits
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		if ( joy.ji.dwPOV != JOY_POVCENTERED ) {
			if (joy.ji.dwPOV == JOY_POVFORWARD)
				povstate |= 1<<12;
			if (joy.ji.dwPOV == JOY_POVBACKWARD)
				povstate |= 1<<13;
			if (joy.ji.dwPOV == JOY_POVRIGHT)
				povstate |= 1<<14;
			if (joy.ji.dwPOV == JOY_POVLEFT)
				povstate |= 1<<15;
		}
	}

	// determine which bits have changed and key an auxillary event for each change
	for (i=0 ; i < 16 ; i++) {
		if ( (povstate & (1<<i)) && !(joy.oldpovstate & (1<<i)) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qtrue, 0, NULL );
		}

		if ( !(povstate & (1<<i)) && (joy.oldpovstate & (1<<i)) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qfalse, 0, NULL );
		}
	}
	joy.oldpovstate = povstate;

	// if there is a trackball like interface, simulate mouse moves
	if ( joy.jc.wNumAxes >= 6 ) {
		x = JoyToI( joy.ji.dwUpos ) * in_joyBallScale->value;
		y = JoyToI( joy.ji.dwVpos ) * in_joyBallScale->value;
		if ( x || y ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, x, y, 0, NULL );
		}
	}
}
#endif

/*
=========================================================================

MIDI

=========================================================================
*/

#ifdef USE_MIDI
static void MIDI_NoteOff( int note )
{
	int qkey;

	qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 )
		return;

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qfalse, 0, NULL );
}

static void MIDI_NoteOn( int note, int velocity )
{
	int qkey;

	if ( velocity == 0 )
		MIDI_NoteOff( note );

	qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 )
		return;

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qtrue, 0, NULL );
}

static void CALLBACK MidiInProc( HMIDIIN hMidiIn, UINT uMsg, DWORD dwInstance, 
								 DWORD dwParam1, DWORD dwParam2 )
{
	int message;

	switch ( uMsg )
	{
	case MIM_OPEN:
		break;
	case MIM_CLOSE:
		break;
	case MIM_DATA:
		message = dwParam1 & 0xff;

		// note on
		if ( ( message & 0xf0 ) == 0x90 )
		{
			if ( ( ( message & 0x0f ) + 1 ) == in_midichannel->integer )
				MIDI_NoteOn( ( dwParam1 & 0xff00 ) >> 8, ( dwParam1 & 0xff0000 ) >> 16 );
		}
		else if ( ( message & 0xf0 ) == 0x80 )
		{
			if ( ( ( message & 0x0f ) + 1 ) == in_midichannel->integer )
				MIDI_NoteOff( ( dwParam1 & 0xff00 ) >> 8 );
		}
		break;
	case MIM_LONGDATA:
		break;
	case MIM_ERROR:
		break;
	case MIM_LONGERROR:
		break;
	}

//	Sys_QueEvent( sys_msg_time, SE_KEY, wMsg, qtrue, 0, NULL );
}

static void MidiInfo_f( void )
{
	int i;

	const char *enableStrings[] = { "disabled", "enabled" };

	Com_Printf( "\nMIDI control:       %s\n", enableStrings[in_midi->integer != 0] );
	Com_Printf( "port:               %d\n", in_midiport->integer );
	Com_Printf( "channel:            %d\n", in_midichannel->integer );
	Com_Printf( "current device:     %d\n", in_mididevice->integer );
	Com_Printf( "number of devices:  %d\n", s_midiInfo.numDevices );
	for ( i = 0; i < s_midiInfo.numDevices; i++ )
	{
		if ( i == Cvar_VariableIntegerValue( "in_mididevice" ) )
			Com_Printf( "***" );
		else
			Com_Printf( "..." );
		Com_Printf(    "device %2d:       %s\n", i, s_midiInfo.caps[i].szPname );
		Com_Printf( "...manufacturer ID: 0x%hx\n", s_midiInfo.caps[i].wMid );
		Com_Printf( "...product ID:      0x%hx\n", s_midiInfo.caps[i].wPid );

		Com_Printf( "\n" );
	}
}

static void IN_StartupMIDI( void )
{
	int i;

	if ( !Cvar_VariableIntegerValue( "in_midi" ) )
		return;

	//
	// enumerate MIDI IN devices
	//
	s_midiInfo.numDevices = midiInGetNumDevs();

	for ( i = 0; i < s_midiInfo.numDevices; i++ )
	{
		midiInGetDevCaps( i, &s_midiInfo.caps[i], sizeof( s_midiInfo.caps[i] ) );
	}

	//
	// open the MIDI IN port
	//
	if ( midiInOpen( &s_midiInfo.hMidiIn, 
		             in_mididevice->integer,
					 ( unsigned long ) MidiInProc,
					 ( unsigned long ) NULL,
					 CALLBACK_FUNCTION ) != MMSYSERR_NOERROR )
	{
		Com_DPrintf( "WARNING: could not open MIDI device %d: '%s'\n",
								in_mididevice->integer , s_midiInfo.caps[( int ) in_mididevice->value].szPname );
		return;
	}

	midiInStart( s_midiInfo.hMidiIn );
}

static void IN_ShutdownMIDI( void )
{
	if ( s_midiInfo.hMidiIn )
	{
		midiInClose( s_midiInfo.hMidiIn );
	}
	Com_Memset( &s_midiInfo, 0, sizeof( s_midiInfo ) );
}
#endif
