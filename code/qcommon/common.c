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
// common.c -- misc functions used in client and server

#include "q_shared.h"
#include "qcommon.h"
#include <setjmp.h>
#ifndef _WIN32
#include <netinet/in.h>
#include <sys/stat.h> // umask
#include <sys/time.h>
#else
#include <winsock.h>
#endif

#include "../client/keys.h"

const int demo_protocols[] = { 66, 67, PROTOCOL_VERSION, NEW_PROTOCOL_VERSION, 0 };

#define USE_MULTI_SEGMENT // allocate additional zone segments on demand

#define MIN_DEDICATED_COMHUNKMEGS 8
#ifdef DEDICATED
#define MIN_COMHUNKMEGS		48
#define DEF_COMHUNKMEGS		56
#else
#define MIN_COMHUNKMEGS		64
#define DEF_COMHUNKMEGS		128
#endif
#ifdef USE_MULTI_SEGMENT
#define DEF_COMZONEMEGS		9
#else
#define DEF_COMZONEMEGS		25
#endif
#define DEF_COMHUNKMEGS_S	XSTRING(DEF_COMHUNKMEGS)
#define DEF_COMZONEMEGS_S	XSTRING(DEF_COMZONEMEGS)

jmp_buf abortframe;		// an ERR_DROP occured, exit the entire frame

int		CPU_Flags = 0;

FILE *debuglogfile;
static fileHandle_t logfile = FS_INVALID_HANDLE;
static fileHandle_t com_journalFile = FS_INVALID_HANDLE ; // events are written here
fileHandle_t com_journalDataFile = FS_INVALID_HANDLE; // config files are written here

cvar_t	*com_viewlog;
cvar_t	*com_speeds;
cvar_t	*com_developer;
cvar_t	*com_dedicated;
cvar_t	*com_timescale;
cvar_t	*com_fixedtime;
cvar_t	*com_journal;
#ifndef DEDICATED
cvar_t	*com_maxfps;
cvar_t	*com_maxfpsUnfocused;
cvar_t	*com_yieldCPU;
cvar_t	*com_timedemo;
#endif
cvar_t	*com_affinityMask;
cvar_t	*com_logfile;		// 1 = buffer log, 2 = flush after each print
cvar_t	*com_showtrace;
cvar_t	*com_version;
cvar_t	*com_buildScript;	// for automated data building scripts
cvar_t	*com_blood;

#ifndef DEDICATED
cvar_t	*com_introPlayed;
cvar_t	*com_skipIdLogo;

cvar_t	*cl_paused;
cvar_t	*cl_packetdelay;
cvar_t	*com_cl_running;
#endif

cvar_t	*sv_paused;
cvar_t  *sv_packetdelay;
cvar_t	*com_sv_running;

cvar_t	*com_cameraMode;
#if defined(_WIN32) && defined(_DEBUG)
cvar_t	*com_noErrorInterrupt;
#endif

// com_speeds times
int		time_game;
int		time_frontend;		// renderer frontend time
int		time_backend;		// renderer backend time

int			com_frameTime;
int			com_frameMsec;
int			com_frameNumber;

qboolean	com_errorEntered = qfalse;
qboolean	com_fullyInitialized = qfalse;
qboolean	com_gameRestarting = qfalse;

// renderer window states
qboolean	gw_minimized = qfalse; // this will be always true for dedicated servers
#ifndef DEDICATED
qboolean	gw_active = qtrue;
#endif

static char com_errorMessage[ MAXPRINTMSG ];

static void Com_Shutdown( void );
static void Com_WriteConfig_f( void );
void CIN_CloseAllVideos( void );

//============================================================================

static char	*rd_buffer;
static int	rd_buffersize;
static qboolean rd_flushing = qfalse;
static void	(*rd_flush)( const char *buffer );

void Com_BeginRedirect( char *buffer, int buffersize, void (*flush)(const char *) )
{
	if (!buffer || !buffersize || !flush)
		return;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = '\0';
}


void Com_EndRedirect( void )
{
	if ( rd_flush ) {
		rd_flushing = qtrue;
		rd_flush( rd_buffer );
		rd_flushing = qfalse;
	}

	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}


/*
=============
Com_Printf

Both client and server can use this, and it will output
to the apropriate place.

A raw string should NEVER be passed as fmt, because of "%f" type crashers.
=============
*/
void QDECL Com_Printf( const char *fmt, ... ) {
	static qboolean opening_qconsole = qfalse;
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	int			len;

	va_start( argptr, fmt );
	len = Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	if ( rd_buffer && !rd_flushing ) {
		if ( len + strlen( rd_buffer ) > ( rd_buffersize - 1 ) ) {
			rd_flushing = qtrue;
			rd_flush( rd_buffer );
			rd_flushing = qfalse;
			*rd_buffer = '\0';
		}
		Q_strcat( rd_buffer, rd_buffersize, msg );
		// TTimo nooo .. that would defeat the purpose
		//rd_flush(rd_buffer);
		//*rd_buffer = '\0';
		return;
	}

#ifndef DEDICATED
	// echo to client console if we're not a dedicated server
	if ( !com_dedicated || !com_dedicated->integer ) {
		CL_ConsolePrint( msg );
	}
#endif

	// echo to dedicated console and early console
	Sys_Print( msg );

	// logfile
	if ( com_logfile && com_logfile->integer ) {
    // TTimo: only open the qconsole.log if the filesystem is in an initialized state
    //   also, avoid recursing in the qconsole.log opening (i.e. if fs_debug is on)
		if ( logfile == FS_INVALID_HANDLE && FS_Initialized() && !opening_qconsole ) {
			struct tm *newtime;
			time_t aclock;

			opening_qconsole = qtrue;

			time( &aclock );
			newtime = localtime( &aclock );

			logfile = FS_FOpenFileWrite( "qconsole.log" );
			
			if ( logfile != FS_INVALID_HANDLE )
			{
				Com_Printf( "logfile opened on %s\n", asctime( newtime ) );
		
				if ( com_logfile->integer > 1 )
				{
					// force it to not buffer so we get valid
					// data even if we are crashing
					FS_ForceFlush(logfile);
				}
			}
			else
			{
				Com_Printf( "Opening qconsole.log failed!\n" );
				Cvar_Set( "logfile", "0" );
			}

			opening_qconsole = qfalse;
		}
		if ( logfile != FS_INVALID_HANDLE && FS_Initialized() ) {
			FS_Write( msg, len, logfile );
		}
	}
}


/*
================
Com_DPrintf

A Com_Printf that only shows up if the "developer" cvar is set
================
*/
void QDECL Com_DPrintf( const char *fmt, ...) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];
		
	if ( !com_developer || !com_developer->integer ) {
		return;			// don't confuse non-developers with techie stuff...
	}

	va_start( argptr,fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "%s", msg );
}


/*
=============
Com_Error

Both client and server can use this, and it will
do the apropriate things.
=============
*/
void QDECL Com_Error( errorParm_t code, const char *fmt, ... ) {
	va_list		argptr;
	static int	lastErrorTime;
	static int	errorCount;
	static qboolean	calledSysError = qfalse;
	int			currentTime;

#if defined(_WIN32) && defined(_DEBUG)
	if ( code != ERR_DISCONNECT && code != ERR_NEED_CD ) {
		if (!com_noErrorInterrupt->integer) {
			__debugbreak();
		}
	}
#endif

	if(com_errorEntered)
	{
		if(!calledSysError)
        {
			calledSysError = qtrue;
			Sys_Error("recursive error after: %s", com_errorMessage);
		}
	}

	com_errorEntered = qtrue;

	Cvar_Set( "com_errorCode", va( "%i", code ) );

	// when we are running automated scripts, make sure we
	// know if anything failed
	if ( com_buildScript && com_buildScript->integer ) {
		code = ERR_FATAL;
	}

	// if we are getting a solid stream of ERR_DROP, do an ERR_FATAL
	currentTime = Sys_Milliseconds();
	if ( currentTime - lastErrorTime < 100 ) {
		if ( ++errorCount > 3 ) {
			code = ERR_FATAL;
		}
	} else {
		errorCount = 0;
	}
	lastErrorTime = currentTime;

	va_start( argptr, fmt );
	Q_vsnprintf( com_errorMessage, sizeof( com_errorMessage ), fmt, argptr );
	va_end( argptr );

	if ( code != ERR_DISCONNECT && code != ERR_NEED_CD ) {
		// we can't recover from ERR_FATAL so there is no recipients for com_errorMessage
		// also if ERR_FATAL was called from S_Malloc - CopyString for a long (2+ chars) text
		// will trigger recursive error without proper client/server shutdown
		if ( code != ERR_FATAL ) {
			Cvar_Set( "com_errorMessage", com_errorMessage );
		}
	}

	Cbuf_Init();

	if ( code == ERR_DISCONNECT || code == ERR_SERVERDISCONNECT ) {
		VM_Forced_Unload_Start();
		SV_Shutdown( "Server disconnected" );
		Com_EndRedirect();
#ifndef DEDICATED
		CL_Disconnect( qfalse );
		CL_FlushMemory();
#endif
		VM_Forced_Unload_Done();

		// make sure we can get at our local stuff
		FS_PureServerSetLoadedPaks( "", "" );
		com_errorEntered = qfalse;

		longjmp( abortframe, -1 );
	} else if ( code == ERR_DROP ) {
		Com_Printf( "********************\nERROR: %s\n********************\n", 
			com_errorMessage );
		VM_Forced_Unload_Start();
		SV_Shutdown( va( "Server crashed: %s",  com_errorMessage ) );
		Com_EndRedirect();
#ifndef DEDICATED
		CL_Disconnect( qfalse );
		CL_FlushMemory();
#endif
		VM_Forced_Unload_Done();
		
		FS_PureServerSetLoadedPaks( "", "" );
		com_errorEntered = qfalse;

		longjmp( abortframe, -1 );
	} else if ( code == ERR_NEED_CD ) {
		SV_Shutdown( "Server didn't have CD" );
		Com_EndRedirect();
#ifndef DEDICATED
		if ( com_cl_running && com_cl_running->integer ) {
			CL_Disconnect( qfalse );
			VM_Forced_Unload_Start();
			CL_FlushMemory();
			VM_Forced_Unload_Done();
			CL_CDDialog();
		} else {
			Com_Printf( "Server didn't have CD\n" );
		}
#endif
		FS_PureServerSetLoadedPaks( "", "" );
		com_errorEntered = qfalse;

		longjmp( abortframe, -1 );
	} else {
		VM_Forced_Unload_Start();
#ifndef DEDICATED
		CL_Shutdown( va( "Server fatal crashed: %s", com_errorMessage ), qtrue );
#endif
		SV_Shutdown( va( "Server fatal crashed: %s", com_errorMessage ) );
		Com_EndRedirect();
		VM_Forced_Unload_Done();
	}

	Com_Shutdown();

	calledSysError = qtrue;
	Sys_Error( "%s", com_errorMessage );
}


/*
=============
Com_Quit_f

Both client and server can use this, and it will
do the apropriate things.
=============
*/
void Com_Quit_f( void ) {
	const char *p = Cmd_ArgsFrom( 1 );
	// don't try to shutdown if we are in a recursive error
	if ( !com_errorEntered ) {
		// Some VMs might execute "quit" command directly,
		// which would trigger an unload of active VM error.
		// Sys_Quit will kill this process anyways, so
		// a corrupt call stack makes no difference
		VM_Forced_Unload_Start();
		SV_Shutdown( p[0] ? p : "Server quit" );
#ifndef DEDICATED
		CL_Shutdown( p[0] ? p : "Client quit", qtrue );
#endif
		VM_Forced_Unload_Done();
		Com_Shutdown();
		FS_Shutdown( qtrue );
	}
	Sys_Quit();
}


/*
============================================================================

COMMAND LINE FUNCTIONS

+ characters seperate the commandLine string into multiple console
command lines.

All of these are valid:

quake3 +set test blah +map test
quake3 set test blah+map test
quake3 set test blah + map test

============================================================================
*/

#define	MAX_CONSOLE_LINES	32
int		com_numConsoleLines;
char	*com_consoleLines[MAX_CONSOLE_LINES];
// master rcon password
char	rconPassword2[MAX_CVAR_VALUE_STRING];

/*
==================
Com_ParseCommandLine

Break it up into multiple console lines
==================
*/
void Com_ParseCommandLine( char *commandLine ) {
	static int parsed = 0;
	int inq;

	if ( parsed )
		return;

	inq = 0;
	com_consoleLines[0] = commandLine;
	rconPassword2[0] = '\0';

	while ( *commandLine ) {
		if (*commandLine == '"') {
			inq = !inq;
		}
		// look for a + separating character
		// if commandLine came from a file, we might have real line seperators
		if ( (*commandLine == '+' && !inq) || *commandLine == '\n'  || *commandLine == '\r' ) {
			if ( com_numConsoleLines == MAX_CONSOLE_LINES ) {
				break;
			}
			com_consoleLines[com_numConsoleLines] = commandLine + 1;
			com_numConsoleLines++;
			*commandLine = '\0';
		}
		commandLine++;
	}
	parsed = 1;
}


