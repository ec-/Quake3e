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
#include "client.h"

/*

key up events are sent even if in console mode

*/

field_t		g_consoleField;
field_t		chatField;
qboolean	chat_team;

int			chat_playerNum;

static void Field_CharEvent( field_t *edit, int ch );

/*
=============================================================================

EDIT FIELDS

=============================================================================
*/


/*
===================
Field_Draw

Handles horizontal scrolling and cursor blinking
x, y, and width are in pixels
===================
*/
static void Field_VariableSizeDraw( field_t *edit, int x, int y, int width, int size, qboolean showCursor,
		qboolean noColorEscape ) {
	int		len;
	int		drawLen;
	int		prestep;
	int		cursorChar;
	char	str[MAX_STRING_CHARS], *s;
	int		i;
	int		curColor;

	drawLen = edit->widthInChars - 1; // - 1 so there is always a space for the cursor
	len = strlen( edit->buffer );

	// guarantee that cursor will be visible
	if ( len <= drawLen ) {
		prestep = 0;
	} else {
		if ( edit->scroll + drawLen > len ) {
			edit->scroll = len - drawLen;
			if ( edit->scroll < 0 ) {
				edit->scroll = 0;
			}
		}
		prestep = edit->scroll;
	}

	if ( prestep + drawLen > len ) {
		drawLen = len - prestep;
	}

	// extract <drawLen> characters from the field at <prestep>
	if ( drawLen >= MAX_STRING_CHARS ) {
		Com_Error( ERR_DROP, "drawLen >= MAX_STRING_CHARS" );
	}

	Com_Memcpy( str, edit->buffer + prestep, drawLen );
	str[ drawLen ] = '\0';

	// color tracking
	curColor = COLOR_WHITE;

	if ( prestep > 0 ) {
		// we need to track last actual color because we cut some text before
		s = edit->buffer;
		for ( i = 0; i < prestep + 1; i++, s++ ) {
			if ( Q_IsColorString( s ) ) {
				curColor = *(s+1);
				s++;						
			}
		}
		// scroll marker
		// FIXME: force white color?
		if ( str[0] ) {
			str[0] = '<';
		}
	}

	// draw it
	if ( size == smallchar_width ) {
		SCR_DrawSmallStringExt( x, y, str, g_color_table[ ColorIndexFromChar( curColor ) ],
			qfalse, noColorEscape );
		if ( len > drawLen + prestep ) {
			SCR_DrawSmallChar( x + ( edit->widthInChars - 1 ) * size, y, '>' );
		}
	} else {
		if ( len > drawLen + prestep ) {
			SCR_DrawStringExt( x + ( edit->widthInChars - 1 ) * BIGCHAR_WIDTH, y, size, ">",
				g_color_table[ ColorIndex( COLOR_WHITE ) ], qfalse, noColorEscape );
		}
		// draw big string with drop shadow
		SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, str, g_color_table[ ColorIndexFromChar( curColor ) ],
			qfalse, noColorEscape );
	}

	// draw the cursor
	if ( showCursor ) {
		if ( cls.realtime & 256 ) {
			return;		// off blink
		}

		if ( key_overstrikeMode ) {
			cursorChar = 11;
		} else {
			cursorChar = 10;
		}

		i = drawLen - strlen( str );

		if ( size == smallchar_width ) {
			SCR_DrawSmallChar( x + ( edit->cursor - prestep - i ) * size, y, cursorChar );
		} else {
			str[0] = cursorChar;
			str[1] = '\0';
			SCR_DrawBigString( x + ( edit->cursor - prestep - i ) * BIGCHAR_WIDTH, y, str, 1.0, qfalse );
		}
	}
}


void Field_Draw( field_t *edit, int x, int y, int width, qboolean showCursor, qboolean noColorEscape )
{
	Field_VariableSizeDraw( edit, x, y, width, smallchar_width, showCursor, noColorEscape );
}


void Field_BigDraw( field_t *edit, int x, int y, int width, qboolean showCursor, qboolean noColorEscape )
{
	Field_VariableSizeDraw( edit, x, y, width, bigchar_width, showCursor, noColorEscape );
}


