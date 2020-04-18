
#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#include "../client/client.h"
#include "../sdl/sdl_glw.h"

static cvar_t *in_keyboardDebug;

#ifdef USE_JOYSTICK
static SDL_GameController *gamepad;
static SDL_Joystick *stick = NULL;
#endif

static qboolean mouseAvailable = qfalse;
static qboolean mouseActive = qfalse;

static cvar_t *in_mouse;

static cvar_t *in_joystick;
static cvar_t *in_joystickThreshold;
#ifdef USE_JOYSTICK
static cvar_t *in_joystickNo;
static cvar_t *in_joystickUseAnalog;

static cvar_t *j_pitch;
static cvar_t *j_yaw;
static cvar_t *j_forward;
static cvar_t *j_side;
static cvar_t *j_up;
static cvar_t *j_pitch_axis;
static cvar_t *j_yaw_axis;
static cvar_t *j_forward_axis;
static cvar_t *j_side_axis;
static cvar_t *j_up_axis;
#endif

#define Com_QueueEvent Sys_QueEvent

static cvar_t *cl_consoleKeys;

static int in_eventTime = 0;

#define CTRL(a) ((a)-'a'+1)

static keyNum_t lastKeyDown = 0;
static float touchhats[5][2] = {};

/*
===============
IN_PrintKey
===============
*/
static void IN_PrintKey( const SDL_Keysym *keysym, keyNum_t key, qboolean down )
{
	if( down )
		Com_Printf( "+ " );
	else
		Com_Printf( "  " );

	Com_Printf( "Scancode: 0x%02x(%s) Sym: 0x%02x(%s)",
			keysym->scancode, SDL_GetScancodeName( keysym->scancode ),
			keysym->sym, SDL_GetKeyName( keysym->sym ) );

	if( keysym->mod & KMOD_LSHIFT )   Com_Printf( " KMOD_LSHIFT" );
	if( keysym->mod & KMOD_RSHIFT )   Com_Printf( " KMOD_RSHIFT" );
	if( keysym->mod & KMOD_LCTRL )    Com_Printf( " KMOD_LCTRL" );
	if( keysym->mod & KMOD_RCTRL )    Com_Printf( " KMOD_RCTRL" );
	if( keysym->mod & KMOD_LALT )     Com_Printf( " KMOD_LALT" );
	if( keysym->mod & KMOD_RALT )     Com_Printf( " KMOD_RALT" );
	if( keysym->mod & KMOD_LGUI )     Com_Printf( " KMOD_LGUI" );
	if( keysym->mod & KMOD_RGUI )     Com_Printf( " KMOD_RGUI" );
	if( keysym->mod & KMOD_NUM )      Com_Printf( " KMOD_NUM" );
	if( keysym->mod & KMOD_CAPS )     Com_Printf( " KMOD_CAPS" );
	if( keysym->mod & KMOD_MODE )     Com_Printf( " KMOD_MODE" );
	if( keysym->mod & KMOD_RESERVED ) Com_Printf( " KMOD_RESERVED" );

	Com_Printf( " Q:0x%02x(%s)\n", key, Key_KeynumToString( key ) );
}


#define MAX_CONSOLE_KEYS 16

/*
===============
IN_IsConsoleKey

TODO: If the SDL_Scancode situation improves, use it instead of
      both of these methods
===============
*/
static qboolean IN_IsConsoleKey( keyNum_t key, int character )
{
	typedef struct consoleKey_s
	{
		enum
		{
			QUAKE_KEY,
			CHARACTER
		} type;

		union
		{
			keyNum_t key;
			int character;
		} u;
	} consoleKey_t;

	static consoleKey_t consoleKeys[ MAX_CONSOLE_KEYS ];
	static int numConsoleKeys = 0;
	int i;

	// Only parse the variable when it changes
	if ( cl_consoleKeys->modified )
	{
		const char *text_p, *token;

		cl_consoleKeys->modified = qfalse;
		text_p = cl_consoleKeys->string;
		numConsoleKeys = 0;

		while( numConsoleKeys < MAX_CONSOLE_KEYS )
		{
			consoleKey_t *c = &consoleKeys[ numConsoleKeys ];
			int charCode = 0;

			token = COM_Parse( &text_p );
			if( !token[ 0 ] )
				break;

			charCode = Com_HexStrToInt( token );

			if( charCode > 0 )
			{
				c->type = CHARACTER;
				c->u.character = charCode;
			}
			else
			{
				c->type = QUAKE_KEY;
				c->u.key = Key_StringToKeynum( token );

				// 0 isn't a key
				if ( c->u.key <= 0 )
					continue;
			}

			numConsoleKeys++;
		}
	}

	// If the character is the same as the key, prefer the character
	if ( key == character )
		key = 0;

	for ( i = 0; i < numConsoleKeys; i++ )
	{
		consoleKey_t *c = &consoleKeys[ i ];

		switch ( c->type )
		{
			case QUAKE_KEY:
				if( key && c->u.key == key )
					return qtrue;
				break;

			case CHARACTER:
				if( c->u.character == character )
					return qtrue;
				break;
		}
	}

	return qfalse;
}