/*
===================
Com_EarlyParseCmdLine

returns qtrue if both vid_xpos and vid_ypos was set
===================
*/
qboolean Com_EarlyParseCmdLine( char *commandLine, char *con_title, int title_size, int *vid_xpos, int *vid_ypos ) 
{
	int		flags = 0;
	int		i;
	
	*con_title = '\0';
	Com_ParseCommandLine( commandLine );

	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( !Q_stricmpn( Cmd_Argv(0), "set", 3 ) && !Q_stricmp( Cmd_Argv(1), "con_title" ) ) {
			com_consoleLines[i][0] = '\0';
			Q_strncpyz( con_title, Cmd_ArgsFrom( 2 ), title_size );
			continue;
		}
		if ( !Q_stricmp( Cmd_Argv(0), "con_title" ) ) {
			com_consoleLines[i][0] = '\0';
			Q_strncpyz( con_title, Cmd_ArgsFrom( 1 ), title_size );
			continue;
		}
		if ( !Q_stricmpn( Cmd_Argv(0), "set", 3 ) && !Q_stricmp( Cmd_Argv(1), "vid_xpos" ) ) {
			*vid_xpos = atoi( Cmd_Argv( 2 ) );
			flags |= 1;
			continue;
		}
		if ( !Q_stricmp( Cmd_Argv(0), "vid_xpos" ) ) {
			*vid_xpos = atoi( Cmd_Argv( 1 ) );
			flags |= 1;
			continue;
		}
		if ( !Q_stricmpn( Cmd_Argv(0), "set", 3 ) && !Q_stricmp( Cmd_Argv(1), "vid_ypos" ) ) {
			*vid_ypos = atoi( Cmd_Argv( 2 ) );
			flags |= 2;
			continue;
		}
		if ( !Q_stricmp( Cmd_Argv(0), "vid_ypos" ) ) {
			*vid_ypos = atoi( Cmd_Argv( 1 ) );
			flags |= 2;
			continue;
		}
		if ( !Q_stricmpn( Cmd_Argv(0), "set", 3 ) && !Q_stricmp( Cmd_Argv(1), "rconPassword2" ) ) {
			com_consoleLines[i][0] = '\0';
			Q_strncpyz( rconPassword2, Cmd_Argv( 2 ), sizeof( rconPassword2 ) );
			continue;
		}
	}

	return (flags == 3) ? qtrue : qfalse ;
}


/*
===================
Com_SafeMode

Check for "safe" on the command line, which will
skip loading of q3config.cfg
===================
*/
qboolean Com_SafeMode( void ) {
	int		i;

	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( !Q_stricmp( Cmd_Argv(0), "safe" )
			|| !Q_stricmp( Cmd_Argv(0), "cvar_restart" ) ) {
			com_consoleLines[i][0] = '\0';
			return qtrue;
		}
	}
	return qfalse;
}


/*
===============
Com_StartupVariable

Searches for command line parameters that are set commands.
If match is not NULL, only that cvar will be looked for.
That is necessary because cddir and basedir need to be set
before the filesystem is started, but all other sets should
be after execing the config and default.
===============
*/
void Com_StartupVariable( const char *match ) {
	int		i;
	char	*s;

	for (i=0 ; i < com_numConsoleLines ; i++) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( strcmp( Cmd_Argv(0), "set" ) ) {
			continue;
		}

		s = Cmd_Argv(1);
		if( !match || !strcmp( s, match ) )
		{
			if ( Cvar_Flags( s ) == CVAR_NONEXISTENT )
				Cvar_Get( s, Cmd_ArgsFrom( 2 ), CVAR_USER_CREATED );
			else
				Cvar_Set2( s, Cmd_ArgsFrom( 2 ), qfalse );
		}
	}
}


/*
=================
Com_AddStartupCommands

Adds command line parameters as script statements
Commands are seperated by + signs

Returns qtrue if any late commands were added, which
will keep the demoloop from immediately starting
=================
*/
static qboolean Com_AddStartupCommands( void ) {
	int		i;
	qboolean	added;

	added = qfalse;
	// quote every token, so args with semicolons can work
	for (i=0 ; i < com_numConsoleLines ; i++) {
		if ( !com_consoleLines[i] || !com_consoleLines[i][0] ) {
			continue;
		}

		// set commands already added with Com_StartupVariable
		if ( !Q_stricmpn( com_consoleLines[i], "set ", 4 ) ) {
			continue;
		}

		added = qtrue;
		Cbuf_AddText( com_consoleLines[i] );
		Cbuf_AddText( "\n" );
	}

	return added;
}


//============================================================================

void Info_Print( const char *s ) {
	char	key[BIG_INFO_KEY];
	char	value[BIG_INFO_VALUE];
	char	*o;
	int		l;

	if (*s == '\\')
		s++;
	while (*s)
	{
		o = key;
		while (*s && *s != '\\')
			*o++ = *s++;

		l = o - key;
		if (l < 20)
		{
			Com_Memset (o, ' ', 20-l);
			key[20] = '\0';
		}
		else
			*o = '\0';
		Com_Printf ("%s ", key);

		if (!*s)
		{
			Com_Printf ("MISSING VALUE\n");
			return;
		}

		o = value;
		s++;
		while (*s && *s != '\\')
			*o++ = *s++;
		*o = '\0';

		if (*s)
			s++;
		Com_Printf ("%s\n", value);
	}
}


/*
============
Com_StringContains
============
*/
static const char *Com_StringContains( const char *str1, const char *str2, int casesensitive ) {
	int len, i, j;

	len = strlen(str1) - strlen(str2);
	for (i = 0; i <= len; i++, str1++) {
		for (j = 0; str2[j]; j++) {
			if (casesensitive) {
				if (str1[j] != str2[j]) {
					break;
				}
			}
			else {
				if (locase[(byte)str1[j]] != locase[(byte)str2[j]]) {
					break;
				}
			}
		}
		if (!str2[j]) {
			return str1;
		}
	}
	return NULL;
}


/*
============
Com_Filter
============
*/
int Com_Filter( const char *filter, const char *name, int casesensitive )
{
	char buf[ MAX_TOKEN_CHARS ];
	const char *ptr;
	int i, found;

	while(*filter) {
		if (*filter == '*') {
			filter++;
			for (i = 0; *filter; i++) {
				if (*filter == '*' || *filter == '?') break;
				buf[i] = *filter;
				filter++;
			}
			buf[i] = '\0';
			if (strlen(buf)) {
				ptr = Com_StringContains(name, buf, casesensitive);
				if (!ptr) return qfalse;
				name = ptr + strlen(buf);
			}
		}
		else if (*filter == '?') {
			filter++;
			name++;
		}
		else if (*filter == '[' && *(filter+1) == '[') {
			filter++;
		}
		else if (*filter == '[') {
			filter++;
			found = qfalse;
			while(*filter && !found) {
				if (*filter == ']' && *(filter+1) != ']') break;
				if (*(filter+1) == '-' && *(filter+2) && (*(filter+2) != ']' || *(filter+3) == ']')) {
					if (casesensitive) {
						if (*name >= *filter && *name <= *(filter+2)) found = qtrue;
					}
					else {
						if (locase[(byte)*name] >= locase[(byte)*filter] &&
							locase[(byte)*name] <= locase[(byte)*(filter+2)]) found = qtrue;
					}
					filter += 3;
				}
				else {
					if (casesensitive) {
						if (*filter == *name) found = qtrue;
					}
					else {
						if (locase[(byte)*filter] == locase[(byte)*name]) found = qtrue;
					}
					filter++;
				}
			}
			if (!found) return qfalse;
			while(*filter) {
				if (*filter == ']' && *(filter+1) != ']') break;
				filter++;
			}
			filter++;
			name++;
		}
		else {
			if (casesensitive) {
				if (*filter != *name) return qfalse;
			}
			else {
				if (locase[(byte)*filter] != locase[(byte)*name]) return qfalse;
			}
			filter++;
			name++;
		}
	}
	return qtrue;
}


/*
============
Com_FilterPath
============
*/
int Com_FilterPath(const char *filter, const char *name, int casesensitive)
{
	int i;
	char new_filter[MAX_QPATH];
	char new_name[MAX_QPATH];

	for (i = 0; i < MAX_QPATH-1 && filter[i]; i++) {
		if ( filter[i] == '\\' || filter[i] == ':' ) {
			new_filter[i] = '/';
		}
		else {
			new_filter[i] = filter[i];
		}
	}
	new_filter[i] = '\0';
	for (i = 0; i < MAX_QPATH-1 && name[i]; i++) {
		if ( name[i] == '\\' || name[i] == ':' ) {
			new_name[i] = '/';
		}
		else {
			new_name[i] = name[i];
		}
	}
	new_name[i] = '\0';
	return Com_Filter(new_filter, new_name, casesensitive);
}


/*
================
Com_RealTime
================
*/
int Com_RealTime(qtime_t *qtime) {
	time_t t;
	struct tm *tms;

	t = time(NULL);
	if (!qtime)
		return t;
	tms = localtime(&t);
	if (tms) {
		qtime->tm_sec = tms->tm_sec;
		qtime->tm_min = tms->tm_min;
		qtime->tm_hour = tms->tm_hour;
		qtime->tm_mday = tms->tm_mday;
		qtime->tm_mon = tms->tm_mon;
		qtime->tm_year = tms->tm_year;
		qtime->tm_wday = tms->tm_wday;
		qtime->tm_yday = tms->tm_yday;
		qtime->tm_isdst = tms->tm_isdst;
	}
	return t;
}


/*
================
Sys_Microseconds
================
*/
int64_t Sys_Microseconds( void )
{
#ifdef _WIN32
	static qboolean inited = qfalse;
	static LARGE_INTEGER base;
	static LARGE_INTEGER freq;
	LARGE_INTEGER curr;

	if ( !inited )
	{
		QueryPerformanceFrequency( &freq );
		QueryPerformanceCounter( &base );
		if ( !freq.QuadPart )
		{
			return (int64_t)Sys_Milliseconds() * 1000LL; // fallback
		} 
		inited = qtrue;
		return 0;
	}

	QueryPerformanceCounter( &curr );

	return ((curr.QuadPart - base.QuadPart) * 1000000LL) / freq.QuadPart;
#else
	struct timeval curr;
	gettimeofday( &curr, NULL );

	return (int64_t)curr.tv_sec * 1000000LL + (int64_t)curr.tv_usec;
#endif
}


/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

#define	ZONEID	0x1d4a11
#define MINFRAGMENT	64

#ifdef ZONE_DEBUG
typedef struct zonedebug_s {
	char *label;
	char *file;
	int line;
	int allocSize;
} zonedebug_t;
#endif

struct memzone_s;

typedef struct memblock_s {
	int			size;	// including the header and possibly tiny fragments
	memtag_t	tag;	// a tag of 0 is a free block
	struct memblock_s	*next, *prev;
#ifdef USE_MULTI_SEGMENT
	struct memzone_s	*parent; // required for deallocation
#endif
	int			id;		// should be ZONEID
#ifdef ZONE_DEBUG
	zonedebug_t d;
#endif
} memblock_t;

typedef struct memzone_s {
	int		size;			// total bytes malloced, including header
	int		used;			// total bytes used
	memblock_t	blocklist;	// start / end cap for linked list
	memblock_t	*rover;
#ifdef USE_MULTI_SEGMENT
	struct memzone_s *next;
	struct memzone_s *head; // first segment, for updating stats
	int		totalSize;
	int		totalUsed;
	int		segnum;
#endif
} memzone_t;

// main zone for all "dynamic" memory allocation
memzone_t	*mainzone;
// we also have a small zone for small allocations that would only
// fragment the main zone (think of cvar and cmd strings)
memzone_t	*smallzone;


/*
========================
Z_ClearZone
========================
*/
static void Z_ClearZone( memzone_t *zone, int size ) {
	memblock_t	*block;
	
	// set the entire zone to one free block

	zone->blocklist.next = zone->blocklist.prev = block =
		(memblock_t *)( (byte *)zone + sizeof(memzone_t) );
	zone->blocklist.tag = TAG_GENERAL;	// in use block
#ifdef USE_MULTI_SEGMENT
	zone->blocklist.parent = zone;
#endif
	zone->blocklist.id = 0;
	zone->blocklist.size = 0;
	zone->rover = block;
	zone->size = size;
	zone->used = 0;
	
	block->prev = block->next = &zone->blocklist;
	block->tag = TAG_FREE;	// free block
	block->id = ZONEID;
#ifdef USE_MULTI_SEGMENT
	block->parent = zone;
#endif
	block->size = size - sizeof(memzone_t);
}


/*
========================
Z_AvailableZoneMemory
========================
*/
static int Z_AvailableZoneMemory( const memzone_t *zone ) {
#ifdef USE_MULTI_SEGMENT
	return zone->totalSize - zone->totalUsed;
#else
	return zone->size - zone->used;
#endif
}


/*
========================
Z_AvailableMemory
========================
*/
int Z_AvailableMemory( void ) {
	return Z_AvailableZoneMemory( mainzone );
}


