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
** GLW_IMP.C
**
** This file contains ALL Linux specific stuff having to do with the
** OpenGL refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_Shutdown
** GLimp_SetGamma
**
*/

#include <termios.h>
#include <sys/ioctl.h>
#ifdef __linux__
  #include <sys/stat.h>
  #include <sys/vt.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>

// bk001204
#include <dlfcn.h>

// bk001206 - from my Heretic2 by way of Ryan's Fakk2
// Needed for the new X11_PendingInput() function.
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "../client/client.h"
#include "linux_local.h"
#include "unix_glw.h"
#include "../renderer/qgl.h"
#include "../renderer/qgl_linked.h"

#include <GL/glx.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>

#include <X11/XKBlib.h>

#if !defined(__sun)
#include <X11/extensions/Xxf86dga.h>
#include <X11/extensions/xf86vmode.h>
#endif

#if defined(__sun)
#include <X11/Sunkeysym.h>
#endif

#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h>

#ifdef _XF86DGA_H_
#define HAVE_XF86DGA
#endif

typedef enum
{
  RSERR_OK,

  RSERR_INVALID_FULLSCREEN,
  RSERR_INVALID_MODE,

  RSERR_UNKNOWN
} rserr_t;

glwstate_t glw_state;

static Display *dpy = NULL;
static int scrnum;
static Window win = 0;
static GLXContext ctx = NULL;
static Atom wmDeleteEvent = None;

static int window_width = 0;
static int window_height = 0;
static qboolean window_created = qfalse;
static qboolean window_focused = qfalse;

static int desktop_width = 0;
static int desktop_height = 0;
static int desktop_x = 0;
static int desktop_y = 0;

static qboolean desktop_ok = qfalse;

#define KEY_MASK (KeyPressMask | KeyReleaseMask)
#define MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask )
#define X_MASK (KEY_MASK | MOUSE_MASK | VisibilityChangeMask | StructureNotifyMask | FocusChangeMask )

static qboolean mouse_avail;
static qboolean mouse_active = qfalse;
static int mwx, mwy;
static int mx = 0, my = 0;

// Time mouse was reset, we ignore the first 50ms of the mouse to allow settling of events
static int mouseResetTime = 0;
#define MOUSE_RESET_DELAY 50

static cvar_t *in_mouse;
static cvar_t *in_dgamouse; // user pref for dga mouse
static cvar_t *in_shiftedKeys; // obey modifiers for certain keys in non-console (comma, numbers, etc)

cvar_t *in_subframe;
cvar_t *in_nograb; // this is strictly for developers

cvar_t *in_forceCharset;

#ifdef USE_JOYSTICK
cvar_t   *in_joystick      = NULL;
cvar_t   *in_joystickDebug = NULL;
cvar_t   *joy_threshold    = NULL;
#endif

cvar_t   *vid_xpos;
cvar_t   *vid_ypos;

static qboolean vidmode_ext = qfalse;
static qboolean vidmode_active = qfalse;

#ifdef HAVE_XF86DGA
static int vidmode_MajorVersion = 0, vidmode_MinorVersion = 0; // major and minor of XF86VidExtensions

// gamma value of the X display before we start playing with it
static XF86VidModeGamma vidmode_InitialGamma;

static XF86VidModeModeInfo **vidmodes = NULL;
#endif /* HAVE_XF86DGA */

static int mouse_accel_numerator;
static int mouse_accel_denominator;
static int mouse_threshold;

// xrandr
static qboolean xrandr_ext = qfalse;
static qboolean xrandr_active = qfalse;
static qboolean xrandr_gamma = qfalse;

unsigned short old_gamma[3][4096]; // backup
int old_gamma_size;

static int win_x, win_y;

void RestoreMonitorMode( void );
void LocateCurrentMonitor( int x, int y, int w, int h );

qboolean BackupMonitorGamma( void );
void RestoreMonitorGamma( void );
void SetMonitorGamma( unsigned short *red, unsigned short *green, unsigned short *blue, int size );

/*****************************************************************************
** KEYBOARD
** NOTE TTimo the keyboard handling is done with KeySyms
**   that means relying on the keyboard mapping provided by X
**   in-game it would probably be better to use KeyCode (i.e. hardware key codes)
**   you would still need the KeySyms in some cases, such as for the console and all entry textboxes
**     (cause there's nothing worse than a qwerty mapping on a french keyboard)
**
** you can turn on some debugging and verbose of the keyboard code with #define KBD_DBG
******************************************************************************/

//#define KBD_DBG
static const char s_keytochar[ 128 ] =
{
//0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F 
 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  '1',  '2',  '3',  '4',  '5',  '6',  // 0
 '7',  '8',  '9',  '0',  '-',  '=',  0x8,  0x9,  'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 1
 'o',  'p',  '[',  ']',  0x0,  0x0,  'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 2
 '\'', 0x0,  0x0,  '\\', 'z',  'x',  'c',  'v',  'b',  'n',  'm',  ',',  '.',  '/',  0x0,  '*',  // 3

//0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F 
 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  '!',  '@',  '#',  '$',  '%',  '^',  // 4
 '&',  '*',  '(',  ')',  '_',  '+',  0x8,  0x9,  'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 5
 'O',  'P',  '{',  '}',  0x0,  0x0,  'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 6
 '"',  0x0,  0x0,  '|',  'Z',  'X',  'C',  'V',  'B',  'N',  'M',  '<',  '>',  '?',  0x0,  '*',  // 7
};


int Sys_XTimeToSysTime( Time xtime );
void IN_ActivateMouse( void );
void IN_DeactivateMouse( void );
qboolean IN_MouseActive( void );