/*
===============
IN_TranslateSDLToQ3Key
===============
*/
static keyNum_t IN_TranslateSDLToQ3Key( SDL_Keysym *keysym, qboolean down )
{
	keyNum_t key = 0;

	if ( keysym->scancode >= SDL_SCANCODE_1 && keysym->scancode <= SDL_SCANCODE_0 )
	{
		// Always map the number keys as such even if they actually map
		// to other characters (eg, "1" is "&" on an AZERTY keyboard).
		// This is required for SDL before 2.0.6, except on Windows
		// which already had this behavior.
		if( keysym->scancode == SDL_SCANCODE_0 )
			key = '0';
		else
			key = '1' + keysym->scancode - SDL_SCANCODE_1;
	}
	else if( keysym->sym >= SDLK_SPACE && keysym->sym < SDLK_DELETE )
	{
		// These happen to match the ASCII chars
		key = (int)keysym->sym;
	}
	else
	{
		switch( keysym->sym )
		{
			case SDLK_PAGEUP:       key = K_PGUP;          break;
			case SDLK_KP_9:         key = K_KP_PGUP;       break;
			case SDLK_PAGEDOWN:     key = K_PGDN;          break;
			case SDLK_KP_3:         key = K_KP_PGDN;       break;
			case SDLK_KP_7:         key = K_KP_HOME;       break;
			case SDLK_HOME:         key = K_HOME;          break;
			case SDLK_KP_1:         key = K_KP_END;        break;
			case SDLK_END:          key = K_END;           break;
			case SDLK_KP_4:         key = K_KP_LEFTARROW;  break;
			case SDLK_LEFT:         key = K_LEFTARROW;     break;
			case SDLK_KP_6:         key = K_KP_RIGHTARROW; break;
			case SDLK_RIGHT:        key = K_RIGHTARROW;    break;
			case SDLK_KP_2:         key = K_KP_DOWNARROW;  break;
			case SDLK_DOWN:         key = K_DOWNARROW;     break;
			case SDLK_KP_8:         key = K_KP_UPARROW;    break;
			case SDLK_UP:           key = K_UPARROW;       break;
			case SDLK_ESCAPE:       key = K_ESCAPE;        break;
			case SDLK_KP_ENTER:     key = K_KP_ENTER;      break;
			case SDLK_RETURN:       key = K_ENTER;         break;
			case SDLK_TAB:          key = K_TAB;           break;
			case SDLK_F1:           key = K_F1;            break;
			case SDLK_F2:           key = K_F2;            break;
			case SDLK_F3:           key = K_F3;            break;
			case SDLK_F4:           key = K_F4;            break;
			case SDLK_F5:           key = K_F5;            break;
			case SDLK_F6:           key = K_F6;            break;
			case SDLK_F7:           key = K_F7;            break;
			case SDLK_F8:           key = K_F8;            break;
			case SDLK_F9:           key = K_F9;            break;
			case SDLK_F10:          key = K_F10;           break;
			case SDLK_F11:          key = K_F11;           break;
			case SDLK_F12:          key = K_F12;           break;
			case SDLK_F13:          key = K_F13;           break;
			case SDLK_F14:          key = K_F14;           break;
			case SDLK_F15:          key = K_F15;           break;

			case SDLK_BACKSPACE:    key = K_BACKSPACE;     break;
			case SDLK_KP_PERIOD:    key = K_KP_DEL;        break;
			case SDLK_DELETE:       key = K_DEL;           break;
			case SDLK_PAUSE:        key = K_PAUSE;         break;

			case SDLK_LSHIFT:
			case SDLK_RSHIFT:       key = K_SHIFT;         break;

			case SDLK_LCTRL:
			case SDLK_RCTRL:        key = K_CTRL;          break;

#ifdef __APPLE__
			case SDLK_RGUI:
			case SDLK_LGUI:         key = K_COMMAND;       break;
#else
			case SDLK_RGUI:
			case SDLK_LGUI:         key = K_SUPER;         break;
#endif

			case SDLK_RALT:
			case SDLK_LALT:         key = K_ALT;           break;

			case SDLK_KP_5:         key = K_KP_5;          break;
			case SDLK_INSERT:       key = K_INS;           break;
			case SDLK_KP_0:         key = K_KP_INS;        break;
			case SDLK_KP_MULTIPLY:  key = K_KP_STAR;       break;
			case SDLK_KP_PLUS:      key = K_KP_PLUS;       break;
			case SDLK_KP_MINUS:     key = K_KP_MINUS;      break;
			case SDLK_KP_DIVIDE:    key = K_KP_SLASH;      break;

			case SDLK_MODE:         key = K_MODE;          break;
			case SDLK_HELP:         key = K_HELP;          break;
			case SDLK_PRINTSCREEN:  key = K_PRINT;         break;
			case SDLK_SYSREQ:       key = K_SYSREQ;        break;
			case SDLK_MENU:         key = K_MENU;          break;
			case SDLK_APPLICATION:	key = K_MENU;          break;
			case SDLK_POWER:        key = K_POWER;         break;
			case SDLK_UNDO:         key = K_UNDO;          break;
			case SDLK_SCROLLLOCK:   key = K_SCROLLOCK;     break;
			case SDLK_NUMLOCKCLEAR: key = K_KP_NUMLOCK;    break;
			case SDLK_CAPSLOCK:     key = K_CAPSLOCK;      break;

			default:
#if 1
				key = 0;
#else
				if( !( keysym->sym & SDLK_SCANCODE_MASK ) && keysym->scancode <= 95 )
				{
					// Map Unicode characters to 95 world keys using the key's scan code.
					// FIXME: There aren't enough world keys to cover all the scancodes.
					// Maybe create a map of scancode to quake key at start up and on
					// key map change; allocate world key numbers as needed similar
					// to SDL 1.2.
					key = K_WORLD_0 + (int)keysym->scancode;
				}
#endif
				break;
		}
	}

	if( in_keyboardDebug->integer )
		IN_PrintKey( keysym, key, down );

	if( IN_IsConsoleKey( key, 0 ) )
	{
		// Console keys can't be bound or generate characters
		key = K_CONSOLE;
	}

	return key;
}

