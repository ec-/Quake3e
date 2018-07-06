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
// qcommon.h -- definitions common between client and server, but not game.or ref modules
#ifndef _QCOMMON_H_
#define _QCOMMON_H_

#include "../qcommon/cm_public.h"

//Ignore __attribute__ on non-gcc platforms
#ifndef __GNUC__
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif

/* C99 defines __func__ */
#if __STDC_VERSION__ < 199901L 
#if __GNUC__ >= 2 || _MSC_VER >= 1300 
#define __func__ __FUNCTION__ 
#else 
#define __func__ "(unknown)" 
#endif
#endif

// stringify macro
#define XSTRING(x)	STRING(x)
#define STRING(x)	#x

//#define	PRE_RELEASE_DEMO
#define DELAY_WRITECONFIG

//============================================================================

//
// msg.c
//
typedef struct {
	qboolean	allowoverflow;	// if false, do a Com_Error
	qboolean	overflowed;		// set to true if the buffer size failed (with allowoverflow set)
	qboolean	oob;			// raw out-of-band operation, no static huffman encoding/decoding
	byte	*data;
	int		maxsize;
	int		maxbits;			// maxsize in bits, for overflow checks
	int		cursize;
	int		readcount;
	int		bit;				// for bitwise reads and writes
} msg_t;

void MSG_Init( msg_t *buf, byte *data, int length );
void MSG_InitOOB( msg_t *buf, byte *data, int length );
void MSG_Clear( msg_t *buf );
void MSG_WriteData( msg_t *buf, const void *data, int length );
void MSG_Bitstream( msg_t *buf );

// TTimo
// copy a msg_t in case we need to store it as is for a bit
// (as I needed this to keep an msg_t from a static var for later use)
// sets data buffer as MSG_Init does prior to do the copy
void MSG_Copy(msg_t *buf, byte *data, int length, msg_t *src);

struct usercmd_s;
struct entityState_s;
struct playerState_s;

void MSG_WriteBits( msg_t *msg, int value, int bits );

void MSG_WriteChar (msg_t *sb, int c);
void MSG_WriteByte (msg_t *sb, int c);
void MSG_WriteShort (msg_t *sb, int c);
void MSG_WriteLong (msg_t *sb, int c);
void MSG_WriteFloat (msg_t *sb, float f);
void MSG_WriteString (msg_t *sb, const char *s);
void MSG_WriteBigString (msg_t *sb, const char *s);
void MSG_WriteAngle16 (msg_t *sb, float f);
int MSG_HashKey(const char *string, int maxlen);

void	MSG_BeginReading (msg_t *sb);
void	MSG_BeginReadingOOB(msg_t *sb);

int		MSG_ReadBits( msg_t *msg, int bits );

int		MSG_ReadChar (msg_t *sb);
int		MSG_ReadByte (msg_t *sb);
int		MSG_ReadShort (msg_t *sb);
int		MSG_ReadLong (msg_t *sb);
float	MSG_ReadFloat (msg_t *sb);
const char *MSG_ReadString (msg_t *sb);
const char *MSG_ReadBigString (msg_t *sb);
const char *MSG_ReadStringLine (msg_t *sb);
float	MSG_ReadAngle16 (msg_t *sb);
void	MSG_ReadData (msg_t *sb, void *buffer, int size);

void MSG_WriteDeltaUsercmdKey( msg_t *msg, int key, const usercmd_t *from, const usercmd_t *to );
void MSG_ReadDeltaUsercmdKey( msg_t *msg, int key, const usercmd_t *from, usercmd_t *to );

void MSG_WriteDeltaEntity( msg_t *msg, const entityState_t *from, const entityState_t *to, qboolean force );
void MSG_ReadDeltaEntity( msg_t *msg, const entityState_t *from, entityState_t *to, int number );

void MSG_WriteDeltaPlayerstate( msg_t *msg, const playerState_t *from, const playerState_t *to );
void MSG_ReadDeltaPlayerstate( msg_t *msg, const playerState_t *from, playerState_t *to );

void MSG_ReportChangeVectors_f( void );

//============================================================================

/*
==============================================================

NET

==============================================================
*/

#define NET_ENABLEV4            0x01
#define NET_ENABLEV6            0x02
// if this flag is set, always attempt ipv6 connections instead of ipv4 if a v6 address is found.
#define NET_PRIOV6              0x04
// disables ipv6 multicast support if set.
#define NET_DISABLEMCAST        0x08


#define	PACKET_BACKUP	32	// number of old messages that must be kept on client and
							// server for delta comrpession and ping estimation
#define	PACKET_MASK		(PACKET_BACKUP-1)

#define	MAX_PACKET_USERCMDS		32		// max number of usercmd_t in a packet

#define	MAX_SNAPSHOT_ENTITIES	256

#define	PORT_ANY			-1

#define	MAX_RELIABLE_COMMANDS	64			// max string commands buffered for restransmit

typedef enum {
	NA_BAD = 0,					// an address lookup failed
	NA_BOT,
	NA_LOOPBACK,
	NA_BROADCAST,
	NA_IP,
	NA_IP6,
	NA_MULTICAST6,
	NA_UNSPEC
} netadrtype_t;


typedef enum {
	NS_CLIENT,
	NS_SERVER
} netsrc_t;


#define NET_ADDRSTRMAXLEN 48	// maximum length of an IPv6 address string including trailing '\0'