static char *XLateKey( XKeyEvent *ev, int *key )
{
  static unsigned char buf[64];
  static unsigned char bufnomod[2];
  KeySym keysym;
  int XLookupRet;

  *key = 0;

  XLookupRet = XLookupString(ev, (char*)buf, sizeof(buf), &keysym, 0);
#ifdef KBD_DBG
  Com_Printf( "XLookupString ret: %d buf: %s keysym: %x\n", XLookupRet, buf, (int)keysym) ;
#endif

  if (!in_shiftedKeys->integer) {
    // also get a buffer without modifiers held
    ev->state = 0;
    XLookupRet = XLookupString(ev, (char*)bufnomod, sizeof(bufnomod), &keysym, 0);
#ifdef KBD_DBG
    Com_Printf( "XLookupString (minus modifiers) ret: %d buf: %s keysym: %x\n", XLookupRet, buf, (int)keysym );
#endif
  } else {
    bufnomod[0] = '\0';
  }

  switch (keysym)
  {
  case XK_grave:
  case XK_twosuperior:
    *key = K_CONSOLE;
    buf[0] = '\0';
    return (char*)buf;

  case XK_KP_Page_Up:
  case XK_KP_9:  *key = K_KP_PGUP; break;
  case XK_Page_Up:   *key = K_PGUP; break;

  case XK_KP_Page_Down:
  case XK_KP_3: *key = K_KP_PGDN; break;
  case XK_Page_Down:   *key = K_PGDN; break;

  case XK_KP_Home: *key = K_KP_HOME; break;
  case XK_KP_7: *key = K_KP_HOME; break;
  case XK_Home:  *key = K_HOME; break;

  case XK_KP_End:
  case XK_KP_1:   *key = K_KP_END; break;
  case XK_End:   *key = K_END; break;

  case XK_KP_Left: *key = K_KP_LEFTARROW; break;
  case XK_KP_4: *key = K_KP_LEFTARROW; break;
  case XK_Left:  *key = K_LEFTARROW; break;

  case XK_KP_Right: *key = K_KP_RIGHTARROW; break;
  case XK_KP_6: *key = K_KP_RIGHTARROW; break;
  case XK_Right:  *key = K_RIGHTARROW;    break;

  case XK_KP_Down:
  case XK_KP_2:  if ( Key_GetCatcher() && (buf[0] || bufnomod[0]) )
                   *key = 0;
                 else
                   *key = K_KP_DOWNARROW;
                 break;

  case XK_Down:  *key = K_DOWNARROW; break;

  case XK_KP_Up:
  case XK_KP_8:  if ( Key_GetCatcher() && (buf[0] || bufnomod[0]) )
                   *key = 0;
                 else
                   *key = K_KP_UPARROW;
                 break;

  case XK_Up:    *key = K_UPARROW;   break;

  case XK_Escape: *key = K_ESCAPE;    break;

  case XK_KP_Enter: *key = K_KP_ENTER;  break;
  case XK_Return: *key = K_ENTER;    break;

  case XK_Tab:    *key = K_TAB;      break;

  case XK_F1:    *key = K_F1;       break;

  case XK_F2:    *key = K_F2;       break;

  case XK_F3:    *key = K_F3;       break;

  case XK_F4:    *key = K_F4;       break;

  case XK_F5:    *key = K_F5;       break;

  case XK_F6:    *key = K_F6;       break;

  case XK_F7:    *key = K_F7;       break;

  case XK_F8:    *key = K_F8;       break;

  case XK_F9:    *key = K_F9;       break;

  case XK_F10:    *key = K_F10;      break;

  case XK_F11:    *key = K_F11;      break;

  case XK_F12:    *key = K_F12;      break;

    // bk001206 - from Ryan's Fakk2
    //case XK_BackSpace: *key = 8; break; // ctrl-h
  case XK_BackSpace: *key = K_BACKSPACE; break; // ctrl-h

  case XK_KP_Delete:
  case XK_KP_Decimal: *key = K_KP_DEL; break;
  case XK_Delete: *key = K_DEL; break;

  case XK_Pause:  *key = K_PAUSE;    break;

  case XK_Shift_L:
  case XK_Shift_R:  *key = K_SHIFT;   break;

  case XK_Execute:
  case XK_Control_L:
  case XK_Control_R:  *key = K_CTRL;  break;

  case XK_Alt_L:
  case XK_Meta_L:
  case XK_Alt_R:
  case XK_Meta_R: *key = K_ALT;     break;

  case XK_KP_Begin: *key = K_KP_5;  break;

  case XK_Insert:   *key = K_INS; break;
  case XK_KP_Insert:
  case XK_KP_0: *key = K_KP_INS; break;

  case XK_KP_Multiply: *key = '*'; break;
  case XK_KP_Add:  *key = K_KP_PLUS; break;
  case XK_KP_Subtract: *key = K_KP_MINUS; break;
  case XK_KP_Divide: *key = K_KP_SLASH; break;

  case XK_exclam: *key = '1'; break;
  case XK_at: *key = '2'; break;
  case XK_numbersign: *key = '3'; break;
  case XK_dollar: *key = '4'; break;
  case XK_percent: *key = '5'; break;
  case XK_asciicircum: *key = '6'; break;
  case XK_ampersand: *key = '7'; break;
  case XK_asterisk: *key = '8'; break;
  case XK_parenleft: *key = '9'; break;
  case XK_parenright: *key = '0'; break;

  // weird french keyboards ..
  // NOTE: console toggle is hardcoded in cl_keys.c, can't be unbound
  //   cleaner would be .. using hardware key codes instead of the key syms
  //   could also add a new K_KP_CONSOLE
  //case XK_twosuperior: *key = '~'; break;

  case XK_space:
  case XK_KP_Space: *key = K_SPACE; break;

  case XK_Menu:	*key = K_MENU; break;
  case XK_Print: *key = K_PRINT; break;
  case XK_Super_L:
  case XK_Super_R: *key = K_SUPER; break;
  case XK_Num_Lock: *key = K_KP_NUMLOCK; break;
  case XK_Caps_Lock: *key = K_CAPSLOCK; break;
  case XK_Scroll_Lock: *key = K_SCROLLOCK; break;
  case XK_backslash: *key = '\\'; break;

  default:
    //Com_Printf( "unknown keysym: %08X\n", keysym );
    if (XLookupRet == 0)
    {
      if (com_developer->value)
      {
        Com_Printf( "Warning: XLookupString failed on KeySym %d\n", (int)keysym );
      }
      buf[0] = '\0';
      return (char*)buf;
    }
    else
    {
      // XK_* tests failed, but XLookupString got a buffer, so let's try it
      if (in_shiftedKeys->integer) {
        *key = *(unsigned char *)buf;
        if (*key >= 'A' && *key <= 'Z')
          *key = *key - 'A' + 'a';
        // if ctrl is pressed, the keys are not between 'A' and 'Z', for instance ctrl-z == 26 ^Z ^C etc.
        // see https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=19
        else if (*key >= 1 && *key <= 26)
          *key = *key + 'a' - 1;
      } else {
        *key = bufnomod[0];
      }
    }
    break;
  }

  return (char*)buf;
}


// ========================================================================
// makes a null cursor
// ========================================================================

static Cursor CreateNullCursor( Display *display, Window root )
{
	Pixmap cursormask;
	XGCValues xgc;
	GC gc;
	XColor dummycolour;
	Cursor cursor;

	cursormask = XCreatePixmap( display, root, 1, 1, 1/*depth*/ );
	xgc.function = GXclear;
	gc = XCreateGC( display, cursormask, GCFunction, &xgc );
	XFillRectangle( display, cursormask, gc, 0, 0, 1, 1 );
	dummycolour.pixel = 0;
	dummycolour.red = 0;
	dummycolour.flags = 04;
	cursor = XCreatePixmapCursor( display, cursormask, cursormask, &dummycolour, &dummycolour, 0, 0 );
	XFreePixmap( display, cursormask );
	XFreeGC( display, gc );
	return cursor;
}


static void install_mouse_grab( void )
{
	int res;

	// move pointer to destination window area
	XWarpPointer( dpy, None, win, 0, 0, 0, 0, window_width / 2, window_height / 2 );

	XSync( dpy, False );

	// hide cursor
	XDefineCursor( dpy, win, CreateNullCursor( dpy, win ) );

	// save old mouse settings
	XGetPointerControl( dpy, &mouse_accel_numerator, &mouse_accel_denominator, &mouse_threshold );

	// do this earlier?
	res = XGrabPointer( dpy, win, False, MOUSE_MASK, GrabModeAsync, GrabModeAsync, win, None, CurrentTime );
	if ( res != GrabSuccess )
	{
		//Com_Printf( S_COLOR_YELLOW "Warning: XGrabPointer() failed\n" );
	}
	else
	{
		// set new mouse settings
		XChangePointerControl( dpy, True, True, 1, 1, 1 );
	}

	XSync( dpy, False );

	mouseResetTime = Sys_Milliseconds();

#ifdef HAVE_XF86DGA
	if ( in_dgamouse->value )
	{
		int MajorVersion, MinorVersion;

		if ( !XF86DGAQueryVersion( dpy, &MajorVersion, &MinorVersion ) )
		{
			// unable to query, probalby not supported, force the setting to 0
			Com_Printf( "Failed to detect XF86DGA Mouse\n" );
			Cvar_Set( "in_dgamouse", "0" );
		}
		else
		{
			XF86DGADirectVideo( dpy, DefaultScreen( dpy ), XF86DGADirectMouse );
			XWarpPointer( dpy, None, win, 0, 0, 0, 0, window_width / 2, window_height / 2 );
		}
	}
	else
#endif /* HAVE_XF86DGA */
	{
		mwx = window_width / 2;
		mwy = window_height / 2;
		mx = my = 0;
	}

	XSync( dpy, False );
}


static void install_kb_grab( void )
{
	int res;

	res = XGrabKeyboard( dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime );
	if ( res != GrabSuccess )
	{
		//Com_Printf( S_COLOR_YELLOW "Warning: XGrabKeyboard() failed\n" );
	}

	XSync( dpy, False );
}


static void uninstall_mouse_grab( void )
{
#ifdef HAVE_XF86DGA
	if ( in_dgamouse->integer )
	{
		if ( com_developer->integer )
		{
			Com_Printf( "DGA Mouse - Disabling DGA DirectVideo\n" );
		}
		XF86DGADirectVideo( dpy, DefaultScreen( dpy ), 0 );
	}
#endif /* HAVE_XF86DGA */

	// restore mouse settings
	XChangePointerControl( dpy, qtrue, qtrue, mouse_accel_numerator, 
		mouse_accel_denominator, mouse_threshold );

	XWarpPointer( dpy, None, win, 0, 0, 0, 0, window_width / 2, window_height / 2 );

	XUngrabPointer( dpy, CurrentTime );
	XUngrabKeyboard( dpy, CurrentTime );

	// show cursor
	XUndefineCursor( dpy, win );

	XSync( dpy, False );
}