/*
===============
IN_ActivateMouse
===============
*/
static void IN_ActivateMouse( qboolean isFullscreen )
{
	if ( !mouseAvailable || !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	if ( !mouseActive )
	{
		SDL_SetRelativeMouseMode( in_mouse->integer == 1 ? SDL_TRUE : SDL_FALSE );
		SDL_SetWindowGrab( SDL_window, SDL_TRUE );
	}

	// in_nograb makes no sense in fullscreen mode
	if ( !isFullscreen )
	{
		if ( in_nograb->modified || !mouseActive )
		{
			if ( in_nograb->integer ) {
				SDL_SetRelativeMouseMode( SDL_FALSE );
				SDL_SetWindowGrab( SDL_window, SDL_FALSE );
			} else {
				SDL_SetRelativeMouseMode( in_mouse->integer == 1 ? SDL_TRUE : SDL_FALSE );
				SDL_SetWindowGrab( SDL_window, SDL_TRUE );
			}

			in_nograb->modified = qfalse;
		}
	}

	mouseActive = qtrue;
}


/*
===============
IN_DeactivateMouse
===============
*/
static void IN_DeactivateMouse( qboolean isFullscreen )
{
	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	// Always show the cursor when the mouse is disabled,
	// but not when fullscreen
	if ( !isFullscreen )
		SDL_ShowCursor( SDL_TRUE );

	if ( !mouseAvailable )
		return;

	if ( mouseActive )
	{
		SDL_SetWindowGrab( SDL_window, SDL_FALSE );
		SDL_SetRelativeMouseMode( SDL_FALSE );

		// Don't warp the mouse unless the cursor is within the window
		if ( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_MOUSE_FOCUS )
			SDL_WarpMouseInWindow( SDL_window, glw_state.window_width / 2, glw_state.window_height / 2 );

		mouseActive = qfalse;
	}
}

void IN_PushKeyDown(SDL_KeyboardEvent e)
{
  keyNum_t key = 0;

  if ( e.repeat && Key_GetCatcher() == 0 )
    return;
  key = IN_TranslateSDLToQ3Key( &e.keysym, qtrue );
	
  if ( key == K_ENTER && keys[K_ALT].down ) {
    Cvar_SetIntegerValue( "r_fullscreen", glw_state.isFullscreen ? 0 : 1 );
    Cbuf_AddText( "vid_restart\n" );
    return;
  }

  if ( key ) {
    Com_QueueEvent( in_eventTime, SE_KEY, key, qtrue, 0, NULL );

    if ( key == K_BACKSPACE )
      Com_QueueEvent( in_eventTime, SE_CHAR, CTRL('h'), 0, 0, NULL );

    else if( keys[K_CTRL].down && key >= 'a' && key <= 'z' )
      Com_QueueEvent( in_eventTime, SE_CHAR, CTRL(key), 0, 0, NULL );
  }

  lastKeyDown = key;
}

void IN_PushKeyUp(SDL_KeyboardEvent e)
{
	keyNum_t key = 0;
	
  if( ( key = IN_TranslateSDLToQ3Key( &e.keysym, qfalse ) ) )
    Com_QueueEvent( in_eventTime, SE_KEY, key, qfalse, 0, NULL );

  lastKeyDown = 0;
}

void IN_PushTextEntry(SDL_TextInputEvent e) {
	if( lastKeyDown != K_CONSOLE )
	{
		char *c = e.text;

		// Quick and dirty UTF-8 to UTF-32 conversion
		while ( *c )
		{
			int utf32 = 0;

			if( ( *c & 0x80 ) == 0 )
				utf32 = *c++;
			else if( ( *c & 0xE0 ) == 0xC0 ) // 110x xxxx
			{
				utf32 |= ( *c++ & 0x1F ) << 6;
				utf32 |= ( *c++ & 0x3F );
			}
			else if( ( *c & 0xF0 ) == 0xE0 ) // 1110 xxxx
			{
				utf32 |= ( *c++ & 0x0F ) << 12;
				utf32 |= ( *c++ & 0x3F ) << 6;
				utf32 |= ( *c++ & 0x3F );
			}
			else if( ( *c & 0xF8 ) == 0xF0 ) // 1111 0xxx
			{
				utf32 |= ( *c++ & 0x07 ) << 18;
				utf32 |= ( *c++ & 0x3F ) << 12;
				utf32 |= ( *c++ & 0x3F ) << 6;
				utf32 |= ( *c++ & 0x3F );
			}
			else
			{
				Com_DPrintf( "Unrecognised UTF-8 lead byte: 0x%x\n", (unsigned int)*c );
				c++;
			}

			if( utf32 != 0 )
			{
				if ( IN_IsConsoleKey( 0, utf32 ) )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_CONSOLE, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_CONSOLE, qfalse, 0, NULL );
				}
				else
					Com_QueueEvent( in_eventTime, SE_CHAR, utf32, 0, 0, NULL );
			}
		}
	}
}