typedef struct {
	netadrtype_t	type;
	union {
		byte	_4[4];
		byte	_6[16];
	} ipv;
	unsigned short	port;
	unsigned long	scope_id;	// Needed for IPv6 link-local addresses
} netadr_t;

void		NET_Init( void );
void		NET_Shutdown( void );
void		NET_FlushPacketQueue(void);
void		NET_SendPacket( netsrc_t sock, int length, const void *data, const netadr_t *to );
void		QDECL NET_OutOfBandPrint( netsrc_t net_socket, const netadr_t *adr, const char *format, ...) __attribute__ ((format (printf, 3, 4)));
void		NET_OutOfBandCompress( netsrc_t sock, const netadr_t *adr, const byte *data, int len );

qboolean	NET_CompareAdr( const netadr_t *a, const netadr_t *b );
qboolean	NET_CompareBaseAdrMask( const netadr_t *a, const netadr_t *b, unsigned int netmask );
qboolean	NET_CompareBaseAdr( const netadr_t *a, const netadr_t *b );
qboolean	NET_IsLocalAddress( const netadr_t *adr );
const char	*NET_AdrToString( const netadr_t *a );
const char	*NET_AdrToStringwPort( const netadr_t *a );
int         NET_StringToAdr( const char *s, netadr_t *a, netadrtype_t family );
qboolean	NET_GetLoopPacket( netsrc_t sock, netadr_t *net_from, msg_t *net_message );
void		NET_JoinMulticast6( void );
void		NET_LeaveMulticast6( void );
qboolean	NET_Sleep( int msec, int usec_bias );

#define	MAX_PACKETLEN	1400	// max size of a network packet

#define	MAX_MSGLEN		16384	// max length of a message, which may
								// be fragmented into multiple packets

#define	MAX_MSGLEN_BUF	(MAX_MSGLEN+8)	// real buffer size that we need to allocate
										// to safely handle overflows

#define MAX_DOWNLOAD_WINDOW		48	// ACK window of 48 download chunks. Cannot set this higher, or clients
						// will overflow the reliable commands buffer
#define MAX_DOWNLOAD_BLKSIZE		1024	// 896 byte block chunks

#define NETCHAN_GENCHECKSUM(challenge, sequence) ((challenge) ^ ((sequence) * (challenge)))

/*
Netchan handles packet fragmentation and out of order / duplicate suppression
*/

typedef struct {
	netsrc_t	sock;

	int			dropped;			// between last packet and previous

	netadr_t	remoteAddress;
	int			qport;				// qport value to write when transmitting

	// sequencing variables
	int			incomingSequence;
	int			outgoingSequence;

	// incoming fragment assembly buffer
	int			fragmentSequence;
	int			fragmentLength;	
	byte		fragmentBuffer[MAX_MSGLEN];

	// outgoing fragment buffer
	// we need to space out the sending of large fragmented messages
	qboolean	unsentFragments;
	int			unsentFragmentStart;
	int			unsentLength;
	byte		unsentBuffer[MAX_MSGLEN];

	int			challenge;
	int			lastSentTime;
	int			lastSentSize;

	qboolean	compat; // ioq3 extension
	qboolean	isLANAddress;

} netchan_t;

void Netchan_Init( int qport );
void Netchan_Setup( netsrc_t sock, netchan_t *chan, const netadr_t *adr, int port, int challenge, qboolean compat );

void Netchan_Transmit( netchan_t *chan, int length, const byte *data );
void Netchan_TransmitNextFragment( netchan_t *chan );

qboolean Netchan_Process( netchan_t *chan, msg_t *msg );


/*
==============================================================

PROTOCOL

==============================================================
*/

#define	PROTOCOL_VERSION		68
// new protocol with UDP spoofing protection:
#define	NEW_PROTOCOL_VERSION	71
// 1.31 - 67


// maintain a list of compatible protocols for demo playing
// NOTE: that stuff only works with two digits protocols
extern const int demo_protocols[];

#define	UPDATE_SERVER_NAME	"update.quake3arena.com"
// override on command line, config files etc.
#ifndef MASTER_SERVER_NAME
#define MASTER_SERVER_NAME	"master.quake3arena.com"
#endif
#ifndef AUTHORIZE_SERVER_NAME
#define	AUTHORIZE_SERVER_NAME	"authorize.quake3arena.com"
#endif

#define	PORT_MASTER			27950
#define	PORT_UPDATE			27951
#ifndef PORT_AUTHORIZE
#define	PORT_AUTHORIZE		27952
#endif
#define	PORT_SERVER			27960
#define	NUM_SERVER_PORTS	4		// broadcast scan this many ports after
									// PORT_SERVER so a single machine can
									// run multiple servers


// the svc_strings[] array in cl_parse.c should mirror this
//
// server to client
//
enum svc_ops_e {
	svc_bad,
	svc_nop,
	svc_gamestate,
	svc_configstring,			// [short] [string] only in gamestate messages
	svc_baseline,				// only in gamestate messages
	svc_serverCommand,			// [string] to be executed by client game module
	svc_download,				// [short] size [size bytes]
	svc_snapshot,
	svc_EOF,

	// new commands, supported only by ioquake3 protocol but not legacy
	svc_voipSpeex,     // not wrapped in USE_VOIP, so this value is reserved.
	svc_voipOpus,      //
};