/*
================
Field_Paste
================
*/
static void Field_Paste( field_t *edit ) {
	char	*cbd;
	int		pasteLen, i;

	cbd = Sys_GetClipboardData();

	if ( !cbd ) {
		return;
	}

	// send as if typed, so insert / overstrike works properly
	pasteLen = strlen( cbd );
	for ( i = 0 ; i < pasteLen ; i++ ) {
		Field_CharEvent( edit, cbd[i] );
	}

	Z_Free( cbd );
}


/*
=================
Field_KeyDownEvent

Performs the basic line editing functions for the console,
in-game talk, and menu fields

Key events are used for non-printable characters, others are gotten from char events.
=================
*/
static void Field_KeyDownEvent( field_t *edit, int key ) {
	int		len;

	// shift-insert is paste
	if ( ( ( key == K_INS ) || ( key == K_KP_INS ) ) && keys[K_SHIFT].down ) {
		Field_Paste( edit );
		return;
	}

	len = strlen( edit->buffer );

	switch ( key ) {
		case K_DEL:
			if ( edit->cursor < len ) {
				memmove( edit->buffer + edit->cursor, 
					edit->buffer + edit->cursor + 1, len - edit->cursor );
			}
			break;

		case K_RIGHTARROW:
			if ( edit->cursor < len ) {
				edit->cursor++;
			}
			break;

		case K_LEFTARROW:
			if ( edit->cursor > 0 ) {
				edit->cursor--;
			}
			break;

		case K_HOME:
			edit->cursor = 0;
			break;

		case K_END:
			edit->cursor = len;
			break;

		case K_INS:
			key_overstrikeMode = !key_overstrikeMode;
			break;

		default:
			break;
	}

	// Change scroll if cursor is no longer visible
	if ( edit->cursor < edit->scroll ) {
		edit->scroll = edit->cursor;
	} else if ( edit->cursor >= edit->scroll + edit->widthInChars && edit->cursor <= len ) {
		edit->scroll = edit->cursor - edit->widthInChars + 1;
	}
}


/*
==================
Field_CharEvent
==================
*/
static void Field_CharEvent( field_t *edit, int ch ) {
	int		len;

	if ( ch == 'v' - 'a' + 1 ) {	// ctrl-v is paste
		Field_Paste( edit );
		return;
	}

	if ( ch == 'c' - 'a' + 1 ) {	// ctrl-c clears the field
		Field_Clear( edit );
		return;
	}

	len = strlen( edit->buffer );

	if ( ch == 'h' - 'a' + 1 )	{	// ctrl-h is backspace
		if ( edit->cursor > 0 ) {
			memmove( edit->buffer + edit->cursor - 1, 
				edit->buffer + edit->cursor, len + 1 - edit->cursor );
			edit->cursor--;
			if ( edit->cursor < edit->scroll )
			{
				edit->scroll--;
			}
		}
		return;
	}

	if ( ch == 'a' - 'a' + 1 ) {	// ctrl-a is home
		edit->cursor = 0;
		edit->scroll = 0;
		return;
	}

	if ( ch == 'e' - 'a' + 1 ) {	// ctrl-e is end
		edit->cursor = len;
		edit->scroll = edit->cursor - edit->widthInChars;
		return;
	}

	//
	// ignore any other non printable chars
	//
	if ( ch < ' ' ) {
		return;
	}

	if ( key_overstrikeMode ) {	
		// - 2 to leave room for the leading slash and trailing \0
		if ( edit->cursor == MAX_EDIT_LINE - 2 )
			return;
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	} else {	// insert mode
		// - 2 to leave room for the leading slash and trailing \0
		if ( len == MAX_EDIT_LINE - 2 ) {
			return; // all full
		}
		memmove( edit->buffer + edit->cursor + 1, 
			edit->buffer + edit->cursor, len + 1 - edit->cursor );
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	}


	if ( edit->cursor >= edit->widthInChars ) {
		edit->scroll++;
	}

	if ( edit->cursor == len + 1) {
		edit->buffer[edit->cursor] = '\0';
	}
}


