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
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <libgen.h> // dirname
#ifdef __linux__ // rb010123
  #include <mntent.h>
#endif

#include <dlfcn.h>

#ifdef __linux__
  #include <fpu_control.h> // bk001213 - force dumps on divide by zero
#endif

#if defined(__sun)
  #include <sys/file.h>
#endif

// FIXME TTimo should we gard this? most *nix system should comply?
#include <termios.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"

#include "linux_local.h" // bk001204

#ifndef DEDICATED
#include "../client/client.h"
#endif

unsigned sys_frame_time;

qboolean stdin_active = qfalse;
int      stdin_flags = 0;

// =============================================================
// tty console variables
// =============================================================

typedef enum {
	TTY_ENABLED,
	TTY_DISABLED,
	TTY_ERROR
} tty_err;

// enable/disabled tty input mode
// NOTE TTimo this is used during startup, cannot be changed during run
static cvar_t *ttycon = NULL;

// general flag to tell about tty console mode
static qboolean ttycon_on = qfalse;

// when printing general stuff to stdout stderr (Sys_Printf)
//   we need to disable the tty console stuff
// this increments so we can recursively disable
static int ttycon_hide = 0;

// some key codes that the terminal may be using
// TTimo NOTE: I'm not sure how relevant this is
static int tty_erase;
static int tty_eof;

static struct termios tty_tc;

static field_t tty_con;

static cvar_t *ttycon_ansicolor = NULL;
static qboolean ttycon_color_on = qfalse;

tty_err Sys_ConsoleInputInit( void );

// =======================================================================
// General routines
// =======================================================================

// bk001207 
#define MEM_THRESHOLD 96*1024*1024

/*
==================
Sys_LowPhysicalMemory()
==================
*/
qboolean Sys_LowPhysicalMemory( void )
{
	//MEMORYSTATUS stat;
	//GlobalMemoryStatus (&stat);
	//return (stat.dwTotalPhys <= MEM_THRESHOLD) ? qtrue : qfalse;
	return qfalse; // bk001207 - FIXME
}


void Sys_BeginProfiling( void )
{

}


/*
=================
Sys_In_Restart_f

Restart the input subsystem
=================
*/
#ifndef DEDICATED
void Sys_In_Restart_f( void )
{
	IN_Shutdown();
	IN_Init();
}
#endif


// =============================================================
// tty console routines
// NOTE: if the user is editing a line when something gets printed to the early console then it won't look good
//   so we provide tty_Clear and tty_Show to be called before and after a stdout or stderr output
// =============================================================

// flush stdin, I suspect some terminals are sending a LOT of shit
// FIXME TTimo relevant?
static void tty_FlushIn( void )
{
#if 1
	tcflush( STDIN_FILENO, TCIFLUSH );
#else
	char key;
	while ( read( STDIN_FILENO, &key, 1 ) > 0 );
#endif
}


// do a backspace
// TTimo NOTE: it seems on some terminals just sending '\b' is not enough
//   so for now, in any case we send "\b \b" .. yeah well ..
//   (there may be a way to find out if '\b' alone would work though)
static void tty_Back( void )
{
	write( STDOUT_FILENO, "\b \b", 3 );
}


// clear the display of the line currently edited
// bring cursor back to beginning of line
void tty_Hide( void )
{
	int i;

	if ( !ttycon_on )
		return;

	if ( ttycon_hide )
	{
		ttycon_hide++;
		return;
	}

	if ( tty_con.cursor > 0 )
	{
		for ( i = 0; i < tty_con.cursor; i++ )
		{
			tty_Back();
		}
	}
	tty_Back(); // delete "]" ? -EC-
	ttycon_hide++;
}


// show the current line
// FIXME TTimo need to position the cursor if needed??
void tty_Show( void )
{
	if ( !ttycon_on )
		return;

	if ( ttycon_hide > 0 )
	{
		ttycon_hide--;
		if ( ttycon_hide == 0 )
		{
			write( STDOUT_FILENO, "]", 1 ); // -EC-

			if ( tty_con.cursor > 0 )
			{
				write( STDOUT_FILENO, tty_con.buffer, tty_con.cursor );
			}
		}
	}
}