static void uninstall_kb_grab( void )
{
	XUngrabKeyboard( dpy, CurrentTime );

	XSync( dpy, False );
}


// bk001206 - from Ryan's Fakk2
/**
 * XPending() actually performs a blocking read 
 *  if no events available. From Fakk2, by way of
 *  Heretic2, by way of SDL, original idea GGI project.
 * The benefit of this approach over the quite
 *  badly behaved XAutoRepeatOn/Off is that you get
 *  focus handling for free, which is a major win
 *  with debug and windowed mode. It rests on the
 *  assumption that the X server will use the
 *  same timestamp on press/release event pairs 
 *  for key repeats.
 */
static qboolean X11_PendingInput( void )
{
	assert(dpy != NULL);

	// Flush the display connection and look to see if events are queued
	XFlush( dpy );

	if ( XEventsQueued( dpy, QueuedAlready ) )
	{
		return qtrue;
	}

	// More drastic measures are required -- see if X is ready to talk
	{
		static struct timeval zero_time;
		int x11_fd;
		fd_set fdset;

		x11_fd = ConnectionNumber( dpy );
		FD_ZERO( &fdset );
		FD_SET( x11_fd, &fdset );
		if ( select( x11_fd+1, &fdset, NULL, NULL, &zero_time ) == 1 )
		{
			return( XPending( dpy ) );
		}
	}

	// Oh well, nothing is ready ..
	return qfalse;
}


static qboolean repeated_press( XEvent *event )
{
	XEvent        peek;

	assert( dpy != NULL );

	if ( X11_PendingInput() )
	{
		XPeekEvent( dpy, &peek );

		if ( ( peek.type == KeyPress ) &&
			 ( peek.xkey.keycode == event->xkey.keycode ) &&
			 ( peek.xkey.time == event->xkey.time ) )
		{
			return qtrue;
		}
	}

	return qfalse;
}


static qboolean WindowMinimized( Display *dpy, Window win )
{
	unsigned long i, num_items, bytes_after;
	Atom actual_type, *atoms, nws, nwsh;
	int actual_format;

	nws = XInternAtom( dpy, "_NET_WM_STATE", True );
	if ( nws == BadValue || nws == None )
		return qfalse;

	nwsh = XInternAtom( dpy, "_NET_WM_STATE_HIDDEN", True );
	if ( nwsh == BadValue || nwsh == None )
		return qfalse;

	atoms = NULL;

	XGetWindowProperty( dpy, win, nws, 0, 0x7FFFFFFF, False, XA_ATOM,
		&actual_type, &actual_format, &num_items,
		&bytes_after, (unsigned char**)&atoms );

	for ( i = 0; i < num_items; i++ )
	{
		if ( atoms[i] == nwsh )
		{
			XFree( atoms );
			return qtrue;
		}
	}

	XFree( atoms );
	return qfalse;
}


static qboolean directMap( const byte chr )
{
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


void HandleX11Events( void )
{
	XEvent event;
	int b;
	int key;
	qboolean dowarp = qfalse;
	char *p;
	int dx, dy;
	int t = 0; // default to 0 in case we don't set
	qboolean btn_press;
	char buf[2];

	if ( !dpy )
		return;

	while( XPending( dpy ) )
	{
		XNextEvent( dpy, &event );

		switch( event.type )
		{

		case ClientMessage:

			if ( event.xclient.data.l[0] == wmDeleteEvent )
				Com_Quit_f();
			break;

		case KeyPress:
			// Com_Printf("^2K+^7 %08X\n", event.xkey.keycode );
			t = Sys_XTimeToSysTime( event.xkey.time );
			if ( event.xkey.keycode == 0x31 )
			{
				key = K_CONSOLE;
				p = "";
			}
			else
			{
				int shift = (event.xkey.state & 1);
				p = XLateKey( &event.xkey, &key );
				if ( *p && event.xkey.keycode == 0x5B )
				{
					p = ".";
				}
				else
				if ( !directMap( *p ) && event.xkey.keycode < 0x3F )
				{
					char ch;
					ch = s_keytochar[ event.xkey.keycode ];
					if ( ch >= 'a' && ch <= 'z' )
					{
						unsigned int capital;
						XkbGetIndicatorState( dpy, XkbUseCoreKbd, &capital );
						capital &= 1;
						if ( capital ^ shift )
						{
							ch = ch - 'a' + 'A';
						}
					}
					else
					{
						ch = s_keytochar[ event.xkey.keycode | (shift<<6) ];
					}
					buf[0] = ch;
					buf[1] = '\0';
					p = buf;
				}
			}
			if (key)
			{
				Sys_QueEvent( t, SE_KEY, key, qtrue, 0, NULL );
			}
			while (*p)
			{
				Sys_QueEvent( t, SE_CHAR, *p++, 0, 0, NULL );
			}
			break; // case KeyPress

		case KeyRelease:

			if ( repeated_press( &event ) )
				break; // XNextEvent( dpy, &event )

			t = Sys_XTimeToSysTime( event.xkey.time );
#if 0
			Com_Printf("^5K-^7 %08X %s\n",
				event.xkey.keycode,
				X11_PendingInput()?"pending":"");
#endif
			XLateKey( &event.xkey, &key );
			Sys_QueEvent( t, SE_KEY, key, qfalse, 0, NULL );

			break; // case KeyRelease

		case MotionNotify:
			if ( IN_MouseActive() )
			{
				t = Sys_XTimeToSysTime( event.xkey.time );
#ifdef HAVE_XF86DGA
				if ( in_dgamouse->value )
				{
					mx += event.xmotion.x_root;
					my += event.xmotion.y_root;
					if (t - mouseResetTime > MOUSE_RESET_DELAY )
					{
						Sys_QueEvent( t, SE_MOUSE, mx, my, 0, NULL );
					}
					mx = my = 0;
				} 
				else
#endif // HAVE_XF86DGA
				{
					// If it's a center motion, we've just returned from our warp
					if ( event.xmotion.x == window_width/2 && event.xmotion.y == window_height/2 )
					{
						mwx = window_width/2;
						mwy = window_height/2;
						if (t - mouseResetTime > MOUSE_RESET_DELAY )
						{
							Sys_QueEvent( t, SE_MOUSE, mx, my, 0, NULL );
						}
						mx = my = 0;
						break;
					}

					dx = ((int)event.xmotion.x - mwx);
					dy = ((int)event.xmotion.y - mwy);
					mx += dx;
					my += dy;
					mwx = event.xmotion.x;
					mwy = event.xmotion.y;
					dowarp = qtrue;
				} // if ( !in_dgamouse->value )
			} // if ( mouse_active )
			break;

		case ButtonPress:
		case ButtonRelease:
			if ( !IN_MouseActive() )
				break;

			if ( event.type == ButtonPress )
				btn_press = qtrue;
			else
				btn_press = qfalse;

			t = Sys_XTimeToSysTime( event.xkey.time );
			// NOTE TTimo there seems to be a weird mapping for K_MOUSE1 K_MOUSE2 K_MOUSE3 ..
			b = -1;
			switch ( event.xbutton.button )
			{
				case 1: b = 0; break; // K_MOUSE1
				case 2: b = 2; break; // K_MOUSE3
				case 3: b = 1; break; // K_MOUSE2
				case 4: Sys_QueEvent( t, SE_KEY, K_MWHEELUP, btn_press, 0, NULL ); break;
				case 5: Sys_QueEvent( t, SE_KEY, K_MWHEELDOWN, btn_press, 0, NULL ); break;
				case 6: b = 3; break; // K_MOUSE4
				case 7: b = 4; break; // K_MOUSE5
				case 8: case 9:       // K_AUX1..K_AUX8
				case 10: case 11:
				case 12: case 13:
				case 14: case 15:
						Sys_QueEvent( t, SE_KEY, event.xbutton.button - 8 + K_AUX1, 
							btn_press, 0, NULL ); break;
			}
			if ( b != -1 ) // K_MOUSE1..K_MOUSE5
			{
				Sys_QueEvent( t, SE_KEY, K_MOUSE1 + b, btn_press, 0, NULL );
			}
			break; // case ButtonPress/ButtonRelease

		case CreateNotify:
			win_x = event.xcreatewindow.x;
			win_y = event.xcreatewindow.y;
			break;

		case ConfigureNotify:
			gw_minimized = WindowMinimized( dpy, win );
			Com_DPrintf( "ConfigureNotify gw_minimized: %i\n", gw_minimized );
			win_x = event.xconfigure.x;
			win_y = event.xconfigure.y;
			
			if ( !glw_state.cdsFullscreen && window_created )
			{
				Cvar_Set( "vid_xpos", va( "%i", win_x ) );
				Cvar_Set( "vid_ypos", va( "%i", win_y ) );
				LocateCurrentMonitor( win_x, win_y,
					event.xconfigure.width,
					event.xconfigure.height );
			}
			Key_ClearStates();
			break;

		case FocusIn:
		case FocusOut:
			if ( event.type == FocusIn ) {
				window_focused = qtrue;
				Com_DPrintf( "FocusIn\n" );
			} else {
				window_focused = qfalse;
				Com_DPrintf( "FocusOut\n" );
			}
			Key_ClearStates();
			break;
		}

	}

	if ( dowarp )
	{
		XWarpPointer( dpy, None, win, 0, 0, 0, 0, window_width/2, window_height/2 );
	}
}


// NOTE TTimo for the tty console input, we didn't rely on those .. 
//   it's not very surprising actually cause they are not used otherwise
void KBD_Init( void )
{

}


void KBD_Close( void )
{

}


/*
================
IN_ActivateMouse
================
*/
void IN_ActivateMouse( void )
{
	if ( !mouse_avail || !dpy || !win )
	{
		return;
	}

	if ( !mouse_active )
	{
		install_mouse_grab();
		install_kb_grab();
		if ( in_dgamouse->integer && in_nograb->integer ) // force dga mouse to 0 if using nograb
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
		mouse_active = qtrue;
	}
}


/*
================
IN_DeactivateMouse
================
*/
void IN_DeactivateMouse( void )
{
	if ( !mouse_avail || !dpy || !win )
	{
		return;
	}

	if ( mouse_active )
	{
		uninstall_mouse_grab();
		uninstall_kb_grab();
		if ( in_dgamouse->integer && in_nograb->integer ) // force dga mouse to 0 if using nograb
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
		mouse_active = qfalse;
	}
}


/*
================
IN_MouseActive
================
*/
qboolean IN_MouseActive( void )
{
	return ( in_nograb->integer == 0 && mouse_active );
}


static qboolean BuildGammaRampTable( unsigned char *red, unsigned char *green, unsigned char *blue, int gammaRampSize, unsigned short table[3][4096] )
{
	int i, j;
	int m, m1;
	int shift;

	switch ( gammaRampSize )
	{
		case 256: shift = 0; break;
		case 512: shift = 1; break;
		case 1024: shift = 2; break;
		case 2048: shift = 3; break;
		case 4096: shift = 4; break;
		default:
			Com_Printf( "Unsupported gamma ramp size: %d\n", gammaRampSize );
		return qfalse;
	};
	
	m = gammaRampSize / 256;
	m1 = 256 / m;

	for ( i = 0; i < 256; i++ ) {
		for ( j = 0; j < m; j++ ) {
			table[0][i*m+j] = (unsigned short)(red[i] << 8)   | (m1 * j) | ( red[i] >> shift );
			table[1][i*m+j] = (unsigned short)(green[i] << 8) | (m1 * j) | ( green[i] >> shift );
			table[2][i*m+j] = (unsigned short)(blue[i] << 8)  | (m1 * j) | ( blue[i] >> shift );
		}
	}

	// enforce constantly increasing
	for ( j = 0 ; j < 3 ; j++ ) {
		for ( i = 1 ; i < gammaRampSize ; i++ ) {
			if ( table[j][i] < table[j][i-1] ) {
				table[j][i] = table[j][i-1];
			}
		}
	}

	return qtrue;
}

/*****************************************************************************/

/*
** GLimp_SetGamma
**
** This routine should only be called if glConfig.deviceSupportsGamma is TRUE
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
	unsigned short table[3][4096];
	int size;

	if ( xrandr_gamma )
	{
		if ( BuildGammaRampTable( red, green, blue, old_gamma_size, table ) )
		{
			SetMonitorGamma( table[0], table[1], table[2], old_gamma_size );
			glw_state.gammaSet = qtrue;
		}
		return;
	}

#ifdef HAVE_XF86DGA
	if ( vidmode_ext )
	{
		XF86VidModeGetGammaRampSize( dpy, scrnum, &size );
		if ( BuildGammaRampTable( red, green, blue, size, table ) )
		{
			XF86VidModeSetGammaRamp( dpy, scrnum, size, table[0], table[1], table[2] );
			glw_state.gammaSet = qtrue;
		}
	}
#endif /* HAVE_XF86DGA */
}