void IN_PushMouseMove(SDL_MouseMotionEvent e) {
	if( mouseActive && !in_joystick->integer )
	{
		if( !e.xrel && !e.yrel )
			return;
		Com_QueueEvent( in_eventTime, SE_MOUSE, e.xrel, e.yrel, 0, NULL );
	}
}

void IN_PushMouseButton(SDL_MouseButtonEvent e) {
	int b;
	if(!mouseActive || in_joystick->integer) {
		return;
	}
	switch( e.button )
	{
		case SDL_BUTTON_LEFT:   b = K_MOUSE1;     break;
		case SDL_BUTTON_MIDDLE: b = K_MOUSE3;     break;
		case SDL_BUTTON_RIGHT:  b = K_MOUSE2;     break;
		case SDL_BUTTON_X1:     b = K_MOUSE4;     break;
		case SDL_BUTTON_X2:     b = K_MOUSE5;     break;
		default:                b = K_AUX1 + ( e.button - SDL_BUTTON_X2 + 1 ) % 16; break;
	}
	Com_QueueEvent( in_eventTime, SE_KEY, b,
		( e.type == SDL_MOUSEBUTTONDOWN ? qtrue : qfalse ), 0, NULL );
}

void IN_PushMouseWheel(SDL_MouseWheelEvent e)
{
	if( e.y > 0 )
	{
		Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
		Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
	}
	else if( e.y < 0 )
	{
		Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
		Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
	}
}