// never exit without calling this, or your terminal will be left in a pretty bad state
void Sys_ConsoleInputShutdown( void )
{
	if ( ttycon_on )
	{
//		Com_Printf( "Shutdown tty console\n" ); // -EC-
		tty_Back(); // delete "]" ? -EC-
		tcsetattr( STDIN_FILENO, TCSADRAIN, &tty_tc );
	}

	// Restore blocking to stdin reads
	if ( stdin_active )
	{
		fcntl( STDIN_FILENO, F_SETFL, stdin_flags );
//		fcntl( STDIN_FILENO, F_SETFL, fcntl( STDIN_FILENO, F_GETFL, 0 ) & ~O_NONBLOCK );
	}

	Com_Memset( &tty_con, 0, sizeof( tty_con ) );

	stdin_active = qfalse;
	ttycon_on = qfalse;
	
	ttycon_hide = 0;
}

/*
==================
CON_SigCont
Reinitialize console input after receiving SIGCONT, as on Linux the terminal seems to lose all
set attributes if user did CTRL+Z and then does fg again.
==================
*/
void CON_SigCont( int signum )
{
	Sys_ConsoleInputInit();
}


void CON_SigTStp( int signum )
{
	sigset_t mask;
	
	tty_FlushIn();
	Sys_ConsoleInputShutdown();

	sigemptyset( &mask );
	sigaddset( &mask, SIGTSTP );
	sigprocmask( SIG_UNBLOCK, &mask, NULL );
	
	signal( SIGTSTP, SIG_DFL );
	
	kill( getpid(),  SIGTSTP );
}


// =============================================================
// general sys routines
// =============================================================

// single exit point (regular exit or in case of signal fault)
void Sys_Exit( int code ) __attribute((noreturn));
void Sys_Exit( int code )
{
	Sys_ConsoleInputShutdown();

#ifdef NDEBUG // regular behavior
	// We can't do this 
	//  as long as GL DLL's keep installing with atexit...
	//exit(ex);
	_exit( code );
#else
	// Give me a backtrace on error exits.
	assert( code == 0 );
	exit( code );
#endif
}


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
	Cmd_AddCommand( "in_restart", Sys_In_Restart_f );
#endif

	Cvar_Set( "arch", OS_STRING " " ARCH_STRING );

	//IN_Init();   // rcg08312005 moved into glimp.
}


void Sys_Error( const char *format, ... )
{
	va_list     argptr;
	char        text[1024];

	// change stdin to non blocking
	// NOTE TTimo not sure how well that goes with tty console mode
	if ( stdin_active )
	{
//		fcntl( STDIN_FILENO, F_SETFL, fcntl( STDIN_FILENO, F_GETFL, 0) & ~FNDELAY );
		fcntl( STDIN_FILENO, F_SETFL, stdin_flags );
	}

	// don't bother do a show on this one heh
	if ( ttycon_on )
	{
		tty_Hide();
	}

	va_start( argptr, format );
	Q_vsnprintf( text, sizeof( text ), format, argptr );
	va_end( argptr );

#ifndef DEDICATED
	CL_Shutdown( text, qtrue );
#endif

	fprintf( stderr, "Sys_Error: %s\n", text );

	Sys_Exit( 1 ); // bk010104 - use single exit point.
}


void floating_point_exception_handler( int whatever )
{
	signal( SIGFPE, floating_point_exception_handler );
}


