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
** GLimp_SwitchFullscreen
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

#include "../renderer/tr_local.h"
#include "../client/client.h"
#include "linux_local.h" // bk001130

#include "unix_glw.h"

#include <GL/glx.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>

#if !defined(__sun)
#include <X11/extensions/Xxf86dga.h>
#include <X11/extensions/xf86vmode.h>
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

static Display *dpy = NULL;
static int scrnum;
static Window win = 0;
static GLXContext ctx = NULL;
static Atom wmDeleteEvent = None;

static int desktop_width = 0;
static int desktop_height = 0;
static qboolean desktop_ok = qfalse;

// bk001206 - not needed anymore
// static qboolean autorepeaton = qtrue;

#define KEY_MASK (KeyPressMask | KeyReleaseMask)
#define MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask )
#define X_MASK (KEY_MASK | MOUSE_MASK | VisibilityChangeMask | StructureNotifyMask )

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

// bk001130 - from cvs1.17 (mkv), but not static
#ifdef USE_JOYSTICK
cvar_t   *in_joystick      = NULL;
cvar_t   *in_joystickDebug = NULL;
cvar_t   *joy_threshold    = NULL;
#endif

cvar_t  *r_allowSoftwareGL;   // don't abort out if the pixelformat claims software
cvar_t  *r_previousglDriver;

static qboolean vidmode_ext = qfalse;
static qboolean vidmode_active = qfalse;

#ifdef HAVE_XF86DGA
static int vidmode_MajorVersion = 0, vidmode_MinorVersion = 0; // major and minor of XF86VidExtensions

// gamma value of the X display before we start playing with it
static XF86VidModeGamma vidmode_InitialGamma;

static XF86VidModeModeInfo **vidmodes = NULL;
#endif /* HAVE_XF86DGA */


static int win_x, win_y;

static int mouse_accel_numerator;
static int mouse_accel_denominator;
static int mouse_threshold;


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