//
// client to server
//
enum clc_ops_e {
	clc_bad,
	clc_nop, 		
	clc_move,				// [[usercmd_t]
	clc_moveNoDelta,		// [[usercmd_t]
	clc_clientCommand,		// [string] message
	clc_EOF,

	// new commands, supported only by ioquake3 protocol but not legacy
	clc_voipSpeex,   // not wrapped in USE_VOIP, so this value is reserved.
	clc_voipOpus,    //
};

/*
==============================================================

VIRTUAL MACHINE

==============================================================
*/
typedef struct vm_s vm_t;

typedef enum {
	VMI_NATIVE,
	VMI_BYTECODE,
	VMI_COMPILED
} vmInterpret_t;

typedef enum {
	TRAP_MEMSET = 100,
	TRAP_MEMCPY,
	TRAP_STRNCPY,
	TRAP_SIN,
	TRAP_COS,
	TRAP_ATAN2,
	TRAP_SQRT,
} sharedTraps_t;

typedef enum {
	VM_BAD = -1,
	VM_GAME = 0,
#ifndef USE_DEDICATED
	VM_CGAME,
	VM_UI,
#endif
	VM_COUNT
} vmIndex_t;

void	VM_Init( void );
vm_t	*VM_Create( vmIndex_t index, syscall_t systemCalls, dllSyscall_t dllSyscalls, const int *vmMainArgs, vmInterpret_t interpret );

// module should be bare: "cgame", not "cgame.dll" or "vm/cgame.qvm"

void	VM_Free( vm_t *vm );
void	VM_Clear(void);
void	VM_Forced_Unload_Start(void);
void	VM_Forced_Unload_Done(void);
vm_t	*VM_Restart( vm_t *vm );

intptr_t	QDECL VM_Call( vm_t *vm, int callNum, ... );

void	VM_Debug( int level );
void	VM_CheckBounds( const vm_t *vm, unsigned int address, unsigned int length );
void	VM_CheckBounds2( const vm_t *vm, unsigned int addr1, unsigned int addr2, unsigned int length );

#if 1
#define VM_CHECKBOUNDS VM_CheckBounds
#define VM_CHECKBOUNDS2 VM_CheckBounds2
#else // for performance evaluation purposes
#define VM_CHECKBOUNDS(vm,a,b)
#define VM_CHECKBOUNDS2(vm,a,b,c)
#endif

void	*GVM_ArgPtr( intptr_t intValue );

#define	VMA(x) VM_ArgPtr(args[x])
static ID_INLINE float _vmf(intptr_t x)
{
	floatint_t v;
	v.i = (int)x;
	return v.f;
}
#define	VMF(x)	_vmf(args[x])


/*
==============================================================

CMD

Command text buffering and command execution

==============================================================
*/

/*

Any number of commands can be added in a frame, from several different sources.
Most commands come from either keybindings or console line input, but entire text
files can be execed.

*/

#define MAX_CMD_LINE 1024

void Cbuf_Init( void );
// allocates an initial text buffer that will grow as needed

void Cbuf_AddText( const char *text );
// Adds command text at the end of the buffer, does NOT add a final \n

void Cbuf_ExecuteText( cbufExec_t exec_when, const char *text );
// this can be used in place of either Cbuf_AddText or Cbuf_InsertText

void Cbuf_Execute( void );
// Pulls off \n terminated lines of text from the command buffer and sends
// them through Cmd_ExecuteString.  Stops when the buffer is empty.
// Normally called once per frame, but may be explicitly invoked.
// Do not call inside a command function, or current args will be destroyed.

//===========================================================================

/*

Command execution takes a null terminated string, breaks it into tokens,
then searches for a command or variable that matches the first token.

*/

typedef void (*xcommand_t) (void);

void	Cmd_Init( void );

void	Cmd_AddCommand( const char *cmd_name, xcommand_t function );
// called by the init functions of other parts of the program to
// register commands and functions to call for them.
// The cmd_name is referenced later, so it should not be in temp memory
// if function is NULL, the command will be forwarded to the server
// as a clc_clientCommand instead of executed locally

void	Cmd_RemoveCommand( const char *cmd_name );
void	Cmd_RemoveCgameCommands( void );

typedef void (*completionFunc_t)( char *args, int argNum );

// don't allow VMs to remove system commands
void	Cmd_RemoveCommandSafe( const char *cmd_name );

void	Cmd_CommandCompletion( void(*callback)(const char *s) );
// callback with each valid string
void	Cmd_SetCommandCompletionFunc( const char *command, completionFunc_t complete );
void	Cmd_CompleteArgument( const char *command, char *args, int argNum );
void	Cmd_CompleteWriteCfgName( char *args, int argNum );

int		Cmd_Argc( void );
void	Cmd_Clear( void );
char	*Cmd_Argv( int arg );
void	Cmd_ArgvBuffer( int arg, char *buffer, int bufferLength );
char	*Cmd_ArgsFrom( int arg );
void	Cmd_ArgsBuffer( char *buffer, int bufferLength );
char	*Cmd_Cmd( void );
void	Cmd_Args_Sanitize( void );
// The functions that execute commands get their parameters with these
// functions. Cmd_Argv () will return an empty string, not a NULL
// if arg > argc, so string operations are allways safe.

void	Cmd_TokenizeString( const char *text );
void	Cmd_TokenizeStringIgnoreQuotes( const char *text_in );
// Takes a null terminated string.  Does not need to be /n terminated.
// breaks the string up into arg tokens.