// initialize the console input (tty mode if wanted and possible)
// warning: might be called from signal handler
tty_err Sys_ConsoleInputInit( void )
{
	struct termios tc;
	const char* term;

	// TTimo
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=390
	// ttycon 0 or 1, if the process is backgrounded (running non interactively)
	// then SIGTTIN or SIGTOU is emitted, if not catched, turns into a SIGSTP
	signal( SIGTTIN, SIG_IGN );
	signal( SIGTTOU, SIG_IGN );

	// If SIGCONT is received, reinitialize console
	signal( SIGCONT, CON_SigCont );
	
	if ( signal( SIGTSTP, SIG_IGN ) == SIG_DFL )
	{
		signal( SIGTSTP, CON_SigTStp );
	}

	stdin_flags = fcntl( STDIN_FILENO, F_GETFL, 0 );
	if ( stdin_flags == -1 )
	{
		stdin_active = qfalse;
		return TTY_ERROR;
	}

	// set non-blocking mode
	fcntl( STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK );
	stdin_active = qtrue;

	// FIXME TTimo initialize this in Sys_Init or something?
	if ( !ttycon || !ttycon->integer )
	{
		ttycon_on = qfalse;
		return TTY_DISABLED;

	}
	term = getenv( "TERM" );
	if ( isatty( STDIN_FILENO ) != 1 || !term || !strcmp( term, "dumb" ) || !strcmp( term, "raw" ) )
	{
		ttycon_on = qfalse;
		return TTY_ERROR;
	}

	Field_Clear( &tty_con );
	tcgetattr( STDIN_FILENO, &tty_tc );
	tty_erase = tty_tc.c_cc[ VERASE ];
	tty_eof = tty_tc.c_cc[ VEOF ];
	tc = tty_tc;

	/*
		ECHO: don't echo input characters
		ICANON: enable canonical mode.  This  enables  the  special
			characters  EOF,  EOL,  EOL2, ERASE, KILL, REPRINT,
			STATUS, and WERASE, and buffers by lines.
		ISIG: when any of the characters  INTR,  QUIT,  SUSP,  or
			DSUSP are received, generate the corresponding signal
	*/
	tc.c_lflag &= ~(ECHO | ICANON);
	/*
		ISTRIP strip off bit 8
		INPCK enable input parity checking
	*/
	tc.c_iflag &= ~(ISTRIP | INPCK);
	tc.c_cc[VMIN] = 1;
	tc.c_cc[VTIME] = 0;
	tcsetattr( STDIN_FILENO, TCSADRAIN, &tc );

	if ( ttycon_ansicolor && ttycon_ansicolor->integer )
	{
		ttycon_color_on = qtrue;
	}

	ttycon_on = qtrue;

	tty_Hide();
	tty_Show();

	return TTY_ENABLED;
}


