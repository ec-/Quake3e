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
#endif

#if defined(__sun)
#include <X11/Sunkeysym.h>
#endif

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

Display *dpy = NULL;
int scrnum;

static Window win = 0;
static GLXContext ctx = NULL;
static Atom wmDeleteEvent = None;

static int window_width = 0;
static int window_height = 0;
static qboolean window_created = qfalse;
static qboolean window_focused = qfalse;

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

static int mouse_accel_numerator;
static int mouse_accel_denominator;
static int mouse_threshold;

static int win_x, win_y;

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
	if ( in_dgamouse->integer )
	{
		if ( !glw_state.dga_ext )
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
		else
		{
			DGA_Mouse( qtrue );
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
		DGA_Mouse( qfalse );
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

			if ( event.xclient.data.l[0] == wmDeleteEvent ) {
				Cmd_Clear();
				Com_Quit_f();
			}
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
				if ( in_dgamouse->integer )
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
			
			if ( !glw_state.cdsFullscreen && window_created && !gw_minimized )
			{
				Cvar_SetIntegerValue( "vid_xpos", win_x );
				Cvar_SetIntegerValue( "vid_ypos", win_y );
				RandR_UpdateMonitor( win_x, win_y,
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
		if ( in_dgamouse->integer && in_nograb->integer ) // force dga mouse to 0 if using nograb
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
		install_mouse_grab();
		install_kb_grab();
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


qboolean BuildGammaRampTable( unsigned char *red, unsigned char *green, unsigned char *blue, int gammaRampSize, unsigned short table[3][4096] )
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
	if ( glw_state.randr_gamma )
	{
		RandR_SetGamma( red, green, blue );
		return;
	}
	
	if ( glw_state.vidmode_gamma )
	{
		VidMode_SetGamma( red, green, blue );
		return;
	}
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
void GLimp_Shutdown( qboolean unloadDLL )
{
	IN_DeactivateMouse();

	if ( dpy )
	{
		if ( glw_state.randr_gamma && glw_state.gammaSet )
		{
			RandR_RestoreGamma();
			glw_state.gammaSet = qfalse;
		}

		RandR_RestoreMode();

		if ( ctx )
			qglXDestroyContext( dpy, ctx );

		if ( win )
			XDestroyWindow( dpy, win );

		if ( glw_state.gammaSet )
		{
			VidMode_RestoreGamma();
			glw_state.gammaSet = qfalse;
		}

		if ( glw_state.vidmode_active )
			VidMode_RestoreMode();

		// NOTE TTimo opening/closing the display should be necessary only once per run
		// but it seems QGL_Shutdown gets called in a lot of occasion
		// in some cases, this XCloseDisplay is known to raise some X errors
		// ( https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=33 )
		XCloseDisplay( dpy );
	}

	RandR_Done();
	VidMode_Done();

	glw_state.desktop_ok = qfalse;

	dpy = NULL;
	win = 0;
	ctx = NULL;

	unsetenv( "vblank_mode" );
	
	//if ( glw_state.cdsFullscreen )
	{
		glw_state.cdsFullscreen = qfalse;
	}

	QGL_Shutdown( unloadDLL );
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
	int actualWidth, actualHeight, actualRate;
	int i;
	//const char* glstring; // bk001130 - from cvs1.17 (mkv)

	window_width = 0;
	window_height = 0;
	window_created = qfalse;

	glw_state.dga_ext = qfalse;
	glw_state.randr_ext = qfalse;
	glw_state.vidmode_ext = qfalse;

	dpy = XOpenDisplay( NULL );

	if ( dpy == NULL )
	{
		fprintf( stderr, "Error: couldn't open the X display\n" );
		return RSERR_INVALID_MODE;
	}

	scrnum = DefaultScreen( dpy );
	root = RootWindow( dpy, scrnum );

	// Init xrandr and get desktop resolution if available
	RandR_Init( vid_xpos->integer, vid_ypos->integer, 640, 480 );

	if ( !glw_state.randr_ext )
	{
		VidMode_Init();
	}

#ifdef HAVE_XF86DGA
	if ( in_dgamouse && in_dgamouse->integer )
	{
		if ( !DGA_Init( dpy ) )
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
	}
#endif
	Com_Printf( "Initializing OpenGL display\n" );

	Com_Printf( "...setting mode %d:", mode );

	if ( !CL_GetModeInfo( &config->vidWidth, &config->vidHeight, &config->windowAspect,
		mode, modeFS, glw_state.desktop_width, glw_state.desktop_height, fullscreen ) )
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
		RandR_SetMode( &actualWidth, &actualHeight, &actualRate );
	}

	if ( glw_state.vidmode_ext && !glw_state.randr_active )
	{
		if ( fullscreen )
			VidMode_SetMode( &actualWidth, &actualHeight, &actualRate );
		else
			Com_Printf( "XFree86-VidModeExtension: Ignored on non-fullscreen\n" );
	}

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

	if ( fullscreen )
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
		if ( glw_state.randr_active || glw_state.vidmode_active )
			XMoveWindow( dpy, win, glw_state.desktop_x, glw_state.desktop_y );
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

	if ( glw_state.randr_gamma )
	{
		Com_Printf( "...using xrandr gamma extension\n" );
		config->deviceSupportsGamma = qtrue;
		return;
	}

	if ( glw_state.vidmode_gamma )
	{
		Com_Printf( "...using vidmode gamma extension\n" );
		config->deviceSupportsGamma = qtrue;
		return;
	}
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

	QGL_Shutdown( qtrue );

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
	IN_JoyMove();
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
		if ( XGetWindowProperty( dpy, win, XA_PRIMARY, 0, MAX_EDIT_LINE/4, False, AnyPropertyType,
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