void	Cmd_ExecuteString( const char *text );
// Parses a single line of text into arguments and tries to execute it
// as if it was typed at the console


/*
==============================================================

CVAR

==============================================================
*/

/*

cvar_t variables are used to hold scalar or string variables that can be changed
or displayed at the console or prog code as well as accessed directly
in C code.

The user can access cvars from the console in three ways:
r_draworder			prints the current value
r_draworder 0		sets the current value to 0
set r_draworder 0	as above, but creates the cvar if not present

Cvars are restricted from having the same names as commands to keep this
interface from being ambiguous.

The are also occasionally used to communicated information between different
modules of the program.

*/

cvar_t *Cvar_Get( const char *var_name, const char *value, int flags );
// creates the variable if it doesn't exist, or returns the existing one
// if it exists, the value will not be changed, but flags will be ORed in
// that allows variables to be unarchived without needing bitflags
// if value is "", the value will not override a previously set value.

void	Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags );
// basically a slightly modified Cvar_Get for the interpreted modules

void	Cvar_Update( vmCvar_t *vmCvar );
// updates an interpreted modules' version of a cvar

void 	Cvar_Set( const char *var_name, const char *value );
// will create the variable with no flags if it doesn't exist

cvar_t	*Cvar_Set2(const char *var_name, const char *value, qboolean force);
// same as Cvar_Set, but allows more control over setting of cvar

void	Cvar_SetSafe( const char *var_name, const char *value );
// sometimes we set variables from an untrusted source: fail if flags & CVAR_PROTECTED

void	Cvar_SetLatched( const char *var_name, const char *value);
// don't set the cvar immediately

void	Cvar_SetValue( const char *var_name, float value );
void	Cvar_SetIntegerValue( const char *var_name, int value );
void	Cvar_SetValueSafe( const char *var_name, float value );
// expands value to a string and calls Cvar_Set/Cvar_SetSafe

qboolean Cvar_SetModified( const char *var_name, qboolean modified );

float	Cvar_VariableValue( const char *var_name );
int		Cvar_VariableIntegerValue( const char *var_name );
// returns 0 if not defined or non numeric

const char *Cvar_VariableString( const char *var_name );
void	Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize );
void	Cvar_VariableStringBufferSafe( const char *var_name, char *buffer, int bufsize, int flag );
// returns an empty string if not defined

int		Cvar_Flags(const char *var_name);
// returns CVAR_NONEXISTENT if cvar doesn't exist or the flags of that particular CVAR.

void	Cvar_CommandCompletion( void(*callback)(const char *s) );
// callback with each valid string

void 	Cvar_Reset( const char *var_name );
void 	Cvar_ForceReset(const char *var_name);

void	Cvar_SetCheatState( void );
// reset all testing vars to a safe value

qboolean Cvar_Command( void );
// called by Cmd_ExecuteString when Cmd_Argv(0) doesn't match a known
// command.  Returns true if the command was a variable reference that
// was handled. (print or change)

void 	Cvar_WriteVariables( fileHandle_t f );
// writes lines containing "set variable value" for all variables
// with the archive flag set to true.

void	Cvar_Init( void );

const char *Cvar_InfoString( int bit, qboolean *truncated );
const char *Cvar_InfoString_Big( int bit, qboolean *truncated );
// returns an info string containing all the cvars that have the given bit set
// in their flags ( CVAR_USERINFO, CVAR_SERVERINFO, CVAR_SYSTEMINFO, etc )
void	Cvar_InfoStringBuffer( int bit, char *buff, int buffsize );
void	Cvar_CheckRange( cvar_t *cv, const char *minVal, const char *maxVal, cvarValidator_t type );
void	Cvar_SetDescription( cvar_t *var, const char *var_description );

void	Cvar_SetGroup( cvar_t *var, cvarGroup_t group );
int		Cvar_CheckGroup( cvarGroup_t group );
void	Cvar_ResetGroup( cvarGroup_t group, qboolean resetModifiedFlags );

void	Cvar_Restart( qboolean unsetVM );

void	Cvar_CompleteCvarName( char *args, int argNum );

extern	int			cvar_modifiedFlags;
// whenever a cvar is modifed, its flags will be OR'd into this, so
// a single check can determine if any CVAR_USERINFO, CVAR_SERVERINFO,
// etc, variables have been modified since the last check.  The bit
// can then be cleared to allow another change detection.

unsigned int crc32_buffer( const byte *buf, unsigned int len );

/*
==============================================================

FILESYSTEM

No stdio calls should be used by any part of the game, because
we need to deal with all sorts of directory and seperator char
issues.
==============================================================
*/

// referenced flags
// these are in loop specific order so don't change the order
#define FS_GENERAL_REF	0x01
#define FS_UI_REF		0x02
#define FS_CGAME_REF	0x04
// number of id paks that will never be autodownloaded from baseq3/missionpack
#define NUM_ID_PAKS		9
#define NUM_TA_PAKS		4

typedef enum {
	H_SYSTEM,
	H_QAGAME,
	H_CGAME,
	H_Q3UI
} handleOwner_t;

#define FS_MATCH_EXTERN (1<<0)
#define FS_MATCH_PURE   (1<<1)
#define FS_MATCH_UNPURE (1<<2)
#define FS_MATCH_STICK  (1<<3)
#define FS_MATCH_PK3s   (FS_MATCH_PURE | FS_MATCH_UNPURE)
#define FS_MATCH_ANY    (FS_MATCH_EXTERN | FS_MATCH_PURE | FS_MATCH_UNPURE)