/*
========================
Z_Free
========================
*/
void Z_Free( void *ptr ) {
	memblock_t	*block, *other;
	memzone_t *zone;
	
	if (!ptr) {
		Com_Error( ERR_DROP, "Z_Free: NULL pointer" );
	}

	block = (memblock_t *) ( (byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID) {
		Com_Error( ERR_FATAL, "Z_Free: freed a pointer without ZONEID" );
	}
	if (block->tag == TAG_FREE) {
		Com_Error( ERR_FATAL, "Z_Free: freed a freed pointer" );
	}
	// if static memory
	if (block->tag == TAG_STATIC) {
		return;
	}

	// check the memory trash tester
	if ( *(int *)((byte *)block + block->size - 4 ) != ZONEID ) {
		Com_Error( ERR_FATAL, "Z_Free: memory block wrote past end" );
	}

#ifdef USE_MULTI_SEGMENT
	zone = block->parent;
	zone->head->totalUsed -= block->size;
#else
	if ( block->tag == TAG_SMALL ) {
		zone = smallzone;
	} else {
		zone = mainzone;
	}
#endif

	zone->used -= block->size;
	// set the block to something that should cause problems
	// if it is referenced...
	Com_Memset( ptr, 0xaa, block->size - sizeof( *block ) );

	block->tag = TAG_FREE; // mark as free
	
	other = block->prev;
	if ( other->tag == TAG_FREE ) {
		// merge with previous free block
		other->size += block->size;
		other->next = block->next;
		other->next->prev = other;
		if (block == zone->rover) {
			zone->rover = other;
		}
		block = other;
	}

	zone->rover = block;

	other = block->next;
	if ( other->tag == TAG_FREE ) {
		// merge the next free block onto the end
		block->size += other->size;
		block->next = other->next;
		block->next->prev = block;
	}
}


/*
================
Z_FreeTags
================
*/
void Z_FreeTags( memtag_t tag ) {
	//int			count;
	memzone_t	*zone;

	if ( tag == TAG_SMALL ) {
		zone = smallzone;
	}
	else {
		zone = mainzone;
	}
	//count = 0;
	// use the rover as our pointer, because
	// Z_Free automatically adjusts it
	zone->rover = zone->blocklist.next;
	do {
		if ( zone->rover->tag == tag ) {
		//	count++;
			Z_Free( (void *)(zone->rover + 1) );
			continue;
		}
		zone->rover = zone->rover->next;
	} while ( zone->rover != &zone->blocklist );
}


/*
================
Z_TagMalloc
================
*/
#ifdef ZONE_DEBUG
void *Z_TagMallocDebug( int size, memtag_t tag, char *label, char *file, int line ) {
	int		allocSize;
#else
void *Z_TagMalloc( int size, memtag_t tag ) {
#endif
	int		extra;
	memblock_t	*start, *rover, *new, *base;
	memzone_t *zone;
#ifdef USE_MULTI_SEGMENT
	memzone_t *newz;
#endif
	if ( tag == TAG_FREE ) {
		Com_Error( ERR_FATAL, "Z_TagMalloc: tried to use with TAG_FREE" );
	}

	if ( tag == TAG_SMALL ) {
		zone = smallzone;
	} else {
		zone = mainzone;
	}

#ifdef ZONE_DEBUG
	allocSize = size;
#endif
	//
	// scan through the block list looking for the first free block
	// of sufficient size
	//
	size += sizeof(memblock_t);	// account for size of block header
	size += 4;					// space for memory trash tester
	size = PAD(size, sizeof(intptr_t));		// align to 32/64 bit boundary

	base = rover = zone->rover;
	start = base->prev;
	
	do {
		if ( rover == start ) {
#ifdef USE_MULTI_SEGMENT
			// try to swich to next zone segment
			/*if ( tag != TAG_SMALL )*/ {
				int newsize;
				if ( !zone->next ) {
					// try to allocate new zone segment
					newsize = PAD( size + sizeof( memzone_t ), 2*(1<<20) ); // round up to 2M blocks
					newz = calloc( newsize, 1 );
					if ( newz ) {
						Z_ClearZone( newz, newsize );
						zone->next = newz;
						// stats
						newz->head = zone->head;
						newz->head->totalSize += newz->size;
						newz->head->totalUsed += newz->used;
						newz->segnum = zone->segnum + 1;
					}
				}
				zone = zone->next;
				if ( zone ) {
					// these operations will take more time than regular allocations from primary zone
					// but in general it will happen only for large temp blocks -
					// which already comes with some CPU workload and/or IO-wait
					// so we don't care :P
					base = rover = zone->rover;
					start = base->prev;
					continue;
				}
				// if no success - passthrough to ComError()
			}
#endif
			// scaned all the way around the list
#ifdef ZONE_DEBUG
			Z_LogHeap();
			Com_Error( ERR_FATAL, "Z_Malloc: failed on allocation of %i bytes from the %s zone: %s, line: %d (%s)",
								size, zone == smallzone ? "small" : "main", file, line, label );
#else
			Com_Error( ERR_FATAL, "Z_Malloc: failed on allocation of %i bytes from the %s zone",
								size, zone == smallzone ? "small" : "main" );
#endif
			return NULL;
		}
		if ( rover->tag != TAG_FREE ) {
			base = rover = rover->next;
		} else {
			rover = rover->next;
		}
	} while (base->tag != TAG_FREE || base->size < size);
	
	//
	// found a block big enough
	//
	extra = base->size - size;
	if (extra > MINFRAGMENT) {
		// there will be a free fragment after the allocated block
		new = (memblock_t *) ((byte *)base + size );
		new->size = extra;
		new->tag = TAG_FREE; // free block
		new->prev = base;
		new->id = ZONEID;
		new->next = base->next;
		new->next->prev = new;
#ifdef USE_MULTI_SEGMENT
		new->parent = zone;
#endif
		base->next = new;
		base->size = size;
	}
	
	base->tag = tag;			// no longer a free block
	zone->rover = base->next;	// next allocation will start looking here
	zone->used += base->size;	//
#ifdef USE_MULTI_SEGMENT
	zone->head->totalUsed += base->size;
	base->parent = zone;
#endif
	base->id = ZONEID;

#ifdef ZONE_DEBUG
	base->d.label = label;
	base->d.file = file;
	base->d.line = line;
	base->d.allocSize = allocSize;
#endif

	// marker for memory trash testing
	*(int *)((byte *)base + base->size - 4) = ZONEID;

	return (void *) ((byte *)base + sizeof(memblock_t));
}


/*
========================
Z_Malloc
========================
*/
#ifdef ZONE_DEBUG
void *Z_MallocDebug( int size, char *label, char *file, int line ) {
#else
void *Z_Malloc( int size ) {
#endif
	void	*buf;
	
  //Z_CheckHeap ();	// DEBUG

#ifdef ZONE_DEBUG
	buf = Z_TagMallocDebug( size, TAG_GENERAL, label, file, line );
#else
	buf = Z_TagMalloc( size, TAG_GENERAL );
#endif
	Com_Memset( buf, 0, size );

	return buf;
}


/*
========================
S_Malloc
========================
*/
#ifdef ZONE_DEBUG
void *S_MallocDebug( int size, char *label, char *file, int line ) {
	return Z_TagMallocDebug( size, TAG_SMALL, label, file, line );
}
#else
void *S_Malloc( int size ) {
	return Z_TagMalloc( size, TAG_SMALL );
}
#endif


/*
========================
Z_CheckHeap
========================
*/
static void Z_CheckHeap( void ) {
	const memblock_t *block;
	const memzone_t *zone;

	zone =  mainzone;
	for ( block = zone->blocklist.next ; ; ) {
		if ( block->next == &zone->blocklist ) {
#ifdef USE_MULTI_SEGMENT
			if ( zone->next ) {
				zone = zone->next;
				block = zone->blocklist.next;
				continue;
			}
#endif
			break;	// all blocks have been hit
		}
		if ( (byte *)block + block->size != (byte *)block->next)
			Com_Error( ERR_FATAL, "Z_CheckHeap: block size does not touch the next block" );
		if ( block->next->prev != block) {
			Com_Error( ERR_FATAL, "Z_CheckHeap: next block doesn't have proper back link" );
		}
		if ( block->tag == TAG_FREE && block->next->tag == TAG_FREE ) {
			Com_Error( ERR_FATAL, "Z_CheckHeap: two consecutive free blocks" );
		}
		block = block->next;
	}
}


/*
========================
Z_LogZoneHeap
========================
*/
static void Z_LogZoneHeap( memzone_t *zone, const char *name ) {
#ifdef ZONE_DEBUG
	char dump[32], *ptr;
	int  i, j;
#endif
	memblock_t	*block;
	char		buf[4096];
	int size, allocSize, numBlocks;
	int len;

	if ( logfile == FS_INVALID_HANDLE || !FS_Initialized() )
		return;

	size = numBlocks = 0;
#ifdef ZONE_DEBUG
	allocSize = 0;
#endif
	len = Com_sprintf( buf, sizeof(buf), "\r\n================\r\n%s log\r\n================\r\n", name );
	FS_Write( buf, len, logfile );
	for ( block = zone->blocklist.next ; ; ) {
		if ( block->tag != TAG_FREE ) {
#ifdef ZONE_DEBUG
			ptr = ((char *) block) + sizeof(memblock_t);
			j = 0;
			for (i = 0; i < 20 && i < block->d.allocSize; i++) {
				if (ptr[i] >= 32 && ptr[i] < 127) {
					dump[j++] = ptr[i];
				}
				else {
					dump[j++] = '_';
				}
			}
			dump[j] = '\0';
			len = Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s) [%s]\r\n", block->d.allocSize, block->d.file, block->d.line, block->d.label, dump);
			FS_Write( buf, len, logfile );
			allocSize += block->d.allocSize;
#endif
			size += block->size;
			numBlocks++;
		}
		if ( block->next == &zone->blocklist ) {
#ifdef USE_MULTI_SEGMENT
			if ( zone->next ) {
				zone = zone->next;
				block = zone->blocklist.next;
				continue;
			}
#endif
			break; // all blocks have been hit
		}
		block = block->next;
	}
#ifdef ZONE_DEBUG
	// subtract debug memory
	size -= numBlocks * sizeof(zonedebug_t);
#else
	allocSize = numBlocks * sizeof(memblock_t); // + 32 bit alignment
#endif
	len = Com_sprintf( buf, sizeof( buf ), "%d %s memory in %d blocks\r\n", size, name, numBlocks );
	FS_Write( buf, len, logfile );
	len = Com_sprintf( buf, sizeof( buf ), "%d %s memory overhead\r\n", size - allocSize, name );
	FS_Write( buf, len, logfile );
	FS_Flush( logfile );
}


/*
========================
Z_LogHeap
========================
*/
void Z_LogHeap( void ) {
	Z_LogZoneHeap( mainzone, "MAIN" );
	Z_LogZoneHeap( smallzone, "SMALL" );
}


// static mem blocks to reduce a lot of small zone overhead
typedef struct memstatic_s {
	memblock_t b;
	byte mem[2];
} memstatic_t;

#ifdef USE_MULTI_SEGMENT
#define MEM_STATIC(chr) { { PAD(sizeof(memstatic_t),4), TAG_STATIC, NULL, NULL, NULL, ZONEID}, {chr,'\0'} }
#else
#define MEM_STATIC(chr) { { PAD(sizeof(memstatic_t),4), TAG_STATIC, NULL, NULL, ZONEID}, {chr,'\0'} }
#endif

static const memstatic_t emptystring =
	MEM_STATIC( '\0' );

static const memstatic_t numberstring[] = {
	MEM_STATIC( '0' ),
	MEM_STATIC( '1' ),
	MEM_STATIC( '2' ),
	MEM_STATIC( '3' ),
	MEM_STATIC( '4' ),
	MEM_STATIC( '5' ),
	MEM_STATIC( '6' ),
	MEM_STATIC( '7' ),
	MEM_STATIC( '8' ),
	MEM_STATIC( '9' )
};


/*
========================
CopyString

 NOTE:	never write over the memory CopyString returns because
		memory from a memstatic_t might be returned
========================
*/
char *CopyString( const char *in ) {
	char	*out;

	if (!in[0]) {
		return ((char *)&emptystring) + sizeof(memblock_t);
	}
	else if (!in[1]) {
		if (in[0] >= '0' && in[0] <= '9') {
			return ((char *)&numberstring[in[0]-'0']) + sizeof(memblock_t);
		}
	}
	out = S_Malloc (strlen(in)+1);
	strcpy (out, in);
	return out;
}


/*
==============================================================================

Goals:
	reproducable without history effects -- no out of memory errors on weird map to map changes
	allow restarting of the client without fragmentation
	minimize total pages in use at run time
	minimize total pages needed during load time

  Single block of memory with stack allocators coming from both ends towards the middle.

  One side is designated the temporary memory allocator.

  Temporary memory can be allocated and freed in any order.

  A highwater mark is kept of the most in use at any time.

  When there is no temporary memory allocated, the permanent and temp sides
  can be switched, allowing the already touched temp memory to be used for
  permanent storage.

  Temp memory must never be allocated on two ends at once, or fragmentation
  could occur.

  If we have any in-use temp memory, additional temp allocations must come from
  that side.

  If not, we can choose to make either side the new temp side and push future
  permanent allocations to the other side.  Permanent allocations should be
  kept on the side that has the current greatest wasted highwater mark.

==============================================================================
*/


#define	HUNK_MAGIC	0x89537892
#define	HUNK_FREE_MAGIC	0x89537893

typedef struct {
	int		magic;
	int		size;
} hunkHeader_t;

typedef struct {
	int		mark;
	int		permanent;
	int		temp;
	int		tempHighwater;
} hunkUsed_t;

typedef struct hunkblock_s {
	int size;
	byte printed;
	struct hunkblock_s *next;
	char *label;
	char *file;
	int line;
} hunkblock_t;

static	hunkblock_t *hunkblocks;

static	hunkUsed_t	hunk_low, hunk_high;
static	hunkUsed_t	*hunk_permanent, *hunk_temp;

static	byte	*s_hunkData = NULL;
static	int		s_hunkTotal;

static const char *tagName[ TAG_COUNT ] = {
	"FREE",
	"GENERAL",
	"BOTLIB",
	"RENDERER",
	"SMALL",
	"STATIC"
};

typedef struct zone_stats_s {
	int	zoneSegments;
	int zoneBlocks;
	int	zoneBytes;
	int	botlibBytes;
	int	rendererBytes;
} zone_stats_t;


static void Zone_Stats( const char *name, const memzone_t *z, qboolean printDetails, zone_stats_t *stats ) 
{
	const memblock_t *block;
	const memzone_t *zone;
	zone_stats_t st;

	memset( &st, 0, sizeof( st ) );
	zone = z;
	st.zoneSegments = 1;
	
	//if ( printDetails ) {
	//	Com_Printf( "---------- %s zone segment #%i ----------\n", name, zone->segnum );
	//}

	for ( block = zone->blocklist.next ; ; ) {
		if ( printDetails ) {
			Com_Printf( "block:%p  size:%8i  tag: %s\n", (void *)block, block->size, 
				(unsigned)block->tag < TAG_COUNT ? tagName[ block->tag ] : va( "%i", block->tag ) );
		}
		if ( block->tag != TAG_FREE ) {
			st.zoneBytes += block->size;
			st.zoneBlocks++;
			if ( block->tag == TAG_BOTLIB ) {
				st.botlibBytes += block->size;
			} else if ( block->tag == TAG_RENDERER ) {
				st.rendererBytes += block->size;
			}
		}
		if ( block->next == &zone->blocklist ) {
#ifdef USE_MULTI_SEGMENT
			if ( zone->next ) {
				zone = zone->next;
				block = zone->blocklist.next;
				if ( printDetails ) {
					Com_Printf( "---------- %s zone segment #%i ----------\n", name, zone->segnum );
				}
				st.zoneSegments++;
				continue;
			}
#endif
			break; // all blocks have been hit
		}
		if ( (byte *)block + block->size != (byte *)block->next) {
			Com_Printf( "ERROR: block size does not touch the next block\n" );
		}
		if ( block->next->prev != block) {
			Com_Printf( "ERROR: next block doesn't have proper back link\n" );
		}
		if ( block->tag == TAG_FREE && block->next->tag == TAG_FREE ) {
			Com_Printf( "ERROR: two consecutive free blocks\n" );
		}
		block = block->next;
	}

	// export stats
	if ( stats ) {
		memcpy( stats, &st, sizeof( *stats ) );
	}
}


/*
=================
Com_Meminfo_f
=================
*/
void Com_Meminfo_f( void ) {
	zone_stats_t st;
	int		unused;

	Com_Printf( "%8i bytes total hunk\n", s_hunkTotal );
	Com_Printf( "%8i bytes total zone\n", mainzone->totalSize );
	Com_Printf( "\n" );
	Com_Printf( "%8i low mark\n", hunk_low.mark );
	Com_Printf( "%8i low permanent\n", hunk_low.permanent );
	if ( hunk_low.temp != hunk_low.permanent ) {
		Com_Printf( "%8i low temp\n", hunk_low.temp );
	}
	Com_Printf( "%8i low tempHighwater\n", hunk_low.tempHighwater );
	Com_Printf( "\n" );
	Com_Printf( "%8i high mark\n", hunk_high.mark );
	Com_Printf( "%8i high permanent\n", hunk_high.permanent );
	if ( hunk_high.temp != hunk_high.permanent ) {
		Com_Printf( "%8i high temp\n", hunk_high.temp );
	}
	Com_Printf( "%8i high tempHighwater\n", hunk_high.tempHighwater );
	Com_Printf( "\n" );
	Com_Printf( "%8i total hunk in use\n", hunk_low.permanent + hunk_high.permanent );
	unused = 0;
	if ( hunk_low.tempHighwater > hunk_low.permanent ) {
		unused += hunk_low.tempHighwater - hunk_low.permanent;
	}
	if ( hunk_high.tempHighwater > hunk_high.permanent ) {
		unused += hunk_high.tempHighwater - hunk_high.permanent;
	}
	Com_Printf( "%8i unused highwater\n", unused );
	Com_Printf( "\n" );

	Zone_Stats( "main", mainzone, !Q_stricmp( Cmd_Argv(1), "main" ) || !Q_stricmp( Cmd_Argv(1), "all" ), &st );
	Com_Printf( "%8i bytes in %i main zone blocks%s\n", st.zoneBytes, st.zoneBlocks,
		st.zoneSegments > 1 ? va( " and %i segments", st.zoneSegments ) : "" );
	Com_Printf( "        %8i bytes in botlib\n", st.botlibBytes );
	Com_Printf( "        %8i bytes in renderer\n", st.rendererBytes );
	Com_Printf( "        %8i bytes in other\n", st.zoneBytes - ( st.botlibBytes + st.rendererBytes ) );

	Zone_Stats( "small", smallzone, !Q_stricmp( Cmd_Argv(1), "small" ) || !Q_stricmp( Cmd_Argv(1), "all" ), &st );
	Com_Printf( "%8i bytes in %i small zone blocks%s\n", st.zoneBytes, st.zoneBlocks,
		st.zoneSegments > 1 ? va( " and %i segments", st.zoneSegments ) : "" );
}


/*
===============
Com_TouchMemory

Touch all known used data to make sure it is paged in
===============
*/
void Com_TouchMemory( void ) {
	const memblock_t *block;
	const memzone_t *zone;
	int		start, end;
	int		i, j;
	unsigned int sum;

	Z_CheckHeap();

	start = Sys_Milliseconds();

	sum = 0;

	j = hunk_low.permanent >> 2;
	for ( i = 0 ; i < j ; i+=64 ) {			// only need to touch each page
		sum += ((unsigned int *)s_hunkData)[i];
	}

	i = ( s_hunkTotal - hunk_high.permanent ) >> 2;
	j = hunk_high.permanent >> 2;
	for (  ; i < j ; i+=64 ) {			// only need to touch each page
		sum += ((unsigned int *)s_hunkData)[i];
	}

	zone = mainzone;
	for (block = zone->blocklist.next ; ; block = block->next) {
		if ( block->tag != TAG_FREE ) {
			j = block->size >> 2;
			for ( i = 0 ; i < j ; i+=64 ) {				// only need to touch each page
				sum += ((unsigned int *)block)[i];
			}
		}
		if ( block->next == &zone->blocklist ) {
#ifdef USE_MULTI_SEGMENT
			if ( zone->next ) {
				zone = zone->next;
				block = zone->blocklist.next;
				continue;
			}
#endif
			break; // all blocks have been hit
		}
	}

	end = Sys_Milliseconds();

	Com_Printf( "Com_TouchMemory: %i msec\n", end - start );
}


/*
=================
Com_InitSmallZoneMemory
=================
*/
void Com_InitSmallZoneMemory( void ) {
	static byte s_buf[ 512 * 1024 ];
	int smallZoneSize;

	smallZoneSize = sizeof( s_buf );
	Com_Memset( s_buf, 0, smallZoneSize );
	smallzone = (memzone_t *)s_buf;
	Z_ClearZone( smallzone, smallZoneSize );
#ifdef USE_MULTI_SEGMENT
	// initial segment points on itself
	smallzone->head = smallzone;
	smallzone->totalSize = smallzone->size;
	smallzone->totalUsed = smallzone->used;
	smallzone->segnum = 1; // stats
#endif
}


/*
=================
Com_InitZoneMemory
=================
*/
static void Com_InitZoneMemory( void ) {
	int		mainZoneSize;
	cvar_t	*cv;

	// Please note: com_zoneMegs can only be set on the command line, and
	// not in q3config.cfg or Com_StartupVariable, as they haven't been
	// executed by this point. It's a chicken and egg problem. We need the
	// memory manager configured to handle those places where you would
	// configure the memory manager.

	// allocate the random block zone
	cv = Cvar_Get( "com_zoneMegs", DEF_COMZONEMEGS_S, CVAR_LATCH | CVAR_ARCHIVE );

	if ( cv->integer < DEF_COMZONEMEGS ) {
		mainZoneSize = 1024 * 1024 * DEF_COMZONEMEGS;
	} else {
		mainZoneSize = cv->integer * 1024 * 1024;
	}
	mainzone = calloc( mainZoneSize, 1 );
	if ( !mainzone ) {
		Com_Error( ERR_FATAL, "Zone data failed to allocate %i megs", mainZoneSize / (1024*1024) );
	}
	Z_ClearZone( mainzone, mainZoneSize );
#ifdef USE_MULTI_SEGMENT
	// initial segment points on itself
	mainzone->head = mainzone;
	mainzone->totalSize = mainzone->size;
	mainzone->totalUsed = mainzone->used;
	mainzone->segnum = 1; // stats
#endif
}


/*
=================
Hunk_Log
=================
*/
void Hunk_Log( void ) {
	hunkblock_t	*block;
	char		buf[4096];
	int size, numBlocks;

	if (!logfile || !FS_Initialized())
		return;
	size = 0;
	numBlocks = 0;
	Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk log\r\n================\r\n");
	FS_Write(buf, strlen(buf), logfile);
	for (block = hunkblocks ; block; block = block->next) {
#ifdef HUNK_DEBUG
		Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s)\r\n", block->size, block->file, block->line, block->label);
		FS_Write(buf, strlen(buf), logfile);
#endif
		size += block->size;
		numBlocks++;
	}
	Com_sprintf(buf, sizeof(buf), "%d Hunk memory\r\n", size);
	FS_Write(buf, strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d hunk blocks\r\n", numBlocks);
	FS_Write(buf, strlen(buf), logfile);
}


/*
=================
Hunk_SmallLog
=================
*/
void Hunk_SmallLog( void ) {
	hunkblock_t	*block, *block2;
	char		buf[4096];
	int size, locsize, numBlocks;

	if (!logfile || !FS_Initialized())
		return;
	for (block = hunkblocks ; block; block = block->next) {
		block->printed = qfalse;
	}
	size = 0;
	numBlocks = 0;
	Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk Small log\r\n================\r\n");
	FS_Write(buf, strlen(buf), logfile);
	for (block = hunkblocks; block; block = block->next) {
		if (block->printed) {
			continue;
		}
		locsize = block->size;
		for (block2 = block->next; block2; block2 = block2->next) {
			if (block->line != block2->line) {
				continue;
			}
			if (Q_stricmp(block->file, block2->file)) {
				continue;
			}
			size += block2->size;
			locsize += block2->size;
			block2->printed = qtrue;
		}
#ifdef HUNK_DEBUG
		Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s)\r\n", locsize, block->file, block->line, block->label);
		FS_Write(buf, strlen(buf), logfile);
#endif
		size += block->size;
		numBlocks++;
	}
	Com_sprintf(buf, sizeof(buf), "%d Hunk memory\r\n", size);
	FS_Write(buf, strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d hunk blocks\r\n", numBlocks);
	FS_Write(buf, strlen(buf), logfile);
}


/*
=================
Com_InitHunkMemory
=================
*/
static void Com_InitHunkMemory( void ) {
	cvar_t	*cv;
	int nMinAlloc;
	char *pMsg = NULL;

	// make sure the file system has allocated and "not" freed any temp blocks
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redunant routines in the file system utilizing different 
	// memory systems
	if (FS_LoadStack() != 0) {
		Com_Error( ERR_FATAL, "Hunk initialization failed. File system load stack not zero");
	}

	// allocate the stack based hunk allocator
	cv = Cvar_Get( "com_hunkMegs", DEF_COMHUNKMEGS_S, CVAR_LATCH | CVAR_ARCHIVE );
	Cvar_SetDescription( cv, "The size of the hunk memory segment" );

	// if we are not dedicated min allocation is 56, otherwise min is 1
	if (com_dedicated && com_dedicated->integer) {
		nMinAlloc = MIN_DEDICATED_COMHUNKMEGS;
		pMsg = "Minimum com_hunkMegs for a dedicated server is %i, allocating %i megs.\n";
	}
	else {
		nMinAlloc = MIN_COMHUNKMEGS;
		pMsg = "Minimum com_hunkMegs is %i, allocating %i megs.\n";
	}

	if ( cv->integer < nMinAlloc ) {
		s_hunkTotal = 1024 * 1024 * nMinAlloc;
	    Com_Printf(pMsg, nMinAlloc, s_hunkTotal / (1024 * 1024));
	} else {
		s_hunkTotal = cv->integer * 1024 * 1024;
	}

	s_hunkData = calloc( s_hunkTotal + 63, 1 );
	if ( !s_hunkData ) {
		Com_Error( ERR_FATAL, "Hunk data failed to allocate %i megs", s_hunkTotal / (1024*1024) );
	}
	// cacheline align
	s_hunkData = PADP( s_hunkData, 64 );
	Hunk_Clear();

	Cmd_AddCommand( "meminfo", Com_Meminfo_f );
#ifdef ZONE_DEBUG
	Cmd_AddCommand( "zonelog", Z_LogHeap );
#endif
#ifdef HUNK_DEBUG
	Cmd_AddCommand( "hunklog", Hunk_Log );
	Cmd_AddCommand( "hunksmalllog", Hunk_SmallLog );
#endif
}


/*
====================
Hunk_MemoryRemaining
====================
*/
int	Hunk_MemoryRemaining( void ) {
	int		low, high;

	low = hunk_low.permanent > hunk_low.temp ? hunk_low.permanent : hunk_low.temp;
	high = hunk_high.permanent > hunk_high.temp ? hunk_high.permanent : hunk_high.temp;

	return s_hunkTotal - ( low + high );
}


/*
===================
Hunk_SetMark

The server calls this after the level and game VM have been loaded
===================
*/
void Hunk_SetMark( void ) {
	hunk_low.mark = hunk_low.permanent;
	hunk_high.mark = hunk_high.permanent;
}


/*
=================
Hunk_ClearToMark

The client calls this before starting a vid_restart or snd_restart
=================
*/
void Hunk_ClearToMark( void ) {
	hunk_low.permanent = hunk_low.temp = hunk_low.mark;
	hunk_high.permanent = hunk_high.temp = hunk_high.mark;
}


/*
=================
Hunk_CheckMark
=================
*/
qboolean Hunk_CheckMark( void ) {
	if( hunk_low.mark || hunk_high.mark ) {
		return qtrue;
	}
	return qfalse;
}

void CL_ShutdownCGame( void );
void CL_ShutdownUI( void );
void SV_ShutdownGameProgs( void );

/*
=================
Hunk_Clear

The server calls this before shutting down or loading a new map
=================
*/
void Hunk_Clear( void ) {

#ifndef DEDICATED
	CL_ShutdownCGame();
	CL_ShutdownUI();
#endif
	SV_ShutdownGameProgs();
#ifndef DEDICATED
	CIN_CloseAllVideos();
#endif
	hunk_low.mark = 0;
	hunk_low.permanent = 0;
	hunk_low.temp = 0;
	hunk_low.tempHighwater = 0;

	hunk_high.mark = 0;
	hunk_high.permanent = 0;
	hunk_high.temp = 0;
	hunk_high.tempHighwater = 0;

	hunk_permanent = &hunk_low;
	hunk_temp = &hunk_high;

	Com_Printf( "Hunk_Clear: reset the hunk ok\n" );
	VM_Clear();
#ifdef HUNK_DEBUG
	hunkblocks = NULL;
#endif
}


static void Hunk_SwapBanks( void ) {
	hunkUsed_t	*swap;

	// can't swap banks if there is any temp already allocated
	if ( hunk_temp->temp != hunk_temp->permanent ) {
		return;
	}

	// if we have a larger highwater mark on this side, start making
	// our permanent allocations here and use the other side for temp
	if ( hunk_temp->tempHighwater - hunk_temp->permanent >
		hunk_permanent->tempHighwater - hunk_permanent->permanent ) {
		swap = hunk_temp;
		hunk_temp = hunk_permanent;
		hunk_permanent = swap;
	}
}


/*
=================
Hunk_Alloc

Allocate permanent (until the hunk is cleared) memory
=================
*/
#ifdef HUNK_DEBUG
void *Hunk_AllocDebug( int size, ha_pref preference, char *label, char *file, int line ) {
#else
void *Hunk_Alloc( int size, ha_pref preference ) {
#endif
	void	*buf;

	if ( s_hunkData == NULL)
	{
		Com_Error( ERR_FATAL, "Hunk_Alloc: Hunk memory system not initialized" );
	}

	// can't do preference if there is any temp allocated
	if (preference == h_dontcare || hunk_temp->temp != hunk_temp->permanent) {
		Hunk_SwapBanks();
	} else {
		if (preference == h_low && hunk_permanent != &hunk_low) {
			Hunk_SwapBanks();
		} else if (preference == h_high && hunk_permanent != &hunk_high) {
			Hunk_SwapBanks();
		}
	}

#ifdef HUNK_DEBUG
	size += sizeof(hunkblock_t);
#endif

	// round to cacheline
	size = PAD( size, 64 );

	if ( hunk_low.temp + hunk_high.temp + size > s_hunkTotal ) {
#ifdef HUNK_DEBUG
		Hunk_Log();
		Hunk_SmallLog();

		Com_Error(ERR_DROP, "Hunk_Alloc failed on %i: %s, line: %d (%s)", size, file, line, label);
#else
		Com_Error(ERR_DROP, "Hunk_Alloc failed on %i", size);
#endif
	}

	if ( hunk_permanent == &hunk_low ) {
		buf = (void *)(s_hunkData + hunk_permanent->permanent);
		hunk_permanent->permanent += size;
	} else {
		hunk_permanent->permanent += size;
		buf = (void *)(s_hunkData + s_hunkTotal - hunk_permanent->permanent );
	}

	hunk_permanent->temp = hunk_permanent->permanent;

	Com_Memset( buf, 0, size );

#ifdef HUNK_DEBUG
	{
		hunkblock_t *block;

		block = (hunkblock_t *) buf;
		block->size = size - sizeof(hunkblock_t);
		block->file = file;
		block->label = label;
		block->line = line;
		block->next = hunkblocks;
		hunkblocks = block;
		buf = ((byte *) buf) + sizeof(hunkblock_t);
	}
#endif
	return buf;
}


/*
=================
Hunk_AllocateTempMemory

This is used by the file loading system.
Multiple files can be loaded in temporary memory.
When the files-in-use count reaches zero, all temp memory will be deleted
=================
*/
void *Hunk_AllocateTempMemory( int size ) {
	void		*buf;
	hunkHeader_t	*hdr;

	// return a Z_Malloc'd block if the hunk has not been initialized
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redunant routines in the file system utilizing different 
	// memory systems
	if ( s_hunkData == NULL )
	{
		return Z_Malloc(size);
	}

	Hunk_SwapBanks();

	size = PAD(size, sizeof(intptr_t)) + sizeof( hunkHeader_t );

	if ( hunk_temp->temp + hunk_permanent->permanent + size > s_hunkTotal ) {
		Com_Error( ERR_DROP, "Hunk_AllocateTempMemory: failed on %i", size );
	}

	if ( hunk_temp == &hunk_low ) {
		buf = (void *)(s_hunkData + hunk_temp->temp);
		hunk_temp->temp += size;
	} else {
		hunk_temp->temp += size;
		buf = (void *)(s_hunkData + s_hunkTotal - hunk_temp->temp );
	}

	if ( hunk_temp->temp > hunk_temp->tempHighwater ) {
		hunk_temp->tempHighwater = hunk_temp->temp;
	}

	hdr = (hunkHeader_t *)buf;
	buf = (void *)(hdr+1);

	hdr->magic = HUNK_MAGIC;
	hdr->size = size;

	// don't bother clearing, because we are going to load a file over it
	return buf;
}


/*
==================
Hunk_FreeTempMemory
==================
*/
void Hunk_FreeTempMemory( void *buf ) {
	hunkHeader_t	*hdr;

	// free with Z_Free if the hunk has not been initialized
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redunant routines in the file system utilizing different 
	// memory systems
	if ( s_hunkData == NULL )
	{
		Z_Free(buf);
		return;
	}

	hdr = ( (hunkHeader_t *)buf ) - 1;
	if ( hdr->magic != HUNK_MAGIC ) {
		Com_Error( ERR_FATAL, "Hunk_FreeTempMemory: bad magic" );
	}

	hdr->magic = HUNK_FREE_MAGIC;

	// this only works if the files are freed in stack order,
	// otherwise the memory will stay around until Hunk_ClearTempMemory
	if ( hunk_temp == &hunk_low ) {
		if ( hdr == (void *)(s_hunkData + hunk_temp->temp - hdr->size ) ) {
			hunk_temp->temp -= hdr->size;
		} else {
			Com_Printf( "Hunk_FreeTempMemory: not the final block\n" );
		}
	} else {
		if ( hdr == (void *)(s_hunkData + s_hunkTotal - hunk_temp->temp ) ) {
			hunk_temp->temp -= hdr->size;
		} else {
			Com_Printf( "Hunk_FreeTempMemory: not the final block\n" );
		}
	}
}


/*
=================
Hunk_ClearTempMemory

The temp space is no longer needed.  If we have left more
touched but unused memory on this side, have future
permanent allocs use this side.
=================
*/
void Hunk_ClearTempMemory( void ) {
	if ( s_hunkData != NULL ) {
		hunk_temp->temp = hunk_temp->permanent;
	}
}

/*
===================================================================

EVENTS AND JOURNALING

In addition to these events, .cfg files are also copied to the
journaled file
===================================================================
*/

#define	MAX_PUSHED_EVENTS	            1024
static int com_pushedEventsHead = 0;
static int com_pushedEventsTail = 0;
static sysEvent_t	com_pushedEvents[MAX_PUSHED_EVENTS];

/*
=================
Com_InitJournaling
=================
*/
void Com_InitJournaling( void ) {
	Com_StartupVariable( "journal" );
	com_journal = Cvar_Get ("journal", "0", CVAR_INIT);
	if ( !com_journal->integer ) {
		return;
	}

	if ( com_journal->integer == 1 ) {
		Com_Printf( "Journaling events\n");
		com_journalFile = FS_FOpenFileWrite( "journal.dat" );
		com_journalDataFile = FS_FOpenFileWrite( "journaldata.dat" );
	} else if ( com_journal->integer == 2 ) {
		Com_Printf( "Replaying journaled events\n");
		FS_FOpenFileRead( "journal.dat", &com_journalFile, qtrue );
		FS_FOpenFileRead( "journaldata.dat", &com_journalDataFile, qtrue );
	}

	if ( !com_journalFile || !com_journalDataFile ) {
		Cvar_Set( "com_journal", "0" );
		if ( com_journalFile )
			FS_FCloseFile( com_journalFile );
		if ( com_journalDataFile )
			FS_FCloseFile( com_journalDataFile );
		com_journalFile = FS_INVALID_HANDLE;
		com_journalDataFile = FS_INVALID_HANDLE;
		Com_Printf( "Couldn't open journal files\n" );
	}
}



/*
========================================================================

EVENT LOOP

========================================================================
*/

#define MAX_QUED_EVENTS		128
#define MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

static sysEvent_t			eventQue[ MAX_QUED_EVENTS ];
static sysEvent_t			*lastEvent = NULL;
unsigned int				eventHead = 0;
unsigned int				eventTail = 0;

static const char *Sys_EventName( sysEventType_t evType ) {

	static const char *evNames[ SE_MAX ] = {
		"SE_NONE",
		"SE_KEY",
		"SE_CHAR",
		"SE_MOUSE",
		"SE_JOYSTICK_AXIS",
		"SE_CONSOLE" 
	};

	if ( evType >= SE_MAX ) {
		return "SE_UNKNOWN";
	} else {
		return evNames[ evType ];
	}
}


/*
================
Sys_QueEvent

A time of 0 will get the current time
Ptr should either be null, or point to a block of data that can
be freed by the game later.
================
*/
void Sys_QueEvent( int evTime, sysEventType_t evType, int value, int value2, int ptrLength, void *ptr ) {
	sysEvent_t	*ev;

#if 0
	Com_Printf( "%-10s: evTime=%i, evTail=%i, evHead=%i\n",
		Sys_EventName( evType ), evTime, eventTail, eventHead );
#endif

	if ( evTime == 0 ) {
		evTime = Sys_Milliseconds();
	}

	// try to combine all sequential mouse moves in one event
	if ( evType == SE_MOUSE && lastEvent && lastEvent->evType == SE_MOUSE ) {
		// try to reuse already processed item
		if ( eventTail == eventHead ) {
			lastEvent->evValue = value;
			lastEvent->evValue2 = value2;
			eventTail--;
		} else {
			lastEvent->evValue += value;
			lastEvent->evValue2 += value2;
		}
		lastEvent->evTime = evTime;
		return;
	}

	ev = &eventQue[ eventHead & MASK_QUED_EVENTS ];

	if ( eventHead - eventTail >= MAX_QUED_EVENTS ) {
		Com_Printf( "%s(type=%s,keys=(%i,%i),time=%i): overflow\n", __func__, Sys_EventName( evType ), value, value2, evTime );
		// we are discarding an event, but don't leak memory
		if ( ev->evPtr ) {
			Z_Free( ev->evPtr );
		}
		eventTail++;
	}

	eventHead++;

	ev->evTime = evTime;
	ev->evType = evType;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;

	lastEvent = ev;
}


/*
================
Com_GetSystemEvent
================
*/
static sysEvent_t Com_GetSystemEvent( void )
{
	sysEvent_t  ev;
	char		*s;
	int			evTime;

	// return if we have data
	if ( eventHead > eventTail )
	{
		eventTail++;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	Sys_SendKeyEvents();

	evTime = Sys_Milliseconds();

	// check for console commands
	s = Sys_ConsoleInput();
	if ( s )
	{
		char  *b;
		int   len;

		len = strlen( s ) + 1;
		b = Z_Malloc( len );
		strcpy( b, s );
		Sys_QueEvent( evTime, SE_CONSOLE, 0, 0, len, b );
	}

	// return if we have data
	if ( eventHead > eventTail )
	{
		eventTail++;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	// create an empty event to return
	memset( &ev, 0, sizeof( ev ) );
	ev.evTime = evTime;

	return ev;
}


/*
=================
Com_GetRealEvent
=================
*/
static sysEvent_t Com_GetRealEvent( void ) {
	int			r;
	sysEvent_t	ev;

	// either get an event from the system or the journal file
	if ( com_journal->integer == 2 ) {
		r = FS_Read( &ev, sizeof(ev), com_journalFile );
		if ( r != sizeof(ev) ) {
			Com_Error( ERR_FATAL, "Error reading from journal file" );
		}
		if ( ev.evPtrLength ) {
			ev.evPtr = Z_Malloc( ev.evPtrLength );
			r = FS_Read( ev.evPtr, ev.evPtrLength, com_journalFile );
			if ( r != ev.evPtrLength ) {
				Com_Error( ERR_FATAL, "Error reading from journal file" );
			}
		}
	} else {
		ev = Com_GetSystemEvent();

		// write the journal value out if needed
		if ( com_journal->integer == 1 ) {
			r = FS_Write( &ev, sizeof(ev), com_journalFile );
			if ( r != sizeof(ev) ) {
				Com_Error( ERR_FATAL, "Error writing to journal file" );
			}
			if ( ev.evPtrLength ) {
				r = FS_Write( ev.evPtr, ev.evPtrLength, com_journalFile );
				if ( r != ev.evPtrLength ) {
					Com_Error( ERR_FATAL, "Error writing to journal file" );
				}
			}
		}
	}

	return ev;
}


/*
=================
Com_InitPushEvent
=================
*/
static void Com_InitPushEvent( void ) {
  // clear the static buffer array
  // this requires SE_NONE to be accepted as a valid but NOP event
  memset( com_pushedEvents, 0, sizeof(com_pushedEvents) );
  // reset counters while we are at it
  // beware: GetEvent might still return an SE_NONE from the buffer
  com_pushedEventsHead = 0;
  com_pushedEventsTail = 0;
}


/*
=================
Com_PushEvent
=================
*/
static void Com_PushEvent( const sysEvent_t *event ) {
	sysEvent_t		*ev;
	static int printedWarning = 0;

	ev = &com_pushedEvents[ com_pushedEventsHead & (MAX_PUSHED_EVENTS-1) ];

	if ( com_pushedEventsHead - com_pushedEventsTail >= MAX_PUSHED_EVENTS ) {

		// don't print the warning constantly, or it can give time for more...
		if ( !printedWarning ) {
			printedWarning = qtrue;
			Com_Printf( "WARNING: Com_PushEvent overflow\n" );
		}

		if ( ev->evPtr ) {
			Z_Free( ev->evPtr );
		}
		com_pushedEventsTail++;
	} else {
		printedWarning = qfalse;
	}

	*ev = *event;
	com_pushedEventsHead++;
}


/*
=================
Com_GetEvent
=================
*/
static sysEvent_t Com_GetEvent( void ) {
	if ( com_pushedEventsHead > com_pushedEventsTail ) {
		com_pushedEventsTail++;
		return com_pushedEvents[ (com_pushedEventsTail-1) & (MAX_PUSHED_EVENTS-1) ];
	}
	return Com_GetRealEvent();
}


/*
=================
Com_RunAndTimeServerPacket
=================
*/
void Com_RunAndTimeServerPacket( const netadr_t *evFrom, msg_t *buf ) {
	int		t1, t2, msec;

	t1 = 0;

	if ( com_speeds->integer ) {
		t1 = Sys_Milliseconds ();
	}

	SV_PacketEvent( evFrom, buf );

	if ( com_speeds->integer ) {
		t2 = Sys_Milliseconds ();
		msec = t2 - t1;
		if ( com_speeds->integer == 3 ) {
			Com_Printf( "SV_PacketEvent time: %i\n", msec );
		}
	}
}


/*
=================
Com_EventLoop

Returns last event time
=================
*/
int Com_EventLoop( void ) {
	sysEvent_t	ev;
	netadr_t	evFrom;
	byte		bufData[ MAX_MSGLEN_BUF ];
	msg_t		buf;

	MSG_Init( &buf, bufData, MAX_MSGLEN );

	while ( 1 ) {
		ev = Com_GetEvent();

		// if no more events are available
		if ( ev.evType == SE_NONE ) {
			// manually send packet events for the loopback channel
#ifndef DEDICATED
			while ( NET_GetLoopPacket( NS_CLIENT, &evFrom, &buf ) ) {
				CL_PacketEvent( &evFrom, &buf );
			}
#endif
			while ( NET_GetLoopPacket( NS_SERVER, &evFrom, &buf ) ) {
				// if the server just shut down, flush the events
				if ( com_sv_running->integer ) {
					Com_RunAndTimeServerPacket( &evFrom, &buf );
				}
			}

			return ev.evTime;
		}


		switch ( ev.evType ) {
#ifndef DEDICATED
		case SE_KEY:
			CL_KeyEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_CHAR:
			CL_CharEvent( ev.evValue );
			break;
		case SE_MOUSE:
			CL_MouseEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_JOYSTICK_AXIS:
			CL_JoystickEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
#endif
		case SE_CONSOLE:
			Cbuf_AddText( (char *)ev.evPtr );
			Cbuf_AddText( "\n" );
			break;
			default:
				Com_Error( ERR_FATAL, "Com_EventLoop: bad event type %i", ev.evType );
			break;
		}

		// free any block data
		if ( ev.evPtr ) {
			Z_Free( ev.evPtr );
		}
	}

	return 0;	// never reached
}


/*
================
Com_Milliseconds

Can be used for profiling, but will be journaled accurately
================
*/
int Com_Milliseconds( void ) {
	sysEvent_t	ev;

	// get events and push them until we get a null event with the current time
	do {

		ev = Com_GetRealEvent();
		if ( ev.evType != SE_NONE ) {
			Com_PushEvent( &ev );
		}
	} while ( ev.evType != SE_NONE );
	
	return ev.evTime;
}

//============================================================================

/*
=============
Com_Error_f

Just throw a fatal error to
test error shutdown procedures
=============
*/
static void __attribute__((__noreturn__)) Com_Error_f (void) {
	if ( Cmd_Argc() > 1 ) {
		Com_Error( ERR_DROP, "Testing drop error" );
	} else {
		Com_Error( ERR_FATAL, "Testing fatal error" );
	}
}


/*
=============
Com_Freeze_f

Just freeze in place for a given number of seconds to test
error recovery
=============
*/
static void Com_Freeze_f( void ) {
	float	s;
	int		start, now;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "freeze <seconds>\n" );
		return;
	}
	s = atof( Cmd_Argv(1) );

	start = Com_Milliseconds();

	while ( 1 ) {
		now = Com_Milliseconds();
		if ( ( now - start ) * 0.001 > s ) {
			break;
		}
	}
}


/*
=================
Com_Crash_f

A way to force a bus error for development reasons
=================
*/
static void Com_Crash_f( void ) {
	* ( volatile int * ) 0 = 0x12345678;
}


/*
==================
Com_ExecuteCfg

For controlling environment variables
==================
*/
static void Com_ExecuteCfg( void )
{
	Cbuf_ExecuteText(EXEC_NOW, "exec default.cfg\n");
	Cbuf_Execute(); // Always execute after exec to prevent text buffer overflowing

	if(!Com_SafeMode())
	{
		// skip the q3config.cfg and autoexec.cfg if "safe" is on the command line
		Cbuf_ExecuteText(EXEC_NOW, "exec " Q3CONFIG_CFG "\n");
		Cbuf_Execute();
		Cbuf_ExecuteText(EXEC_NOW, "exec autoexec.cfg\n");
		Cbuf_Execute();
	}
}


/*
==================
Com_GameRestart

Change to a new mod properly with cleaning up cvars before switching.
==================
*/
void Com_GameRestart(int checksumFeed, qboolean clientRestart)
{
	// make sure no recursion can be triggered
	if(!com_gameRestarting && com_fullyInitialized)
	{
		com_gameRestarting = qtrue;
#ifndef DEDICATED		
		if( clientRestart )
		{
			CL_Disconnect( qfalse );
			CL_ShutdownAll();
			CL_ClearMemory(); // Hunk_Clear(); // -EC- 
		}
#endif

		// Kill server if we have one
		if( com_sv_running->integer )
			SV_Shutdown("Game directory changed");

		FS_Restart(checksumFeed);
	
		// Clean out any user and VM created cvars
		Cvar_Restart(qtrue);
		Com_ExecuteCfg();
		
#ifndef DEDICATED
		// Restart sound subsystem so old handles are flushed
		//CL_Snd_Restart();
		if ( clientRestart )
			CL_StartHunkUsers();
#endif
		
		com_gameRestarting = qfalse;
	}
}


/*
==================
Com_GameRestart_f

Expose possibility to change current running mod to the user
==================
*/
static void Com_GameRestart_f( void )
{
	Cvar_Set( "fs_game", Cmd_Argv( 1 ) );

	Com_GameRestart( 0, qtrue );
}


// TTimo: centralizing the cl_cdkey stuff after I discovered a buffer overflow problem with the dedicated server version
//   not sure it's necessary to have different defaults for regular and dedicated, but I don't want to risk it
//   https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=470
#ifndef DEDICATED
char	cl_cdkey[34] = "                                ";
#else
char	cl_cdkey[34] = "123456789";
#endif

/*
=================
bool CL_CDKeyValidate
=================
*/
qboolean Com_CDKeyValidate( const char *key, const char *checksum ) {
#ifdef STANDALONE
	return qtrue;
#else
	char	ch;
	byte	sum;
	char	chs[10];
	int i, len;

	len = strlen(key);
	if( len != CDKEY_LEN ) {
		return qfalse;
	}

	if( checksum && strlen( checksum ) != CDCHKSUM_LEN ) {
		return qfalse;
	}

	sum = 0;
	// for loop gets rid of conditional assignment warning
	for (i = 0; i < len; i++) {
		ch = *key++;
		if (ch>='a' && ch<='z') {
			ch -= 32;
		}
		switch( ch ) {
		case '2':
		case '3':
		case '7':
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'G':
		case 'H':
		case 'J':
		case 'L':
		case 'P':
		case 'R':
		case 'S':
		case 'T':
		case 'W':
			sum += ch;
			continue;
		default:
			return qfalse;
		}
	}

	sprintf(chs, "%02x", sum);
	
	if (checksum && !Q_stricmp(chs, checksum)) {
		return qtrue;
	}

	if (!checksum) {
		return qtrue;
	}

	return qfalse;
#endif
}


/*
=================
Com_ReadCDKey
=================
*/
void Com_ReadCDKey( const char *filename ) {
	fileHandle_t	f;
	char			buffer[33];
	char			fbuffer[MAX_OSPATH];

	Com_sprintf( fbuffer, sizeof( fbuffer ), "%s/q3key", filename );

	FS_SV_FOpenFileRead( fbuffer, &f );
	if ( f == FS_INVALID_HANDLE ) {
		Q_strncpyz( cl_cdkey, "                ", 17 );
		return;
	}

	Com_Memset( buffer, 0, sizeof( buffer ) );

	FS_Read( buffer, 16, f );
	FS_FCloseFile( f );

	if ( Com_CDKeyValidate(buffer, NULL) ) {
		Q_strncpyz( cl_cdkey, buffer, 17 );
	} else {
		Q_strncpyz( cl_cdkey, "                ", 17 );
	}
}


/*
=================
Com_AppendCDKey
=================
*/
void Com_AppendCDKey( const char *filename ) {
	fileHandle_t	f;
	char			buffer[33];
	char			fbuffer[MAX_OSPATH];

	Com_sprintf(fbuffer, sizeof(fbuffer), "%s/q3key", filename);

	FS_SV_FOpenFileRead( fbuffer, &f );
	if ( f == FS_INVALID_HANDLE ) {
		Q_strncpyz( &cl_cdkey[16], "                ", 17 );
		return;
	}

	Com_Memset( buffer, 0, sizeof(buffer) );

	FS_Read( buffer, 16, f );
	FS_FCloseFile( f );

	if ( Com_CDKeyValidate(buffer, NULL)) {
		strcat( &cl_cdkey[16], buffer );
	} else {
		Q_strncpyz( &cl_cdkey[16], "                ", 17 );
	}
}


#ifndef DEDICATED // bk001204
/*
=================
Com_WriteCDKey
=================
*/
static void Com_WriteCDKey( const char *filename, const char *ikey ) {
	fileHandle_t	f;
	char			fbuffer[MAX_OSPATH];
	char			key[17];
#ifndef _WIN32
	mode_t			savedumask;
#endif

	Com_sprintf( fbuffer, sizeof(fbuffer), "%s/q3key", filename );

	Q_strncpyz( key, ikey, 17 );

	if( !Com_CDKeyValidate(key, NULL) ) {
		return;
	}

#ifndef _WIN32
	savedumask = umask(0077);
#endif
	f = FS_SV_FOpenFileWrite( fbuffer );
	if ( f == FS_INVALID_HANDLE ) {
		Com_Printf( "Couldn't write CD key to %s.\n", fbuffer );
		goto out;
	}

	FS_Write( key, 16, f );

	FS_Printf( f, Q_NEWLINE "// generated by quake, do not modify" Q_NEWLINE );
	FS_Printf( f, "// Do not give this file to ANYONE." Q_NEWLINE );
	FS_Printf( f, "// id Software and Activision will NOT ask you to send this file to them." Q_NEWLINE );

	FS_FCloseFile( f );
out:
#ifndef _WIN32
	umask(savedumask);
#else
	;
#endif
}
#endif


/*
** --------------------------------------------------------------------------------
**
** PROCESSOR STUFF
**
** --------------------------------------------------------------------------------
*/

#if defined _MSC_VER

static void CPUID( int func, unsigned int *regs )
{
#if _MSC_VER >= 1400
	__cpuid( regs, func );
#else
	__asm {
		mov edi,regs
		mov eax,[edi]
		cpuid
		mov [edi], eax
		mov [edi+4], ebx
		mov [edi+8], ecx
		mov [edi+12], edx
	}
#endif
}

#else

static void CPUID( int func, unsigned int *regs )
{
	__asm__ __volatile__( "cpuid" :
		"=a"(regs[0]),
		"=b"(regs[1]),
		"=c"(regs[2]),
		"=d"(regs[3]) :
		"a"(func) );
}
#endif

int Sys_GetProcessorId( char *vendor )
{
	unsigned int regs[4];

	// setup initial features
#if idx64
	CPU_Flags |= CPU_SSE | CPU_SSE2 | CPU_FCOM;
#else
	CPU_Flags = 0;
#endif

	// get CPU feature bits
	CPUID( 1, regs );

	// bit 15 of EDX denotes CMOV/FCMOV/FCOMI existence
	if ( regs[3] & ( 1 << 15 ) )
		CPU_Flags |= CPU_FCOM;

	// bit 23 of EDX denotes MMX existence
	if ( regs[3] & ( 1 << 23 ) )
		CPU_Flags |= CPU_MMX;

	// bit 25 of EDX denotes SSE existence
	if ( regs[3] & ( 1 << 25 ) )
		CPU_Flags |= CPU_SSE;

	// bit 26 of EDX denotes SSE2 existence
	if ( regs[3] & ( 1 << 26 ) )
		CPU_Flags |= CPU_SSE2;

	// bit 0 of ECX denotes SSE3 existence
	if ( regs[2] & ( 1 << 0 ) )
		CPU_Flags |= CPU_SSE3;

	if ( vendor ) {
#if idx64
		strcpy( vendor, "64-bit " );
		vendor += strlen( vendor );
#else
		vendor[0] = '\0';
#endif
		// get CPU vendor string
		CPUID( 0, regs );
		memcpy( vendor+0, (char*) &regs[1], 4 );
		memcpy( vendor+4, (char*) &regs[3], 4 );
		memcpy( vendor+8, (char*) &regs[2], 4 );
		vendor[12] = '\0'; vendor += 12;
		if ( CPU_Flags ) {
			// print features
#if !idx64	// do not print default 64-bit features in 32-bit mode
			strcat( vendor, " w/" );
			if ( CPU_Flags & CPU_FCOM )
				strcat( vendor, " CMOV" );
			if ( CPU_Flags & CPU_MMX )
				strcat( vendor, " MMX" );
			if ( CPU_Flags & CPU_SSE )
				strcat( vendor, " SSE" );
			if ( CPU_Flags & CPU_SSE2 )
				strcat( vendor, " SSE2" );
#endif
			//if ( CPU_Flags & CPU_SSE3 )
			//	strcat( vendor, " SSE3" );
		}
	}
	return 1;
}


/*
================
Sys_SnapVector
================
*/
#ifdef _MSC_VER
#include <intrin.h>
#if idx64
void Sys_SnapVector( float *vector ) 
{
	__m128 vf0, vf1, vf2;
	__m128i vi;
	DWORD mxcsr;

	mxcsr = _mm_getcsr();
	vf0 = _mm_setr_ps( vector[0], vector[1], vector[2], 0.0f );
	
	_mm_setcsr( mxcsr & ~0x6000 ); // enforce rounding mode to "round to nearest"

	vi = _mm_cvtps_epi32( vf0 );
	vf0 = _mm_cvtepi32_ps( vi );

	vf1 = _mm_shuffle_ps(vf0, vf0, _MM_SHUFFLE(1,1,1,1));
	vf2 = _mm_shuffle_ps(vf0, vf0, _MM_SHUFFLE(2,2,2,2));

	_mm_setcsr( mxcsr ); // restore rounding mode

	_mm_store_ss( &vector[0], vf0 );
	_mm_store_ss( &vector[1], vf1 );
	_mm_store_ss( &vector[2], vf2 );
}
#else // id386
void Sys_SnapVector( float *vector ) 
{
	static const DWORD cw037F = 0x037F;
	DWORD cwCurr;
__asm {
	fnstcw word ptr [cwCurr]
	mov ecx, vector
	fldcw word ptr [cw037F]

	fld   dword ptr[ecx+8]
	fistp dword ptr[ecx+8]
	fild  dword ptr[ecx+8]
	fstp  dword ptr[ecx+8]

	fld   dword ptr[ecx+4]
	fistp dword ptr[ecx+4]
	fild  dword ptr[ecx+4]
	fstp  dword ptr[ecx+4]

	fld   dword ptr[ecx+0]
	fistp dword ptr[ecx+0]
	fild  dword ptr[ecx+0]
	fstp  dword ptr[ecx+0]

	fldcw word ptr cwCurr
	}; // __asm
}
#endif // id386

#else // linux/mingw

#if idx64

void Sys_SnapVector( vec3_t vec )
{
	vec[0] = round(vec[0]);
	vec[1] = round(vec[1]);
	vec[2] = round(vec[2]);
}

#else // id386

#define QROUNDX87(src) \
	"flds " src "\n" \
	"fistpl " src "\n" \
	"fildl " src "\n" \
	"fstps " src "\n"

void Sys_SnapVector( vec3_t vector )
{
	static const unsigned short cw037F = 0x037F;
	unsigned short cwCurr;

	__asm__ volatile
	(
		"fnstcw %1\n" \
		"fldcw %2\n" \
		QROUNDX87("0(%0)")
		QROUNDX87("4(%0)")
		QROUNDX87("8(%0)")
		"fldcw %1\n" \
		:
		: "r" (vector), "m"(cwCurr), "m"(cw037F)
		: "memory", "st"
	);
}
#endif // id386
#endif // linux/mingw


/*
=================
Com_Init
=================
*/
void Com_Init( char *commandLine ) {
	const char *s;
	int	qport;

	Com_Printf( "%s %s %s\n", SVN_VERSION, PLATFORM_STRING, __DATE__ );

	if ( setjmp (abortframe) ) {
		Sys_Error ("Error during initialization");
	}

	// bk001129 - do this before anything else decides to push events
	Com_InitPushEvent();

	Com_InitSmallZoneMemory();
	Cvar_Init();

#if defined(_WIN32) && defined(_DEBUG)
	com_noErrorInterrupt = Cvar_Get( "com_noErrorInterrupt", "0", 0 );
#endif

#ifdef DEFAULT_GAME
	Cvar_Set( "fs_game", DEFAULT_GAME );
#endif

	// prepare enough of the subsystems to handle
	// cvar and command buffer management
	Com_ParseCommandLine( commandLine );

//	Swap_Init ();
	Cbuf_Init();

	// override anything from the config files with command line args
	Com_StartupVariable( NULL );

	Com_InitZoneMemory();
	Cmd_Init();

	// get the developer cvar set as early as possible
	Com_StartupVariable( "developer" );
	com_developer = Cvar_Get( "developer", "0", CVAR_TEMP );

	Com_StartupVariable( "vm_rtChecks" );
	vm_rtChecks = Cvar_Get( "vm_rtChecks", "15", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( vm_rtChecks, 
		"Runtime checks in compiled vm code, bitmask:\n 1 - program stack overflow\n" \
		" 2 - opcode stack overflow\n 4 - jump target range\n 8 - data read/write range" );

	// done early so bind command exists
	Com_InitKeyCommands();

	FS_InitFilesystem();

	Com_InitJournaling();

	Com_ExecuteCfg();

	// override anything from the config files with command line args
	Com_StartupVariable( NULL );

  // get dedicated here for proper hunk megs initialization
#ifdef DEDICATED
	com_dedicated = Cvar_Get( "dedicated", "1", CVAR_INIT );
	Cvar_CheckRange( com_dedicated, "1", "2", CV_INTEGER );
#else
	com_dedicated = Cvar_Get( "dedicated", "0", CVAR_LATCH );
	Cvar_CheckRange( com_dedicated, "0", "2", CV_INTEGER );
#endif
	// allocate the stack based hunk allocator
	Com_InitHunkMemory();

	// if any archived cvars are modified after this, we will trigger a writing
	// of the config file
	cvar_modifiedFlags &= ~CVAR_ARCHIVE;

	//
	// init commands and vars
	//
#ifndef DEDICATED
	com_maxfps = Cvar_Get( "com_maxfps", "125", 0 ); // try to force that in some light way
	com_maxfpsUnfocused = Cvar_Get( "com_maxfpsUnfocused", "60", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( com_maxfps, "0", "1000", CV_INTEGER );
	Cvar_CheckRange( com_maxfpsUnfocused, "0", "1000", CV_INTEGER );
	com_yieldCPU = Cvar_Get( "com_yieldCPU", "2", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( com_yieldCPU, "0", "4", CV_INTEGER );
#endif
	com_affinityMask = Cvar_Get( "com_affinityMask", "0", CVAR_ARCHIVE_ND );
	com_affinityMask->modified = qfalse;

	com_blood = Cvar_Get ("com_blood", "1", CVAR_ARCHIVE_ND );

	com_logfile = Cvar_Get ("logfile", "0", CVAR_TEMP );

	com_timescale = Cvar_Get( "timescale", "1", CVAR_CHEAT | CVAR_SYSTEMINFO );
	Cvar_CheckRange( com_timescale, "0", NULL, CV_FLOAT );
	com_fixedtime = Cvar_Get ("fixedtime", "0", CVAR_CHEAT);
	com_showtrace = Cvar_Get ("com_showtrace", "0", CVAR_CHEAT);
	com_viewlog = Cvar_Get( "viewlog", "0", 0 );
	com_speeds = Cvar_Get ("com_speeds", "0", 0);
	com_cameraMode = Cvar_Get ("com_cameraMode", "0", CVAR_CHEAT);

#ifndef DEDICATED	
	com_timedemo = Cvar_Get("timedemo", "0", CVAR_CHEAT);
	Cvar_CheckRange( com_timedemo, NULL, NULL, CV_BOOLEAN );
	cl_paused = Cvar_Get ("cl_paused", "0", CVAR_ROM);
	cl_packetdelay = Cvar_Get ("cl_packetdelay", "0", CVAR_CHEAT);
	com_cl_running = Cvar_Get ("cl_running", "0", CVAR_ROM);
#endif

	sv_paused = Cvar_Get ("sv_paused", "0", CVAR_ROM);
	sv_packetdelay = Cvar_Get ("sv_packetdelay", "0", CVAR_CHEAT);
	com_sv_running = Cvar_Get ("sv_running", "0", CVAR_ROM);

	com_buildScript = Cvar_Get( "com_buildScript", "0", 0 );

	Cvar_Get( "com_errorMessage", "", CVAR_ROM | CVAR_NORESTART );

#ifndef DEDICATED
	com_introPlayed = Cvar_Get( "com_introplayed", "0", CVAR_ARCHIVE );
	com_skipIdLogo  = Cvar_Get( "com_skipIdLogo", "0", CVAR_ARCHIVE );
#endif

	if ( com_dedicated->integer ) {
		if ( !com_viewlog->integer ) {
			Cvar_Set( "viewlog", "1" );
		}
		gw_minimized = qtrue;
	} else {
		gw_minimized = qfalse;
	}

	if ( com_developer->integer ) {
		Cmd_AddCommand( "error", Com_Error_f );
		Cmd_AddCommand( "crash", Com_Crash_f );
		Cmd_AddCommand( "freeze", Com_Freeze_f );
	}

	Cmd_AddCommand( "quit", Com_Quit_f );
	Cmd_AddCommand( "changeVectors", MSG_ReportChangeVectors_f );
	Cmd_AddCommand( "writeconfig", Com_WriteConfig_f );
	Cmd_SetCommandCompletionFunc( "writeconfig", Cmd_CompleteWriteCfgName );
	Cmd_AddCommand( "game_restart", Com_GameRestart_f );

	s = va( "%s %s %s", Q3_VERSION, PLATFORM_STRING, __DATE__ );
	com_version = Cvar_Get( "version", s, CVAR_PROTECTED | CVAR_ROM | CVAR_SERVERINFO );

#ifndef DEDICATED
	// for now - this will be used to inform server about q3msgboom fix
	Cvar_Get( "client", Q3_VERSION, CVAR_PROTECTED | CVAR_ROM | CVAR_USERINFO );
#endif

	// this cvar is the single entry point of the entire extension system
	Cvar_Get( "//trap_GetValue", va( "%i", COM_TRAP_GETVALUE ), CVAR_PROTECTED | CVAR_ROM );

	Sys_Init();

#if defined (id386) || defined (idx64)
	// CPU detection
	Cvar_Get( "sys_cpustring", "detect", CVAR_PROTECTED | CVAR_ROM | CVAR_NORESTART );
	if ( !Q_stricmp( Cvar_VariableString( "sys_cpustring" ), "detect" ) )
	{
		static char vendor[128];
		Com_Printf( "...detecting CPU, found " );
		Sys_GetProcessorId( vendor );
		Cvar_Set( "sys_cpustring", vendor );
	}
	Com_Printf( "%s\n", Cvar_VariableString( "sys_cpustring" ) );
#endif

	if ( com_affinityMask->integer )
		Sys_SetAffinityMask( com_affinityMask->integer );

	// Pick a random port value
	Com_RandomBytes( (byte*)&qport, sizeof( qport ) );
	Netchan_Init( qport & 0xffff );

	VM_Init();
	SV_Init();

	com_dedicated->modified = qfalse;

#ifndef DEDICATED
	if ( !com_dedicated->integer ) {
		CL_Init();
		// Sys_ShowConsole( com_viewlog->integer, qfalse ); // moved down
	}
#endif

	// set com_frameTime so that if a map is started on the
	// command line it will still be able to count on com_frameTime
	// being random enough for a serverid
	com_frameTime = Com_Milliseconds();

	// add + commands from command line
	if ( !Com_AddStartupCommands() ) {
		// if the user didn't give any commands, run default action
		if ( !com_dedicated->integer ) {
#ifndef DEDICATED
			if ( !com_skipIdLogo || !com_skipIdLogo->integer )
				Cbuf_AddText( "cinematic idlogo.RoQ\n" );
			if( !com_introPlayed->integer ) {
				Cvar_Set( com_introPlayed->name, "1" );
				Cvar_Set( "nextmap", "cinematic intro.RoQ" );
			}
#endif
		}
	}

#ifndef DEDICATED
	// start in full screen ui mode
	Cvar_Set( "r_uiFullScreen", "1" );
	CL_StartHunkUsers();
#endif

	if ( !com_errorEntered )
		Sys_ShowConsole( com_viewlog->integer, qfalse );

#ifndef DEDICATED
	// make sure single player is off by default
	Cvar_Set( "ui_singlePlayerActive", "0" );
#endif

	com_fullyInitialized = qtrue;

	Com_Printf( "--- Common Initialization Complete ---\n" );
}


//==================================================================

static void Com_WriteConfigToFile( const char *filename ) {
	fileHandle_t	f;

	f = FS_FOpenFileWrite( filename );
	if ( f == FS_INVALID_HANDLE ) {
		Com_Printf( "Couldn't write %s.\n", filename );
		return;
	}

	FS_Printf( f, "// generated by quake, do not modify" Q_NEWLINE );
#ifndef DEDICATED
	Key_WriteBindings( f );
#endif
	Cvar_WriteVariables( f );
	FS_FCloseFile( f );
}


/*
===============
Com_WriteConfiguration

Writes key bindings and archived cvars to config file if modified
===============
*/
static void Com_WriteConfiguration( void ) {
#ifndef DEDICATED
	const char *basegame;
	const char *gamedir;
#endif
	// if we are quiting without fully initializing, make sure
	// we don't write out anything
	if ( !com_fullyInitialized ) {
		return;
	}

	if ( !(cvar_modifiedFlags & CVAR_ARCHIVE ) ) {
		return;
	}
	cvar_modifiedFlags &= ~CVAR_ARCHIVE;

	Com_WriteConfigToFile( Q3CONFIG_CFG );

#ifndef DEDICATED
	gamedir = Cvar_VariableString( "fs_game" );
	basegame = Cvar_VariableString( "fs_basegame" );
	if ( UI_usesUniqueCDKey() && gamedir[0] && Q_stricmp( basegame, gamedir ) ) {
		Com_WriteCDKey( gamedir, &cl_cdkey[16] );
	} else {
		Com_WriteCDKey( basegame, cl_cdkey );
	}
#endif
}


/*
===============
Com_WriteConfig_f

Write the config file to a specific name
===============
*/
static void Com_WriteConfig_f( void ) {
	char	filename[MAX_QPATH];
	const char *ext;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: writeconfig <filename>\n" );
		return;
	}

	Q_strncpyz( filename, Cmd_Argv(1), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );

	if ( !FS_AllowedExtension( filename, qfalse, &ext ) ) {
		Com_Printf( "%s: Invalid filename extension: '%s'.\n", __func__, ext );
		return;
	}

	Com_Printf( "Writing %s.\n", filename );
	Com_WriteConfigToFile( filename );
}


/*
================
Com_ModifyMsec
================
*/
static int Com_ModifyMsec( int msec ) {
	int		clampTime;

	//
	// modify time for debugging values
	//
	if ( com_fixedtime->integer ) {
		msec = com_fixedtime->integer;
	} else if ( com_timescale->value ) {
		msec *= com_timescale->value;
	} else if (com_cameraMode->integer) {
		msec *= com_timescale->value;
	}
	
	// don't let it scale below 1 msec
	if ( msec < 1 && com_timescale->value) {
		msec = 1;
	}

	if ( com_dedicated->integer ) {
		// dedicated servers don't want to clamp for a much longer
		// period, because it would mess up all the client's views
		// of time.
		if (com_sv_running->integer && msec > 500)
			Com_Printf( "Hitch warning: %i msec frame time\n", msec );

		clampTime = 5000;
	} else 
	if ( !com_sv_running->integer ) {
		// clients of remote servers do not want to clamp time, because
		// it would skew their view of the server's time temporarily
		clampTime = 5000;
	} else {
		// for local single player gaming
		// we may want to clamp the time to prevent players from
		// flying off edges when something hitches.
		clampTime = 200;
	}

	if ( msec > clampTime ) {
		msec = clampTime;
	}

	return msec;
}


/*
=================
Com_TimeVal
=================
*/
static int Com_TimeVal( int minMsec )
{
	int timeVal;

	timeVal = Sys_Milliseconds() - com_frameTime;

	if ( timeVal >= minMsec )
		timeVal = 0;
	else
		timeVal = minMsec - timeVal;

	return timeVal;
}


/*
=================
Com_Frame
=================
*/
void Com_Frame( qboolean noDelay ) {

	static int lastTime = 0;
#ifndef DEDICATED
	static int bias = 0;
#endif
	int	msec, minMsec;
	int	sleepMsec;
	int	timeVal;
	int	timeValSV;

	int	timeBeforeFirstEvents;
	int	timeBeforeServer;
	int	timeBeforeEvents;
	int	timeBeforeClient;
	int	timeAfter;

	if ( setjmp( abortframe ) ) {
		return;			// an ERR_DROP was thrown
	}

	minMsec = 0; // silent compiler warning

	// bk001204 - init to zero.
	//  also:  might be clobbered by `longjmp' or `vfork'
	timeBeforeFirstEvents = 0;
	timeBeforeServer = 0;
	timeBeforeEvents = 0;
	timeBeforeClient = 0;
	timeAfter = 0;

	// write config file if anything changed
	Com_WriteConfiguration();

	// if "viewlog" has been modified, show or hide the log console
	if ( com_viewlog->modified ) {
		if ( !com_dedicated->integer ) {
			Sys_ShowConsole( com_viewlog->integer, qfalse );
		}
		com_viewlog->modified = qfalse;
	}

	if ( com_affinityMask->modified ) {
		Cvar_Get( "com_affinityMask", "0", CVAR_ARCHIVE );
		com_affinityMask->modified = qfalse;
		Sys_SetAffinityMask( com_affinityMask->integer );
	}

	//
	// main event loop
	//
	if ( com_speeds->integer ) {
		timeBeforeFirstEvents = Sys_Milliseconds();
	}
	
	// we may want to spin here if things are going too fast
	if ( com_dedicated->integer ) {
		minMsec = SV_FrameMsec();
#ifndef DEDICATED
		bias = 0;
#endif
	} else {
#ifndef DEDICATED
		if ( noDelay ) {
			minMsec = 0;
			bias = 0;
		} else {
			if ( !gw_active && com_maxfpsUnfocused->integer > 0 )
				minMsec = 1000 / com_maxfpsUnfocused->integer;
			else
			if ( com_maxfps->integer > 0 )
				minMsec = 1000 / com_maxfps->integer;
			else
				minMsec = 1;

			timeVal = com_frameTime - lastTime;
			bias += timeVal - minMsec;
			
			if ( bias > minMsec / 2 )
				bias = minMsec / 2;

			// Adjust minMsec if previous frame took too long to render so
			// that framerate is stable at the requested value.
			minMsec -= bias;
		}
#endif
	}

	// waiting for incoming packets
	if ( noDelay == qfalse )
	do {
		if ( com_sv_running->integer ) {
			timeValSV = SV_SendQueuedPackets();
			timeVal = Com_TimeVal( minMsec );
			if ( timeValSV < timeVal )
				timeVal = timeValSV;
		} else {
			timeVal = Com_TimeVal( minMsec );
		}
		sleepMsec = timeVal;
#ifndef DEDICATED
		if ( !gw_minimized && timeVal > com_yieldCPU->integer )
			sleepMsec = com_yieldCPU->integer;
		if ( timeVal > sleepMsec )
			Com_EventLoop();
#endif
		NET_Sleep( sleepMsec, -500 );
	} while( Com_TimeVal( minMsec ) );
	
	lastTime = com_frameTime;
	com_frameTime = Com_EventLoop();
	
	msec = com_frameTime - lastTime;

	Cbuf_Execute();

	// mess with msec if needed
	msec = Com_ModifyMsec( msec );

	//
	// server side
	//
	if ( com_speeds->integer ) {
		timeBeforeServer = Sys_Milliseconds();
	}

	SV_Frame( msec );

	// if "dedicated" has been modified, start up
	// or shut down the client system.
	// Do this after the server may have started,
	// but before the client tries to auto-connect
	if ( com_dedicated->modified ) {
		// get the latched value
		Cvar_Get( "dedicated", "0", 0 );
		com_dedicated->modified = qfalse;
		if ( !com_dedicated->integer ) {
			SV_Shutdown( "dedicated set to 0" );
			SV_RemoveDedicatedCommands();
#ifndef DEDICATED
			CL_Init();
#endif
			Sys_ShowConsole( com_viewlog->integer, qfalse );
#ifndef DEDICATED
			gw_minimized = qfalse;
			CL_StartHunkUsers();
#endif
		} else {
#ifndef DEDICATED
			CL_Shutdown( "", qfalse );
			CL_ClearMemory();
#endif
			Sys_ShowConsole( 1, qtrue );
			SV_AddDedicatedCommands();
			gw_minimized = qtrue;
		}
	}

#ifdef DEDICATED
	if ( com_speeds->integer ) {
		timeAfter = Sys_Milliseconds ();
		timeBeforeEvents = timeAfter;
		timeBeforeClient = timeAfter;
	}
#else
	//
	// client system
	//
	if ( !com_dedicated->integer ) {
		//
		// run event loop a second time to get server to client packets
		// without a frame of latency
		//
		if ( com_speeds->integer ) {
			timeBeforeEvents = Sys_Milliseconds();
		}
		Com_EventLoop();
		Cbuf_Execute();

		//
		// client side
		//
		if ( com_speeds->integer ) {
			timeBeforeClient = Sys_Milliseconds();
		}

		CL_Frame( msec );

		if ( com_speeds->integer ) {
			timeAfter = Sys_Milliseconds();
		}
	}
#endif

	NET_FlushPacketQueue();

	//
	// report timing information
	//
	if ( com_speeds->integer ) {
		int			all, sv, ev, cl;

		all = timeAfter - timeBeforeServer;
		sv = timeBeforeEvents - timeBeforeServer;
		ev = timeBeforeServer - timeBeforeFirstEvents + timeBeforeClient - timeBeforeEvents;
		cl = timeAfter - timeBeforeClient;
		sv -= time_game;
		cl -= time_frontend + time_backend;

		Com_Printf ("frame:%i all:%3i sv:%3i ev:%3i cl:%3i gm:%3i rf:%3i bk:%3i\n", 
					 com_frameNumber, all, sv, ev, cl, time_game, time_frontend, time_backend );
	}	

	//
	// trace optimization tracking
	//
	if ( com_showtrace->integer ) {
	
		extern	int c_traces, c_brush_traces, c_patch_traces;
		extern	int	c_pointcontents;

		Com_Printf ("%4i traces  (%ib %ip) %4i points\n", c_traces,
			c_brush_traces, c_patch_traces, c_pointcontents);
		c_traces = 0;
		c_brush_traces = 0;
		c_patch_traces = 0;
		c_pointcontents = 0;
	}

	com_frameNumber++;
}


/*
=================
Com_Shutdown
=================
*/
static void Com_Shutdown( void ) {
	if ( logfile != FS_INVALID_HANDLE ) {
		FS_FCloseFile( logfile );
		logfile = FS_INVALID_HANDLE;
	}

	if ( com_journalFile != FS_INVALID_HANDLE ) {
		FS_FCloseFile( com_journalFile );
		com_journalFile = FS_INVALID_HANDLE;
	}

	if ( com_journalDataFile != FS_INVALID_HANDLE ) {
		FS_FCloseFile( com_journalDataFile );
		com_journalDataFile = FS_INVALID_HANDLE;
	}
}

//------------------------------------------------------------------------


/*
===========================================
command line completion
===========================================
*/

/*
==================
Field_Clear
==================
*/
void Field_Clear( field_t *edit ) {
	memset( edit->buffer, 0, sizeof( edit->buffer ) );
	edit->cursor = 0;
	edit->scroll = 0;
}

static const char *completionString;
static char shortestMatch[MAX_TOKEN_CHARS];
static int	matchCount;
// field we are working on, passed to Field_AutoComplete(&g_consoleCommand for instance)
static field_t *completionField;

/*
===============
FindMatches
===============
*/
static void FindMatches( const char *s ) {
	int		i, n;

	if ( Q_stricmpn( s, completionString, strlen( completionString ) ) ) {
		return;
	}
	matchCount++;
	if ( matchCount == 1 ) {
		Q_strncpyz( shortestMatch, s, sizeof( shortestMatch ) );
		return;
	}

	n = (int)strlen(s);
	// cut shortestMatch to the amount common with s
	for ( i = 0 ; shortestMatch[i] ; i++ ) {
		if ( i >= n ) {
			shortestMatch[i] = '\0';
			break;
		}

		if ( tolower(shortestMatch[i]) != tolower(s[i]) ) {
			shortestMatch[i] = '\0';
		}
	}
}


/*
===============
PrintMatches
===============
*/
static void PrintMatches( const char *s ) {
	if ( !Q_stricmpn( s, shortestMatch, strlen( shortestMatch ) ) ) {
		Com_Printf( "    %s\n", s );
	}
}


/*
===============
PrintCvarMatches
===============
*/
static void PrintCvarMatches( const char *s ) {
	char value[ TRUNCATE_LENGTH ];

	if ( !Q_stricmpn( s, shortestMatch, strlen( shortestMatch ) ) ) {
		Com_TruncateLongString( value, Cvar_VariableString( s ) );
		Com_Printf( "    %s = \"%s\"\n", s, value );
	}
}


/*
===============
Field_FindFirstSeparator
===============
*/
static char *Field_FindFirstSeparator( char *s )
{
	char c;
	while ( (c = *s) != '\0' ) {
		if ( c == ';' )
			return s;
		s++;
	}
	return NULL;
}


/*
===============
Field_Complete
===============
*/
static qboolean Field_Complete( void )
{
	int completionOffset;

	if( matchCount == 0 )
		return qtrue;

	completionOffset = strlen( completionField->buffer ) - strlen( completionString );

	Q_strncpyz( &completionField->buffer[ completionOffset ], shortestMatch,
		sizeof( completionField->buffer ) - completionOffset );

	completionField->cursor = strlen( completionField->buffer );

	if( matchCount == 1 )
	{
		Q_strcat( completionField->buffer, sizeof( completionField->buffer ), " " );
		completionField->cursor++;
		return qtrue;
	}

	Com_Printf( "]%s\n", completionField->buffer );
	
	return qfalse;
}


/*
===============
Field_CompleteKeyname
===============
*/
void Field_CompleteKeyname( void )
{
	matchCount = 0;
	shortestMatch[ 0 ] = '\0';

	Key_KeynameCompletion( FindMatches );

	if( !Field_Complete( ) )
		Key_KeynameCompletion( PrintMatches );
}


/*
===============
Field_CompleteFilename
===============
*/
void Field_CompleteFilename( const char *dir, const char *ext, qboolean stripExt, int flags )
{
	matchCount = 0;
	shortestMatch[ 0 ] = '\0';

	FS_FilenameCompletion( dir, ext, stripExt, FindMatches, flags );

	if ( !Field_Complete() )
		FS_FilenameCompletion( dir, ext, stripExt, PrintMatches, flags );
}


/*
===============
Field_CompleteCommand
===============
*/
void Field_CompleteCommand( char *cmd, qboolean doCommands, qboolean doCvars )
{
	int		completionArgument = 0;

	// Skip leading whitespace and quotes
	cmd = Com_SkipCharset( cmd, " \"" );

	Cmd_TokenizeStringIgnoreQuotes( cmd );
	completionArgument = Cmd_Argc( );

	// If there is trailing whitespace on the cmd
	if( *( cmd + strlen( cmd ) - 1 ) == ' ' )
	{
		completionString = "";
		completionArgument++;
	}
	else
		completionString = Cmd_Argv( completionArgument - 1 );

#ifndef DEDICATED
	// Unconditionally add a '\' to the start of the buffer
	if( completionField->buffer[ 0 ] &&
			completionField->buffer[ 0 ] != '\\' )
		{
		if( completionField->buffer[ 0 ] != '/' )
		{
			// Buffer is full, refuse to complete
			if( strlen( completionField->buffer ) + 1 >=
				sizeof( completionField->buffer ) )
				return;

			memmove( &completionField->buffer[ 1 ],
				&completionField->buffer[ 0 ],
				strlen( completionField->buffer ) + 1 );
			completionField->cursor++;
			}

		completionField->buffer[ 0 ] = '\\';
			}
#endif

	if( completionArgument > 1 )
			{
		const char *baseCmd = Cmd_Argv( 0 );
		char *p;

#ifndef DEDICATED
		// This should always be true
		if( baseCmd[ 0 ] == '\\' || baseCmd[ 0 ] == '/' )
			baseCmd++;
#endif

		if( ( p = Field_FindFirstSeparator( cmd ) ) != NULL )
 			Field_CompleteCommand( p + 1, qtrue, qtrue ); // Compound command
		else
 			Cmd_CompleteArgument( baseCmd, cmd, completionArgument ); 
	}
	else
	{
		if( completionString[0] == '\\' || completionString[0] == '/' )
			completionString++;

		matchCount = 0;
		shortestMatch[ 0 ] = '\0';

		if( completionString[0] == '\0' )
			return;

		if( doCommands )
			Cmd_CommandCompletion( FindMatches );

		if( doCvars )
			Cvar_CommandCompletion( FindMatches );

		if( !Field_Complete( ) )
		{
		// run through again, printing matches
		if( doCommands )
			Cmd_CommandCompletion( PrintMatches );

		if( doCvars )
			Cvar_CommandCompletion( PrintCvarMatches );
	}
	}
}


/*
===============
Field_AutoComplete

Perform Tab expansion
===============
*/
void Field_AutoComplete( field_t *field )
{
	completionField = field;

	Field_CompleteCommand( completionField->buffer, qtrue, qtrue );
}


/*
==================
Com_RandomBytes

fills string array with len random bytes, preferably from the OS randomizer
==================
*/
void Com_RandomBytes( byte *string, int len )
{
	int i;

	if( Sys_RandomBytes( string, len ) )
		return;

	Com_Printf( "Com_RandomBytes: using weak randomization\n" );
	srand( time( NULL ) );
	for( i = 0; i < len; i++ )
		string[i] = (unsigned char)( rand() % 256 );
}
