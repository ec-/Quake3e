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
#include <signal.h>

#ifdef _DEBUG
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#ifndef DEDICATED
#include "../renderer/tr_local.h"
#endif

static qboolean signalcaught = qfalse;

extern void Sys_Exit( int code );

static void signal_handler( int sig )
{
	char msg[32];

	if ( signalcaught == qtrue )
	{
		printf( "DOUBLE SIGNAL FAULT: Received signal %d, exiting...\n", sig );
		Sys_Exit( 1 ); // abstraction
	}

	printf( "Received signal %d, exiting...\n", sig );

#ifdef _DEBUG
	if ( sig == SIGSEGV || sig == SIGILL || sig == SIGBUS )
	{
		void *syms[10];
		const size_t size = backtrace( syms, ARRAY_LEN( syms ) );
		backtrace_symbols_fd( syms, size, STDERR_FILENO );
	}
#endif

	signalcaught = qtrue;
	sprintf( msg, "Signal caught (%d)", sig );
#ifndef DEDICATED
	CL_Shutdown( msg, qtrue );
#endif
	SV_Shutdown( msg );
	Sys_Exit( 0 ); // send a 0 to avoid DOUBLE SIGNAL FAULT
}


void InitSig( void )
{
	signal( SIGINT, SIG_IGN );
	signal( SIGHUP, signal_handler );
	signal( SIGQUIT, signal_handler );
	signal( SIGILL, signal_handler );
	signal( SIGTRAP, signal_handler );
	signal( SIGIOT, signal_handler );
	signal( SIGBUS, signal_handler );
	signal( SIGFPE, signal_handler );
	signal( SIGSEGV, signal_handler );
	signal( SIGTERM, signal_handler );
}
