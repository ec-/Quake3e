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

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#include "sys_local.h"

#ifndef DEDICATED
#include "../client/client.h"
#include "../client/snd_local.h"
#endif

#undef PATH_MAX
#define PATH_MAX 1024

#ifndef Q_EXPORT
#define Q_EXPORT __attribute__((visibility("default")))
#endif

__attribute__((visibility("default")))
qboolean first_click = qtrue;




extern void IN_Shutdown( void );
extern void IN_Init( void );
extern void IN_Frame( void );
Q_EXPORT int errno;
pthread_t _tp;
pthread_t *__get_tp(void) { return &_tp; }
int *___errno_location(void) { return &errno; }
long __syscall3(long n, long a1, long a2, long a3) { DebugBreak(); return 0; }
long __syscall4(long n, long a1, long a2, long a3, long a4) { DebugBreak(); return 0; }


// =======================================================================
// General routines
// =======================================================================

// some things are easier to leave here instead of in javscript?

qboolean Sys_LowPhysicalMemory( void ) { return qfalse; }
void Sys_BeginProfiling( void ) { }
const char *Sys_SteamPath( void ) { return ""; }
void Sys_SendKeyEvents( void ) { } // moved to push in sys_in.js
void Sys_ShowConsole( int visLevel, qboolean quitOnClose ) { }
const char *Sys_DefaultBasePath( void ) { return "/base"; }
qboolean Sys_ResetReadOnlyAttribute( const char *ospath ) { return qfalse; }
const char *Sys_DefaultHomePath( void ) { return "/base/home"; }

/*
=================
Sys_FreeFileList
=================
*/
void Sys_FreeFileList( char **list ) {
	int		i;

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}


// =============================================================
// tty console routines
// NOTE: if the user is editing a line when something gets printed to the early console then it won't look good
//   so we provide tty_Clear and tty_Show to be called before and after a stdout or stderr output
// =============================================================
void Sys_Exit( int code ) __attribute((noreturn));
void *try_dlopen( const char* base, const char* gamedir, const char* fname );
char *dlerror( void );
void *dlsym( void *handle, char *symbol );
void *dlclose( void *handle );

const char *Sys_Pwd( void ) { return "/base"; }

void Sys_Quit( void )
{
#ifndef DEDICATED
	CL_Shutdown( "", qtrue );
#endif

	Sys_Exit( 0 );
}


void Sys_Init( void )
{

#ifndef DEDICATED
	// TODO: 
	//Cmd_AddCommand( "in_restart", Sys_In_Restart_f );
	//Cmd_SetDescription( "in_restart", "Restart all the input drivers, dinput, joystick, etc\nUsage: in_restart" );
#endif

	Cvar_Set( "arch", OS_STRING " " ARCH_STRING );
}



#undef stdout
#define stdout 0

void Sys_PrintBinVersion( const char* name )
{
	const char *date = __DATE__;
	const char *time = __TIME__;
	const char *sep = "==============================================================";

	fprintf( stdout, "\n\n%s\n", sep );
#ifdef DEDICATED
	fprintf( stdout, "WAST Quake3 Dedicated Server [%s %s]\n", date, time );
#else
	fprintf( stdout, "WAST Quake3 Full Executable  [%s %s]\n", date, time );
#endif
	fprintf( stdout, " local install: %s\n", name );
	fprintf( stdout, "%s\n\n", sep );
}


const char *Sys_BinName( const char *arg0 )
{
	static char   dst[ PATH_MAX ];

	Q_strncpyz( dst, arg0, PATH_MAX );

	return dst;
}


int Sys_ParseArgs( int argc, char* argv[] )
{
	if ( argc == 2 )
	{
		if ( ( !strcmp( argv[1], "--version" ) ) || ( !strcmp( argv[1], "-v" ) ) )
		{
			Sys_PrintBinVersion( Sys_BinName( argv[0] ) );
			return 1;
		}
	}

	return 0;
}


Q_EXPORT int _start( int argc, char* argv[] )
{
	char con_title[ MAX_CVAR_VALUE_STRING ];
	int xpos, ypos;
	//qboolean useXYpos;
	char  *cmdline;
	int   len, i;

	if(!argc) {
		Sys_Error("No startup options specified.");
		return 1;
	}

	if ( Sys_ParseArgs( argc, argv ) ) // added this for support
		return 0;

	// merge the command line, this is kinda silly
	for ( len = 1, i = 1; i < argc; i++ ) {
		len += strlen( argv[i] ) + 1;
	}

	cmdline = malloc( len );
	*cmdline = '\0';
	for ( i = 1; i < argc; i++ )
	{
		if ( i > 1 )
			strcat( cmdline, " " );
		strcat( cmdline, argv[i] );
	}

	/*useXYpos = */ Com_EarlyParseCmdLine( cmdline, con_title, sizeof( con_title ), &xpos, &ypos );

	// get the initial time base
	Sys_Milliseconds();

	Sys_SetStatus("Starting up...\n");

	Com_Init( cmdline );
	NET_Init();

	// JavaScript console doesn't report input
  Cvar_Set( "ttycon", "0" );

  //Browser.requestAnimationFrame(_Sys_Frame);
  //var timeUntilNextTick = Math.max(0, Browser.mainLoop.tickStartTime + value - Sys_Milliseconds)|0;

	//emscripten_set_main_loop(Sys_Frame, 160, 0);
	return 0;
}