/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.  Under OpenGL this means NULLing out the current DC and
** HGLRC, deleting the rendering context, and releasing the DC acquired
** for the window.  The state structure is also nulled out.
**
*/
void GLimp_Shutdown( void )
{
	if ( !ctx || !dpy )
		return;

	IN_DeactivateMouse();

	if ( dpy )
	{
		if ( xrandr_gamma && glw_state.gammaSet )
		{
			RestoreMonitorGamma();
			glw_state.gammaSet = qfalse;
		}
		RestoreMonitorMode();

		if ( ctx )
			qglXDestroyContext( dpy, ctx );
		if ( win )
			XDestroyWindow( dpy, win );

#ifdef HAVE_XF86DGA
		if ( vidmode_active )
		{
			if ( vidmodes )
			{
				XF86VidModeSwitchToMode( dpy, scrnum, vidmodes[ 0 ] );
				// don't forget to release memory:
				free( vidmodes );
				vidmodes = NULL;
			}
		}

		if ( glw_state.gammaSet )
		{
			XF86VidModeSetGamma( dpy, scrnum, &vidmode_InitialGamma );
			glw_state.gammaSet = qfalse;
		}

#endif /* HAVE_XF86DGA */
		// NOTE TTimo opening/closing the display should be necessary only once per run
		// but it seems QGL_Shutdown gets called in a lot of occasion
		// in some cases, this XCloseDisplay is known to raise some X errors
		// ( https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=33 )
		XCloseDisplay( dpy );
	}

	vidmode_active = qfalse;
	desktop_ok = qfalse;

	dpy = NULL;
	win = 0;
	ctx = NULL;

	unsetenv( "vblank_mode" );
	
	//if ( glw_state.cdsFullscreen )
	{
		glw_state.cdsFullscreen = qfalse;
	}

	QGL_Shutdown();
}


/*
** GLimp_LogComment
*/
void GLimp_LogComment( char *comment )
{
	if ( glw_state.log_fp )
	{
		fprintf( glw_state.log_fp, "%s", comment );
	}
}


/*
** GLW_StartDriverAndSetMode
*/
// bk001204 - prototype needed
int GLW_SetMode( const char *drivername, int mode, const char *modeFS, qboolean fullscreen );