void IN_PushTouchFinger(SDL_TouchFingerEvent e)
{
	if(e.type == SDL_FINGERMOTION) {
		//Com_QueueEvent( in_eventTime, SE_MOUSE_ABS, fingerMinusGap, e.tfinger.y * 480, 0, NULL );
		float ratio = (float)cls.glconfig.vidWidth / (float)cls.glconfig.vidHeight;
		touchhats[e.fingerId][0] = (e.x * ratio) * 50;
		touchhats[e.fingerId][1] = e.y * 50;
	}
	else if (e.type == SDL_FINGERDOWN) {
		if((Key_GetCatcher( ) & KEYCATCH_UI) && e.fingerId == 3) {
			Com_QueueEvent( in_eventTime, SE_MOUSE_ABS, e.x * cls.glconfig.vidWidth, e.y * cls.glconfig.vidHeight, 0, NULL );
		}
		Com_QueueEvent( in_eventTime+1, SE_FINGER_DOWN, K_MOUSE1, e.fingerId, 0, NULL );
	}
	else if(e.type == SDL_FINGERUP) {
		//Com_QueueEvent( in_eventTime+1, SE_KEY, K_MOUSE1, qfalse, 0, NULL );
		Com_QueueEvent( in_eventTime+1, SE_FINGER_UP, K_MOUSE1, e.fingerId, 0, NULL );
		touchhats[e.fingerId][0] = 0;
		touchhats[e.fingerId][1] = 0;
	}
}

void IN_PushEvent(int type, int *event)
{
	in_eventTime = Sys_Milliseconds();
	
  if(type == (int)&IN_PushKeyDown) {
    IN_PushKeyDown(*(SDL_KeyboardEvent *)event);
  }
  if(type == (int)&IN_PushKeyUp) {
    IN_PushKeyUp(*(SDL_KeyboardEvent *)event);
  }
	if(type == (int)&IN_PushTextEntry) {
    IN_PushTextEntry(*(SDL_TextInputEvent *)event);
  }
	if(type == (int)&IN_PushMouseMove) {
		IN_PushMouseMove(*(SDL_MouseMotionEvent *)event);
	}
	if(type == (int)&IN_PushMouseButton) {
		IN_PushMouseButton(*(SDL_MouseButtonEvent *)event);
	}
	if(type == (int)&IN_PushMouseWheel) {
		IN_PushMouseWheel(*(SDL_MouseWheelEvent *)event);
	}
	if(type == (int)&IN_PushTouchFinger) {
		IN_PushTouchFinger(*(SDL_TouchFingerEvent *)event);
	}
}

void IN_PushInit(int *inputInterface)
{
  inputInterface[0] = (int)&IN_PushKeyDown;
  inputInterface[1] = (int)&IN_PushKeyUp;
	inputInterface[2] = (int)&IN_PushTextEntry;
	inputInterface[3] = (int)&IN_PushMouseMove;
	inputInterface[4] = (int)&IN_PushMouseButton;
	inputInterface[5] = (int)&IN_PushMouseWheel;
	inputInterface[6] = (int)&IN_PushTouchFinger;
}

/*
===============
IN_Minimize

Minimize the game so that user is back at the desktop
===============
*/
static void IN_Minimize( void )
{
	SDL_MinimizeWindow( SDL_window );
}