#define	MAX_FILE_HANDLES	64
#define	FS_INVALID_HANDLE	0

#define	MAX_FOUND_FILES		0x2000

#ifdef DEDICATED
#define Q3CONFIG_CFG "q3config_server.cfg"
#define CONSOLE_HISTORY_FILE "q3history_server"
#else
#define Q3CONFIG_CFG "q3config.cfg"
#define CONSOLE_HISTORY_FILE "q3history"
#endif

qboolean FS_Initialized( void );

void	FS_InitFilesystem ( void );
void	FS_Shutdown( qboolean closemfp );

qboolean	FS_ConditionalRestart( int checksumFeed, qboolean clientRestart );

void	FS_Restart( int checksumFeed );
// shutdown and restart the filesystem so changes to fs_gamedir can take effect

void	FS_Reload( void );

char	**FS_ListFiles( const char *directory, const char *extension, int *numfiles );
// directory should not have either a leading or trailing /
// if extension is "/", only subdirectories will be returned
// the returned files will not include any directories or /

void	FS_FreeFileList( char **list );

qboolean FS_FileExists( const char *file );

char   *FS_BuildOSPath( const char *base, const char *game, const char *qpath );

qboolean FS_CompareZipChecksum( const char *zipfile );
int		FS_GetZipChecksum( const char *zipfile );

int		FS_LoadStack( void );

int		FS_GetFileList(  const char *path, const char *extension, char *listbuf, int bufsize );

fileHandle_t	FS_FOpenFileWrite( const char *qpath );
// will properly create any needed paths and deal with seperater character issues

qboolean FS_SV_FileExists( const char *file );

fileHandle_t FS_SV_FOpenFileWrite( const char *filename );
int		FS_SV_FOpenFileRead( const char *filename, fileHandle_t *fp );
void	FS_SV_Rename( const char *from, const char *to );
int		FS_FOpenFileRead( const char *qpath, fileHandle_t *file, qboolean uniqueFILE );
// if uniqueFILE is true, then a new FILE will be fopened even if the file
// is found in an already open pak file.  If uniqueFILE is false, you must call
// FS_FCloseFile instead of fclose, otherwise the pak FILE would be improperly closed
// It is generally safe to always set uniqueFILE to true, because the majority of
// file IO goes through FS_ReadFile, which Does The Right Thing already.

int FS_Home_FOpenFileRead( const char *filename, fileHandle_t *file );

qboolean FS_FileIsInPAK( const char *filename, int *pChecksum, char *pakName );
// returns qtrue if a file is in the PAK file, otherwise qfalse

int		FS_PakIndexForHandle( fileHandle_t f );

// returns pak index or -1 if file is not in pak
extern int fs_lastPakIndex;

extern qboolean fs_reordered;

int		FS_Write( const void *buffer, int len, fileHandle_t f );

int		FS_Read( void *buffer, int len, fileHandle_t f );
// properly handles partial reads and reads from other dlls

void	FS_FCloseFile( fileHandle_t f );
// note: you can't just fclose from another DLL, due to MS libc issues

int		FS_ReadFile( const char *qpath, void **buffer );
// returns the length of the file
// a null buffer will just return the file length without loading
// as a quick check for existance. -1 length == not present
// A 0 byte will always be appended at the end, so string ops are safe.
// the buffer should be considered read-only, because it may be cached
// for other uses.

void	FS_ForceFlush( fileHandle_t f );
// forces flush on files we're writing to.

void	FS_FreeFile( void *buffer );
// frees the memory returned by FS_ReadFile

void	FS_WriteFile( const char *qpath, const void *buffer, int size );
// writes a complete file, creating any subdirectories needed

int		FS_filelength( fileHandle_t f );
// doesn't work for files that are opened from a pack file

int		FS_FTell( fileHandle_t f );
// where are we?

void	FS_Flush( fileHandle_t f );

void 	QDECL FS_Printf( fileHandle_t f, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));
// like fprintf

int		FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode );
// opens a file for reading, writing, or appending depending on the value of mode

int		FS_Seek( fileHandle_t f, long offset, fsOrigin_t origin );
// seek on a file

qboolean FS_FilenameCompare( const char *s1, const char *s2 );

const char *FS_LoadedPakNames( void );
const char *FS_LoadedPakChecksums( qboolean *overflowed );
// Returns a space separated string containing the checksums of all loaded pk3 files.
// Servers with sv_pure set will get this string and pass it to clients.

const char *FS_ReferencedPakNames( void );
const char *FS_ReferencedPakChecksums( void );
const char *FS_ReferencedPakPureChecksums( int maxlen );
// Returns a space separated string containing the checksums of all loaded 
// AND referenced pk3 files. Servers with sv_pure set will get this string 
// back from clients for pure validation 

void FS_ClearPakReferences( int flags );
// clears referenced booleans on loaded pk3s

void FS_PureServerSetReferencedPaks( const char *pakSums, const char *pakNames );
void FS_PureServerSetLoadedPaks( const char *pakSums, const char *pakNames );
// If the string is empty, all data sources will be allowed.
// If not empty, only pk3 files that match one of the space
// separated checksums will be checked for files, with the
// sole exception of .cfg files.

qboolean FS_IsPureChecksum( int sum );