static qboolean GLW_StartDriverAndSetMode( const char *drivername, int mode, const char *modeFS, qboolean fullscreen )
{
	rserr_t err;
	
	if ( fullscreen && in_nograb->integer )
	{
		Com_Printf( "Fullscreen not allowed with in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}

	err = GLW_SetMode( drivername, mode, modeFS, fullscreen );

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

	glw_state.config->isFullscreen = fullscreen;

	return qtrue;
}


#define MAX_MONITORS 16

typedef struct
{
	int x, y;
	int w, h;
	RROutput outputn;
	RRCrtc crtcn;
	RRMode curMode;
	RRMode oldMode;
	char name[32];
} monitor_t;

monitor_t monitors[ MAX_MONITORS ];
monitor_t *current_monitor;
monitor_t desktop_monitor;

static qboolean monitor_in_list( int x, int y, int w, int h, RROutput outputn, RRCrtc crtcn )
{
	int i;

	for ( i = 0; i < glw_state.monitorCount; i++ )
	{
		if ( monitors[i].x != x || monitors[i].y != y )
			continue;
		if ( monitors[i].w != w || monitors[i].h != h )
			continue;
		if ( monitors[i].outputn != outputn )
			continue;
		if ( monitors[i].crtcn != crtcn )
			continue;

		return qtrue;
	}

	return qfalse;
}


void monitor_add( int x, int y, int w, int h, const char *name, RROutput outputn, RRCrtc crtcn, RRMode mode )
{
	monitor_t *m;
	
	if ( glw_state.monitorCount >= MAX_MONITORS )
		return;

	if ( monitor_in_list( x, y, w, h, outputn, crtcn ) )
		return;

	m = monitors + glw_state.monitorCount;

	m->x = x; m->y = y;
	m->w = w; m->h = h;

	Q_strncpyz( m->name, name, sizeof( m->name ) );

	m->outputn = outputn;
	m->crtcn = crtcn;

	m->curMode = mode;
	m->oldMode = mode;

	glw_state.monitorCount++;
}


static int getRefreshRate( const XRRModeInfo *mode_info )
{
	if ( mode_info->hTotal && mode_info->vTotal )
		return ( (double)mode_info->dotClock / ( (double)mode_info->hTotal * (double)mode_info->vTotal ) );
	else
		return 0;
}


const XRRModeInfo* getModeInfo( const XRRScreenResources* sr, RRMode id )
{
	int i;

	for ( i = 0; i < sr->nmode; i++ )
		if ( sr->modes[ i ].id == id )
			return sr->modes + i;

	return NULL;
}


qboolean SetMonitorMode( int *width, int *height, int *rate )
{
	monitor_t *m = &desktop_monitor;
	XRRScreenResources *sr;
	const XRRModeInfo *mode_info;
	XRROutputInfo *output_info;
	XRRCrtcInfo *crtc_info;
	RRMode newMode;
	int best_fit, best_dist, best_rate;
	int dist, r, rr;
	int x, y, w, h;
	int n;

	xrandr_active = qfalse;

	if ( !xrandr_ext )
		return xrandr_active;

	if ( *width == m->w && *height == m->h )
	{
		Com_Printf( "...using desktop display mode\n" );
		xrandr_active = qtrue;
		return xrandr_active;
	}
	
	sr = XRRGetScreenResources( dpy, DefaultRootWindow( dpy ) ); // FIXME: use faster version if randr13 is available?
	output_info = XRRGetOutputInfo( dpy, sr, m->outputn );
	crtc_info = XRRGetCrtcInfo( dpy, sr, m->crtcn );

	best_rate = 999999999;
	best_dist = 999999999;
	best_fit = -1;

	// find best-matching mode from available
	for ( n = 0; n < output_info->nmode; n++ )
	{
		mode_info = getModeInfo( sr, output_info->modes[ n ] );

		if ( !mode_info || ( mode_info->modeFlags & RR_Interlace ) )
			continue;
	
		if ( mode_info->width > *width || mode_info->height > *height )
			continue;

		x = *width - mode_info->width;
		y = *height - mode_info->height;
		dist = ( x * x ) + ( y * y );

		if ( *rate ) {
			r = *rate - getRefreshRate( mode_info );
			r = ( r * r );
		} else {
			r = best_rate;
		}
	
		if ( dist < best_dist || ( dist == best_dist && r < best_rate ) )
		{
			best_dist = dist;
			best_rate = r;
			best_fit = n;
			newMode = output_info->modes[ n ];
			w = mode_info->width; // save adjusted with
			h = mode_info->height; // save adjusted height
			rr = getRefreshRate( mode_info );
			
		}
		//fprintf( stderr, "mode[%i]: %i x %i @ %iHz.\n", i, mode_info->width, mode_info->height, getRefreshRate( mode_info ) );
	}
	
	if ( best_fit != -1 )
	{
		//Com_Printf( "...setting new mode 0x%x via xrandr \n", (int)newMode );
		XRRSetCrtcConfig( dpy, sr, m->crtcn, CurrentTime, crtc_info->x, crtc_info->y,
			newMode, crtc_info->rotation, crtc_info->outputs, crtc_info->noutput );

		m->curMode = newMode;
		xrandr_active = qtrue;
		*width = w;
		*height = h;
		*rate = rr;
	}

	XRRFreeCrtcInfo( crtc_info );
	XRRFreeOutputInfo( output_info );
	XRRFreeScreenResources( sr );

	return xrandr_active;
}


void RestoreMonitorMode( void )
{
	monitor_t *m = &desktop_monitor;
	XRRScreenResources *sr;
	XRROutputInfo *output_info;
	XRRCrtcInfo *crtc_info;
	
	if ( !xrandr_ext || !xrandr_active )
		return;

	xrandr_active = qfalse;

	if ( m->curMode == m->oldMode )
		return;

	Com_Printf( "...restoring desktop display mode\n" );

	sr = XRRGetScreenResources( dpy, DefaultRootWindow( dpy ) ); // FIXME: use faster version if randr13 is available?
	output_info = XRRGetOutputInfo( dpy, sr, m->outputn );
	crtc_info = XRRGetCrtcInfo( dpy, sr, m->crtcn );

	XRRSetCrtcConfig( dpy, sr, m->crtcn, CurrentTime, crtc_info->x, crtc_info->y,
		m->oldMode, crtc_info->rotation, crtc_info->outputs, crtc_info->noutput );

	XRRFreeCrtcInfo( crtc_info );
	XRRFreeOutputInfo( output_info );
	XRRFreeScreenResources( sr );

	m->curMode = m->oldMode;

	current_monitor = NULL;
}


static void BuildMonitorList( void )
{
	XRRScreenResources *sr;
	XRRCrtcInfo *crtc_info;
	XRROutputInfo *info;
	int outn;

	glw_state.monitorCount = 0;

	sr = XRRGetScreenResources( dpy, DefaultRootWindow( dpy ) );
	if ( !sr )
		return;

	for ( outn = 0; outn < sr->noutput; outn++ )
	{
		info = XRRGetOutputInfo( dpy, sr, sr->outputs[ outn ] );
		if ( info )
		{
			if ( info->connection == RR_Connected )
			{
				crtc_info = XRRGetCrtcInfo( dpy, sr, info->crtc );
				if ( crtc_info )
				{
					//fprintf( stderr, "%ix%i @%ix%i outn:%i (crtc:%i) %s\n",
					//		crtc_info->width, crtc_info->height,
					//		crtc_info->x, crtc_info->y,
					//		(int)outn, (int)info->crtc, info->name );
					if ( crtc_info->width && crtc_info->height )
					{
						monitor_add( crtc_info->x, crtc_info->y, crtc_info->width, crtc_info->height,
							info->name, sr->outputs[ outn ], info->crtc, crtc_info->mode );
					}
					XRRFreeCrtcInfo( crtc_info );
				}
			}
			XRRFreeOutputInfo( info );
		}
	}
	XRRFreeScreenResources( sr );
}


monitor_t *FindNearestMonitor( int x, int y, int w, int h )
{
	monitor_t *m, *found;
	unsigned long dx, dy, dist, nearest;
	int cx, cy;
	int i;

	found = NULL;
	nearest = 0xFFFFFFFF;

	cx = x + w/2;
	cy = y + h/2;

	for ( i = 0; i < glw_state.monitorCount; i++ )
	{
		m = &monitors[ i ];
		// window center intersection
		if ( cx >= m->x && cx < (m->x + m->w) && cy >= m->y && cy < (m->y + m->h) )
		{
			//Com_Printf( "match by center on %s\n", m->name );
			return m;
		}
		// nearest distance
		//dx = MIN( abs( m->x - ( x + w ) ), abs( x - ( m->x + m->w ) ) );
		//dy = MIN( abs( m->y - ( y + h ) ), abs( y - ( m->y + m->h ) ) );
		// nearest distance from window center to screen center
		dx = (m->x + m->w/2) - cx;
		dy = (m->y + m->h/2) - cy;
		dist = ( dx * dx ) + ( dy * dy );
		if ( nearest > dist )
		{
			nearest = dist;
			found = m;
		}
	}

	return found;
}


void LocateCurrentMonitor( int x, int y, int w, int h )
{
	monitor_t *cm;
//	int i;
	
	if ( !glw_state.monitorCount || glw_state.cdsFullscreen )
		return;

	// try to find monitor to which input coordinates belongs to
	cm = FindNearestMonitor( x, y, w, h );

	if ( !cm )
		return;
	
	if ( cm != current_monitor )
	{
		qboolean gammaSet = glw_state.gammaSet;

		if ( xrandr_gamma && gammaSet )
		{
			RestoreMonitorGamma();
		}

		// save new monitor
		current_monitor = cm;
		memcpy( &desktop_monitor, cm, sizeof( desktop_monitor ) );

		desktop_x = cm->x;
		desktop_y = cm->y;
		desktop_width = cm->w;
		desktop_height = cm->h;

		desktop_ok = qtrue;

		Com_Printf( "...current monitor: %ix%i@%i,%i %s\n", desktop_width, desktop_height,
			desktop_x, desktop_y, desktop_monitor.name );

		BackupMonitorGamma();

		if ( xrandr_gamma && gammaSet && re.SetColorMappings ) 
		{
			re.SetColorMappings();
		}
	}
}


qboolean BackupMonitorGamma( void )
{
	XRRCrtcGamma* gamma;
	int gammaRampSize;

	xrandr_gamma = qfalse;

	if ( !glw_state.monitorCount )
	{
		return qfalse;
	}

	gammaRampSize = XRRGetCrtcGammaSize( dpy, desktop_monitor.crtcn );
	if ( gammaRampSize < 256 || gammaRampSize > 4096 )
	{
		fprintf( stderr, "...unsupported gamma ramp size: %i\n", gammaRampSize );
		return qfalse;
	}
	
	gamma = XRRGetCrtcGamma( dpy, desktop_monitor.crtcn );

	if ( gamma )
	{
		memcpy( old_gamma[0], gamma->red,   gammaRampSize * sizeof( unsigned short ) );
		memcpy( old_gamma[1], gamma->green, gammaRampSize * sizeof( unsigned short ) );
		memcpy( old_gamma[2], gamma->blue,  gammaRampSize * sizeof( unsigned short ) );
		old_gamma_size = gammaRampSize;

		XRRFreeGamma( gamma );
		xrandr_gamma = qtrue;
		return qtrue;
	}

	return qfalse;
}


void SetMonitorGamma( unsigned short *red, unsigned short *green, unsigned short *blue, int size ) 
{
	XRRCrtcGamma* gamma;

	gamma = XRRAllocGamma( size );
	if ( gamma )
	{
		memcpy( gamma->red,   red,   size * sizeof( unsigned short ) );
		memcpy( gamma->green, green, size * sizeof( unsigned short ) );
		memcpy( gamma->blue,  blue,  size * sizeof( unsigned short ) );
		XRRSetCrtcGamma( dpy, desktop_monitor.crtcn, gamma );
		XRRFreeGamma( gamma );
	}
}


void RestoreMonitorGamma( void )
{
	if ( xrandr_gamma && old_gamma_size )
		SetMonitorGamma( old_gamma[0], old_gamma[1], old_gamma[2], old_gamma_size );

	old_gamma_size = 0;
}


static qboolean InitXRandr( Display *dpy, int x, int y, int w, int h )
{
	int event_base, error_base;
	int ver_major = 1, ver_minor = 2;

	xrandr_ext = qfalse;
	xrandr_active = qfalse;

	glw_state.monitorCount = 0;
	current_monitor = NULL;
	memset( monitors, 0, sizeof( monitors ) );
	memset( &desktop_monitor, 0, sizeof( desktop_monitor ) );

	if ( !XRRQueryExtension( dpy, &event_base, &error_base ) || !XRRQueryVersion( dpy, &ver_major, &ver_minor ) )
	{
		Com_Printf( "...RandR extension is not available.\n" );
		return qfalse;
	}

	Com_Printf( "...RandR extension version %i.%i detected.\n", ver_major, ver_minor );

	xrandr_ext = qtrue;

	BuildMonitorList();

	LocateCurrentMonitor( x, y, w, h );

	BackupMonitorGamma();

	return qtrue;
}


/*
** GLW_SetMode
*/
int GLW_SetMode( const char *drivername, int mode, const char *modeFS, qboolean fullscreen )
{
	// these match in the array
	#define ATTR_RED_IDX 2
	#define ATTR_GREEN_IDX 4
	#define ATTR_BLUE_IDX 6
	#define ATTR_DEPTH_IDX 9
	#define ATTR_STENCIL_IDX 11

	static int attrib[] =
	{
		GLX_RGBA,         // 0
		GLX_RED_SIZE, 4,      // 1, 2
		GLX_GREEN_SIZE, 4,      // 3, 4
		GLX_BLUE_SIZE, 4,     // 5, 6
		GLX_DOUBLEBUFFER,     // 7
		GLX_DEPTH_SIZE, 1,      // 8, 9
		GLX_STENCIL_SIZE, 1,    // 10, 11
		None
	};

	glconfig_t *config = glw_state.config;

	Window root;
	XVisualInfo *visinfo;

	XSetWindowAttributes attr;
	XSizeHints sizehints;
	unsigned long mask;
	int colorbits, depthbits, stencilbits;
	int tcolorbits, tdepthbits, tstencilbits;
	int dga_MajorVersion, dga_MinorVersion;
	int actualWidth, actualHeight, actualRate;
	int i;
	//const char* glstring; // bk001130 - from cvs1.17 (mkv)

	window_width = 0;
	window_height = 0;
	window_created = qfalse;

	vidmode_ext = qfalse;

	dpy = XOpenDisplay( NULL );

	if ( dpy == NULL )
	{
		fprintf( stderr, "Error: couldn't open the X display\n" );
		return RSERR_INVALID_MODE;
	}

	scrnum = DefaultScreen( dpy );
	root = RootWindow( dpy, scrnum );

	// Init xrandr and get desktop resolution if available
	InitXRandr( dpy, vid_xpos->integer, vid_ypos->integer, 320, 240 );

	// Get video mode list
#ifdef HAVE_XF86DGA
	if ( XF86VidModeQueryVersion( dpy, &vidmode_MajorVersion, &vidmode_MinorVersion ) )
	{
		Com_Printf( "Using XFree86-VidModeExtension Version %d.%d\n",
			vidmode_MajorVersion, vidmode_MinorVersion );
		vidmode_ext = qtrue;

		if ( desktop_ok == qfalse )
		{
			XF86VidModeModeLine c;
			int n;
			if ( XF86VidModeGetModeLine( dpy, scrnum, &n, &c ) )
			{
				desktop_width = c.hdisplay;
				desktop_height = c.vdisplay;
				desktop_ok = qtrue;
			}
			else
			{
				Com_Printf( "XF86VidModeGetModeLine failed.\n" );
			}
		}
		Com_Printf( "desktop width:%i height:%i\n", desktop_width, desktop_height );

	}

	// Check for DGA
	dga_MajorVersion = 0;
	dga_MinorVersion = 0;
	if ( in_dgamouse && in_dgamouse->integer )
	{
		if ( !XF86DGAQueryVersion( dpy, &dga_MajorVersion, &dga_MinorVersion ) )
		{
			// unable to query, probably not supported
			Com_Printf( "Failed to detect XF86DGA Mouse\n" );
			Cvar_Set( "in_dgamouse", "0" );
		}
		else
		{
			Com_Printf( "XF86DGA Mouse (Version %d.%d) initialized\n",
				dga_MajorVersion, dga_MinorVersion );
		}
	}
#endif

	Com_Printf( "Initializing OpenGL display\n");

	Com_Printf( "...setting mode %d:", mode );

	if ( !CL_GetModeInfo( &config->vidWidth, &config->vidHeight, &config->windowAspect,
		mode, modeFS, desktop_width, desktop_height, fullscreen ) )
	{
		Com_Printf( " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}

	actualWidth = config->vidWidth;
	actualHeight = config->vidHeight;
	actualRate = r_displayRefresh->integer;

	if ( actualRate )
		Com_Printf( " %d %d @%iHz\n", actualWidth, actualHeight, actualRate );
	else
		Com_Printf( " %d %d\n", actualWidth, actualHeight );

	if ( fullscreen ) // try randr first
	{
		SetMonitorMode( &actualWidth, &actualHeight, &actualRate );
	}

#ifdef HAVE_XF86DGA
	if ( vidmode_ext && !xrandr_active )
	{
		int best_fit, best_dist, dist, x, y;
		int num_vidmodes;

		// Are we going fullscreen?  If so, let's change video mode
		if ( fullscreen )
		{
			XF86VidModeGetAllModeLines( dpy, scrnum, &num_vidmodes, &vidmodes );
			
			best_dist = 9999999;
			best_fit = -1;

			for ( i = 0; i < num_vidmodes; i++ )
			{
				if ( config->vidWidth > vidmodes[i]->hdisplay ||
					config->vidHeight > vidmodes[i]->vdisplay)
					continue;

				x = config->vidWidth - vidmodes[i]->hdisplay;
				y = config->vidHeight - vidmodes[i]->vdisplay;
				dist = (x * x) + (y * y);
				if (dist < best_dist)
				{
					best_dist = dist;
					best_fit = i;
				}
			}

			if ( best_fit != -1 )
			{
				actualWidth = vidmodes[ best_fit ]->hdisplay;
				actualHeight = vidmodes[ best_fit ]->vdisplay;

				// change to the mode
				XF86VidModeSwitchToMode( dpy, scrnum, vidmodes[ best_fit ] );
				XFlush( dpy );  // drakkar - man 3 XF86VidModeSwitchToMode
				vidmode_active = qtrue;

				// drakkar - XF86VidModeSetViewPort problems
				// if windows is placed out of screen
				// if ( win )
				//	XMoveWindow( dpy, win, 0, 0 );

				// Move the viewport to top left
				XF86VidModeSetViewPort( dpy, scrnum, 0, 0 );

				Com_Printf( "XFree86-VidModeExtension Activated at %dx%d\n",
					actualWidth, actualHeight );
			}
			else
			{
				//fullscreen = 0;
				Com_Printf( "XFree86-VidModeExtension: No acceptable modes found\n" );
			}
		}
		else
		{
			Com_Printf( "XFree86-VidModeExtension:  Ignored on non-fullscreen/Voodoo\n");
		}
	}
#endif /* HAVE_XF86DGA */

	if ( !r_colorbits->integer )
		colorbits = 24;
	else
		colorbits = r_colorbits->integer;

	if ( !r_depthbits->integer )
		depthbits = 24;
	else
		depthbits = r_depthbits->integer;

	stencilbits = r_stencilbits->integer;

	for ( i = 0; i < 16; i++ )
	{
		// 0 - default
		// 1 - minus colorbits
		// 2 - minus depthbits
		// 3 - minus stencil
		if ( (i % 4) == 0 && i )
		{
			// one pass, reduce
			switch (i / 4)
			{
			case 2 :
				if ( colorbits == 24 )
					colorbits = 16;
				break;
			case 1 :
				if ( depthbits == 24 )
					depthbits = 16;
				else if ( depthbits == 16 )
					depthbits = 8;
			case 3 :
				if ( stencilbits == 24 )
					stencilbits = 16;
				else if ( stencilbits == 16 )
					stencilbits = 8;
			}
		}

		tcolorbits = colorbits;
		tdepthbits = depthbits;
		tstencilbits = stencilbits;

		if ( (i % 4) == 3 )
		{ // reduce colorbits
			if ( tcolorbits == 24 )
				tcolorbits = 16;
		}

		if ( (i % 4) == 2 )
		{ // reduce depthbits
			if ( tdepthbits == 24 )
				tdepthbits = 16;
			else if ( tdepthbits == 16 )
				tdepthbits = 8;
		}

		if ((i % 4) == 1)
		{ // reduce stencilbits
			if ( tstencilbits == 24 )
				tstencilbits = 16;
			else if ( tstencilbits == 16 )
				tstencilbits = 8;
			else
				tstencilbits = 0;
		}

		if (tcolorbits == 24)
		{
			attrib[ATTR_RED_IDX] = 8;
			attrib[ATTR_GREEN_IDX] = 8;
			attrib[ATTR_BLUE_IDX] = 8;
		}
		else
		{
			// must be 16 bit
			attrib[ATTR_RED_IDX] = 4;
			attrib[ATTR_GREEN_IDX] = 4;
			attrib[ATTR_BLUE_IDX] = 4;
		}

		attrib[ATTR_DEPTH_IDX] = tdepthbits; // default to 24 depth
		attrib[ATTR_STENCIL_IDX] = tstencilbits;

		visinfo = qglXChooseVisual( dpy, scrnum, attrib );
		if ( !visinfo )
		{
			continue;
		}

		Com_Printf( "Using %d/%d/%d Color bits, %d depth, %d stencil display.\n", 
			attrib[ATTR_RED_IDX], attrib[ATTR_GREEN_IDX], attrib[ATTR_BLUE_IDX],
			attrib[ATTR_DEPTH_IDX], attrib[ATTR_STENCIL_IDX]);

		config->colorBits = tcolorbits;
		config->depthBits = tdepthbits;
		config->stencilBits = tstencilbits;
		break;
	}

	if ( !visinfo )
	{
		Com_Printf( "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	window_width = actualWidth;
	window_height = actualHeight;

	glw_state.cdsFullscreen = fullscreen;

	/* window attributes */
	attr.background_pixel = BlackPixel( dpy, scrnum );
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap( dpy, root, visinfo->visual, AllocNone );
	attr.event_mask = X_MASK;
	if ( vidmode_active || xrandr_active )
	{
		mask = CWBackPixel | CWColormap | CWSaveUnder | CWBackingStore |
			CWEventMask | CWOverrideRedirect;
		attr.override_redirect = True;
		attr.backing_store = NotUseful;
		attr.save_under = False;
	}
	else
	{
		mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
	}

	win = XCreateWindow( dpy, root, 0, 0,
		actualWidth, actualHeight,
		0, visinfo->depth, InputOutput,
		visinfo->visual, mask, &attr );

	XStoreName( dpy, win, CLIENT_WINDOW_TITLE );

	/* GH: Don't let the window be resized */
	sizehints.flags = PMinSize | PMaxSize;
	sizehints.min_width = sizehints.max_width = actualWidth;
	sizehints.min_height = sizehints.max_height = actualHeight;

	XSetWMNormalHints( dpy, win, &sizehints );

	XMapWindow( dpy, win );

	wmDeleteEvent = XInternAtom( dpy, "WM_DELETE_WINDOW", True );
	if ( wmDeleteEvent == BadValue )
		wmDeleteEvent = None;
	if ( wmDeleteEvent != None )
		XSetWMProtocols( dpy, win, &wmDeleteEvent, 1 );

	window_created = qtrue;

	if ( fullscreen )
	{
		if ( xrandr_active || vidmode_active )
			XMoveWindow( dpy, win, desktop_x, desktop_y );
	}
	else
	{
		XMoveWindow( dpy, win, vid_xpos->integer, vid_ypos->integer );
	}

	XFlush( dpy );
	XSync( dpy, False );
	ctx = qglXCreateContext( dpy, visinfo, NULL, True );
	XSync( dpy, False );

	/* GH: Free the visinfo after we're done with it */
	XFree( visinfo );

	qglXMakeCurrent( dpy, win, ctx );

#if 0
	glstring = (char *)qglGetString( GL_RENDERER );

	if ( !Q_stricmp( glstring, "Mesa X11") || !Q_stricmp( glstring, "Mesa GLX Indirect") )
	{
		if ( !r_allowSoftwareGL->integer )
		{
			Com_Printf( "GL_RENDERER: %s\n", glstring );
			Com_Printf( "\n\n***********************************************************\n" );
			Com_Printf( " You are using software Mesa (no hardware acceleration)!   \n" );
			Com_Printf( " Driver DLL used: %s\n", drivername ); 
			Com_Printf( " If this is intentional, add\n" );
			Com_Printf( "       \"+set r_allowSoftwareGL 1\"\n" );
			Com_Printf( " to the command line when starting the game.\n" );
			Com_Printf( "***********************************************************\n");
			GLimp_Shutdown();
			return RSERR_INVALID_MODE;
		}
		else
		{
			Com_Printf( "...using software Mesa (r_allowSoftwareGL==1).\n" );
		}
	}
#endif
	Key_ClearStates();

	XSetInputFocus( dpy, win, RevertToParent, CurrentTime );

	return RSERR_OK;
}


void GLimp_InitGamma( glconfig_t *config )
{
	config->deviceSupportsGamma = qfalse;

	if ( xrandr_gamma )
	{
		Com_Printf( "...using xrandr gamma extension\n" );
		config->deviceSupportsGamma = qtrue;
		return;
	}

	/* Minimum extension version required */
	#define GAMMA_MINMAJOR 2
	#define GAMMA_MINMINOR 0

#ifdef HAVE_XF86DGA
	if ( vidmode_ext )
	{
		if ( vidmode_MajorVersion < GAMMA_MINMAJOR || 
			(vidmode_MajorVersion == GAMMA_MINMAJOR && vidmode_MinorVersion < GAMMA_MINMINOR) )
		{
			Com_Printf( "XF86 Gamma extension not supported in this version.\n" );
			return;
		}
		XF86VidModeGetGamma( dpy, scrnum, &vidmode_InitialGamma );
		Com_Printf( "...using vidmode gamma extension\n" );
		config->deviceSupportsGamma = qtrue;
	}
#endif /* HAVE_XF86DGA */
}


/*
** GLW_LoadOpenGL
**
** GLimp_win.c internal function that that attempts to load and use 
** a specific OpenGL DLL.
*/
static qboolean GLW_LoadOpenGL( const char *name )
{
	qboolean fullscreen;

	if ( r_swapInterval->integer )
		setenv( "vblank_mode", "2", 1 );
	else
		setenv( "vblank_mode", "1", 1 );

	// load the QGL layer
	if ( QGL_Init( name ) )
	{
		fullscreen = (r_fullscreen->integer != 0);
		// create the window and set up the context
		if ( !GLW_StartDriverAndSetMode( name, r_mode->integer, r_modeFullscreen->string, fullscreen ) )
		{
			if ( r_mode->integer != 3 )
			{
				if ( !GLW_StartDriverAndSetMode( name, 3, "", fullscreen ) )
				{
					goto fail;
				}
			}
			else
			{
				goto fail;
			}
		}
		return qtrue;
	}
	fail:

	QGL_Shutdown();

	return qfalse;
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
** XErrorHandler
**   the default X error handler exits the application
**   I found out that on some hosts some operations would raise X errors (GLXUnsupportedPrivateRequest)
**   but those don't seem to be fatal .. so the default would be to just ignore them
**   our implementation mimics the default handler behaviour (not completely cause I'm lazy)
*/
int qXErrorHandler( Display *dpy, XErrorEvent *ev )
{
	static char buf[1024];
	XGetErrorText( dpy, ev->error_code, buf, sizeof( buf ) );
	Com_Printf( "X Error of failed request: %s\n", buf) ;
	Com_Printf( "  Major opcode of failed request: %d\n", ev->request_code );
	Com_Printf( "  Minor opcode of failed request: %d\n", ev->minor_code );
	Com_Printf( "  Serial number of failed request: %d\n", (int)ev->serial );
	return 0;
}


/*
** GLimp_Init
**
** This routine is responsible for initializing the OS specific portions
** of OpenGL.
*/
void GLimp_Init( glconfig_t *config )
{
	InitSig();

	IN_Init();   // rcg08312005 moved into glimp.

	// set up our custom error handler for X failures
	XSetErrorHandler( &qXErrorHandler );

	// feedback to renderer configuration
	glw_state.config = config;

	//
	// load and initialize the specific OpenGL driver
	//
	if ( !GLW_StartOpenGL() )
	{
		return;
	}

	// This values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	InitSig(); // not clear why this is at begin & end of function

	// optional
#define GLE( ret, name, ... ) q##name = GL_GetProcAddress( XSTRING( name ) );
	QGL_Swp_PROCS;
#undef GLE

	if ( qglXSwapIntervalEXT || qglXSwapIntervalMESA || qglXSwapIntervalSGI )
	{
		Com_Printf( "...using GLX_EXT_swap_control\n" );
		Cvar_SetModified( "r_swapInterval", qtrue ); // force a set next frame
	}
	else
	{
		Com_Printf( "...GLX_EXT_swap_control not found\n" );
	}

}


/*
** GLimp_EndFrame
** 
** Responsible for doing a swapbuffers and possibly for other stuff
** as yet to be determined.  Probably better not to make this a GLimp
** function and instead do a call to GLimp_SwapBuffers.
*/
void GLimp_EndFrame( void )
{
	//
	// swapinterval stuff
	//
	if ( r_swapInterval->modified ) {
		r_swapInterval->modified = qfalse;

		if ( qglXSwapIntervalEXT ) {
			qglXSwapIntervalEXT( dpy, win, r_swapInterval->integer );
		} else if ( qglXSwapIntervalMESA ) {
			qglXSwapIntervalMESA( r_swapInterval->integer );
		} else if ( qglXSwapIntervalSGI ) {
			qglXSwapIntervalSGI( r_swapInterval->integer );
		}
	}

	// don't flip if drawing to front buffer
	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		qglXSwapBuffers( dpy, win );
	}
}


/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

void IN_Init( void )
{
	Com_DPrintf( "\n------- Input Initialization -------\n" );

	// mouse variables
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
	in_dgamouse = Cvar_Get( "in_dgamouse", "1", CVAR_ARCHIVE_ND );
	in_shiftedKeys = Cvar_Get( "in_shiftedKeys", "0", CVAR_ARCHIVE_ND );

	// turn on-off sub-frame timing of X events
	in_subframe = Cvar_Get( "in_subframe", "1", CVAR_ARCHIVE_ND );

	// developer feature, allows to break without loosing mouse pointer
	in_nograb = Cvar_Get( "in_nograb", "0", 0 );

	in_forceCharset = Cvar_Get( "in_forceCharset", "1", CVAR_ARCHIVE_ND );

#ifdef USE_JOYSTICK
	// bk001130 - from cvs.17 (mkv), joystick variables
	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	// bk001130 - changed this to match win32
	in_joystickDebug = Cvar_Get( "in_debugjoystick", "0", CVAR_TEMP );
	joy_threshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE_ND ); // FIXME: in_joythreshold
#endif

	vid_xpos = Cvar_Get( "vid_xpos", "3", CVAR_ARCHIVE );
	vid_ypos = Cvar_Get( "vid_ypos", "22", CVAR_ARCHIVE );

	if ( in_mouse->integer )
	{
		mouse_avail = qtrue;
	}
	else
	{
		mouse_avail = qfalse;
	}

#ifdef USE_JOYSTICK
	IN_StartupJoystick(); // bk001130 - from cvs1.17 (mkv)
#endif

	Com_DPrintf( "------------------------------------\n" );
}


void IN_Shutdown( void )
{
	mouse_avail = qfalse;
}


void IN_Frame( void )
{

#ifdef USE_JOYSTICK
	// bk001130 - from cvs 1.17 (mkv)
	IN_JoyMove(); // FIXME: disable if on desktop?
#endif

	if ( Key_GetCatcher() & KEYCATCH_CONSOLE )
	{
		// temporarily deactivate if not in the game and
		// running on the desktop with multimonitor configuration
		if ( !glw_state.cdsFullscreen || glw_state.monitorCount > 1 )
		{
			IN_DeactivateMouse();
			return;
		}
	}
/
	if ( !window_focused || gw_minimized || in_nograb->integer ) {
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse();
}


/*
=================
Sys_GetClipboardData
=================
*/
char *Sys_GetClipboardData( void )
{
	const Atom xtarget = XInternAtom( dpy, "UTF8_STRING", 0 );
	unsigned long nitems, rem;
	unsigned char *data;
	Atom type;
	XEvent ev;
	char *buf;
	int format;

	XConvertSelection( dpy, XA_PRIMARY, xtarget, XA_PRIMARY, win, CurrentTime );
	XSync( dpy, False );
	XNextEvent( dpy, &ev );
	if ( !XFilterEvent( &ev, None ) && ev.type == SelectionNotify ) {
		if ( XGetWindowProperty( dpy, win, XA_PRIMARY, 0, 8, False, AnyPropertyType,
			&type, &format, &nitems, &rem, &data ) == 0 ) {
			if ( format == 8 ) {
				if ( nitems > 0 ) {
					buf = Z_Malloc( nitems + 1 );
					Q_strncpyz( buf, (char*)data, nitems + 1 );
					strtok( buf, "\n\r\b" );
					return buf;
				}
			} else {
				fprintf( stderr, "Bad clipboard format %i\n", format );
			}
		} else {
			fprintf( stderr, "Clipboard allocation failed\n" );
		}
	}
	return NULL;
}


/*
=================
Sys_SetClipboardBitmap
=================
*/
void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{
	// TODO: implement
}


#ifdef USE_JOYSTICK
// bk010216 - added stubs for non-Linux UNIXes here
// FIXME - use NO_JOYSTICK or something else generic

#if (defined( __FreeBSD__ ) || defined( __sun)) // rb010123
void IN_StartupJoystick( void ) {}
void IN_JoyMove( void ) {}
#endif
#endif