/*
=============================================================================

CONSOLE LINE EDITING

==============================================================================
*/

/*
====================
Console_Key

Handles history and console scrollback
====================
*/
static void Console_Key( int key ) {
	// ctrl-L clears screen
	if ( key == 'l' && keys[K_CTRL].down ) {
		Cbuf_AddText( "clear\n" );
		return;
	}

	// enter finishes the line
	if ( key == K_ENTER || key == K_KP_ENTER ) {
		// if not in the game explicitly prepend a slash if needed
		if ( cls.state != CA_ACTIVE 
			&& g_consoleField.buffer[0] != '\0'
			&& g_consoleField.buffer[0] != '\\'
			&& g_consoleField.buffer[0] != '/' ) {
			char	temp[MAX_EDIT_LINE-1];

			Q_strncpyz( temp, g_consoleField.buffer, sizeof( temp ) );
			Com_sprintf( g_consoleField.buffer, sizeof( g_consoleField.buffer ), "\\%s", temp );
			g_consoleField.cursor++;
		}

		Com_Printf( "]%s\n", g_consoleField.buffer );

		// leading slash is an explicit command
		if ( g_consoleField.buffer[0] == '\\' || g_consoleField.buffer[0] == '/' ) {
			Cbuf_AddText( g_consoleField.buffer+1 );	// valid command
			Cbuf_AddText( "\n" );
		} else {
			// other text will be chat messages
			if ( !g_consoleField.buffer[0] ) {
				return;	// empty lines just scroll the console without adding to history
			} else {
				Cbuf_AddText( "cmd say " );
				Cbuf_AddText( g_consoleField.buffer );
				Cbuf_AddText( "\n" );
			}
		}

		// copy line to history buffer
		Con_SaveField( &g_consoleField );

		Field_Clear( &g_consoleField );
		g_consoleField.widthInChars = g_console_field_width;

		if ( cls.state == CA_DISCONNECTED ) {
			SCR_UpdateScreen ();	// force an update, because the command
		}							// may take some time
		return;
	}

	// command completion

	if (key == K_TAB) {
		Field_AutoComplete(&g_consoleField);
		return;
	}

	// command history (ctrl-p ctrl-n for unix style)

	if ( (key == K_MWHEELUP && keys[K_SHIFT].down) || ( key == K_UPARROW ) || ( key == K_KP_UPARROW ) ||
		 ( ( tolower(key) == 'p' ) && keys[K_CTRL].down ) ) {
		Con_HistoryGetPrev( &g_consoleField );
		g_consoleField.widthInChars = g_console_field_width;
		return;
	}

	if ( (key == K_MWHEELDOWN && keys[K_SHIFT].down) || ( key == K_DOWNARROW ) || ( key == K_KP_DOWNARROW ) ||
		 ( ( tolower(key) == 'n' ) && keys[K_CTRL].down ) ) {
		Con_HistoryGetNext( &g_consoleField );
		g_consoleField.widthInChars = g_console_field_width;
		return;
	}

	// console scrolling
	if ( key == K_PGUP || key == K_MWHEELUP ) {
		if ( keys[K_CTRL].down ) {	// hold <ctrl> to accelerate scrolling
			Con_PageUp( 0 );		// by one visible page
		} else {
			Con_PageUp( 1 );
		}
		return;
	}

	if ( key == K_PGDN || key == K_MWHEELDOWN ) {
		if ( keys[K_CTRL].down ) {	// hold <ctrl> to accelerate scrolling
			Con_PageDown( 0 );		// by one visible page
		} else {
			Con_PageDown( 1 );
		}
		return;
	}

	// ctrl-home = top of console
	if ( key == K_HOME && keys[K_CTRL].down ) {
		Con_Top();
		return;
	}

	// ctrl-end = bottom of console
	if ( key == K_END && keys[K_CTRL].down ) {
		Con_Bottom();
		return;
	}

	// pass to the normal editline routine
	Field_KeyDownEvent( &g_consoleField, key );
}