qboolean FS_InvalidGameDir( const char *gamedir );
qboolean FS_idPak( const char *pak, const char *base, int numPaks );
qboolean FS_ComparePaks( char *neededpaks, int len, qboolean dlstring );

void FS_Rename( const char *from, const char *to );

void FS_Remove( const char *osPath );
void FS_HomeRemove( const char *homePath );

void	FS_FilenameCompletion( const char *dir, const char *ext,
		qboolean stripExt, void(*callback)(const char *s), int flags );

int FS_VM_OpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode, handleOwner_t owner );
int FS_VM_ReadFile( void *buffer, int len, fileHandle_t f, handleOwner_t owner );
void FS_VM_WriteFile( void *buffer, int len, fileHandle_t f, handleOwner_t owner );
int FS_VM_SeekFile( fileHandle_t f, long offset, fsOrigin_t origin, handleOwner_t owner );
void FS_VM_CloseFile( fileHandle_t f, handleOwner_t owner );
void FS_VM_CloseFiles( handleOwner_t owner );

const char *FS_GetCurrentGameDir( void );
const char *FS_GetBaseGameDir( void );

const char *FS_GetBasePath( void );
const char *FS_GetHomePath( void );
const char *FS_GetGamePath( void );

qboolean FS_StripExt( char *filename, const char *ext );
qboolean FS_AllowedExtension( const char *fileName, qboolean allowPk3s, const char **ext );

typedef qboolean ( *fnamecallback_f )( const char *filename, int length );

void FS_SetFilenameCallback( fnamecallback_f func ); 

char *FS_CopyString( const char *in );

// AVI pipes

fileHandle_t FS_PipeOpenWrite( const char *cmd, const char *filename );
void FS_PipeClose( fileHandle_t f );


/*
==============================================================

Edit fields and command line history/completion

==============================================================
*/

#define	MAX_EDIT_LINE	256
#if MAX_EDIT_LINE > MAX_CMD_LINE
#error "MAX_EDIT_LINE > MAX_CMD_LINE"
#endif
typedef struct {
	int		cursor;
	int		scroll;
	int		widthInChars;
	char	buffer[MAX_EDIT_LINE];
} field_t;

void Field_Clear( field_t *edit );
void Field_AutoComplete( field_t *edit );
void Field_CompleteKeyname( void );
void Field_CompleteFilename( const char *dir, const char *ext, qboolean stripExt, int flags );
void Field_CompleteCommand( char *cmd, qboolean doCommands, qboolean doCvars );

void Con_ResetHistory( void );
void Con_SaveField( const field_t *field );
qboolean Con_HistoryGetPrev( field_t *field );
qboolean Con_HistoryGetNext( field_t *field );

/*
==============================================================

MISC

==============================================================
*/

// centralizing the declarations for cl_cdkey
// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=470
extern char cl_cdkey[34];

extern	int	CPU_Flags;

#define CPU_FCOM   0x01
#define CPU_MMX    0x02
#define CPU_SSE    0x04
#define CPU_SSE2   0x08
#define CPU_SSE3   0x10

// TTimo
// centralized and cleaned, that's the max string you can send to a Com_Printf / Com_DPrintf (above gets truncated)
// bump to 8192 as 4096 may be not enough to print some data like gl extensions - CE
#define	MAXPRINTMSG	8192

char		*CopyString( const char *in );
void		Info_Print( const char *s );

void		Com_BeginRedirect (char *buffer, int buffersize, void (*flush)(const char *));
void		Com_EndRedirect( void );
void 		QDECL Com_Printf( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));
void 		QDECL Com_DPrintf( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));
void 		QDECL Com_Error( errorParm_t code, const char *fmt, ... ) __attribute__ ((noreturn, format (printf, 2, 3)));
void 		Com_Quit_f( void );
void		Com_GameRestart( int checksumFeed, qboolean clientRestart );

int			Com_EventLoop( void );
int			Com_Milliseconds( void );	// will be journaled properly

// MD4 functions
unsigned	Com_BlockChecksum( const void *buffer, int length );

// MD5 functions

char		*Com_MD5File(const char *filename, int length, const char *prefix, int prefix_len);
char		*Com_MD5Buf( const char *data, int length, const char *data2, int length2 );

// stateless challenge functions
void		Com_MD5Init( void );
int			Com_MD5Addr( const netadr_t *addr, int timestamp );

qboolean	Com_CDKeyValidate( const char *key, const char *checksum );
qboolean	Com_EarlyParseCmdLine( char *commandLine, char *con_title, int title_size, int *vid_xpos, int *vid_ypos );
int			Com_Split( char *in, char **out, int outsz, int delim );

int			Com_Filter( const char *filter, const char *name, int casesensitive );
int			Com_FilterPath( const char *filter, const char *name, int casesensitive );
int			Com_RealTime(qtime_t *qtime);
qboolean	Com_SafeMode( void );
void		Com_RunAndTimeServerPacket( const netadr_t *evFrom, msg_t *buf );

void		Com_StartupVariable( const char *match );
// checks for and removes command line "+set var arg" constructs
// if match is NULL, all set commands will be executed, otherwise
// only a set with the exact name.  Only used during startup.

void		Com_WriteConfiguration( void );


extern	cvar_t	*com_developer;
extern	cvar_t	*com_dedicated;
extern	cvar_t	*com_speeds;
extern	cvar_t	*com_timescale;
extern	cvar_t	*com_viewlog;			// 0 = hidden, 1 = visible, 2 = minimized
extern	cvar_t	*com_version;
extern	cvar_t	*com_blood;
extern	cvar_t	*com_buildScript;		// for building release pak files
extern	cvar_t	*com_journal;
extern	cvar_t	*com_cameraMode;

