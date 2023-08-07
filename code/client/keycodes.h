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
//
#ifndef __KEYCODES_H__
#define __KEYCODES_H__

//
// these are the key numbers that should be passed to KeyEvent
//

// normal keys should be passed as lowercased ascii

typedef enum {
	K_TAB = 9,
	K_ENTER = 13,
	K_ESCAPE = 27,
	K_SPACE = 32,

	K_QUOTE = '\'',
	K_PLUS = '+',
	K_COMMA = ',',
	K_MINUS = '-',
	K_DOT = '.',
	K_SLASH = '/',
	K_SEMICOLON = ';',
	K_EQUAL = '=',
	K_BACKSLASH = '\\',
	K_UNDERSCORE = '_',
	K_BRACKET_OPEN = '[',
	K_BRACKET_CLOSE = ']',

	K_A = 'a',
	K_B = 'b',
	K_C = 'c',
	K_D = 'd',
	K_E = 'e',
	K_F = 'f',
	K_G = 'g',
	K_H = 'h',
	K_I = 'i',
	K_J = 'j',
	K_K = 'k',
	K_L = 'l',
	K_M = 'm',
	K_N = 'n',
	K_O = 'o',
	K_P = 'p',
	K_Q = 'q',
	K_R = 'r',
	K_S = 's',
	K_T = 't',
	K_U = 'u',
	K_V = 'v',
	K_W = 'w',
	K_X = 'x',
	K_Y = 'y',
	K_Z = 'z',

	// following definitions must not be changed
	K_BACKSPACE = 127,

	K_COMMAND = 128,
	K_CAPSLOCK,
	K_POWER,
	K_PAUSE,

	K_UPARROW,
	K_DOWNARROW,
	K_LEFTARROW,
	K_RIGHTARROW,

	K_ALT,
	K_CTRL,
	K_SHIFT,
	K_INS,
	K_DEL,
	K_PGDN,
	K_PGUP,
	K_HOME,
	K_END,

	K_F1,
	K_F2,
	K_F3,
	K_F4,
	K_F5,
	K_F6,
	K_F7,
	K_F8,
	K_F9,
	K_F10,
	K_F11,
	K_F12,
	K_F13,
	K_F14,
	K_F15,

	K_KP_HOME,
	K_KP_UPARROW,
	K_KP_PGUP,
	K_KP_LEFTARROW,
	K_KP_5,
	K_KP_RIGHTARROW,
	K_KP_END,
	K_KP_DOWNARROW,
	K_KP_PGDN,
	K_KP_ENTER,
	K_KP_INS,
	K_KP_DEL,
	K_KP_SLASH,
	K_KP_MINUS,
	K_KP_PLUS,
	K_KP_NUMLOCK,
	K_KP_STAR,
	K_KP_EQUALS,

	K_MOUSE1,
	K_MOUSE2,
	K_MOUSE3,
	K_MOUSE4,
	K_MOUSE5,

	K_MWHEELDOWN,
	K_MWHEELUP,

	K_JOY1,
	K_JOY2,
	K_JOY3,
	K_JOY4,
	K_JOY5,
	K_JOY6,
	K_JOY7,
	K_JOY8,
	K_JOY9,
	K_JOY10,
	K_JOY11,
	K_JOY12,
	K_JOY13,
	K_JOY14,
	K_JOY15,
	K_JOY16,
	K_JOY17,
	K_JOY18,
	K_JOY19,
	K_JOY20,
	K_JOY21,
	K_JOY22,
	K_JOY23,
	K_JOY24,
	K_JOY25,
	K_JOY26,
	K_JOY27,
	K_JOY28,
	K_JOY29,
	K_JOY30,
	K_JOY31,
	K_JOY32,

	K_AUX1,
	K_AUX2,
	K_AUX3,
	K_AUX4,
	K_AUX5,
	K_AUX6,
	K_AUX7,
	K_AUX8,
	K_AUX9,
	K_AUX10,
	K_AUX11,
	K_AUX12,
	K_AUX13,
	K_AUX14,
	K_AUX15,
	K_AUX16,
	// end of compatibility list

	K_SUPER,
	K_COMPOSE,
	K_MODE,
	K_HELP,
	K_PRINT,
	K_SYSREQ,
	K_SCROLLOCK,
	K_BREAK,
	K_MENU,
	K_EURO,
	K_UNDO,

	// Gamepad controls
	// Ordered to match SDL2 game controller buttons and axes
	// Do not change this order without also changing IN_GamepadMove() in SDL_input.c
	K_PAD0_A,
	K_PAD0_B,
	K_PAD0_X,
	K_PAD0_Y,
	K_PAD0_BACK,
	K_PAD0_GUIDE,
	K_PAD0_START,
	K_PAD0_LEFTSTICK_CLICK,
	K_PAD0_RIGHTSTICK_CLICK,
	K_PAD0_LEFTSHOULDER,
	K_PAD0_RIGHTSHOULDER,
	K_PAD0_DPAD_UP,
	K_PAD0_DPAD_DOWN,
	K_PAD0_DPAD_LEFT,
	K_PAD0_DPAD_RIGHT,

	K_PAD0_LEFTSTICK_LEFT,
	K_PAD0_LEFTSTICK_RIGHT,
	K_PAD0_LEFTSTICK_UP,
	K_PAD0_LEFTSTICK_DOWN,
	K_PAD0_RIGHTSTICK_LEFT,
	K_PAD0_RIGHTSTICK_RIGHT,
	K_PAD0_RIGHTSTICK_UP,
	K_PAD0_RIGHTSTICK_DOWN,
	K_PAD0_LEFTTRIGGER,
	K_PAD0_RIGHTTRIGGER,

	K_PAD0_MISC1,    /* Xbox Series X share button, PS5 microphone button, Nintendo Switch Pro capture button, Amazon Luna microphone button */
	K_PAD0_PADDLE1,  /* Xbox Elite paddle P1 */
	K_PAD0_PADDLE2,  /* Xbox Elite paddle P3 */
	K_PAD0_PADDLE3,  /* Xbox Elite paddle P2 */
	K_PAD0_PADDLE4,  /* Xbox Elite paddle P4 */
	K_PAD0_TOUCHPAD, /* PS4/PS5 touchpad button */

	// Pseudo-key that brings the console down
	K_CONSOLE,

	MAX_KEYS
} keyNum_t;

// MAX_KEYS replaces K_LAST_KEY, however some mods may have used K_LAST_KEY
// in detecting binds, so we leave it defined to the old hardcoded value
// of maxiumum keys to prevent mods from crashing older versions of the engine
#define K_LAST_KEY              256

// The menu code needs to get both key and char events, but
// to avoid duplicating the paths, the char events are just
// distinguished by or'ing in K_CHAR_FLAG (ugly)
#define	K_CHAR_FLAG		1024

#endif