//============================================================================


/*
================
Message_Key

In game talk message
================
*/
static void Message_Key( int key ) {

	char	buffer[MAX_STRING_CHARS];

	if (key == K_ESCAPE) {
		Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_MESSAGE );
		Field_Clear( &chatField );
		return;
	}

	if ( key == K_ENTER || key == K_KP_ENTER )
	{
		if ( chatField.buffer[0] && cls.state == CA_ACTIVE ) {
			if (chat_playerNum != -1 )

				Com_sprintf( buffer, sizeof( buffer ), "tell %i \"%s\"\n", chat_playerNum, chatField.buffer );

			else if (chat_team)

				Com_sprintf( buffer, sizeof( buffer ), "say_team \"%s\"\n", chatField.buffer );
			else
				Com_sprintf( buffer, sizeof( buffer ), "say \"%s\"\n", chatField.buffer );

			CL_AddReliableCommand( buffer, qfalse );
		}
		Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_MESSAGE );
		Field_Clear( &chatField );
		return;
	}

	Field_KeyDownEvent( &chatField, key );
}

//============================================================================


/*
===================
CL_KeyDownEvent

Called by CL_KeyEvent to handle a keypress
===================
*/
static void CL_KeyDownEvent( int key, unsigned time )
{
	keys[key].down = qtrue;
	keys[key].repeats++;

	if ( keys[key].repeats == 1 )
		anykeydown++;

#ifndef _WIN32
	if( keys[K_ALT].down && key == K_ENTER )
			{
				Cvar_SetValue( "r_fullscreen",
						!Cvar_VariableIntegerValue( "r_fullscreen" ) );
		Cbuf_ExecuteText( EXEC_APPEND, "vid_restart\n" );
				return;
			}
#endif

	// console key is hardcoded, so the user can never unbind it
	if( key == K_CONSOLE || ( keys[K_SHIFT].down && key == K_ESCAPE ) )
	{
		Con_ToggleConsole_f();
		Key_ClearStates();
		return;
	}

	// hardcoded screenshot key
	if ( key == K_PRINT ) {
		if ( keys[K_SHIFT].down ) {
			Cbuf_ExecuteText( EXEC_APPEND, "screenshotBMP\n" );
		} else {
			Cbuf_ExecuteText( EXEC_APPEND, "screenshotBMP clipboard\n" );
		}
		return;
	}

	// keys can still be used for bound actions
	if ( ( key < 128 || key == K_MOUSE1 ) 
		&& cls.state == CA_CINEMATIC && Key_GetCatcher( ) == 0 ) {

		if ( Cvar_VariableIntegerValue( "com_cameraMode" ) == 0 ) {
			Cvar_Set ("nextdemo","");
			key = K_ESCAPE;
		}
	}

	// escape is always handled special
	if ( key == K_ESCAPE ) {
#ifdef USE_CURL
		if ( Com_DL_InProgress( &download ) && download.mapAutoDownload ) {
			Com_DL_Cleanup( &download );
		}
#endif
		if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) {
			// escape always closes console
			Con_ToggleConsole_f();
			Key_ClearStates();
		}

		if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE ) {
			// clear message mode
			Message_Key( key );
			return;
		}

		// escape always gets out of CGAME stuff
		if (Key_GetCatcher( ) & KEYCATCH_CGAME) {
			Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );
			VM_Call (cgvm, CG_EVENT_HANDLING, CGAME_EVENT_NONE);
			return;
		}

		if ( !( Key_GetCatcher( ) & KEYCATCH_UI ) ) {
			if ( cls.state == CA_ACTIVE && !clc.demoplaying ) {
				VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_INGAME );
			}
			else if ( cls.state != CA_DISCONNECTED ) {
#if 0
				CL_Disconnect_f();
				S_StopAllSounds();
#else
				Cmd_Clear();
				Cvar_Set( "com_errorMessage", "" );
				if ( cls.state == CA_CINEMATIC ) {
					SCR_StopCinematic();
				} else if ( !CL_Disconnect( qfalse ) ) { // restart client if not done already
					CL_FlushMemory();
				}
#endif
				VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
			}
			return;
		}

		VM_Call( uivm, UI_KEY_EVENT, key, qtrue );
		return;
	}


	// distribute the key down event to the apropriate handler
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) {
		Console_Key( key );
	} else if ( Key_GetCatcher( ) & KEYCATCH_UI ) {
		if ( uivm ) {
			VM_Call( uivm, UI_KEY_EVENT, key, qtrue );
		} 
	} else if ( Key_GetCatcher( ) & KEYCATCH_CGAME ) {
		if ( cgvm ) {
			VM_Call( cgvm, CG_KEY_EVENT, key, qtrue );
		} 
	} else if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE ) {
		Message_Key( key );
	} else if ( cls.state == CA_DISCONNECTED ) {
		Console_Key( key );
	} else {
		// send the bound action
		Key_ParseBinding( key, qtrue, time );
	}
}