static char *XLateKey( XKeyEvent *ev, int *key )
{
  static unsigned char buf[64];
  static unsigned char bufnomod[2];
  KeySym keysym;
  int XLookupRet;

  *key = 0;

  XLookupRet = XLookupString(ev, (char*)buf, sizeof(buf), &keysym, 0);
#ifdef KBD_DBG
  ri.Printf(PRINT_ALL, "XLookupString ret: %d buf: %s keysym: %x\n", XLookupRet, buf, keysym);
#endif

  if (!in_shiftedKeys->integer) {
    // also get a buffer without modifiers held
    ev->state = 0;
    XLookupRet = XLookupString(ev, (char*)bufnomod, sizeof bufnomod, &keysym, 0);
#ifdef KBD_DBG
  ri.Printf(PRINT_ALL, "XLookupString (minus modifiers) ret: %d buf: %s keysym: %x\n", XLookupRet, buf, keysym);
#endif
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
  case XK_KP_2:    *key = K_KP_DOWNARROW; break;
  case XK_Down:  *key = K_DOWNARROW; break;

  case XK_KP_Up:
  case XK_KP_8:    *key = K_KP_UPARROW; break;
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

    // bk001130 - from cvs1.17 (mkv)
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

  // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=472
  case XK_space:
  case XK_KP_Space: *key = K_SPACE; break;

  case XK_Menu:	*key = K_MENU; break;
  case XK_Print: *key = K_PRINT; break;
  case XK_Super_L:
  case XK_Super_R: *key = K_SUPER; break;
  case XK_Num_Lock: *key = K_KP_NUMLOCK; break;
  case XK_Caps_Lock: *key = K_CAPSLOCK; break;
  case XK_Scroll_Lock: *key = K_SCROLLOCK; break;

  default:
	//ri.Printf( PRINT_ALL, "unknown keysym: %08X\n", keysym );
    if (XLookupRet == 0)
    {
      if (com_developer->value)
      {
        ri.Printf( PRINT_ALL, "Warning: XLookupString failed on KeySym %d\n", (int)keysym );
      }
      return NULL;
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


static void install_grabs( void )
{
	int res;

	// move pointer to destination window area
	XWarpPointer( dpy, None, win, 0, 0, 0, 0, glConfig.vidWidth / 2, glConfig.vidHeight / 2 );

	XSync( dpy, False );

	// hide cursor
	XDefineCursor( dpy, win, CreateNullCursor( dpy, win ) );

	// save old mouse settings
	XGetPointerControl( dpy, &mouse_accel_numerator, &mouse_accel_denominator, &mouse_threshold );

	// do this earlier?
	res = XGrabPointer( dpy, win, False, MOUSE_MASK, GrabModeAsync, GrabModeAsync, win, None, CurrentTime );
	if ( res != GrabSuccess )
	{
		//ri.Printf( PRINT_ALL, S_COLOR_YELLOW "Warning: XGrabPointer() failed\n" );
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
			ri.Printf( PRINT_ALL, "Failed to detect XF86DGA Mouse\n" );
			ri.Cvar_Set( "in_dgamouse", "0" );
		} 
		else
		{
			XF86DGADirectVideo( dpy, DefaultScreen( dpy ), XF86DGADirectMouse );
			XWarpPointer( dpy, None, win, 0, 0, 0, 0, 0, 0 );
		}
	} 
	else
#endif /* HAVE_XF86DGA */
	{
		mwx = glConfig.vidWidth / 2;
		mwy = glConfig.vidHeight / 2;
		mx = my = 0;
	}

	res = XGrabKeyboard( dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime );
	if ( res != GrabSuccess )
	{
		//ri.Printf( PRINT_ALL, S_COLOR_YELLOW "Warning: XGrabKeyboard() failed\n" );
	}

	XSync( dpy, False );
}


static void uninstall_grabs( void )
{
#ifdef HAVE_XF86DGA
	if ( in_dgamouse->integer )
	{
		if ( com_developer->integer ) 
		{
			ri.Printf( PRINT_ALL, "DGA Mouse - Disabling DGA DirectVideo\n" );
		}
		XF86DGADirectVideo( dpy, DefaultScreen( dpy ), 0 );
	}
#endif /* HAVE_XF86DGA */

	// restore mouse settings
	XChangePointerControl( dpy, qtrue, qtrue, mouse_accel_numerator, 
		mouse_accel_denominator, mouse_threshold );

	XWarpPointer( dpy, None, win, 0, 0, 0, 0, glConfig.vidWidth / 2, glConfig.vidHeight / 2 );

	XUngrabPointer( dpy, CurrentTime );
	XUngrabKeyboard( dpy, CurrentTime );

	// show cursor
	XUndefineCursor( dpy, win );
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


int Sys_XTimeToSysTime( Time xtime );


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
//			ri.Printf( PRINT_ALL,"^2K+^7 %08X\n", event.xkey.keycode );
			t = Sys_XTimeToSysTime( event.xkey.time );
			p = XLateKey( &event.xkey, &key );
			if (key)
			{
				Sys_QueEvent( t, SE_KEY, key, qtrue, 0, NULL );
			}
			if (p)
			{
				while (*p)
				{
					Sys_QueEvent( t, SE_CHAR, *p++, 0, 0, NULL );
				}
			}
			break; // case KeyPress

		case KeyRelease:

			if ( repeated_press( &event ) ) 
				break; // XNextEvent( dpy, &event )

			t = Sys_XTimeToSysTime( event.xkey.time );
#if 0
			ri.Printf( PRINT_ALL,"^5K-^7 %08X %s\n",
				event.xkey.keycode,
				X11_PendingInput()?"pending":"");
#endif
			XLateKey( &event.xkey, &key );
			Sys_QueEvent( t, SE_KEY, key, qfalse, 0, NULL );

			break; // case KeyRelease

		case MotionNotify:
			t = Sys_XTimeToSysTime( event.xkey.time );
			if ( mouse_active )
			{
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
					if (event.xmotion.x == glConfig.vidWidth/2 &&
						event.xmotion.y == glConfig.vidHeight/2)
					{
						mwx = glConfig.vidWidth/2;
						mwy = glConfig.vidHeight/2;
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
				case 5:	Sys_QueEvent( t, SE_KEY, K_MWHEELDOWN, btn_press, 0, NULL ); break;
				case 6: b = 3; break; // K_MOUSE4
				case 7: b = 4; break; // K_MOUSE5
				case 8:	case 9:       // K_AUX1..K_AUX8
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
//			Com_Printf( "ConfigureNotify minimized: %i\n", cls.soundMuted );
			win_x = event.xconfigure.x;
			win_y = event.xconfigure.y;
			break;
		}
	}

	if ( dowarp )
	{
		XWarpPointer( dpy, None, win, 0, 0, 0, 0, 
		    (glConfig.vidWidth/2), (glConfig.vidHeight/2) );
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


void IN_ActivateMouse( void )
{
	if ( !mouse_avail || !dpy || !win ) 
	{
		return;
	}

	if ( !mouse_active )
	{
		if ( !in_nograb->integer ) 
		{
			install_grabs();
		}
		else if ( in_dgamouse->integer ) // force dga mouse to 0 if using nograb
		{
		    ri.Cvar_Set( "in_dgamouse", "0" );
		}	
		mouse_active = qtrue;
	}
}

void IN_DeactivateMouse( void )
{
	if ( !mouse_avail || !dpy || !win ) 
	{
		return;
	}

	if ( mouse_active )
	{
		if ( !in_nograb->integer ) 
		{
			 uninstall_grabs();
		}
		else if ( in_dgamouse->integer ) // force dga mouse to 0 if using nograb
		{
			ri.Cvar_Set( "in_dgamouse", "0" );
		}
		mouse_active = qfalse;
	}
}

/*****************************************************************************/

/*
** GLimp_SetGamma
**
** This routine should only be called if glConfig.deviceSupportsGamma is TRUE
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
#ifdef HAVE_XF86DGA
	unsigned short table[3][4096];
	int i, j;
	int size;
	int m, m1;
	int shift;

	if ( !glConfig.deviceSupportsGamma || r_ignorehwgamma->integer )
		return;

	XF86VidModeGetGammaRampSize( dpy, scrnum, &size );

	switch ( size ) {
		case 256: shift = 0; break;
		case 512: shift = 1; break;
		case 1024: shift = 2; break;
		case 2048: shift = 3; break;
		case 4096: shift = 4; break;
		default: 
			ri.Printf( PRINT_ALL, "Unsupported gamma ramp size: %d\n", size );
		return;
	};

	m = size / 256;
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
		for ( i = 1 ; i < size ; i++ ) {
			if ( table[j][i] < table[j][i-1] ) {
				table[j][i] = table[j][i-1];
			}
		}
	}

    XF86VidModeSetGammaRamp( dpy, scrnum, size, table[0], table[1], table[2] );
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

#ifdef USE_PMLIGHT
	QGL_DoneARB();
#endif

	IN_DeactivateMouse();
  // bk001206 - replaced with H2/Fakk2 solution
  // XAutoRepeatOn(dpy);
  // autorepeaton = qfalse; // bk001130 - from cvs1.17 (mkv)
	if ( dpy )
	{
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

		if ( glConfig.deviceSupportsGamma )
		{
			XF86VidModeSetGamma( dpy, scrnum, &vidmode_InitialGamma );
		}
#endif /* HAVE_XF86DGA */
		// NOTE TTimo opening/closing the display should be necessary only once per run
		//   but it seems QGL_Shutdown gets called in a lot of occasion
		//   in some cases, this XCloseDisplay is known to raise some X errors
		//   ( https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=33 )
		XCloseDisplay( dpy );
	}
	
	vidmode_active = qfalse;
	dpy = NULL;
	win = 0;
	ctx = NULL;

	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &glState, 0, sizeof( glState ) );

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
		ri.Printf( PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n");
		ri.Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;		
	}

	err = GLW_SetMode( drivername, mode, modeFS, fullscreen );

	switch ( err )
	{
	case RSERR_INVALID_FULLSCREEN:
		ri.Printf( PRINT_ALL, "...WARNING: fullscreen unavailable in this mode\n" );
		return qfalse;

	case RSERR_INVALID_MODE:
		ri.Printf( PRINT_ALL, "...WARNING: could not set the given mode (%d)\n", mode );
		return qfalse;

	default:
	    break;
	}

	glConfig.isFullscreen = fullscreen;

	return qtrue;
}

/*
** GLW_SetMode
*/
int GLW_SetMode( const char *drivername, int mode, const char *modeFS, qboolean fullscreen )
{
	PFNGLXCHOOSEFBCONFIGPROC		qglXChooseFBConfig;
	PFNGLXGETFBCONFIGATTRIBPROC		qglXGetFBConfigAttrib;
	PFNGLXGETVISUALFROMFBCONFIGPROC	qglXGetVisualFromFBConfig;
	
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

	#define MSAA_DEPTH_INDEX   15
	#define MSAA_STENCIL_INDEX 17
	#define MSAA_SAMPLES_INDEX 23

	static int MSAAattrib[] = {
		GLX_X_RENDERABLE    , True,
		GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
		GLX_RENDER_TYPE     , GLX_RGBA_BIT,
		GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
		GLX_RED_SIZE        , 8,
		GLX_GREEN_SIZE      , 8,
		GLX_BLUE_SIZE       , 8,
		GLX_DEPTH_SIZE      , 24, // 15
		GLX_STENCIL_SIZE    , 8,  // 17
		GLX_DOUBLEBUFFER    , True,
     	GLX_SAMPLE_BUFFERS  , 1,
		GLX_SAMPLES         , 1,  // 23
		None		
	};

	Window root;
	XVisualInfo *visinfo;

	XSetWindowAttributes attr;
	XSizeHints sizehints;
	unsigned long mask;
	int colorbits, depthbits, stencilbits;
	int tcolorbits, tdepthbits, tstencilbits;
	int dga_MajorVersion, dga_MinorVersion;
	int actualWidth, actualHeight;
	int i;
	const char*   glstring; // bk001130 - from cvs1.17 (mkv)

	dpy = XOpenDisplay( NULL );

	if ( dpy == NULL )
	{
		fprintf( stderr, "Error: couldn't open the X display\n" );
		return RSERR_INVALID_MODE;
	}

	scrnum = DefaultScreen( dpy );
	root = RootWindow( dpy, scrnum );

/* - moved down
  ri.Printf( PRINT_ALL, "Initializing OpenGL display\n");

  ri.Printf (PRINT_ALL, "...setting mode %d:", mode );

  if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode, 0, 0 ) )
  {
    ri.Printf( PRINT_ALL, " invalid mode\n" );
    return RSERR_INVALID_MODE;
  }
  ri.Printf( PRINT_ALL, " %d %d\n", glConfig.vidWidth, glConfig.vidHeight);
*/

/* - moved up
  if (!(dpy = XOpenDisplay(NULL)))
  {
    fprintf(stderr, "Error couldn't open the X display\n");
    return RSERR_INVALID_MODE;
  }
  
  scrnum = DefaultScreen(dpy);
  root = RootWindow(dpy, scrnum);
*/
/*
  actualWidth = glConfig.vidWidth;
  actualHeight = glConfig.vidHeight;
*/

  // Get video mode list
#ifdef HAVE_XF86DGA
	if ( !XF86VidModeQueryVersion( dpy, &vidmode_MajorVersion, &vidmode_MinorVersion ) )
	{
#endif
		vidmode_ext = qfalse;
#ifdef HAVE_XF86DGA
	} 
	else
	{
		ri.Printf( PRINT_ALL, "Using XFree86-VidModeExtension Version %d.%d\n",
			vidmode_MajorVersion, vidmode_MinorVersion );
		vidmode_ext = qtrue;
	}
#endif

  // Check for DGA
#ifdef HAVE_XF86DGA
	dga_MajorVersion = 0;
	dga_MinorVersion = 0;
	if ( in_dgamouse && in_dgamouse->integer )
	{
		if ( !XF86DGAQueryVersion( dpy, &dga_MajorVersion, &dga_MinorVersion ) )
		{
			// unable to query, probably not supported
			ri.Printf( PRINT_ALL, "Failed to detect XF86DGA Mouse\n" );
			ri.Cvar_Set( "in_dgamouse", "0" );
		} 
		else
		{
			ri.Printf( PRINT_ALL, "XF86DGA Mouse (Version %d.%d) initialized\n",
				dga_MajorVersion, dga_MinorVersion );
		}
	}
#endif /* HAVE_XF86DGA */

#ifdef HAVE_XF86DGA
	if ( vidmode_ext )
	{
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
				ri.Printf( PRINT_ALL, "XF86VidModeGetModeLine failed.\n" );
			}
		}
		ri.Printf( PRINT_ALL, "desktop width:%i height:%i\n", desktop_width, desktop_height );
  }