/*
===============
IN_Frame
===============
*/
void IN_Frame( void )
{
	qboolean loading;
	qboolean fullscreen;
	int i;

#ifdef USE_JOYSTICK
	IN_JoyMove();
#endif

	// If not DISCONNECTED (main menu) or ACTIVE (in game), we're loading
	loading = ( cls.state != CA_DISCONNECTED && cls.state != CA_ACTIVE );

	fullscreen = glw_state.isFullscreen;

	if ( !fullscreen && ( Key_GetCatcher() & KEYCATCH_CONSOLE ) )
	{
		// Console is down in windowed mode
		IN_DeactivateMouse( fullscreen );
	}
	else if( !fullscreen && loading )
	{
		// Loading in windowed mode
		IN_DeactivateMouse( fullscreen );
	}
	else if ( !( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_INPUT_FOCUS ) )
	{
		// Window not got focus
		IN_DeactivateMouse( fullscreen );
	}
	else
		IN_ActivateMouse( fullscreen );

	for(i = 1; i < 4; i++) {
		/*
		if(i == 2 && !(Key_GetCatcher( ) & KEYCATCH_UI)) {
			if(touchhats[i][0] != 0 || touchhats[i][1] != 0) {
				Com_QueueEvent( in_eventTime, SE_MOUSE, touchhats[i][0], touchhats[i][1], 0, NULL );
			}
		}
		*/
		// TODO: make config options for this?
		if(i == 1 && !(Key_GetCatcher( ) & KEYCATCH_UI)) {
			if(touchhats[i][0] != 0 || touchhats[i][1] != 0) {
				Com_QueueEvent( in_eventTime, SE_MOUSE, touchhats[i][0], 0, 0, NULL );
			}
		}
	}
}


/*
===============
IN_Init
===============
*/
void IN_Init( void )
{
	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		Com_Error( ERR_FATAL, "IN_Init called before SDL_Init( SDL_INIT_VIDEO )" );
		return;
	}

	Com_DPrintf( "\n------- Input Initialization -------\n" );

	in_keyboardDebug = Cvar_Get( "in_keyboardDebug", "0", CVAR_ARCHIVE );

	// mouse variables
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( in_mouse, "-1", "1", CV_INTEGER );

	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE|CVAR_LATCH );
	in_joystickThreshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE );
#ifdef USE_JOYSTICK
	j_pitch =        Cvar_Get( "j_pitch",        "0.022", CVAR_ARCHIVE_ND );
	j_yaw =          Cvar_Get( "j_yaw",          "-0.022", CVAR_ARCHIVE_ND );
	j_forward =      Cvar_Get( "j_forward",      "-0.25", CVAR_ARCHIVE_ND );
	j_side =         Cvar_Get( "j_side",         "0.25", CVAR_ARCHIVE_ND );
	j_up =           Cvar_Get( "j_up",           "0", CVAR_ARCHIVE_ND );

	j_pitch_axis =   Cvar_Get( "j_pitch_axis",   "3", CVAR_ARCHIVE_ND );
	j_yaw_axis =     Cvar_Get( "j_yaw_axis",     "2", CVAR_ARCHIVE_ND );
	j_forward_axis = Cvar_Get( "j_forward_axis", "1", CVAR_ARCHIVE_ND );
	j_side_axis =    Cvar_Get( "j_side_axis",    "0", CVAR_ARCHIVE_ND );
	j_up_axis =      Cvar_Get( "j_up_axis",      "4", CVAR_ARCHIVE_ND );

	Cvar_CheckRange( j_pitch_axis,   "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_CheckRange( j_yaw_axis,     "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_CheckRange( j_forward_axis, "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_CheckRange( j_side_axis,    "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_CheckRange( j_up_axis,      "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
#endif

	// ~ and `, as keys and characters
	cl_consoleKeys = Cvar_Get( "cl_consoleKeys", "~ ` 0x7e 0x60", CVAR_ARCHIVE );

#ifndef EMSCRIPTEN
	SDL_StartTextInput();
#endif

	mouseAvailable = ( in_mouse->value != 0 ) ? qtrue : qfalse;

	IN_DeactivateMouse( glw_state.isFullscreen );

#ifdef USE_JOYSTICK
	IN_InitJoystick( );
#endif

	Cmd_AddCommand( "minimize", IN_Minimize );

	Com_DPrintf( "------------------------------------\n" );
}


/*
===============
IN_Shutdown
===============
*/
void IN_Shutdown( void )
{
	SDL_StopTextInput();

	IN_DeactivateMouse( glw_state.isFullscreen );

	mouseAvailable = qfalse;

#ifdef USE_JOYSTICK
	IN_ShutdownJoystick();
#endif
}


/*
===============
IN_Restart
===============
*/
void IN_Restart( void )
{
#ifdef USE_JOYSTICK
	IN_ShutdownJoystick();
#endif
	IN_Init();
}