/*
===================
CL_KeyUpEvent

Called by CL_KeyEvent to handle a keyrelease
===================
*/
static void CL_KeyUpEvent( int key, unsigned time )
{
	keys[key].repeats = 0;
	keys[key].down = qfalse;
	anykeydown--;

	if ( anykeydown < 0 )
		anykeydown = 0;

	// don't process key-up events for the console key
	if ( key == K_CONSOLE || ( key == K_ESCAPE && keys[K_SHIFT].down ) )
		return;

	// hardcoded screenshot key
	if ( key == K_PRINT )
		return;

	//
	// key up events only perform actions if the game key binding is
	// a button command (leading + sign).  These will be processed even in
	// console mode and menu mode, to keep the character from continuing
	// an action started before a mode switch.
	//
	if( cls.state != CA_DISCONNECTED )
		Key_ParseBinding( key, qfalse, time );

	if ( Key_GetCatcher( ) & KEYCATCH_UI && uivm ) {
		VM_Call( uivm, UI_KEY_EVENT, key, qfalse );
	} else if ( Key_GetCatcher( ) & KEYCATCH_CGAME && cgvm ) {
		VM_Call( cgvm, CG_KEY_EVENT, key, qfalse );
	}
}


/*
===================
CL_KeyEvent

Called by the system for both key up and key down events
===================
*/
void CL_KeyEvent( int key, qboolean down, unsigned time )
{
	if ( down )
		CL_KeyDownEvent( key, time );
	else
		CL_KeyUpEvent( key, time );
}


/*
===================
CL_CharEvent

Normal keyboard characters, already shifted / capslocked / etc
===================
*/
void CL_CharEvent( int key )
{
	// delete is not a printable character and is
	// otherwise handled by Field_KeyDownEvent
	if ( key == 127 )
		return;

	// distribute the key down event to the apropriate handler
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
	{
		Field_CharEvent( &g_consoleField, key );
	}
	else if ( Key_GetCatcher( ) & KEYCATCH_UI )
	{
		VM_Call( uivm, UI_KEY_EVENT, key | K_CHAR_FLAG, qtrue );
	}
	else if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE ) 
	{
		Field_CharEvent( &chatField, key );
	}
	else if ( cls.state == CA_DISCONNECTED )
	{
		Field_CharEvent( &g_consoleField, key );
	}
}


/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates( void )
{
	int		i;

	anykeydown = 0;

	for ( i = 0 ; i < MAX_KEYS ; i++ )
	{
		if ( keys[i].down )
			CL_KeyEvent( i, qfalse, 0 );

		keys[i].down = qfalse;
		keys[i].repeats = 0;
	}
}


static int keyCatchers = 0;

/*
====================
Key_GetCatcher
====================
*/
int Key_GetCatcher( void )
{
	return keyCatchers;
}


/*
====================
Key_SetCatcher
====================
*/
void Key_SetCatcher( int catcher )
{
	// If the catcher state is changing, clear all key states
	if ( catcher != keyCatchers )
		Key_ClearStates();

	keyCatchers = catcher;
}