/*
#define VA_ARGS(numargs, pointer) \
intptr_t	args[numargs]; \
va_list	ap; \
va_start( ap, pointer ); \
for (int i = 0; i < ARRAY_LEN( args ); i++ ) \
  args[ i ] = va_arg( ap, intptr_t ); \
va_end( ap );
*/


typedef enum {
RSERR_OK,
RSERR_INVALID_FULLSCREEN,
RSERR_INVALID_MODE,
RSERR_FATAL_ERROR,
RSERR_UNKNOWN
} rserr_t;

cvar_t *r_stereoEnabled;
cvar_t *in_nograb;
extern cvar_t *r_mode;
extern cvar_t *r_modeFullscreen;
extern cvar_t *r_fullscreen;
extern cvar_t *cl_drawBuffer;

void *eglGetProcAddress(const char *procname) {
	DebugBreak();
	return NULL;
}

void emscripten_sleep(unsigned int ms) {
	DebugBreak();
}

/*
===============
GLimp_Shutdown
===============
*/




/*
===============
GLimp_LogComment
===============
*/
void GLimp_LogComment( char *comment )
{
}

extern void  GL_GetDrawableSize(int *w, int *h);
rserr_t GLimp_StartDriverAndSetMode(int mode, const char *modeFS, qboolean fullscreen, qboolean fallback);
void GLimp_Shutdown( qboolean unloadDLL );


#ifndef DEDICATED
/*
===============
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL
===============
*/
void GLimp_Init( glconfig_t *config )
{
  rserr_t err;

#if 0
  InitSig();
#endif

  Com_DPrintf( "GLimp_Init()\n" );

  // Create the window and set up the context
  err = GLimp_StartDriverAndSetMode( r_mode->integer, r_modeFullscreen->string, r_fullscreen->integer, qfalse );
  if ( err != RSERR_OK )
  {
    if ( err == RSERR_FATAL_ERROR )
    {
      Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
      return;
    }

    // instead of changing r_mode, which is just the default resolution,
    //   we try a different opengl version
    Com_Printf( "Setting \\r_mode %d failed, falling back on \\r_mode %d\n", r_mode->integer, 3 );
    if ( GLimp_StartDriverAndSetMode( r_mode->integer, "", r_fullscreen->integer, qtrue ) != RSERR_OK )
    {
      // Nothing worked, give up
      Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
      return;
    }
  }

  GL_GetDrawableSize( &config->vidWidth, &config->vidHeight );
	config->colorBits = 24;
	config->depthBits = 24;
	config->stencilBits = 8;
	config->isFullscreen = qfalse;

  // These values force the UI to disable driver selection
  config->driverType = GLDRV_ICD;
  config->hardwareType = GLHW_GENERIC;

  Key_ClearStates();

  // This depends on SDL_INIT_VIDEO, hence having it here
  IN_Init();
}


/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame( void )
{
  // don't flip if drawing to front buffer
  if ( Q_stricmp( cl_drawBuffer->string, "GL_FRONT" ) != 0 )
  {
    //SDL_GL_SwapWindow( SDL_window );
  }
}


void GLimp_InitGamma( glconfig_t *config )
{
}


void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
}

#endif


/*
===============
Sys_SetClipboardBitmap
===============
*/
void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{

}


/*


void IN_PushDropEvent(SDL_DropEvent e)
{
	char file[MAX_OSPATH];
	if(e.type == SDL_DROPBEGIN) {
		// TODO: show the full console
		if(!(Key_GetCatcher() & KEYCATCH_CONSOLE))
			Key_SetCatcher( Key_GetCatcher() | KEYCATCH_CONSOLE );

		Com_Printf("Dropping files:\n");
	}
	if(e.type == SDL_DROPFILE) {
		// show the contents of the dropped file and offer to load something
		Com_Printf("Opening file: %s\n", e.file);
		Q_strncpyz(file, e.file, MAX_OSPATH);
	}
	if(e.type == SDL_DROPCOMPLETE) {
		Con_ClearNotify();
		memcpy(&g_consoleField.buffer, "", sizeof(g_consoleField.buffer));
		Field_AutoComplete( &g_consoleField );
		g_consoleField.cursor = strlen(g_consoleField.buffer);
	}
}
*/