// both client and server must agree to pause
extern	cvar_t	*sv_paused;
extern	cvar_t	*sv_packetdelay;
extern	cvar_t	*com_sv_running;

#ifndef DEDICATED
extern	cvar_t	*cl_paused;
extern	cvar_t	*cl_packetdelay;
extern	cvar_t	*com_cl_running;
extern	cvar_t	*com_yieldCPU;
#endif

extern	cvar_t	*vm_rtChecks;
extern	cvar_t	*com_affinityMask;

// com_speeds times
extern	int		time_game;
extern	int		time_frontend;
extern	int		time_backend;		// renderer backend time

extern	int		com_frameTime;
extern	int		com_frameMsec;

#ifndef DEDICATED
extern	qboolean	gw_minimized;
extern	qboolean	gw_active;
#endif

extern	qboolean	com_errorEntered;

extern	fileHandle_t	com_journalDataFile;

extern	char	rconPassword2[ MAX_CVAR_VALUE_STRING ];

typedef enum {
	TAG_FREE,
	TAG_GENERAL,
	TAG_BOTLIB,
	TAG_RENDERER,
	TAG_SMALL,
	TAG_STATIC,
	TAG_COUNT
} memtag_t;

/*

--- low memory ----
server vm
server clipmap
---mark---
renderer initialization (shaders, etc)
UI vm
cgame vm
renderer map
renderer models

---free---

temp file loading
--- high memory ---

*/

#if defined(_DEBUG) && !defined(BSPC)
	#define ZONE_DEBUG
#endif

#ifdef ZONE_DEBUG
#define Z_TagMalloc(size, tag)			Z_TagMallocDebug(size, tag, #size, __FILE__, __LINE__)
#define Z_Malloc(size)					Z_MallocDebug(size, #size, __FILE__, __LINE__)
#define S_Malloc(size)					S_MallocDebug(size, #size, __FILE__, __LINE__)
void *Z_TagMallocDebug( int size, memtag_t tag, char *label, char *file, int line );	// NOT 0 filled memory
void *Z_MallocDebug( int size, char *label, char *file, int line );			// returns 0 filled memory
void *S_MallocDebug( int size, char *label, char *file, int line );			// returns 0 filled memory
#else
void *Z_TagMalloc( int size, memtag_t tag );	// NOT 0 filled memory
void *Z_Malloc( int size );			// returns 0 filled memory
void *S_Malloc( int size );			// NOT 0 filled memory only for small allocations
#endif
void Z_Free( void *ptr );
void Z_FreeTags( memtag_t tag );
int Z_AvailableMemory( void );
void Z_LogHeap( void );

void Hunk_Clear( void );
void Hunk_ClearToMark( void );
void Hunk_SetMark( void );
qboolean Hunk_CheckMark( void );
void Hunk_ClearTempMemory( void );
void *Hunk_AllocateTempMemory( int size );
void Hunk_FreeTempMemory( void *buf );
int	Hunk_MemoryRemaining( void );
void Hunk_Log( void);

void Com_TouchMemory( void );

// commandLine should not include the executable name (argv[0])
void Com_Init( char *commandLine );
void Com_Frame( qboolean noDelay );

/*
==============================================================

CLIENT / SERVER SYSTEMS

==============================================================
*/

//
// client interface
//
void CL_Init( void );
qboolean CL_Disconnect( qboolean showMainMenu );
void CL_ResetOldGame( void );
void CL_Shutdown( const char *finalmsg, qboolean quit );
void CL_Frame( int msec );
qboolean CL_GameCommand( void );
void CL_KeyEvent (int key, qboolean down, unsigned time);

void CL_CharEvent( int key );
// char events are for field typing, not game control

void CL_MouseEvent( int dx, int dy, int time );

void CL_JoystickEvent( int axis, int value, int time );

void CL_PacketEvent( const netadr_t *from, msg_t *msg );

void CL_ConsolePrint( const char *text );

void CL_MapLoading( void );
// do a screen update before starting to load a map
// when the server is going to load a new map, the entire hunk
// will be cleared, so the client must shutdown cgame, ui, and
// the renderer

void	CL_ForwardCommandToServer( const char *string );
// adds the current command line as a clc_clientCommand to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void CL_CDDialog( void );
// bring up the "need a cd to play" dialog

void CL_ShutdownAll( void );
// shutdown all the client stuff

void CL_ClearMemory( void );
// clear memory

void CL_FlushMemory( void );
// dump all memory on an error

void CL_StartHunkUsers( void );
// start all the client stuff using the hunk

void CL_Snd_Restart(void);
// Restart sound subsystem

void Key_KeynameCompletion( void(*callback)(const char *s) );
// for keyname autocompletion

void Key_WriteBindings( fileHandle_t f );
// for writing the config files

void S_ClearSoundBuffer( void );
// call before filesystem access

void CL_SystemInfoChanged( qboolean onlyGame );
qboolean CL_GameSwitch( void );

// AVI files have the start of pixel lines 4 byte-aligned
#define AVI_LINE_PADDING 4