char *Sys_ConsoleInput( void )
{
	// we use this when sending back commands
	static char text[ sizeof( tty_con.buffer ) ];
	int avail;
	char key;
	char *s;
	field_t history;

	if ( ttycon_on )
	{
		avail = read( STDIN_FILENO, &key, 1 );
		if (avail != -1)
		{
			// we have something
			// backspace?
			// NOTE TTimo testing a lot of values .. seems it's the only way to get it to work everywhere
			if ((key == tty_erase) || (key == 127) || (key == 8))
			{
				if (tty_con.cursor > 0)
				{
					tty_con.cursor--;
					tty_con.buffer[tty_con.cursor] = '\0';
					tty_Back();
				}
				return NULL;
			}

			// check if this is a control char
			if (key && key < ' ')
			{
				if (key == '\n')
				{
					// push it in history
					Con_SaveField( &tty_con );
					s = tty_con.buffer;
					while ( *s == '\\' || *s == '/' ) // skip leading slashes
						s++;
					Q_strncpyz( text, s, sizeof( text ) );
					Field_Clear( &tty_con );
					write( STDOUT_FILENO, "\n]", 2 );
					return text;
				}

				if (key == '\t')
				{
					tty_Hide();
					Field_AutoComplete( &tty_con );
					tty_Show();
					return NULL;
				}

				avail = read( STDIN_FILENO, &key, 1 );
				if (avail != -1)
				{
					// VT 100 keys
					if (key == '[' || key == 'O')
					{
						avail = read( STDIN_FILENO, &key, 1 );
						if (avail != -1)
						{
							switch (key)
							{
							case 'A':
								if ( Con_HistoryGetPrev( &history ) )
								{
									tty_Hide();
									tty_con = history;
									tty_Show();
								}
								tty_FlushIn();
								return NULL;
								break;
							case 'B':
								if ( Con_HistoryGetNext( &history ) )
								{
									tty_Hide();
									tty_con = history;
									tty_Show();
								}
								tty_FlushIn();
								return NULL;
								break;
							case 'C': // right
							case 'D': // left
							//case 'H': // home
							//case 'F': // end
								return NULL;
							}
						}
					}
				}

				if ( key == 12 ) // clear teaminal
				{
					write( STDOUT_FILENO, "\ec]", 3 );
					if ( tty_con.cursor )
					{
						write( STDOUT_FILENO, tty_con.buffer, tty_con.cursor );
					}
					tty_FlushIn();
					return NULL;
				}

				Com_DPrintf( "dropping ISCTL sequence: %d, tty_erase: %d\n", key, tty_erase );
				tty_FlushIn();
				return NULL;
			}
			if ( tty_con.cursor >= sizeof( text ) - 1 )
				return NULL;
			// push regular character
			tty_con.buffer[ tty_con.cursor ] = key;
			tty_con.cursor++;
			// print the current line (this is differential)
			write( STDOUT_FILENO, &key, 1 );
		}
		return NULL;
	}
	else if ( stdin_active && com_dedicated->integer )
	{
		int len;
		fd_set fdset;
		struct timeval timeout;

		FD_ZERO( &fdset );
		FD_SET( STDIN_FILENO, &fdset ); // stdin
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if ( select( STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET( STDIN_FILENO, &fdset ) )
		{
			return NULL;
		}

		len = read( STDIN_FILENO, text, sizeof( text ) );
		if ( len == 0 ) // eof!
		{ 
			fcntl( STDIN_FILENO, F_SETFL, stdin_flags );
			stdin_active = qfalse;
			return NULL;
		}

		if ( len < 1 )
			return NULL;

		text[len-1] = '\0'; // rip off the /n and terminate
		s = text;

		while ( *s == '\\' || *s == '/' ) // skip leading slashes
			s++;

		return s;
	}

	return NULL;
}


/*
=================
Sys_SendKeyEvents

Platform-dependent event handling
=================
*/
void Sys_SendKeyEvents( void )
{
#ifndef DEDICATED
	HandleX11Events();
#endif
}


/*****************************************************************************/

char *do_dlerror( void )
{
	return dlerror();
}


/*
=================
Sys_UnloadDll
=================
*/
void Sys_UnloadDll( void *dllHandle ) {

	if ( !dllHandle )
	{
		Com_Printf( "Sys_UnloadDll(NULL)\n" );
		return;
	}

	dlclose( dllHandle );
	{
		const char* err; // rb010123 - now const
		err = dlerror();
		if ( err != NULL )
			Com_Printf ( "Sys_UnloadDLL failed on dlclose: \"%s\"!\n", err );
	}
}


/*
==================
Sys_Sleep

Block execution for msec or until input is recieved.
==================
*/
void Sys_Sleep( int msec ) {
	struct timeval timeout;
	fd_set fdset;
	int res;

	if ( msec == 0 )
		return;

	if ( msec < 0 ) {
		// special case: wait for console input or network packet
		if ( stdin_active ) {
			msec = 300;
			do {
				FD_ZERO( &fdset );
				FD_SET( STDIN_FILENO, &fdset );
				timeout.tv_sec = msec / 1000;
				timeout.tv_usec = (msec % 1000) * 1000;
				res = select( STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout );
			} while ( res == 0 && NET_Sleep( 10, 0 ) );
		} else {
			// can happen only if no map loaded
			// which means we totally stuck as stdin is also disabled :P
			//usleep( 300 * 1000 );
			while ( NET_Sleep( 3000, 0 ) )
				;
		}
		return;
	}

	if ( com_dedicated->integer && stdin_active ) {
		FD_ZERO( &fdset );
		FD_SET( STDIN_FILENO, &fdset );
		timeout.tv_sec = msec / 1000;
		timeout.tv_usec = (msec % 1000) * 1000;
		select( STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout );
	} else {
		usleep( msec * 1000 );
	}
}


/*
=================
Sys_LoadDll

Used to load a development dll instead of a virtual machine
TTimo:
changed the load procedure to match VFS logic, and allow developer use
#1 look down current path
#2 look in fs_homepath
#3 look in fs_basepath
=================
*/
static void* try_dlopen( const char* base, const char* gamedir, const char* fname )
{
	void* libHandle;
	char* fn;

	fn = FS_BuildOSPath( base, gamedir, fname );
	Com_Printf( "Sys_LoadDll(%s)... \n", fn );

	libHandle = dlopen( fn, RTLD_NOW );

	if( !libHandle ) 
	{
    	Com_Printf( "Sys_LoadDll(%s) failed:\n\"%s\"\n", fn, do_dlerror() );
		return NULL;
	}

	Com_Printf ( "Sys_LoadDll(%s): succeeded ...\n", fn );

	return libHandle;
}


void *Sys_LoadDll( const char *name, dllSyscall_t *entryPoint, dllSyscall_t systemcalls )
{
	void		*libHandle;
	dllEntry_t	dllEntry;
#ifdef DEBUG
	char		currpath[MAX_OSPATH];
#endif
	char		fname[MAX_OSPATH];
	const char	*basepath;
	const char	*homepath;
	const char	*gamedir;
	const char	*err = NULL;

	assert( name ); // let's have some paranoia

	snprintf( fname, sizeof( fname ), "%s" ARCH_STRING DLL_EXT, name );

	// TODO: use fs_searchpaths from files.c
	basepath = Cvar_VariableString( "fs_basepath" );
	homepath = Cvar_VariableString( "fs_homepath" );
	gamedir = Cvar_VariableString( "fs_game" );
	if ( !*gamedir ) {
		gamedir = Cvar_VariableString( "fs_basegame" );
	}

#ifdef DEBUG
	if ( getcwd( currpath, sizeof( currpath ) ) )
		libHandle = try_dlopen( currpath, gamedir, fname );
	else
#endif
	libHandle = NULL;

	if ( !libHandle && homepath && homepath[0] )
		libHandle = try_dlopen( homepath, gamedir, fname );

	if( !libHandle && basepath && basepath[0] )
		libHandle = try_dlopen( basepath, gamedir, fname );

	if ( !libHandle ) 
	{
		Com_Printf ( "Sys_LoadDll(%s) failed dlopen() completely!\n", name );
		return NULL;
	}

	dllEntry = dlsym( libHandle, "dllEntry" );
	*entryPoint = dlsym( libHandle, "vmMain" );

	if ( !*entryPoint || !dllEntry )
	{
		err = do_dlerror();
#ifndef NDEBUG // bk001206 - in debug abort on failure
		Com_Error ( ERR_FATAL, "Sys_LoadDll(%s) failed dlsym(vmMain):\n\"%s\" !\n", name, err );
#else
		Com_Printf ( "Sys_LoadDll(%s) failed dlsym(vmMain):\n\"%s\" !\n", name, err );
#endif
		dlclose( libHandle );
		err = do_dlerror();
		if ( err != NULL ) 
		{
			Com_Printf( "Sys_LoadDll(%s) failed dlcose:\n\"%s\"\n", name, err );
		}
		return NULL;
	}

	Com_Printf( "Sys_LoadDll(%s) found **vmMain** at %p\n", name, *entryPoint );
	dllEntry( systemcalls );
	Com_Printf( "Sys_LoadDll(%s) succeeded!\n", name );

	return libHandle;
}

/*****************************************************************************/


static struct Q3ToAnsiColorTable_s
{
	const char Q3color;
	const char *ANSIcolor;
} tty_colorTable[ ] =
{
	{ COLOR_BLACK,    "30" },
	{ COLOR_RED,      "31" },
	{ COLOR_GREEN,    "32" },
	{ COLOR_YELLOW,   "33" },
	{ COLOR_BLUE,     "34" },
	{ COLOR_CYAN,     "36" },
	{ COLOR_MAGENTA,  "35" },
	{ COLOR_WHITE,    "0" }
};


void Sys_ANSIColorify( const char *msg, char *buffer, int bufferSize )
{
  int   msgLength;
  int   i, j;
  const char *escapeCode;
  char  tempBuffer[ 7 ];

  if( !msg || !buffer )
    return;

  msgLength = strlen( msg );
  i = 0;
  buffer[ 0 ] = '\0';

  while( i < msgLength )
  {
    if( msg[ i ] == '\n' )
    {
      Com_sprintf( tempBuffer, 7, "%c[0m\n", 0x1B );
      strncat( buffer, tempBuffer, bufferSize - 1);
      i++;
    }
    else if( msg[ i ] == Q_COLOR_ESCAPE )
    {
      i++;

      if( i < msgLength )
      {
        escapeCode = NULL;
        for( j = 0; j < ARRAY_LEN( tty_colorTable ); j++ )
        {
          if( msg[ i ] == tty_colorTable[ j ].Q3color )
          {
            escapeCode = tty_colorTable[ j ].ANSIcolor;
            break;
          }
        }

        if( escapeCode )
        {
          Com_sprintf( tempBuffer, 7, "%c[%sm", 0x1B, escapeCode );
          strncat( buffer, tempBuffer, bufferSize - 1);
        }

        i++;
      }
    }
    else
    {
      Com_sprintf( tempBuffer, 7, "%c", msg[ i++ ] );
      strncat( buffer, tempBuffer, bufferSize - 1);
    }
  }
}


void Sys_Print( const char *msg )
{
	if ( ttycon_on )
	{
		tty_Hide();
	}

	if ( ttycon_on && ttycon_color_on )
	{
		char ansiColorString[ MAXPRINTMSG ];
		Sys_ANSIColorify( msg, ansiColorString, MAXPRINTMSG );
		fputs( ansiColorString, stderr );
	}
	else
		fputs( msg, stderr );

	if ( ttycon_on )
	{
		tty_Show();
	}
}


void QDECL Sys_SetStatus( const char *format, ... )
{
	return;
}


void Sys_ConfigureFPU( void )  // bk001213 - divide by zero
{
#ifdef __linux__
#ifdef __i386
#ifndef NDEBUG
	// bk0101022 - enable FPE's in debug mode
	static int fpu_word = _FPU_DEFAULT & ~(_FPU_MASK_ZM | _FPU_MASK_IM);
	int current = 0;
	_FPU_GETCW( current );
	if ( current!=fpu_word)
	{
#if 0
		Com_Printf("FPU Control 0x%x (was 0x%x)\n", fpu_word, current );
		_FPU_SETCW( fpu_word );
		_FPU_GETCW( current );
		assert(fpu_word==current);
#endif
	}
#else // NDEBUG
	static int fpu_word = _FPU_DEFAULT;
	_FPU_SETCW( fpu_word );
#endif // NDEBUG
#endif // __i386 
#endif // __linux
}


void Sys_PrintBinVersion( const char* name )
{
	const char *date = __DATE__;
	const char *time = __TIME__;
	const char *sep = "==============================================================";

	fprintf( stdout, "\n\n%s\n", sep );
#ifdef DEDICATED
	fprintf( stdout, "Linux Quake3 Dedicated Server [%s %s]\n", date, time );
#else
	fprintf( stdout, "Linux Quake3 Full Executable  [%s %s]\n", date, time );
#endif
	fprintf( stdout, " local install: %s\n", name );
	fprintf( stdout, "%s\n\n", sep );
}


/*
=================
Sys_BinName

This resolves any symlinks to the binary. It's disabled for debug
builds because there are situations where you are likely to want
to symlink to binaries and /not/ have the links resolved.
=================
*/
const char *Sys_BinName( const char *arg0 )
{
	static char   dst[ PATH_MAX ];

#ifdef NDEBUG

#ifdef __linux__
	int n = readlink( "/proc/self/exe", dst, PATH_MAX - 1 );

	if ( n >= 0 && n < PATH_MAX )
		dst[ n ] = '\0';
	else
		Q_strncpyz( dst, arg0, PATH_MAX );
#else
#warning Sys_BinName not implemented
	Q_strncpyz( dst, arg0, PATH_MAX );
#endif

#else
	Q_strncpyz( dst, arg0, PATH_MAX );
#endif

	return dst;
}


int Sys_ParseArgs( int argc, const char* argv[] )
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


int main( int argc, const char* argv[] )
{
	char  *cmdline;
	int   len, i;
	tty_err	err;

	if ( Sys_ParseArgs( argc, argv ) ) // added this for support
		return 0;

	// merge the command line, this is kinda silly
	for ( len = 1, i = 1; i < argc; i++ )
    	len += strlen( argv[i] ) + 1;

	cmdline = malloc( len );
	*cmdline = '\0';
	for ( i = 1; i < argc; i++ )
	{
		if ( i > 1 )
			strcat( cmdline, " " );
		strcat( cmdline, argv[i] );
	}

	// bk000306 - clear queues
//	memset( &eventQue[0], 0, sizeof( eventQue ) );
//	memset( &sys_packetReceived[0], 0, sizeof( sys_packetReceived ) );

	// get the initial time base
	Sys_Milliseconds();

	Com_Init( cmdline );
	NET_Init();

	Com_Printf( "Working directory: %s\n", Sys_Pwd() );

	// Sys_ConsoleInputInit() might be called in signal handler
	// so modify/init any cvars here
	ttycon = Cvar_Get( "ttycon", "1", 0 );
	ttycon_ansicolor = Cvar_Get( "ttycon_ansicolor", "0", CVAR_ARCHIVE );

	err = Sys_ConsoleInputInit();
	if ( err == TTY_ENABLED )
	{
		Com_Printf( "Started tty console (use +set ttycon 0 to disable)\n" );
	}
	else
	{
		if ( err == TTY_ERROR )
		{
			Com_Printf( "stdin is not a tty, tty console mode failed\n" );
			Cvar_Set( "ttycon", "0" );
		}
	}

#ifdef DEDICATED
	// init here for dedicated, as we don't have GLimp_Init
	InitSig();
#endif

	while (1)
	{
#ifdef __linux__
		Sys_ConfigureFPU();
#endif

#ifdef DEDICATED
		// run the game
		Com_Frame( qfalse );
#else
		// check for other input devices
		IN_Frame();
		// run the game
		Com_Frame( CL_NoDelay() );
#endif
	}
	// never gets here
	return 0;
}