#endif

	ri.Printf( PRINT_ALL, "Initializing OpenGL display\n");

	ri.Printf (PRINT_ALL, "...setting mode %d:", mode );

	if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect,
		mode, modeFS, desktop_width, desktop_height, fullscreen ) )
	{
		ri.Printf( PRINT_ALL, " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}

	ri.Printf( PRINT_ALL, " %d %d\n", glConfig.vidWidth, glConfig.vidHeight );

	actualWidth = glConfig.vidWidth;
	actualHeight = glConfig.vidHeight;

#ifdef HAVE_XF86DGA
	if ( vidmode_ext )
	{
		int best_fit, best_dist, dist, x, y;
		int num_vidmodes;

		XF86VidModeGetAllModeLines( dpy, scrnum, &num_vidmodes, &vidmodes );

		// Are we going fullscreen?  If so, let's change video mode
		if ( fullscreen )
		{
			best_dist = 9999999;
			best_fit = -1;

			for ( i = 0; i < num_vidmodes; i++ )
			{
				if (glConfig.vidWidth > vidmodes[i]->hdisplay ||
					glConfig.vidHeight > vidmodes[i]->vdisplay)
					continue;

				x = glConfig.vidWidth - vidmodes[i]->hdisplay;
				y = glConfig.vidHeight - vidmodes[i]->vdisplay;
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

				ri.Printf( PRINT_ALL, "XFree86-VidModeExtension Activated at %dx%d\n",
					actualWidth, actualHeight );
			}
			else
			{
				//fullscreen = 0;
				ri.Printf( PRINT_ALL, "XFree86-VidModeExtension: No acceptable modes found\n" );
			}
		}
		else
		{
			ri.Printf(PRINT_ALL, "XFree86-VidModeExtension:  Ignored on non-fullscreen/Voodoo\n");
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

		ri.Printf( PRINT_ALL, "Using %d/%d/%d Color bits, %d depth, %d stencil display.\n", 
			attrib[ATTR_RED_IDX], attrib[ATTR_GREEN_IDX], attrib[ATTR_BLUE_IDX],
			attrib[ATTR_DEPTH_IDX], attrib[ATTR_STENCIL_IDX]);

		glConfig.colorBits = tcolorbits;
		glConfig.depthBits = tdepthbits;
		glConfig.stencilBits = tstencilbits;
		break;
	}

	if ( !visinfo )
	{
		ri.Printf( PRINT_ALL, "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	qglXChooseFBConfig = qwglGetProcAddress( "glXChooseFBConfig" );
	qglXGetFBConfigAttrib = qwglGetProcAddress( "glXGetFBConfigAttrib" );
	qglXGetVisualFromFBConfig = qwglGetProcAddress( "glXGetVisualFromFBConfig" );
	
	if ( r_ext_multisample->integer > 0 && colorbits == 24 && qglXChooseFBConfig && qglXGetFBConfigAttrib && qglXGetVisualFromFBConfig )
	{
		GLXFBConfig *fbconfig;
		int numfbconfig;
		int maxval;
	    int bestfbi
		int value;
		
		maxval = 0;
		bestfbi = 0;

		MSAAattrib[ MSAA_DEPTH_INDEX ] = glConfig.depthBits;
		MSAAattrib[ MSAA_STENCIL_INDEX ] = glConfig.stencilBits;
		
		fbconfig = qglXChooseFBConfig( dpy, scrnum, MSAAattrib, &numfbconfig );
		if ( fbconfig )
		{
			for( i = 0; i < numfbconfig; i++ )
			{
				qglXGetFBConfigAttrib( dpy, fbconfig[ i ], GLX_SAMPLES, &value );
				if ( value > maxval )
				{
					bestfbi = i;
					maxval = value;
					if ( maxval >= r_ext_multisample->integer )
					{
						break;
					}
				}
			}
			if ( value )
			{
				visinfo = qglXGetVisualFromFBConfig( dpy, fbconfig[ bestfbi ] );
				ri.Printf( PRINT_ALL, "...using %ix MSAA visual\n", value );
			}
			else
			{
				ri.Printf( PRINT_ALL, "...no MSAA visuals available\n" );
			}
			//
		}
		else
		{
			ri.Printf( PRINT_ALL, "...no MSAA visuals available\n" );
		}// if ( fbconfig )
	}
	else // verbose errors
	{
		if ( !qglXChooseFBConfig || !qglXGetFBConfigAttrib || !qglXGetVisualFromFBConfig )
		{
			ri.Printf( PRINT_ALL, "...MSAA functions resolve error\n" );
		}
		else if ( colorbits != 24 )
		{
			ri.Printf( PRINT_ALL, "...MSAA requires 24 bit color depth\n" );
		}
	}

	/* window attributes */
	attr.background_pixel = BlackPixel( dpy, scrnum );
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap( dpy, root, visinfo->visual, AllocNone );
	attr.event_mask = X_MASK;
	if ( vidmode_active )
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

	if ( vidmode_active )
		XMoveWindow( dpy, win, 0, 0 );

	XFlush( dpy );
	XSync( dpy, False );
	ctx = qglXCreateContext( dpy, visinfo, NULL, True );
	XSync( dpy, False );

	/* GH: Free the visinfo after we're done with it */
	XFree( visinfo );

	qglXMakeCurrent( dpy, win, ctx );

	glstring = (char *)qglGetString( GL_RENDERER );

	if ( !Q_stricmp( glstring, "Mesa X11") || !Q_stricmp( glstring, "Mesa GLX Indirect") )
	{
		if ( !r_allowSoftwareGL->integer )
		{
			ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", glstring );
			ri.Printf( PRINT_ALL, "\n\n***********************************************************\n" );
			ri.Printf( PRINT_ALL, " You are using software Mesa (no hardware acceleration)!   \n" );
			ri.Printf( PRINT_ALL, " Driver DLL used: %s\n", drivername ); 
			ri.Printf( PRINT_ALL, " If this is intentional, add\n" );
			ri.Printf( PRINT_ALL, "       \"+set r_allowSoftwareGL 1\"\n" );
			ri.Printf( PRINT_ALL, " to the command line when starting the game.\n" );
			ri.Printf( PRINT_ALL, "***********************************************************\n");
			GLimp_Shutdown();
			return RSERR_INVALID_MODE;
		}
		else
		{
			ri.Printf( PRINT_ALL, "...using software Mesa (r_allowSoftwareGL==1).\n" );
		}
	}

	return RSERR_OK;
}


/*
** GLimp_HaveExtension
*/
qboolean GLimp_HaveExtension( const char *ext )
{
	const char *ptr = Q_stristr( glw_state.gl_extensions, ext );
	if (ptr == NULL)
		return qfalse;
	ptr += strlen(ext);
	return ((*ptr == ' ') || (*ptr == '\0'));  // verify it's complete string.
}

/*
** GLW_InitExtensions
*/
static void GLW_InitExtensions( void )
{
	if ( !r_allowExtensions->integer )
	{
		ri.Printf( PRINT_ALL, "*** IGNORING OPENGL EXTENSIONS ***\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

	// GL_EXT_texture_compression_s3tc
	glConfig.textureCompression = TC_NONE;
	if ( GLimp_HaveExtension("GL_ARB_texture_compression") &&
		GLimp_HaveExtension("GL_EXT_texture_compression_s3tc") )
	{
	    if ( r_ext_compressed_textures->value ) { 
        	glConfig.textureCompression = TC_S3TC_ARB;
    	    ri.Printf( PRINT_ALL, "...using GL_EXT_texture_compression_s3tc\n" );
	    } else {
        	ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_compression_s3tc\n" );
    	}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_compression_s3tc not found\n" );
	}

	// GL_S3_s3tc
	if (glConfig.textureCompression == TC_NONE) {
		if ( GLimp_HaveExtension("GL_S3_s3tc") )
		{
			if ( r_ext_compressed_textures->value )
			{
				glConfig.textureCompression = TC_S3TC;
				ri.Printf( PRINT_ALL, "...using GL_S3_s3tc\n" );
			} else
			{
				glConfig.textureCompression = TC_NONE;
				ri.Printf( PRINT_ALL, "...ignoring GL_S3_s3tc\n" );
			}
		} else
		{
			glConfig.textureCompression = TC_NONE;
			ri.Printf( PRINT_ALL, "...GL_S3_s3tc not found\n" );
		}
	}

	// GL_EXT_texture_env_add
	glConfig.textureEnvAddAvailable = qfalse;
	if ( GLimp_HaveExtension("EXT_texture_env_add") )
	{
		if ( r_ext_texture_env_add->integer )
		{
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
		} else
		{
			glConfig.textureEnvAddAvailable = qfalse;
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
		}
	} else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
	}

	// GL_ARB_multitexture
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	if ( GLimp_HaveExtension("GL_ARB_multitexture") )
	{
		if ( r_ext_multitexture->value )
		{
			qglMultiTexCoord2fARB = ( PFNGLMULTITEXCOORD2FARBPROC ) dlsym( glw_state.OpenGLLib, "glMultiTexCoord2fARB" );
			qglActiveTextureARB = ( PFNGLACTIVETEXTUREARBPROC ) dlsym( glw_state.OpenGLLib, "glActiveTextureARB" );
			qglClientActiveTextureARB = ( PFNGLCLIENTACTIVETEXTUREARBPROC ) dlsym( glw_state.OpenGLLib, "glClientActiveTextureARB" );

			if ( qglActiveTextureARB )
			{
				GLint glint = 0;
				qglGetIntegerv( GL_MAX_ACTIVE_TEXTURES_ARB, &glint );
				glConfig.numTextureUnits = (int) glint;

				if ( glConfig.numTextureUnits > 1 )
				{
					ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
				} else
				{
					qglMultiTexCoord2fARB = NULL;
					qglActiveTextureARB = NULL;
					qglClientActiveTextureARB = NULL;
					ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
				}
			}
		} else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
		}
	} else
	{
		ri.Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
	}

	// GL_EXT_compiled_vertex_array
	if ( GLimp_HaveExtension("GL_EXT_compiled_vertex_array") )
	{
		if ( r_ext_compiled_vertex_array->value )
		{
			ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
			qglLockArraysEXT = ( void ( APIENTRY * )( int, int ) ) dlsym( glw_state.OpenGLLib, "glLockArraysEXT" );
			qglUnlockArraysEXT = ( void ( APIENTRY * )( void ) ) dlsym( glw_state.OpenGLLib, "glUnlockArraysEXT" );
		if ( !qglLockArraysEXT || !qglUnlockArraysEXT )
		{
			ri.Error (ERR_FATAL, "bad getprocaddress");
		}
		} else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	} else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

	textureFilterAnisotropic = qfalse;
	if ( GLimp_HaveExtension("GL_EXT_texture_filter_anisotropic") )
	{
		if ( r_ext_texture_filter_anisotropic->integer ) {
			qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy );
			if ( maxAnisotropy <= 0 ) {
				ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not properly supported!\n" );
				maxAnisotropy = 0;
			}
			else
			{
				ri.Printf( PRINT_ALL, "...using GL_EXT_texture_filter_anisotropic (max: %i)\n", maxAnisotropy );
				textureFilterAnisotropic = qtrue;
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_filter_anisotropic\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not found\n" );
	}
}


static void GLW_InitGamma( void )
{
  /* Minimum extension version required */
  #define GAMMA_MINMAJOR 2
  #define GAMMA_MINMINOR 0
  
  glConfig.deviceSupportsGamma = qfalse;

#ifdef HAVE_XF86DGA
  if (vidmode_ext)
  {
    if (vidmode_MajorVersion < GAMMA_MINMAJOR || 
        (vidmode_MajorVersion == GAMMA_MINMAJOR && vidmode_MinorVersion < GAMMA_MINMINOR)) {
      ri.Printf( PRINT_ALL, "XF86 Gamma extension not supported in this version\n");
      return;
    }
    XF86VidModeGetGamma(dpy, scrnum, &vidmode_InitialGamma);
    ri.Printf( PRINT_ALL, "XF86 Gamma extension initialized\n");
    glConfig.deviceSupportsGamma = qtrue;
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

	// load the QGL layer
	if ( QGL_Init( name ) )
	{
		fullscreen = r_fullscreen->integer;

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
				ri.Cvar_Set( "r_glDriver", OPENGL_DRIVER_NAME );
				r_glDriver->modified = qfalse;
				return qtrue;
			}
		}

		ri.Error( ERR_FATAL, "GLW_StartOpenGL() - could not load OpenGL subsystem\n" );
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
	ri.Printf( PRINT_ALL, "X Error of failed request: %s\n", buf) ;
	ri.Printf( PRINT_ALL, "  Major opcode of failed request: %d\n", ev->request_code );
	ri.Printf( PRINT_ALL, "  Minor opcode of failed request: %d\n", ev->minor_code );
	ri.Printf( PRINT_ALL, "  Serial number of failed request: %d\n", (int)ev->serial );
	return 0;
}


/*
** GLimp_Init
**
** This routine is responsible for initializing the OS specific portions
** of OpenGL.
*/
void GLimp_Init( void )
{
	size_t len;
	InitSig();

	IN_Init();   // rcg08312005 moved into glimp.

	// set up our custom error handler for X failures
	XSetErrorHandler( &qXErrorHandler );

	r_allowSoftwareGL = ri.Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );

	//
	// load and initialize the specific OpenGL driver
	//
	if ( !GLW_StartOpenGL() )
    	return;

	// This values force the UI to disable driver selection
	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;

	// get our config strings
	Q_strncpyz( glConfig.vendor_string, (char *)qglGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
	Q_strncpyz( glConfig.renderer_string, (char *)qglGetString (GL_RENDERER), sizeof( glConfig.renderer_string ) );
	len = strlen( glConfig.renderer_string );
	if ( len && glConfig.renderer_string[ len - 1 ] == '\n')
		glConfig.renderer_string[ len - 1 ] = '\0';
	Q_strncpyz( glConfig.version_string, (char *)qglGetString (GL_VERSION), sizeof( glConfig.version_string ) );

	Q_strncpyz( glw_state.gl_extensions, (char *)qglGetString (GL_EXTENSIONS), sizeof( glw_state.gl_extensions ) );
	Q_strncpyz( glConfig.extensions_string, glw_state.gl_extensions, sizeof( glConfig.extensions_string ) );

	// initialize extensions
	GLW_InitExtensions();
#ifdef USE_PMLIGHT
	QGL_InitARB();
#endif
	GLW_InitGamma();

	InitSig(); // not clear why this is at begin & end of function

	return;
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
	// don't flip if drawing to front buffer
	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		qglXSwapBuffers( dpy, win );
	}
}

void GLimp_RenderThreadWrapper( void *stub )
{

}


void *GLimp_RendererSleep( void ) 
{
	return NULL;
}


void GLimp_FrontEndSleep( void )
{

}


void GLimp_WakeRenderer( void *data ) 
{

}


/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

void IN_Init( void )
{
	Com_DPrintf( "\n------- Input Initialization -------\n" );

	// mouse variables
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
	in_dgamouse = Cvar_Get( "in_dgamouse", "1", CVAR_ARCHIVE );
	in_shiftedKeys = Cvar_Get( "in_shiftedKeys", "0", CVAR_ARCHIVE );

	// turn on-off sub-frame timing of X events
	in_subframe = Cvar_Get ("in_subframe", "1", CVAR_ARCHIVE);

	// developer feature, allows to break without loosing mouse pointer
	in_nograb = Cvar_Get ("in_nograb", "0", 0);

#ifdef USE_JOYSTICK
	// bk001130 - from cvs.17 (mkv), joystick variables
	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE|CVAR_LATCH );
	// bk001130 - changed this to match win32
	in_joystickDebug = Cvar_Get( "in_debugjoystick", "0", CVAR_TEMP );
	joy_threshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE ); // FIXME: in_joythreshold
#endif

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

	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
	{
		// temporarily deactivate if not in the game and
		// running on the desktop
		// voodoo always counts as full screen
		if ( Cvar_VariableIntegerValue( "r_fullscreen" ) == 0 )
		{
			IN_DeactivateMouse();
			return;
		}
	}

	IN_ActivateMouse();
}


void IN_Activate( void )
{

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

#ifdef USE_JOYSTICK
// bk010216 - added stubs for non-Linux UNIXes here
// FIXME - use NO_JOYSTICK or something else generic

#if (defined( __FreeBSD__ ) || defined( __sun)) // rb010123
void IN_StartupJoystick( void ) {}
void IN_JoyMove( void ) {}
#endif
#endif

