#include <windows.h>
#include "../client/client.h"
#include "win_local.h"

struct hk_token 
{
	const unsigned int vk;
	const char *name;
} ;

static struct hk_token tokens[] = 
{
	{ VK_BACK,		"BS"  },
	{ VK_BACK,		"BACK"  },
	{ VK_BACK,		"BACKSPACE"  },
	{ VK_UP,		"UP"  },
	{ VK_ESCAPE,	"ESC" },
	{ VK_ESCAPE,	"ESCAPE" },
	{ VK_TAB,		"TAB" },
	{ VK_CONTROL,	"CTL" },
	{ VK_CONTROL,	"CTRL" },
	{ VK_CONTROL,	"CONTROL" },
	{ VK_LCONTROL,	"LCTL" },
	{ VK_LCONTROL,	"LCTRL" },
	{ VK_LCONTROL,	"LCONTROL" },
	{ VK_RCONTROL,	"RCTL" },
	{ VK_RCONTROL,	"RCTRL" },
	{ VK_RCONTROL,	"RCONTROL" },
	{ VK_MENU,		"ALT" },
	{ VK_LMENU,		"LALT" },
	{ VK_RMENU,		"RALT" },
	{ VK_SHIFT,		"SHIFT" },
	{ VK_LSHIFT,	"LSHIFT" },
	{ VK_RSHIFT,	"RSHIFT" },
	{ VK_LWIN,		"WIN" },
	{ VK_END,		"END" },
	{ VK_INSERT,	"INS" },
	{ VK_INSERT,	"INSERT" },
	{ VK_DELETE,	"DEL" },
	{ VK_DELETE,	"DELELE" },
	{ VK_ADD,		"ADD" },
	{ VK_ADD,		"PLUS" },
	{ VK_SUBTRACT,	"SUB" },
	{ VK_SUBTRACT,	"SUBTRACT" },
	{ VK_SUBTRACT,	"MINUS" },
	{ VK_DIVIDE,	"DIV" },
	{ VK_DIVIDE,	"DIVIDE" },
	{ VK_MULTIPLY,  "MUL" },
	{ VK_MULTIPLY,  "MULTIPLY" },
//	{ VK_MULTIPLY,  "STAR" },
	{ VK_NUMLOCK,	"NUM" },
	{ VK_NUMLOCK,	"NUMLOCK" },
	{ VK_NUMLOCK,	"NUM_LOCK" },
	{ VK_CLEAR,		"DOT" },
	{ VK_CLEAR,		"CLEAR" },
	{ VK_CAPITAL,	"CAPS" },
	{ VK_CAPITAL,	"CAPITAL" },
	{ VK_CAPITAL,	"CAPSLOCK" },
	{ VK_CAPITAL,	"CAPS_LOCK" },
	{ VK_HOME,		"HOME" },
	{ VK_APPS,		"MENU" },
	{ VK_PRIOR,	    "PGUP" },
	{ VK_PRIOR,	    "PAGEUP" },
	{ VK_PRIOR,	    "PAGE_UP" },
	{ VK_NEXT,		"PGDN" },
	{ VK_NEXT,		"PAGEDOWN" },
	{ VK_NEXT,		"PAGE_DOWN" },
	{ VK_LEFT,		"LEFT" },
	{ VK_RIGHT,		"RIGHT" },
	{ VK_DOWN,		"DOWN" },
	{ VK_LWIN|HK_MOD_LWIN, "LWIN" },
	{ VK_RWIN|HK_MOD_RWIN, "RWIN" },
	{ VK_SPACE,		"SPACE" },
	{ VK_PAUSE,		"PAUSE" },
//	{ VK_CANCEL,	"BREAK" },
	{ VK_RETURN,	"ENTER" },
	{ VK_RETURN,	"RETURN" },
	{ VK_PRINT,		"PRINT" },
	{ VK_PRINT,		"SYSRQ" },
	{ VK_SCROLL,	"SCROLL" },
	{ VK_OEM_MINUS, "-" },
	{ VK_OEM_MINUS, "_" },
	{ VK_OEM_PLUS,  "=" },
	{ VK_OEM_5,		"\\" },
	{ VK_OEM_5,		"|" },
	{ VK_OEM_4,		"[" },
	{ VK_OEM_4,		"{" },
	{ VK_OEM_6,		"]" },
	{ VK_OEM_6,		"}" },
	{ VK_OEM_1,		";" },
	{ VK_OEM_1,		":" },
	{ VK_OEM_7,		"'" },
	{ VK_OEM_7,		"\"" },
	{ VK_OEM_COMMA, "," },
	{ VK_OEM_COMMA, "<" },
	{ VK_OEM_PERIOD, "." },
	{ VK_OEM_PERIOD, ">" },
	{ VK_OEM_2,		"?" },
	{ VK_OEM_2,		"/" },
	{ VK_OEM_3,		"`" },
	{ VK_OEM_3,		"~" },
	{ '1',			"!" },
	{ '2',			"@" },
	{ '3',			"#" },
	{ '4',			"$" },
	{ '5',			"%" },
	{ '6',			"^" },
	{ '7',			"&" },
	{ '8',			"*" },
	{ '9',			"(" },
	{ '0',			")" },
	{ VK_F1,		"F1" },
	{ VK_F2,		"F2" },
	{ VK_F3,		"F3" },
	{ VK_F4,		"F4" },
	{ VK_F5,		"F5" },
	{ VK_F6,		"F6" },
	{ VK_F7,		"F7" },
	{ VK_F8,		"F8" },
	{ VK_F9,		"F9" },
	{ VK_F10,		"F10" },
	{ VK_F11,		"F11" },
	{ VK_F12,		"F12" },
	{ VK_F13,		"F13" },
	{ VK_F14,		"F14" },
	{ VK_F15,		"F15" },
	{ VK_F16,		"F16" },
	{ VK_F17,		"F17" },
	{ VK_F18,		"F18" },
	{ VK_F19,		"F19" },
	{ VK_NUMPAD0,	"KP_0" },
	{ VK_NUMPAD1,	"KP_1" },
	{ VK_NUMPAD2,	"KP_2" },
	{ VK_NUMPAD3,	"KP_3" },
	{ VK_NUMPAD4,	"KP_4" },
	{ VK_NUMPAD5,	"KP_5" },
	{ VK_NUMPAD6,	"KP_6" },
	{ VK_NUMPAD7,	"KP_7" },
	{ VK_NUMPAD8,   "KP_8" },
	{ VK_NUMPAD9,	"KP_9" },
	{ 0,			"" }
};

int Win32_GetKey( char **s, char *buf, int buflen ) 
{

	char	*base, *end;
    int		len, i;

	base = *s;

	// skip leading separators
	while ( *base == ' ' || *base == '\t' || *base == '+' ) 
		base++;

	// empty token
	if ( !*base )
		return 0;

	end = base;

    while( *end != '\0' && *end != ' '&& *end != '\t' && *end != '+' ) end++;

    len = end - base;

	if ( !len )
		return 0;

	*s = base + len;

	if ( len > buflen-1 )
		len = buflen-1;
	
	strncpy( buf, base, len );
	buf[len]= '\0';
	
	for ( i = 0; i < len ; i++ ) //uppercase
		if ( buf[i] >= 'a' && buf[i] <= 'z' )
			buf[i] += 'A'-'a';

	if ( len == 1 ) {
		if ( buf[0] >='A' && buf[0] <= 'Z' )
			return buf[0];
		if ( buf[0] >='0' && buf[0] <= '9' )
			return buf[0];
	}

	for ( i = 0; i < ARRAY_LEN( tokens ) - 1; i++ )
          if ( !strcmp( buf, tokens[i].name ) )
            return tokens[i].vk;

	return -1;
}