//
// server interface
//
void SV_Init( void );
void SV_Shutdown( const char *finalmsg );
void SV_Frame( int msec );
void SV_TrackCvarChanges( void );
void SV_PacketEvent( const netadr_t *from, msg_t *msg );
int SV_FrameMsec( void );
qboolean SV_GameCommand( void );
int SV_SendQueuedPackets( void );

void SV_AddDedicatedCommands( void );
void SV_RemoveDedicatedCommands( void );


//
// UI interface
//
qboolean UI_GameCommand( void );
qboolean UI_usesUniqueCDKey(void);

/*
==============================================================

NON-PORTABLE SYSTEM SERVICES

==============================================================
*/

typedef enum {
	AXIS_SIDE,
	AXIS_FORWARD,
	AXIS_UP,
	AXIS_ROLL,
	AXIS_YAW,
	AXIS_PITCH,
	MAX_JOYSTICK_AXIS
} joystickAxis_t;

typedef enum {
  // bk001129 - make sure SE_NONE is zero
	SE_NONE = 0,	// evTime is still valid
	SE_KEY,		// evValue is a key code, evValue2 is the down flag
	SE_CHAR,	// evValue is an ascii char
	SE_MOUSE,	// evValue and evValue2 are relative signed x / y moves
	SE_JOYSTICK_AXIS,	// evValue is an axis number and evValue2 is the current state (-127 to 127)
	SE_CONSOLE,	// evPtr is a char*
	SE_MAX,
} sysEventType_t;

typedef struct {
	int				evTime;
	sysEventType_t	evType;
	int				evValue, evValue2;
	int				evPtrLength;	// bytes of data pointed to by evPtr, for journaling
	void			*evPtr;			// this must be manually freed if not NULL
} sysEvent_t;

void	Sys_Init( void );
void	Sys_QueEvent( int evTime, sysEventType_t evType, int value, int value2, int ptrLength, void *ptr );
void	Sys_SendKeyEvents( void );
void	Sys_Sleep( int msec );
char	*Sys_ConsoleInput( void );

// general development dll loading for virtual machine testing
void	*QDECL Sys_LoadDll( const char *name, dllSyscall_t *entryPoint, dllSyscall_t systemcalls );
void	Sys_UnloadDll( void *dllHandle );

void	QDECL Sys_Error( const char *error, ...) __attribute__ ((noreturn, format (printf, 1, 2)));
void	Sys_Quit (void) __attribute__ ((noreturn));
char	*Sys_GetClipboardData( void );	// note that this isn't journaled...
void	Sys_SetClipboardBitmap( const byte *bitmap, int length );

void	Sys_Print( const char *msg );

// dedicated console status, win32-only at the moment
void	QDECL Sys_SetStatus( const char *format, ...) __attribute__ ((format (printf, 1, 2)));

void	Sys_SetAffinityMask( int mask );

// Sys_Milliseconds should only be used for profiling purposes,
// any game related timing information should come from event timestamps
int		Sys_Milliseconds( void );
int64_t	Sys_Microseconds( void );

void	Sys_SnapVector( float *v );

qboolean Sys_RandomBytes( byte *string, int len );

// the system console is shown when a dedicated server is running
void	Sys_DisplaySystemConsole( qboolean show );

int		Sys_GetProcessorId( char *vendor );

void	Sys_ShowConsole( int level, qboolean quitOnClose );
void	Sys_SetErrorText( const char *text );

void	Sys_SendPacket( int length, const void *data, const netadr_t *to );

qboolean	Sys_StringToAdr( const char *s, netadr_t *a, netadrtype_t family );
//Does NOT parse port numbers, only base addresses.

qboolean	Sys_IsLANAddress(const netadr_t *adr);
void		Sys_ShowIP(void);

void	Sys_Mkdir( const char *path );
FILE	*Sys_FOpen( const char *ospath, const char *mode );

const char *Sys_Pwd( void );
const char *Sys_DefaultBasePath( void );
const char *Sys_DefaultHomePath( void );
const char *Sys_SteamPath( void );

char	**Sys_ListFiles( const char *directory, const char *extension, const char *filter, int *numfiles, qboolean wantsubs );
void	Sys_FreeFileList( char **list );

void	Sys_BeginProfiling( void );
void	Sys_EndProfiling( void );

qboolean Sys_LowPhysicalMemory( void );

int Sys_MonkeyShouldBeSpanked( void );

void *Sys_LoadLibrary( const char *name );
void *Sys_LoadFunction( void *handle, const char *name );
int   Sys_LoadFunctionErrors( void );
void  Sys_UnloadLibrary( void *handle );

// adaptive huffman functions
void	Huff_Compress( msg_t *buf, int offset );
void	Huff_Decompress( msg_t *buf, int offset );

// static huffman functions
void HuffmanPutBit( byte* fout, int32_t bitIndex, int bit );
int HuffmanPutSymbol( byte* fout, uint32_t offset, int symbol );
int HuffmanGetBit( const byte* buffer, int bitIndex );
int HuffmanGetSymbol( int* symbol, const byte* buffer, int bitIndex );

#define	SV_ENCODE_START		4
#define	SV_DECODE_START		12
#define	CL_ENCODE_START		12
#define	CL_DECODE_START		4

// flags for sv_allowDownload and cl_allowDownload
#define	DLF_ENABLE		1
#define	DLF_NO_REDIRECT	2
#define	DLF_NO_UDP		4
#define	DLF_NO_DISCONNECT 8

// functional gate syscall number
#define COM_TRAP_GETVALUE 700

#endif // _QCOMMON_H_
