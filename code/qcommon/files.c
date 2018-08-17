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
/*****************************************************************************
 * name:		files.c
 *
 * desc:		handle based filesystem for Quake III Arena 
 *
 * $Archive: /MissionPack/code/qcommon/files.c $
 *
 *****************************************************************************/


#include "q_shared.h"
#include "qcommon.h"
#include "unzip.h"

/*
=============================================================================

QUAKE3 FILESYSTEM

All of Quake's data access is through a hierarchical file system, but the contents of 
the file system can be transparently merged from several sources.

A "qpath" is a reference to game file data.  MAX_ZPATH is 256 characters, which must include
a terminating zero. "..", "\\", and ":" are explicitly illegal in qpaths to prevent any
references outside the quake directory system.

The "base path" is the path to the directory holding all the game directories and usually
the executable.  It defaults to ".", but can be overridden with a "+set fs_basepath c:\quake3"
command line to allow code debugging in a different directory.  Basepath cannot
be modified at all after startup.  Any files that are created (demos, screenshots,
etc) will be created reletive to the base path, so base path should usually be writable.

The "cd path" is the path to an alternate hierarchy that will be searched if a file
is not located in the base path.  A user can do a partial install that copies some
data to a base path created on their hard drive and leave the rest on the cd.  Files
are never writen to the cd path.  It defaults to a value set by the installer, like
"e:\quake3", but it can be overridden with "+set fs_cdpath g:\quake3".

If a user runs the game directly from a CD, the base path would be on the CD.  This
should still function correctly, but all file writes will fail (harmlessly).

The "home path" is the path used for all write access. On win32 systems we have "base path"
== "home path", but on *nix systems the base installation is usually readonly, and
"home path" points to ~/.q3a or similar

The user can also install custom mods and content in "home path", so it should be searched
along with "home path" and "cd path" for game content.


The "base game" is the directory under the paths where data comes from by default, and
can be either "baseq3" or "demoq3".

The "current game" may be the same as the base game, or it may be the name of another
directory under the paths that should be searched for files before looking in the base game.
This is the basis for addons.

Clients automatically set the game directory after receiving a gamestate from a server,
so only servers need to worry about +set fs_game.

No other directories outside of the base game and current game will ever be referenced by
filesystem functions.

To save disk space and speed loading, directory trees can be collapsed into zip files.
The files use a ".pk3" extension to prevent users from unzipping them accidentally, but
otherwise the are simply normal uncompressed zip files.  A game directory can have multiple
zip files of the form "pak0.pk3", "pak1.pk3", etc.  Zip files are searched in decending order
from the highest number to the lowest, and will always take precedence over the filesystem.
This allows a pk3 distributed as a patch to override all existing data.

Because we will have updated executables freely available online, there is no point to
trying to restrict demo / oem versions of the game with code changes.  Demo / oem versions
should be exactly the same executables as release versions, but with different data that
automatically restricts where game media can come from to prevent add-ons from working.

After the paths are initialized, quake will look for the product.txt file.  If not
found and verified, the game will run in restricted mode.  In restricted mode, only 
files contained in demoq3/pak0.pk3 will be available for loading, and only if the zip header is
verified to not have been modified.  A single exception is made for q3config.cfg.  Files
can still be written out in restricted mode, so screenshots and demos are allowed.
Restricted mode can be tested by setting "+set fs_restrict 1" on the command line, even
if there is a valid product.txt under the basepath or cdpath.

If not running in restricted mode, and a file is not found in any local filesystem,
an attempt will be made to download it and save it under the base path.

If the "fs_copyfiles" cvar is set to 1, then every time a file is sourced from the cd
path, it will be copied over to the base path.  This is a development aid to help build
test releases and to copy working sets over slow network links.

File search order: when FS_FOpenFileRead gets called it will go through the fs_searchpaths
structure and stop on the first successful hit. fs_searchpaths is built with successive
calls to FS_AddGameDirectory

Additionaly, we search in several subdirectories:
current game is the current mode
base game is a variable to allow mods based on other mods
(such as baseq3 + missionpack content combination in a mod for instance)
BASEGAME is the hardcoded base game ("baseq3")

e.g. the qpath "sound/newstuff/test.wav" would be searched for in the following places:

home path + current game's zip files
home path + current game's directory
base path + current game's zip files
base path + current game's directory
cd path + current game's zip files
cd path + current game's directory

home path + base game's zip file
home path + base game's directory
base path + base game's zip file
base path + base game's directory
cd path + base game's zip file
cd path + base game's directory

home path + BASEGAME's zip file
home path + BASEGAME's directory
base path + BASEGAME's zip file
base path + BASEGAME's directory
cd path + BASEGAME's zip file
cd path + BASEGAME's directory

server download, to be written to home path + current game's directory


The filesystem can be safely shutdown and reinitialized with different
basedir / cddir / game combinations, but all other subsystems that rely on it
(sound, video) must also be forced to restart.

Because the same files are loaded by both the clip model (CM_) and renderer (TR_)
subsystems, a simple single-file caching scheme is used.  The CM_ subsystems will
load the file with a request to cache.  Only one file will be kept cached at a time,
so any models that are going to be referenced by both subsystems should alternate
between the CM_ load function and the ref load function.

TODO: A qpath that starts with a leading slash will always refer to the base game, even if another
game is currently active.  This allows character models, skins, and sounds to be downloaded
to a common directory no matter which game is active.

How to prevent downloading zip files?
Pass pk3 file names in systeminfo, and download before FS_Restart()?

Aborting a download disconnects the client from the server.

How to mark files as downloadable?  Commercial add-ons won't be downloadable.

Non-commercial downloads will want to download the entire zip file.
the game would have to be reset to actually read the zip in

Auto-update information

Path separators

Casing

  separate server gamedir and client gamedir, so if the user starts
  a local game after having connected to a network game, it won't stick
  with the network game.

  allow menu options for game selection?

Read / write config to floppy option.

Different version coexistance?

When building a pak file, make sure a q3config.cfg isn't present in it,
or configs will never get loaded from disk!

  todo:

  downloading (outside fs?)
  game directory passing and restarting

=============================================================================

*/

// every time a new demo pk3 file is built, this checksum must be updated.
// the easiest way to get it is to just run the game and see what it spits out
#define	DEMO_PAK0_CHECKSUM	2985612116u
static const unsigned pak_checksums[] = {
	1566731103u,
	298122907u,
	412165236u,
	2991495316u,
	1197932710u,
	4087071573u,
	3709064859u,
	908855077u,
	977125798u
};

// if this is defined, the executable positively won't work with any paks other
// than the demo pak, even if productid is present.  This is only used for our
// last demo release to prevent the mac and linux users from using the demo
// executable with the production windows pak before the mac/linux products
// hit the shelves a little later
// NOW defined in build files
//#define PRE_RELEASE_TADEMO

#define USE_PK3_CACHE
#define USE_PK3_CACHE_FILE

#define MAX_ZPATH			256
#define MAX_FILEHASH_SIZE	4096

typedef struct fileInPack_s {
	char					*name;		// name of the file
	unsigned long			pos;		// file info position in zip
	unsigned long			size;		// file size
	struct	fileInPack_s*	next;		// next file in the hash
} fileInPack_t;

typedef struct pack_s {
	char			*pakFilename;				// c:\quake3\baseq3\pak0.pk3
	char			*pakBasename;				// pak0
	const char		*pakGamename;				// baseq3
	unzFile			handle;						// handle to zip file
	int				checksum;					// regular checksum
	int				pure_checksum;				// checksum for pure
	int				numfiles;					// number of files in pk3
	int				referenced;					// referenced file flags
	int				hashSize;					// hash table size (power of 2)
	fileInPack_t*	*hashTable;					// hash table
	fileInPack_t*	buildBuffer;				// buffer with the filenames etc.
	int				index;
	int				used;

	// caching subsystem
#ifdef USE_PK3_CACHE
	int				namehash;
	fileOffset_t	size;
	fileTime_t		mtime;
	fileTime_t		ctime;
	qboolean		touched;
	struct pack_s	*next;
	struct pack_s	*prev;
	int				checksumFeed;
	int				*headerLongs;
	int				numHeaderLongs;
#endif
} pack_t;

typedef struct {
	char		*path;		// c:\quake3
	char		*gamedir;	// baseq3
} directory_t;

typedef enum {
	DIR_STATIC = 0,	// always allowed, never changes
	DIR_ALLOW,
	DIR_DENY
} dirPolicy_t;

typedef struct searchpath_s {
	struct searchpath_s *next;
	pack_t		*pack;		// only one of pack / dir will be non NULL
	directory_t	*dir;
	dirPolicy_t	policy;
} searchpath_t;

static	char		fs_gamedir[MAX_OSPATH];	// this will be a single file name with no separators
static	cvar_t		*fs_debug;
static	cvar_t		*fs_homepath;

static	cvar_t		*fs_steampath;

static	cvar_t		*fs_basepath;
		cvar_t		*fs_basegame;
static	cvar_t		*fs_copyfiles;
		cvar_t		*fs_gamedirvar;
static	cvar_t		*fs_locked;

static	searchpath_t	*fs_searchpaths;
static	int			fs_readCount;			// total bytes read
static	int			fs_loadCount;			// total files read
static	int			fs_loadStack;			// total files in memory
static	int			fs_packFiles = 0;			// total number of files in packs
static	int			fs_packCount = 0;			// total number of packs

static int fs_checksumFeed;

typedef union qfile_gus {
	FILE*		o;
	unzFile		z;
	void*		v;
} qfile_gut;

typedef struct qfile_us {
	qfile_gut	file;
	qboolean	unique;
} qfile_ut;

typedef struct {
	qfile_ut	handleFiles;
	qboolean	handleSync;
	qboolean	zipFile;
	int			zipFilePos;
	int			zipFileLen;
	char		name[MAX_ZPATH];
	handleOwner_t	owner;
	int			pakIndex;
	pack_t		*pak;
} fileHandleData_t;

static fileHandleData_t	fsh[MAX_FILE_HANDLES];

// TTimo - https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=540
// wether we did a reorder on the current search path when joining the server
qboolean fs_reordered;

#define MAX_REF_PAKS	MAX_STRING_TOKENS

// never load anything from pk3 files that are not present at the server when pure
static int		fs_numServerPaks = 0;
static int		fs_serverPaks[MAX_REF_PAKS];			// checksums
static char		*fs_serverPakNames[MAX_REF_PAKS];		// pk3 names

// only used for autodownload, to make sure the client has at least
// all the pk3 files that are referenced at the server side
static int		fs_numServerReferencedPaks;
static int		fs_serverReferencedPaks[MAX_REF_PAKS];		// checksums
static char		*fs_serverReferencedPakNames[MAX_REF_PAKS];	// pk3 names

int	fs_lastPakIndex;

#ifdef FS_MISSING
FILE*		missingFiles = NULL;
#endif

void Com_AppendCDKey( const char *filename );
void Com_ReadCDKey( const char *filename );

static int FS_GetModList( char *listbuf, int bufsize );
static void FS_CheckIdPaks( void );
void FS_Reload( void );


/*
==============
FS_Initialized
==============
*/
qboolean FS_Initialized( void ) {
	return ( fs_searchpaths != NULL );
}


/*
=================
FS_PakIsPure
=================
*/
static qboolean FS_PakIsPure( const pack_t *pack ) {
#ifndef DEDICATED
	int i;
	if ( fs_numServerPaks ) {
		for ( i = 0 ; i < fs_numServerPaks ; i++ ) {
			// FIXME: also use hashed file names
			// NOTE TTimo: a pk3 with same checksum but different name would be validated too
			//   I don't see this as allowing for any exploit, it would only happen if the client does manips of its file names 'not a bug'
			if ( pack->checksum == fs_serverPaks[i] ) {
				return qtrue;		// on the aproved list
			}
		}
		return qfalse;	// not on the pure server pak list
	}
#endif
	return qtrue;
}


/*
=================
FS_LoadStack
return load stack
=================
*/
int FS_LoadStack( void ) {
	return fs_loadStack;
}


/*
================
return a hash value for the filename
================
*/
#define FS_HashFileName Com_GenerateHashValue


/*
=================
FS_HandleForFile
=================
*/
static fileHandle_t	FS_HandleForFile( void ) 
{
	int		i;

	for ( i = 1 ; i < MAX_FILE_HANDLES ; i++ ) 
	{
		if ( fsh[i].handleFiles.file.v == NULL )
			return i;
	}

	Com_Error( ERR_DROP, "FS_HandleForFile: none free" );
	return FS_INVALID_HANDLE;
}


static FILE	*FS_FileForHandle( fileHandle_t f ) {
	if ( f <= 0 || f >= MAX_FILE_HANDLES ) {
		Com_Error( ERR_DROP, "FS_FileForHandle: out of range" );
	}
	if ( fsh[f].zipFile ) {
		Com_Error( ERR_DROP, "FS_FileForHandle: can't get FILE on zip file" );
	}
	if ( ! fsh[f].handleFiles.file.o ) {
		Com_Error( ERR_DROP, "FS_FileForHandle: NULL" );
	}
	
	return fsh[f].handleFiles.file.o;
}


void FS_ForceFlush( fileHandle_t f ) {
	FILE *file;

	file = FS_FileForHandle(f);
	setvbuf( file, NULL, _IONBF, 0 );
}


/*
================
FS_FileLengthByHandle

If this is called on a non-unique FILE (from a pak file),
it will return the size of the pak file, not the expected
size of the file.
================
*/
#if 0
static int FS_FileLengthByHandle( fileHandle_t f ) {
	int		pos;
	int		end;
	FILE*	h;

	h = FS_FileForHandle( f );
	pos = ftell( h );
	fseek( h, 0, SEEK_END );
	end = ftell( h );
	fseek( h, pos, SEEK_SET );

	return end;
}
#endif


/*
================
FS_FileLength
================
*/
static int FS_FileLength( FILE* h ) 
{
	int		pos;
	int		end;

	pos = ftell( h );
	fseek( h, 0, SEEK_END );
	end = ftell( h );
	fseek( h, pos, SEEK_SET );

	return end;
}


/*
====================
FS_PakIndexForHandle
====================
*/
int FS_PakIndexForHandle( fileHandle_t f ) {

	if ( f <= FS_INVALID_HANDLE || f >= MAX_FILE_HANDLES )
		return -1;

	return fsh[ f ].pakIndex;
}


/*
====================
FS_ReplaceSeparators

Fix things up differently for win/unix/mac
====================
*/
static void FS_ReplaceSeparators( char *path ) {
	char	*s;

	for ( s = path ; *s ; s++ ) {
		if ( *s == PATH_SEP_FOREIGN ) {
			*s = PATH_SEP;
		}
	}
}


/*
===================
FS_BuildOSPath

Qpath may have either forward or backwards slashes
===================
*/
char *FS_BuildOSPath( const char *base, const char *game, const char *qpath ) {
	char	temp[MAX_OSPATH*2+1];
	static char ospath[2][sizeof(temp)+MAX_OSPATH];
	static int toggle;
	
	toggle ^= 1;		// flip-flop to allow two returns without clash

	if( !game || !game[0] ) {
		game = fs_gamedir;
	}

	if ( qpath )
		Com_sprintf( temp, sizeof( temp ), "%c%s%c%s", PATH_SEP, game, PATH_SEP, qpath );
	else
		Com_sprintf( temp, sizeof( temp ), "%c%s", PATH_SEP, game );

	FS_ReplaceSeparators( temp );
	Com_sprintf( ospath[toggle], sizeof( ospath[0] ), "%s%s", base, temp );
	
	return ospath[toggle];
}


/*
================
FS_CheckDirTraversal

Check whether the string contains stuff like "../" to prevent directory traversal bugs
and return qtrue if it does.
================
*/
static qboolean FS_CheckDirTraversal( const char *checkdir )
{
	if ( strstr( checkdir, "../" ) || strstr( checkdir, "..\\" ) )
		return qtrue;

	if ( strstr( checkdir, "::" ) )
		return qtrue;
	
	return qfalse;
}


/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
static qboolean FS_CreatePath( const char *OSPath ) {
	char	path[MAX_OSPATH*2+1];
	char	*ofs;
	
	// make absolutely sure that it can't back up the path
	// FIXME: is c: allowed???
	if ( FS_CheckDirTraversal( OSPath ) ) {
		Com_Printf( "WARNING: refusing to create relative path \"%s\"\n", OSPath );
		return qtrue;
	}

	Q_strncpyz( path, OSPath, sizeof( path ) );
	// Make sure we have OS correct slashes
	FS_ReplaceSeparators( path );
	for ( ofs = path + 1; *ofs; ofs++ ) {
		if ( *ofs == PATH_SEP ) {
			// create the directory
			*ofs = '\0';
			Sys_Mkdir( path );
			*ofs = PATH_SEP;
		}
	}
	return qfalse;
}


/*
=================
FS_CopyFile

Copy a fully specified file from one place to another
=================
*/
static void FS_CopyFile( const char *fromOSPath, const char *toOSPath ) {
	FILE	*f;
	size_t	len;
	byte	*buf;

	Com_Printf( "copy %s to %s\n", fromOSPath, toOSPath );

	if (strstr(fromOSPath, "journal.dat") || strstr(fromOSPath, "journaldata.dat")) {
		Com_Printf( "Ignoring journal files\n");
		return;
	}

	f = Sys_FOpen( fromOSPath, "rb" );
	if ( !f ) {
		return;
	}

	len = FS_FileLength( f );

	// we are using direct malloc instead of Z_Malloc here, so it
	// probably won't work on a mac... Its only for developers anyway...
	buf = malloc( len );
	if ( !buf ) {
		fclose( f );
		Com_Error( ERR_FATAL, "Memory alloc error in FS_Copyfiles()\n" );
	}

	if (fread( buf, 1, len, f ) != len) {
		free( buf );
		fclose( f );
		Com_Error( ERR_FATAL, "Short read in FS_Copyfiles()\n" );
	}
	fclose( f );

	f = Sys_FOpen( toOSPath, "wb" );
	if ( !f ) {
		if ( FS_CreatePath( toOSPath ) ) {
			free( buf );
			return;
		}
		f = Sys_FOpen( toOSPath, "wb" );
		if ( !f ) {
			free( buf );
			return;
		}
	}

	if ( fwrite( buf, 1, len, f ) != len ) {
		free( buf );
		fclose( f );
		Com_Error( ERR_FATAL, "Short write in FS_Copyfiles()\n" );
	}
	fclose( f );
	free( buf );
}


static const char *FS_HasExt( const char *fileName, const char **extList, int extCount );

/*
=================
FS_AllowedExtension
=================
*/
qboolean FS_AllowedExtension( const char *fileName, qboolean allowPk3s, const char **ext ) 
{
	static const char *extlist[] =	{ "dll", "exe", "so", "dylib", "qvm", "pk3" };
	const char *e;
	int i, n;

	e = strrchr( fileName, '.' );

	// check for unix '.so.[0-9]' pattern
	if ( e >= (fileName + 3) && *(e+1) >= '0' && *(e+1) <= '9' && *(e+2) == '\0' ) 
	{
		if ( *(e-3) == '.' && (*(e-2) == 's' || *(e-2) == 'S') && (*(e-1) == 'o' || *(e-1) == 'O') )
		{
			if ( ext )
			{
				*ext = (e-2);
			}
			return qfalse;
		}
	}
	if ( !e )
		return qtrue;

	e++; // skip '.'

	if ( allowPk3s )
		n = ARRAY_LEN( extlist ) - 1;
	else
		n = ARRAY_LEN( extlist );
	
	for ( i = 0; i < n; i++ ) 
	{
		if ( Q_stricmp( e, extlist[i] ) == 0 ) 
		{
			if ( ext )
				*ext = e;
			return qfalse;
		}
	}

	return qtrue;
}


/*
=================
FS_CheckFilenameIsNotExecutable

ERR_FATAL if trying to maniuplate a file with the platform library extension
=================
 */
static void FS_CheckFilenameIsNotAllowed( const char *filename, const char *function, qboolean allowPk3s )
{
	const char *extension;
	// Check if the filename ends with the library extension
	if ( FS_AllowedExtension( filename, allowPk3s, &extension ) == qfalse ) 
	{
		Com_Error( ERR_FATAL, "%s: Not allowed to manipulate '%s' due "
			"to %s extension", function, filename, extension );
	}
}


/*
===========
FS_Remove

===========
*/
void FS_Remove( const char *osPath ) 
{
	FS_CheckFilenameIsNotAllowed( osPath, __func__, qtrue );

	remove( osPath );
}


/*
===========
FS_HomeRemove
===========
*/
void FS_HomeRemove( const char *osPath ) 
{
	FS_CheckFilenameIsNotAllowed( osPath, __func__, qfalse );

	remove( FS_BuildOSPath( fs_homepath->string,
			fs_gamedir, osPath ) );
}


/*
================
FS_FileExists

Tests if the file exists in the current gamedir, this DOES NOT
search the paths.  This is to determine if opening a file to write
(which always goes into the current gamedir) will cause any overwrites.
NOTE TTimo: this goes with FS_FOpenFileWrite for opening the file afterwards
================
*/
qboolean FS_FileExists( const char *file )
{
	FILE *f;
	char *testpath;

	testpath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, file );

	f = Sys_FOpen( testpath, "rb" );
	if (f) {
		fclose( f );
		return qtrue;
	}
	return qfalse;
}


/*
================
FS_SV_FileExists

Tests if the file exists 
================
*/
qboolean FS_SV_FileExists( const char *file )
{
	FILE *f;
	char *testpath;

	// search in homepath
	testpath = FS_BuildOSPath( fs_homepath->string, file, NULL );
	f = Sys_FOpen( testpath, "rb" );
	if ( f ) {
		fclose( f );
		return qtrue;
	}

	// search in basepath
	if ( Q_stricmp( fs_homepath->string, fs_basepath->string ) ) {
		testpath = FS_BuildOSPath( fs_basepath->string, file, NULL );
		f = Sys_FOpen( testpath, "rb" );
		if ( f ) {
			fclose( f );
			return qtrue;
		}
	}

	return qfalse;
}


/*
===========
FS_InitHandle
===========
*/
static void FS_InitHandle( fileHandleData_t *fd ) {
	fd->pak = NULL;
	fd->pakIndex = -1;
	fs_lastPakIndex = -1;
}


/*
===========
FS_SV_FOpenFileWrite
===========
*/
fileHandle_t FS_SV_FOpenFileWrite( const char *filename ) {
	char *ospath;
	fileHandle_t	f;
	fileHandleData_t *fd;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !*filename ) {
		return FS_INVALID_HANDLE;
	}

	ospath = FS_BuildOSPath( fs_homepath->string, filename, NULL );

	f = FS_HandleForFile();
	fd = &fsh[ f ];
	FS_InitHandle( fd );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_FOpenFileWrite: %s\n", ospath );
	}

	FS_CheckFilenameIsNotAllowed( ospath, __func__, qtrue );

	Com_DPrintf( "writing to: %s\n", ospath );

	fd->handleFiles.file.o = Sys_FOpen( ospath, "wb" );
	if ( !fd->handleFiles.file.o ) {
		if ( FS_CreatePath( ospath ) ) {
			return FS_INVALID_HANDLE;
		}
		fd->handleFiles.file.o = Sys_FOpen( ospath, "wb" );
		if ( !fd->handleFiles.file.o ) {
			return FS_INVALID_HANDLE;
		}
	}

	Q_strncpyz( fd->name, filename, sizeof( fd->name ) );
	fd->handleSync = qfalse;
	fd->zipFile = qfalse;

	return f;
}


/*
===========
FS_SV_FOpenFileRead
search for a file somewhere below the home path, base path or cd path
we search in that order, matching FS_SV_FOpenFileRead order
===========
*/
int FS_SV_FOpenFileRead( const char *filename, fileHandle_t *fp ) {
	fileHandleData_t *fd;
	fileHandle_t f;
	char *ospath;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	// should never happen but for safe
	if ( !fp ) { 
		return -1;
	}

	// allocate new file handle
	f = FS_HandleForFile(); 
	fd = &fsh[ f ];
	FS_InitHandle( fd );

#ifndef DEDICATED
	// don't let sound stutter
	// S_ClearSoundBuffer();
#endif

	// search homepath
	ospath = FS_BuildOSPath( fs_homepath->string, filename, NULL );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_FOpenFileRead (fs_homepath): %s\n", ospath );
	}

	fd->handleFiles.file.o = Sys_FOpen( ospath, "rb" );
	if ( !fd->handleFiles.file.o )
	{
		// NOTE TTimo on non *nix systems, fs_homepath == fs_basepath, might want to avoid
		if ( Q_stricmp( fs_homepath->string, fs_basepath->string ) != 0 )
		{
			// search basepath
			ospath = FS_BuildOSPath( fs_basepath->string, filename, NULL );

			if ( fs_debug->integer )
			{
				Com_Printf( "FS_SV_FOpenFileRead (fs_basepath): %s\n", ospath );
			}

			fd->handleFiles.file.o = Sys_FOpen( ospath, "rb" );
		}

		// Check fs_steampath too
		if ( !fd->handleFiles.file.o && fs_steampath->string[0] )
		{
			// search steampath
			ospath = FS_BuildOSPath( fs_steampath->string, filename, NULL );

			if ( fs_debug->integer )
			{
				Com_Printf( "FS_SV_FOpenFileRead (fs_steampath): %s\n", ospath );
			}

			fd->handleFiles.file.o = Sys_FOpen( ospath, "rb" );
		}
	}

	if( fd->handleFiles.file.o != NULL ) {
		Q_strncpyz( fd->name, filename, sizeof( fd->name ) );
		fd->handleSync = qfalse;
		fd->zipFile = qfalse;
		*fp = f;
		return FS_FileLength( fd->handleFiles.file.o );
	}

	*fp = FS_INVALID_HANDLE;
	return -1;
}


/*
===========
FS_SV_Rename
===========
*/
void FS_SV_Rename( const char *from, const char *to ) {
	char			*from_ospath, *to_ospath;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

#ifndef DEDICATED
	// don't let sound stutter
	// S_ClearSoundBuffer();
#endif

	from_ospath = FS_BuildOSPath( fs_homepath->string, from, NULL );
	to_ospath = FS_BuildOSPath( fs_homepath->string, to, NULL );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_Rename: %s --> %s\n", from_ospath, to_ospath );
	}

	if ( rename( from_ospath, to_ospath ) ) {
		// Failed, try copying it and deleting the original
		FS_CopyFile( from_ospath, to_ospath );
		FS_Remove( from_ospath );
	}
}


/*
===========
FS_Rename
===========
*/
void FS_Rename( const char *from, const char *to ) {
	char			*from_ospath, *to_ospath;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

#ifndef DEDICATED
	// don't let sound stutter
	// S_ClearSoundBuffer();
#endif

	from_ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, from );
	to_ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, to );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_Rename: %s --> %s\n", from_ospath, to_ospath );
	}

	if ( rename( from_ospath, to_ospath ) ) {
		// Failed, try copying it and deleting the original
		FS_CopyFile( from_ospath, to_ospath );
		FS_Remove( from_ospath );
	}
}


/*
==============
FS_FCloseFile

If the FILE pointer is an open pak file, leave it open.

For some reason, other dll's can't just cal fclose()
on files returned by FS_FOpenFile...
==============
*/
void FS_FCloseFile( fileHandle_t f ) {
	fileHandleData_t *fd;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	fd = &fsh[ f ];

	if ( fd->zipFile && fd->pak ) {
		unzCloseCurrentFile( fd->handleFiles.file.z );
		if ( fd->handleFiles.unique ) {
			unzClose( fd->handleFiles.file.z );
		}
		fd->handleFiles.file.z = NULL;
		fd->zipFile = qfalse;
		fd->pak->used--;
		if ( !fs_locked->integer ) {
			if ( fd->pak->handle && !fd->pak->used ) {
				unzClose( fd->pak->handle );
				fd->pak->handle = NULL;
			}
		}
	} else {
		if ( fd->handleFiles.file.o ) {
			fclose( fd->handleFiles.file.o );
			fd->handleFiles.file.o = NULL;
		}
	}

	Com_Memset( fd, 0, sizeof( *fd ) );
}


/*
===========
FS_FOpenFileWrite
===========
*/
fileHandle_t FS_FOpenFileWrite( const char *filename ) {
	char			*ospath;
	fileHandle_t	f;
	fileHandleData_t *fd;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !filename || !*filename ) {
		return FS_INVALID_HANDLE;
	}

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_FOpenFileWrite: %s\n", ospath );
	}

	FS_CheckFilenameIsNotAllowed( ospath, __func__, qfalse );

	f = FS_HandleForFile();
	fd = &fsh[ f ];
	FS_InitHandle( fd );

	// enabling the following line causes a recursive function call loop
	// when running with +set logfile 1 +set developer 1
	//Com_DPrintf( "writing to: %s\n", ospath );
	fd->handleFiles.file.o = Sys_FOpen( ospath, "wb" );
	if ( fd->handleFiles.file.o == NULL ) {
		if ( FS_CreatePath( ospath ) ) {
			return FS_INVALID_HANDLE;
		}
		fd->handleFiles.file.o = Sys_FOpen( ospath, "wb" );
		if ( fd->handleFiles.file.o == NULL ) {
			return FS_INVALID_HANDLE;
		}
	}

	Q_strncpyz( fd->name, filename, sizeof( fd->name ) );
	fd->handleSync = qfalse;
	fd->zipFile = qfalse;

	return f;
}


/*
===========
FS_FOpenFileAppend
===========
*/
static fileHandle_t FS_FOpenFileAppend( const char *filename ) {
	char			*ospath;
	fileHandleData_t *fd;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !*filename ) {
		return FS_INVALID_HANDLE;
	}

#ifndef DEDICATED
	// don't let sound stutter
	// S_ClearSoundBuffer();
#endif

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_FOpenFileAppend: %s\n", ospath );
	}

	FS_CheckFilenameIsNotAllowed( ospath, __func__, qfalse );

	f = FS_HandleForFile();
	fd = &fsh[ f ];
	FS_InitHandle( fd );

	fd->handleFiles.file.o = Sys_FOpen( ospath, "ab" );
	if ( fd->handleFiles.file.o == NULL ) {
		if ( FS_CreatePath( ospath ) ) {
			return FS_INVALID_HANDLE;
		}
		fd->handleFiles.file.o = Sys_FOpen( ospath, "ab" );
		if ( fd->handleFiles.file.o == NULL ) {
			return FS_INVALID_HANDLE;
		}
	}

	Q_strncpyz( fd->name, filename, sizeof( fd->name ) );
	fd->handleSync = qfalse;
	fd->zipFile = qfalse;

	return f;
}


/*
===========
FS_FilenameCompare

Ignore case and seprator char distinctions
===========
*/
qboolean FS_FilenameCompare( const char *s1, const char *s2 ) {
	int		c1, c2;
	
	do {
		c1 = *s1++;
		c2 = *s2++;

		if ( c1 <= 'Z' && c1 >= 'A' )
			c1 += ('a' - 'A');
		else if ( c1 == '\\' || c1 == ':' )
			c1 = '/';

		if ( c2 <= 'Z' && c2 >= 'A' )
			c2 += ('a' - 'A');
		else if ( c2 == '\\' || c2 == ':' )
			c2 = '/';

		if ( c1 != c2 ) {
			return qtrue;		// strings not equal
		}
	} while ( c1 );
	
	return qfalse;		// strings are equal
}


/*
===========
FS_IsExt

Return qtrue if ext matches file extension filename
===========
*/
static qboolean FS_IsExt( const char *filename, const char *ext, size_t namelen )
{
	size_t extlen;

	extlen = strlen( ext );

	if ( extlen > namelen )
		return qfalse;

	filename += namelen - extlen;

	return !Q_stricmp( filename, ext );
}


/*
===========
FS_StripExt
===========
*/
qboolean FS_StripExt( char *filename, const char *ext )
{
	int extlen, namelen;

	extlen = strlen( ext );
	namelen = strlen( filename );

	if ( extlen > namelen )
		return qfalse;

	filename += namelen - extlen;

	if ( !Q_stricmp( filename, ext ) ) 
	{
		filename[0] = '\0';
		return qtrue;
	}

	return qfalse;
}


static const char *FS_HasExt( const char *fileName, const char **extList, int extCount ) 
{
	const char *e;
	int i;

	e = strrchr( fileName, '.' );

	if ( !e ) 
		return NULL;

	for ( i = 0, e++; i < extCount; i++ ) 
	{
		if ( !Q_stricmp( e, extList[i] ) )
			return e;
	}

	return NULL;
}


static qboolean FS_GeneralRef( const char *filename ) 
{
	// allowed non-ref extensions
	static const char *extList[] = { "config", "shader", "arena", "menu", "bot", "cfg", "txt" };

	if ( FS_HasExt( filename, extList, ARRAY_LEN( extList ) ) )
		return qfalse;
	
	if ( !Q_stricmp( filename, "vm/qagame.qvm" ) )
		return qfalse;

	if ( strstr( filename, "levelshots" ) )
		return qfalse;

	return qtrue;
}


static qboolean FS_DeniedPureFile( const char *filename ) 
{
	// allowed non-ref extensions
	static const char *extList[] = {
		"cfg",	// config files
		"txt",	// config/text files
		"dat",	// misc. data files
		"bot", // bot files
		"c",	// bot files
		"add",	// custom entities
		"set",	// custom entities
		"jpg",	// external hud images
		"tga",	// external hud images
		"png",	// external hud images
		"menu",	// menu files
		"game" // menu files
	};

	if ( FS_HasExt( filename, extList, ARRAY_LEN( extList ) ) )
		return qfalse;
	
	return qtrue;
}


/*
===========
FS_BypassPure
===========
*/
static int numServerPaks;
void FS_BypassPure( void )
{
	numServerPaks = fs_numServerPaks;
	fs_numServerPaks = 0;
}


/*
===========
FS_RestorePure
===========
*/
void FS_RestorePure( void )
{
	fs_numServerPaks = numServerPaks;
}


/*
===========
FS_FOpenFileRead

Finds the file in the search path.
Returns filesize and an open FILE pointer.
Used for streaming data out of either a
separate file or a ZIP file.
===========
*/
extern qboolean		com_fullyInitialized;

int FS_FOpenFileRead( const char *filename, fileHandle_t *file, qboolean uniqueFILE ) {
	searchpath_t	*search;
	char			*netpath;
	pack_t			*pak;
	fileInPack_t	*pakFile;
	directory_t		*dir;
	long			hash;
	long			fullHash;
	unz_s			*zfi;
	FILE			*temp;
	int				length;
	fileHandleData_t *f;

	hash = 0;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !filename ) {
		Com_Error( ERR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed\n" );
	}

	// qpaths are not supposed to have a leading slash
	if ( filename[0] == '/' || filename[0] == '\\' ) {
		filename++;
	}
	
	// we will calculate full hash only once then just mask it by current pack->hashSize
	// we can do that as long as we know properties of our hash function
	fullHash = FS_HashFileName( filename, 0U );

	if ( file == NULL ) {
		// just wants to see if file is there
		for ( search = fs_searchpaths ; search ; search = search->next ) {
			// is the element a pak file?
			if ( search->pack && search->pack->hashTable[ (hash = fullHash & (search->pack->hashSize-1)) ] ) {
				// skip non-pure files
				if ( !FS_PakIsPure( search->pack ) )
					continue;
				// look through all the pak file elements
				pak = search->pack;
				pakFile = pak->hashTable[hash];
				do {
					// case and separator insensitive comparisons
					if ( !FS_FilenameCompare( pakFile->name, filename ) ) {
						// found it!
						return pakFile->size; 
					}
					pakFile = pakFile->next;
				} while ( pakFile != NULL );
			} else if ( search->dir && search->policy != DIR_DENY ) {
				dir = search->dir;
				netpath = FS_BuildOSPath( dir->path, dir->gamedir, filename );
				temp = Sys_FOpen( netpath, "rb" );
				if ( temp ) {
					length = FS_FileLength( temp );
					fclose( temp );
					return length;
				}
			}
		}
		return -1;
	}

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo" 
	if ( FS_CheckDirTraversal( filename ) ) {
		*file = FS_INVALID_HANDLE;
		return -1;
	}

	// make sure the q3key file is only readable by the quake3.exe at initialization
	// any other time the key should only be accessed in memory using the provided functions
	if ( com_fullyInitialized && strstr( filename, "q3key" ) ) {
		*file = FS_INVALID_HANDLE;
		return -1;
	}

	//
	// search through the path, one element at a time
	//

	*file = FS_HandleForFile();
	f = &fsh[ *file ];
	FS_InitHandle( f );

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( search->pack && search->pack->hashTable[ (hash = fullHash & (search->pack->hashSize-1)) ] ) {
			// disregard if it doesn't match one of the allowed pure pak files
			if ( !FS_PakIsPure( search->pack ) ) {
				continue;
			}
			// look through all the pak file elements
			pak = search->pack;
			pakFile = pak->hashTable[hash];
			do {
				// case and separator insensitive comparisons
				if ( !FS_FilenameCompare( pakFile->name, filename ) ) {
					// found it!

					// mark the pak as having been referenced and mark specifics on cgame and ui
					// shaders, txt, arena files  by themselves do not count as a reference as 
					// these are loaded from all pk3s
					// from every pk3 file..
					if ( !( pak->referenced & FS_GENERAL_REF ) && FS_GeneralRef( filename ) ) {
						pak->referenced |= FS_GENERAL_REF;
					}
					if ( !( pak->referenced & FS_CGAME_REF ) && !strcmp( filename, "vm/cgame.qvm" ) ) {
						pak->referenced |= FS_CGAME_REF;
					}
					if ( !( pak->referenced & FS_UI_REF ) && !strcmp( filename, "vm/ui.qvm" ) ) {
						pak->referenced |= FS_UI_REF;
					}

					if ( !pak->handle ) {
						pak->handle = unzOpen( pak->pakFilename );
						if ( !pak->handle ) {
							Com_Printf( S_COLOR_YELLOW "Error opening %s\n", pak->pakBasename );
							pakFile = pakFile->next;
							continue;
						}
					}

					if ( uniqueFILE ) {
						// open a new file on the pakfile
						f->handleFiles.file.z = unzReOpen( pak->pakFilename, pak->handle );
						if ( f->handleFiles.file.z == NULL ) {
							Com_Error( ERR_FATAL, "Couldn't reopen %s", pak->pakFilename );
						}
					} else {
						f->handleFiles.file.z = pak->handle;
					}
					Q_strncpyz( f->name, filename, sizeof( f->name ) );
					zfi = (unz_s *)f->handleFiles.file.z;
					// in case the file was new
					temp = zfi->file;
					// set the file position in the zip file (also sets the current file info)
					unzSetCurrentFileInfoPosition(pak->handle, pakFile->pos);
					// copy the file info into the unzip structure
					Com_Memcpy( zfi, pak->handle, sizeof(unz_s) );
					// we copy this back into the structure
					zfi->file = temp;
					// open the file in the zip
					unzOpenCurrentFile( f->handleFiles.file.z );
					f->zipFilePos = pakFile->pos;
					f->zipFileLen = pakFile->size;
					f->zipFile = qtrue;
					f->handleFiles.unique = uniqueFILE;
					f->pakIndex = pak->index;
					fs_lastPakIndex = pak->index;
					f->pak = pak;
					pak->used++;

					if ( fs_debug->integer ) {
						Com_Printf( "FS_FOpenFileRead: %s (found in '%s')\n", 
							filename, pak->pakFilename );
					}
					return zfi->cur_file_info.uncompressed_size;
				}
				pakFile = pakFile->next;
			} while ( pakFile != NULL );
		} else if ( search->dir && search->policy != DIR_DENY ) {
			// check a file in the directory tree

			// if we are running restricted, the only files we
			// will allow to come from the directory are .cfg files

			// FIXME TTimo I'm not sure about the fs_numServerPaks test
			// if you are using FS_ReadFile to find out if a file exists,
			//   this test can make the search fail although the file is in the directory
			// I had the problem on https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=8
			// turned out I used FS_FileExists instead
			if ( fs_numServerPaks ) {
				if ( FS_DeniedPureFile( filename ) ) {
					continue;
				}
			}

			dir = search->dir;
			
			netpath = FS_BuildOSPath( dir->path, dir->gamedir, filename );
			
			f->handleFiles.file.o = Sys_FOpen( netpath, "rb" );
			if ( f->handleFiles.file.o == NULL ) {
				continue;
			}

			Q_strncpyz( f->name, filename, sizeof( f->name ) );
			f->zipFile = qfalse;

			if ( fs_debug->integer ) {
				Com_Printf( "FS_FOpenFileRead: %s (found in '%s/%s')\n", filename,
					dir->path, dir->gamedir );
			}

			return FS_FileLength( f->handleFiles.file.o );
		}		
	}
	
#ifdef FS_MISSING
	if (missingFiles) {
		fprintf(missingFiles, "%s\n", filename);
	}
#endif

	*file = FS_INVALID_HANDLE;
	return -1;
}


/*
===========
FS_Home_FOpenFileRead
===========
*/
int FS_Home_FOpenFileRead( const char *filename, fileHandle_t *file ) 
{
	char path[ MAX_OSPATH*3 + 1 ];
	fileHandleData_t *fd;
	fileHandle_t f;	

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	// should never happen but for safe
	if ( !file ) { 
		return -1;
	}

	// allocate new file handle
	f = FS_HandleForFile(); 
	fd = &fsh[ f ];
	FS_InitHandle( fd );

	Com_sprintf( path, sizeof( path ), "%s%c%s%c%s", fs_homepath->string,
		PATH_SEP, fs_gamedir, PATH_SEP, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "%s: %s\n", __func__, path );
	}

	fd->handleFiles.file.o = Sys_FOpen( path, "rb" );
	if ( fd->handleFiles.file.o != NULL ) {
		Q_strncpyz( fd->name, filename, sizeof( fd->name ) );
		fd->handleSync = qfalse;
		fd->zipFile = qfalse;
		*file = f;
		return FS_FileLength( fd->handleFiles.file.o );
	}

	*file = FS_INVALID_HANDLE;
	return -1;
}


/*
=================
FS_Read

Properly handles partial reads
=================
*/
int FS_Read( void *buffer, int len, fileHandle_t f ) {
	int		block, remaining;
	int		read;
	byte	*buf;
	int		tries;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( f <= 0 || f >= MAX_FILE_HANDLES ) {
		return 0;
	}

	buf = (byte *)buffer;
	fs_readCount += len;

	if ( !fsh[f].zipFile ) {
		remaining = len;
		tries = 0;
		while (remaining) {
			block = remaining;
			read = fread( buf, 1, block, fsh[f].handleFiles.file.o );
			if (read == 0) {
				// we might have been trying to read from a CD, which
				// sometimes returns a 0 read on windows
				if (!tries) {
					tries = 1;
				} else {
					return len-remaining;	//Com_Error (ERR_FATAL, "FS_Read: 0 bytes read");
				}
			}

			if (read == -1) {
				Com_Error (ERR_FATAL, "FS_Read: -1 bytes read");
			}

			remaining -= read;
			buf += read;
		}
		return len;
	} else {
		return unzReadCurrentFile( fsh[f].handleFiles.file.z, buffer, len );
	}
}


/*
=================
FS_Write

Properly handles partial writes
=================
*/
int FS_Write( const void *buffer, int len, fileHandle_t h ) {
	int		block, remaining;
	int		written;
	byte	*buf;
	int		tries;
	FILE	*f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	//if ( h <= 0 || h >= MAX_FILE_HANDLES ) {
	//	return 0;
	//}

	f = FS_FileForHandle(h);
	buf = (byte *)buffer;

	remaining = len;
	tries = 0;
	while (remaining) {
		block = remaining;
		written = fwrite (buf, 1, block, f);
		if (written == 0) {
			if (!tries) {
				tries = 1;
			} else {
				Com_Printf( "FS_Write: 0 bytes written\n" );
				return 0;
			}
		}

		if (written == -1) {
			Com_Printf( "FS_Write: -1 bytes written\n" );
			return 0;
		}

		remaining -= written;
		buf += written;
	}
	if ( fsh[h].handleSync ) {
		fflush( f );
	}
	return len;
}

void QDECL FS_Printf( fileHandle_t h, const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	Q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	FS_Write(msg, strlen(msg), h);
}

#define PK3_SEEK_BUFFER_SIZE 65536

/*
=================
FS_Seek

=================
*/
int FS_Seek( fileHandle_t f, long offset, fsOrigin_t origin ) {
	int		_origin;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
		return -1;
	}

	if ( fsh[f].zipFile == qtrue ) {
		//FIXME: this is really, really crappy
		//(but better than what was here before)
		byte	buffer[PK3_SEEK_BUFFER_SIZE];
		int		remainder;
		int		currentPosition = FS_FTell( f );

		// change negative offsets into FS_SEEK_SET
		if ( offset < 0 ) {
			switch( origin ) {
				case FS_SEEK_END:
					remainder = fsh[f].zipFileLen + offset;
					break;

				case FS_SEEK_CUR:
					remainder = currentPosition + offset;
					break;

				case FS_SEEK_SET:
				default:
					remainder = 0;
					break;
			}

			if ( remainder < 0 ) {
				remainder = 0;
			}

			origin = FS_SEEK_SET;
		} else {
			if ( origin == FS_SEEK_END ) {
				remainder = fsh[f].zipFileLen - currentPosition + offset;
			} else {
				remainder = offset;
			}
		}

		switch( origin ) {
			case FS_SEEK_SET:
				if ( remainder == currentPosition ) {
					return offset;
				}
				unzSetCurrentFileInfoPosition( fsh[f].handleFiles.file.z, fsh[f].zipFilePos );
				unzOpenCurrentFile( fsh[f].handleFiles.file.z );
				//fallthrough

			case FS_SEEK_END:
			case FS_SEEK_CUR:
				while( remainder > PK3_SEEK_BUFFER_SIZE ) {
					FS_Read( buffer, PK3_SEEK_BUFFER_SIZE, f );
					remainder -= PK3_SEEK_BUFFER_SIZE;
				}
				FS_Read( buffer, remainder, f );
				return offset;

			default:
				Com_Error( ERR_FATAL, "Bad origin in FS_Seek" );
				return -1;
		}
	} else {
		FILE *file;
		file = FS_FileForHandle( f );
		switch( origin ) {
		case FS_SEEK_CUR:
			_origin = SEEK_CUR;
			break;
		case FS_SEEK_END:
			_origin = SEEK_END;
			break;
		case FS_SEEK_SET:
			_origin = SEEK_SET;
			break;
		default:
			Com_Error( ERR_FATAL, "Bad origin in FS_Seek" );
			return -1;
		}

		return fseek( file, offset, _origin );
	}
}


/*
======================================================================================

CONVENIENCE FUNCTIONS FOR ENTIRE FILES

======================================================================================
*/

qboolean FS_FileIsInPAK( const char *filename, int *pChecksum, char *pakName ) {
	const searchpath_t	*search;
	const pack_t		*pak;
	const fileInPack_t	*pakFile;
	long			hash;
	long			fullHash;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !filename ) {
		Com_Error( ERR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed" );
	}

	// qpaths are not supposed to have a leading slashes
	while ( filename[0] == '/' || filename[0] == '\\' )
		filename++;

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo" 
	if ( FS_CheckDirTraversal( filename ) ) {
		return qfalse;
	}

	fullHash = FS_HashFileName( filename, 0U );

	//
	// search through the path, one element at a time
	//
	for ( search = fs_searchpaths ; search ; search = search->next ) {

		// is the element a pak file?
		if ( search->pack && search->pack->hashTable[ (hash = fullHash & (search->pack->hashSize-1)) ] ) {
			// disregard if it doesn't match one of the allowed pure pak files
			//if ( !FS_PakIsPure( search->pack ) ) {
			//	continue;
			//}

			// look through all the pak file elements
			pak = search->pack;
			pakFile = pak->hashTable[hash];
			do {
				// case and separator insensitive comparisons
				if ( !FS_FilenameCompare( pakFile->name, filename ) ) {
					if ( pChecksum ) {
						*pChecksum = pak->pure_checksum;
					}
					if ( pakName ) {
						Com_sprintf( pakName, MAX_OSPATH, "%s/%s", pak->pakGamename, pak->pakBasename );
					}
					return qtrue;
				}
				pakFile = pakFile->next;
			} while ( pakFile != NULL );
		}
	}
	return qfalse;
}


/*
============
FS_ReadFile

Filename are relative to the quake search path
a null buffer will just return the file length without loading
============
*/
int FS_ReadFile( const char *qpath, void **buffer ) {
	fileHandle_t	h;
	byte*			buf;
	qboolean		isConfig;
	long			len;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !qpath || !qpath[0] ) {
		Com_Error( ERR_FATAL, "FS_ReadFile with empty name" );
	}

	buf = NULL;	// quiet compiler warning

	// if this is a .cfg file and we are playing back a journal, read
	// it from the journal file
	if ( strstr( qpath, ".cfg" ) ) {
		isConfig = qtrue;
		if ( com_journal && com_journal->integer == 2 ) {
			int		r;

			Com_DPrintf( "Loading %s from journal file.\n", qpath );
			r = FS_Read( &len, sizeof( len ), com_journalDataFile );
			if ( r != sizeof( len ) ) {
				if (buffer != NULL) *buffer = NULL;
				return -1;
			}
			// if the file didn't exist when the journal was created
			if (!len) {
				if (buffer == NULL) {
					return 1;			// hack for old journal files
				}
				*buffer = NULL;
				return -1;
			}
			if (buffer == NULL) {
				return len;
			}

			buf = Hunk_AllocateTempMemory(len+1);
			*buffer = buf;

			r = FS_Read( buf, len, com_journalDataFile );
			if ( r != len ) {
				Com_Error( ERR_FATAL, "Read from journalDataFile failed" );
			}

			fs_loadCount++;
			fs_loadStack++;

			// guarantee that it will have a trailing 0 for string operations
			buf[len] = '\0';

			return len;
		}
	} else {
		isConfig = qfalse;
	}

	// look for it in the filesystem or pack files
	len = FS_FOpenFileRead( qpath, &h, qfalse );
	if ( h == FS_INVALID_HANDLE ) {
		if ( buffer ) {
			*buffer = NULL;
		}
		// if we are journalling and it is a config file, write a zero to the journal file
		if ( isConfig && com_journal && com_journal->integer == 1 ) {
			Com_DPrintf( "Writing zero for %s to journal file.\n", qpath );
			len = 0;
			FS_Write( &len, sizeof( len ), com_journalDataFile );
			FS_Flush( com_journalDataFile );
		}
		return -1;
	}
	
	if ( !buffer ) {
		if ( isConfig && com_journal && com_journal->integer == 1 ) {
			Com_DPrintf( "Writing len for %s to journal file.\n", qpath );
			FS_Write( &len, sizeof( len ), com_journalDataFile );
			FS_Flush( com_journalDataFile );
		}
		FS_FCloseFile( h );
		return len;
	}

	buf = Hunk_AllocateTempMemory( len + 1 );
	*buffer = buf;

	FS_Read( buf, len, h );

	fs_loadCount++;
	fs_loadStack++;

	// guarantee that it will have a trailing 0 for string operations
	buf[ len ] = '\0';
	FS_FCloseFile( h );

	// if we are journalling and it is a config file, write it to the journal file
	if ( isConfig && com_journal && com_journal->integer == 1 ) {
		Com_DPrintf( "Writing %s to journal file.\n", qpath );
		FS_Write( &len, sizeof( len ), com_journalDataFile );
		FS_Write( buf, len, com_journalDataFile );
		FS_Flush( com_journalDataFile );
	}
	return len;
}


/*
=============
FS_FreeFile
=============
*/
void FS_FreeFile( void *buffer ) {
	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}
	if ( !buffer ) {
		Com_Error( ERR_FATAL, "FS_FreeFile( NULL )" );
	}
	fs_loadStack--;

	Hunk_FreeTempMemory( buffer );

	// if all of our temp files are free, clear all of our space
	if ( fs_loadStack == 0 ) {
		Hunk_ClearTempMemory();
	}
}


/*
============
FS_WriteFile

Filename are relative to the quake search path
============
*/
void FS_WriteFile( const char *qpath, const void *buffer, int size ) {
	fileHandle_t f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !qpath || !buffer ) {
		Com_Error( ERR_FATAL, "FS_WriteFile: NULL parameter" );
	}

	f = FS_FOpenFileWrite( qpath );
	if ( f == FS_INVALID_HANDLE ) {
		Com_Printf( "Failed to open %s\n", qpath );
		return;
	}

	FS_Write( buffer, size, f );

	FS_FCloseFile( f );
}



/*
==========================================================================

ZIP FILE LOADING

==========================================================================
*/
static int FS_PakHashSize( const int filecount )
{
	int hashSize;

	for ( hashSize = 2; hashSize < MAX_FILEHASH_SIZE; hashSize <<= 1 ) {
		if ( hashSize >= filecount ) {
			break;
		}
	}

	return hashSize;
}

#ifdef USE_PK3_CACHE

#define PK3_HASH_SIZE 512

static void FS_FreePak( pack_t *pak );

static pack_t *pakHashTable[ PK3_HASH_SIZE ];

#ifdef USE_PK3_CACHE_FILE

#define CACHE_FILE_NAME "pk3cache.dat"

#define CACHE_SYNC_CONDITION ( fs_paksReaded + fs_paksSkipped + fs_paksReleased >= 8 )

static int fs_paksCached;	// readed from cache file
static int fs_paksSkipped;	// outdated/non-existent cache file pk3 entries

static int fs_paksReaded;	// actually readed from the disk
static int fs_paksReleased;	// unreferenced paks since last FS restart

static qboolean fs_cacheLoaded = qfalse;
static qboolean fs_cacheSynced = qtrue;

#pragma pack( push, 1 )

// platform-specific 4-byte signature:
// 0: [version] anything following depends from it
// 1: [endianess] 0 - LSB, 1 - MSB
// 2: [path separation] '/' or '\\'
// 3: [size of file offset and file time]
// non-matching header will cause whole file being ignored
static const byte cache_header[ 4 ] = {
	0, //version
#ifdef Q3_LITTLE_ENDIAN
	0x0,
#else
	0x1,
#endif
	PATH_SEP,
	( ( sizeof( fileOffset_t ) - 1 ) << 4 ) | ( sizeof( fileTime_t ) - 1 )
};

typedef struct pk3cacheHeader_s {
	int pakNameLen;		// full path
	int namesLen;
	int numFiles;
	int numHeaderLongs; // including first uninitialized
	int contentLen;
	fileTime_t ctime;	// creation/status change time
	fileTime_t mtime;	// modification time
	fileOffset_t size;	// zip file size
} pk3cacheHeader_t;

typedef struct pk3cacheFileItem_s {
	unsigned long name; // offset in namebuffer
	unsigned long size;
	unsigned long pos;	// info position in pk3 file
} pk3cacheFileItem_t;

#pragma pack( pop )

#endif // USE_PK3_CACHE_FILE


static int FS_HashPK3( const char *name )
{
	int c, hash = 0;
	while ( (c = *name++) != '\0' )
	{
		hash = hash * 101 + c;
	}
	hash = hash ^ (hash >> 16);
	return hash & (PK3_HASH_SIZE-1);
}


static pack_t *FS_FindInCache( const char *zipfile )
{
	pack_t *pack;
	int hash;

	hash = FS_HashPK3( zipfile );
	pack = pakHashTable[ hash ];
	while ( pack )
	{
		if ( !strcmp( zipfile, pack->pakFilename ) )
		{
			return pack;
		}
		pack = pack->next;
	}

	return NULL;
}


static void FS_AddToCache( pack_t *pack )
{
	pack->namehash = FS_HashPK3( pack->pakFilename );
	pack->next = pakHashTable[ pack->namehash ];
	pack->prev = NULL;
	if ( pakHashTable[ pack->namehash ] )
		pakHashTable[ pack->namehash ]->prev = pack;
	pakHashTable[ pack->namehash ] = pack;
}


static void FS_RemoveFromCache( pack_t *pack )
{
	if ( !pack->next && !pack->prev && pakHashTable[ pack->namehash ] != pack )
	{
		Com_Error( ERR_FATAL, "Invalid pak link" );
	} 

	if ( pack->prev != NULL )
		pack->prev->next = pack->next;
	else
		pakHashTable[ pack->namehash ] = pack->next;

	if ( pack->next != NULL )
		pack->next->prev = pack->prev;
}


static pack_t *FS_LoadCachedPK3( const char *zipfile )
{
	fileOffset_t size;
	fileTime_t mtime;
	fileTime_t ctime;
	pack_t *pak;

	pak = FS_FindInCache( zipfile );
	if ( pak == NULL )
		return NULL;
	
	if ( !Sys_GetFileStats( zipfile, &size, &mtime, &ctime ) )
	{
		FS_RemoveFromCache( pak );
		FS_FreePak( pak );
		return NULL;
	}

	if ( pak->size != size || pak->mtime != mtime || pak->ctime != ctime )
	{
		// release outdated information
		FS_RemoveFromCache( pak );
		FS_FreePak( pak );
		return NULL;
	}

	return pak;
}


static void FS_InsertPK3ToCache( pack_t *pak )
{
	if ( Sys_GetFileStats( pak->pakFilename, &pak->size, &pak->mtime, &pak->ctime ) )
	{
		FS_AddToCache( pak );
		pak->touched = qtrue;
	}
}


static void FS_ResetCacheReferences( void )
{
	pack_t *pak;
	int i;
	for ( i = 0; i < ARRAY_LEN( pakHashTable ); i++ )
	{
		pak = pakHashTable[ i ];
		while ( pak )
		{
			pak->touched = qfalse;
			pak->referenced = 0;
			pak = pak->next;
		}
	}
}


static void FS_FreeUnusedCache( void )
{
	pack_t *next, *pak;
	int i;

	for ( i = 0; i < ARRAY_LEN( pakHashTable ); i++ )
	{
		pak = pakHashTable[ i ];
		while ( pak ) 
		{
			next = pak->next;
			if ( !pak->touched )
			{
				FS_RemoveFromCache( pak );
				FS_FreePak( pak );
#ifdef USE_PK3_CACHE_FILE
				fs_paksReleased++;
#endif
			}
			pak = next;
		}
	}
}


#ifdef USE_PK3_CACHE_FILE

void FS_WriteCacheHeader( FILE *f )
{
	fwrite( cache_header, sizeof( cache_header ), 1, f );
}


static qboolean FS_ValidateCacheHeader( FILE *f )
{
	byte buf[ sizeof(cache_header) ];

	if ( fread( buf, sizeof( buf ), 1, f ) != 1 )
		return qfalse;

	if ( memcmp( buf, cache_header, sizeof( buf ) ) != 0 )
		return qfalse;

	return qtrue;
}


static qboolean FS_SavePackToFile( const pack_t *pak, FILE *f )
{
	const char *namePtr;
	const char *pakName;
	int i, pakNameLen;
	pk3cacheHeader_t pk;
	pk3cacheFileItem_t it;
	int namesLen, contentLen;
	
	namePtr = (char*)(pak->buildBuffer + pak->numfiles);

	pakName = pak->pakFilename;
	pakNameLen = (int) strlen( pakName ) + 1;
	pakNameLen = PAD( pakNameLen, sizeof( int ) );

	namesLen = pakName - namePtr;

	// file content length
	contentLen = 0;
#if 0
	for ( i = 0; i < pak->numfiles; i++ )
	{
		if ( pak->buildBuffer[ i ].data && pak->buildBuffer[ i ].size ) 
		{
			contentLen += sizeof( int ) + PAD( pak->buildBuffer[ i ].size, sizeof( int ) );
		}
	}
#endif

	// pak filename length
	pk.pakNameLen = pakNameLen;
	// filenames length
	pk.namesLen = namesLen;
	// number of files
	pk.numFiles = pak->numfiles;
	// number of checksums
	pk.numHeaderLongs = pak->numHeaderLongs;
	// content of some files
	pk.contentLen = contentLen;
	// creation/status change time
	pk.ctime = pak->ctime;
	// modification time
	pk.mtime = pak->mtime;
	// pak file size
	pk.size = pak->size;

	// dump header
	fwrite( &pk, sizeof( pk ), 1, f );

	// pak filename
	fwrite( pakName, pakNameLen, 1, f );

	// filenames
	fwrite( namePtr, namesLen, 1, f );

	// file entries
	for ( i = 0; i < pak->numfiles; i++ )
	{
		it.name = (unsigned long)(pak->buildBuffer[i].name - namePtr);
		it.size = pak->buildBuffer[i].size;
		it.pos = pak->buildBuffer[i].pos;
		fwrite( &it, sizeof( it ), 1, f );
	}

	// pure checksums, excluding first uninitialized
	fwrite( pak->headerLongs + 1, (pak->numHeaderLongs - 1) * sizeof( pak->headerLongs[0] ), 1, f );

#if 0
	if ( contentLen )
	{
		const fileInPack_t *currFile = pak->buildBuffer;
		for ( i = 0; i < pak->numfiles; i++, currFile++ )
		{
			if ( currFile->data && currFile->size ) {
				// file index
				fwrite( &i, sizeof( i ), 1, f );
				// file data
				fwrite( currFile->data, PAD( currFile->size, sizeof( int ) ), 1, f );
			}
		}
	}
#endif

	return qtrue;
}


static qboolean FS_LoadPakFromFile( FILE *f )
{
	fileTime_t ctime, mtime;
	fileOffset_t fsize;
	fileInPack_t *curFile;
	char pakName[ PAD( MAX_OSPATH*3+1, sizeof( int ) ) ];
	char pakBase[ PAD( MAX_OSPATH, sizeof( int ) ) ], *basename;
	pk3cacheHeader_t pk;
	pk3cacheFileItem_t it;
	pack_t *pack;
	char *namePtr;
	int size, i;
	int pakBaseLen;
	int hashSize;
	long hash;

	if ( fread( &pk, sizeof( pk ), 1, f ) != 1 )
		return qfalse; // probably EOF

	/// validate header data

	if ( pk.pakNameLen > sizeof( pakName ) || pk.pakNameLen & 3 || pk.pakNameLen == 0 )
	{
		//Com_Printf( "bad pakNameLen: %08X\n", pk.pakNameLen );
		return qfalse;
	}

	if ( pk.namesLen & 3 || pk.namesLen < pk.numFiles )
	{
		//Com_Printf( "bad namesLen: %i\n", pk.namesLen );
		return qfalse;
	}

	if ( pk.numHeaderLongs == 0 || pk.numHeaderLongs > pk.numFiles + 1 )
	{
		//Com_Printf( "bad numHeaderLongs: %i\n", pk.numHeaderLongs );
		return qfalse;
	}

	if ( pk.contentLen & 3 || pk.contentLen < 0 )
	{
		//Com_Printf( "bad contentLen: %i\n", pk.contentLen );
		return qfalse;
	}

	// load filename
	if ( fread( pakName, pk.pakNameLen, 1, f ) != 1 )
	{
		//Com_Printf( "error reading pakname\n" );
		return qfalse;
	}

	// pakName must be zero-terminated
	if ( pakName[ pk.pakNameLen - 1 ] != '\0' )
	{
		//Com_Printf( "pakname is not zero-terminated!\n" );
		return qfalse;
	}

	if ( !Sys_GetFileStats( pakName, &fsize, &mtime, &ctime ) || fsize != pk.size || mtime != pk.mtime || ctime != pk.ctime )
	{
		const int seek_len = pk.namesLen + pk.numFiles * sizeof( it ) + (pk.numHeaderLongs-1) * sizeof( pack->headerLongs[0] ) + pk.contentLen;
		if ( fseek( f, seek_len, SEEK_CUR ) != 0 )
		{
			return qfalse;
		}
		else
		{
			fs_paksSkipped++;
			return qtrue; // just outdated info, we can continue
		}
	}

	// extract basename from zip path
	basename = strrchr( pakName, PATH_SEP );
	if ( basename == NULL )
		basename = pakName;
	else
		basename++;

	Q_strncpyz( pakBase, basename, sizeof( pakBase ) );
	FS_StripExt( pakBase, ".pk3" );
	pakBaseLen = (int) strlen( pakBase ) + 1;
	pakBaseLen = PAD( pakBaseLen, sizeof( int ) );

	hashSize = FS_PakHashSize( pk.numFiles );

	size = sizeof( *pack ) + pk.namesLen + pk.numFiles * sizeof( pack->buildBuffer[0] );
	size += hashSize * sizeof( pack->hashTable[0] );
	size += pk.pakNameLen;
	size += pakBaseLen;
	size += pk.numHeaderLongs * sizeof( pack->headerLongs[0] );

	pack = Z_TagMalloc( size, TAG_PACK );
	Com_Memset( pack, 0, size );

	pack->mtime = pk.mtime;
	pack->ctime = pk.ctime;
	pack->size = pk.size;

//	pack->handle = uf;
	pack->numfiles = pk.numFiles;
	pack->numHeaderLongs = pk.numHeaderLongs;

	// setup memory layout
	pack->hashSize = hashSize;
	pack->hashTable = (fileInPack_t **)( pack + 1 );

	pack->buildBuffer = (fileInPack_t*)( pack->hashTable + pack->hashSize );

	namePtr = (char*)( pack->buildBuffer + pack->numfiles );

	pack->pakFilename = (char*)( namePtr + pk.namesLen );
	pack->pakBasename = (char*)( pack->pakFilename + pk.pakNameLen );
	pack->headerLongs = (int*)( pack->pakBasename + pakBaseLen );

	strcpy( pack->pakFilename, pakName );
	strcpy( pack->pakBasename, pakBase );

	if ( fread( namePtr, pk.namesLen, 1, f ) != 1 )
	{
		//Com_Printf( "error reading pak filenames\n" );
		goto __error;
	}

	// filenames buffer must be zero-terminated
	if ( namePtr[ pk.namesLen - 1 ] != '\0' )
	{
		//Com_Printf( "not zero terminated filenames\n" );
		goto __error;
	}

	curFile = pack->buildBuffer;
	for ( i = 0; i < pack->numfiles; i++ )
	{
		if ( fread( &it, sizeof( it ), 1, f ) != 1 )
		{
			//Com_Printf( "error reading file item[%i]\n", i );
			goto __error;
		}
		if ( it.name >= pk.namesLen )
		{
			//Com_Printf( "bad name offset: %i (expecting less than %i)\n", it.name, pk.namesLen );
			goto __error;
		}
		curFile->name = namePtr + it.name;
		curFile->size = it.size;
		curFile->pos = it.pos;

		// update hash table
		hash = FS_HashFileName( curFile->name, pack->hashSize );
		curFile->next = pack->hashTable[ hash ];
		pack->hashTable[ hash ] = curFile;

		curFile++;
	}

	if ( fread( pack->headerLongs + 1, ( pack->numHeaderLongs - 1 ) * sizeof( pack->headerLongs[0] ), 1, f ) != 1 )
	{
		//Com_Printf( "error reading headerLongs\n" );
		goto __error;
	}

	pack->checksumFeed = fs_checksumFeed;
	pack->headerLongs[ 0 ] = LittleLong( fs_checksumFeed );

	pack->checksum = Com_BlockChecksum( pack->headerLongs + 1, sizeof( pack->headerLongs[0] ) * ( pack->numHeaderLongs - 1 ) );
	pack->checksum = LittleLong( pack->checksum );

	pack->pure_checksum = Com_BlockChecksum( pack->headerLongs, sizeof( pack->headerLongs[0] ) * pack->numHeaderLongs );
	pack->pure_checksum = LittleLong( pack->pure_checksum );

	// seek through unused content
	if ( pk.contentLen > 0 )
	{
		if ( fseek( f, pk.contentLen, SEEK_CUR ) != 0 )
			goto __error;
	}
	else if ( pk.contentLen < 0 )
	{
		goto __error;
	}

	fs_paksCached++;

	FS_InsertPK3ToCache( pack );

	return qtrue;

__error:
	FS_FreePak( pack );
	return qfalse;
}


/*
============
FS_SaveCache

Called at th end of FS_Startup() after releasing unused paks
============
*/
static qboolean FS_SaveCache( void )
{
	const char *filename = CACHE_FILE_NAME;
	const char *ospath;
	const searchpath_t *sp;
	FILE *f;

	if ( !fs_searchpaths )
		return qfalse;

	if ( !fs_cacheLoaded )
	{
		Com_DPrintf( "synced FS cache on startup\n" );
		fs_cacheSynced = qfalse;
		fs_cacheLoaded = qtrue;
	}
	else if ( CACHE_SYNC_CONDITION )
	{
		Com_DPrintf( "synced FS cache on readed=%i, released=%i, skipped=%i\n",
			fs_paksReaded, fs_paksReleased, fs_paksSkipped );
		fs_cacheSynced = qfalse;
	}

	if ( fs_cacheSynced )
		return qtrue;

	sp = fs_searchpaths;

	ospath = FS_BuildOSPath( fs_homepath->string, filename, NULL );

	f = Sys_FOpen( ospath, "wb" );
	if ( f == NULL )
		return qfalse;

	FS_WriteCacheHeader( f );

	while ( sp != NULL )
	{
		if ( sp->pack )
		{
			FS_SavePackToFile( sp->pack, f );
		}
		sp = sp->next;
	}

	fclose( f );

	fs_paksReleased = 0;
	fs_paksSkipped = 0;
	fs_paksReaded = 0;

	fs_cacheSynced = qtrue;

	return qtrue;
}


/*
============
FS_LoadCache

Called at FS_Startup() before loading any pk3 file
============
*/
static void FS_LoadCache( void )
{
	const char *filename = CACHE_FILE_NAME;
	const char *ospath;
	FILE *f;

	fs_paksReaded = 0;
	fs_paksReleased = 0;

	if ( fs_cacheLoaded )
		return;

	fs_paksCached = 0;
	fs_paksSkipped = 0;

	ospath = FS_BuildOSPath( fs_homepath->string, filename, NULL );

	f = Sys_FOpen( ospath, "rb" );
	if ( f == NULL )
		return;

	if ( !FS_ValidateCacheHeader( f ) )
	{
		fclose( f );
		return;
	}

	while ( FS_LoadPakFromFile( f ) )
		;

	fclose( f );

	fs_cacheLoaded = qtrue;

	Com_Printf( "...found %i cached paks\n", fs_paksCached );
}

#endif // USE_PK3_CACHE_FILE

#endif // USE_PK3_CACHE


/*
=================
FS_LoadZipFile

Creates a new pak_t in the search chain for the contents
of a zip file.
=================
*/
static pack_t *FS_LoadZipFile( const char *zipfile )
{
	fileInPack_t	*curFile;
	pack_t			*pack;
	unzFile			uf;
	int				err;
	unz_global_info gi;
	char			filename_inzip[MAX_ZPATH];
	unz_file_info	file_info;
	unsigned int	i, namelen, hashSize, size;
	long			hash;
	int				fs_numHeaderLongs;
	int				*fs_headerLongs;
	int				filecount;
	char			*namePtr;
	const char		*basename;
	int				fileNameLen;
	int				baseNameLen;

#ifdef USE_PK3_CACHE
	pack = FS_LoadCachedPK3( zipfile );
	if ( pack )
	{
		// update pure checksum
		if ( pack->checksumFeed != fs_checksumFeed )
		{
			pack->headerLongs[ 0 ] = LittleLong( fs_checksumFeed );
			pack->pure_checksum = Com_BlockChecksum( pack->headerLongs, sizeof( pack->headerLongs[0] ) * pack->numHeaderLongs );
			pack->pure_checksum = LittleLong( pack->pure_checksum );
			pack->checksumFeed = fs_checksumFeed;
		}

		pack->touched = qtrue;
		return pack; // loaded from cache
	}
#endif

	// extract basename from zip path
	basename = strrchr( zipfile, PATH_SEP );
	if ( basename == NULL ) {
		basename = zipfile;
	} else {
		basename++;
	}

	fileNameLen = (int) strlen( zipfile ) + 1;
	baseNameLen = (int) strlen( basename ) + 1;

	uf = unzOpen(zipfile);
	err = unzGetGlobalInfo (uf,&gi);

	if (err != UNZ_OK)
		return NULL;

	namelen = 0;
	filecount = 0;
	unzGoToFirstFile( uf );
	for (i = 0; i < gi.number_entry; i++)
	{
		err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
		filename_inzip[sizeof(filename_inzip)-1] = '\0';
		if (err != UNZ_OK) {
			break;
		}
		if ( file_info.compression_method != 0 && file_info.compression_method != 8 /*Z_DEFLATED*/ ) {
			Com_Printf( S_COLOR_YELLOW "%s|%s: unsupported compression method %i\n", basename, filename_inzip, (int)file_info.compression_method );
			unzGoToNextFile( uf );
			continue;
		} 
		namelen += strlen( filename_inzip ) + 1;
		unzGoToNextFile( uf );
		filecount++;
	}

	if ( filecount == 0 ) {
		unzClose( uf );
		return NULL;
	}

	// get the hash table size from the number of files in the zip
	// because lots of custom pk3 files have less than 32 or 64 files
	hashSize = FS_PakHashSize( filecount );

	namelen = PAD( namelen, sizeof( int ) );
	size = sizeof( *pack ) + hashSize * sizeof( pack->hashTable[0] ) + filecount * sizeof( pack->buildBuffer[0] ) + namelen;
	size += PAD( fileNameLen, sizeof( int ) );
	size += PAD( baseNameLen, sizeof( int ) );
#ifdef USE_PK3_CACHE
	size += ( filecount + 1 ) * sizeof( fs_headerLongs[0] );
#endif
	pack = Z_TagMalloc( size, TAG_PACK );
	Com_Memset( pack, 0, size );

	pack->handle = uf;
	pack->numfiles = filecount;
	pack->hashSize = hashSize;
	pack->hashTable = (fileInPack_t **)( pack + 1 );

	pack->buildBuffer = (fileInPack_t*)( pack->hashTable + pack->hashSize );
	namePtr = (char*)( pack->buildBuffer + filecount );

	pack->pakFilename = (char*)( namePtr + namelen );
	pack->pakBasename = (char*)( pack->pakFilename + PAD( fileNameLen, sizeof( int ) ) );

#ifdef USE_PK3_CACHE
	fs_headerLongs = (int*)( pack->pakBasename + PAD( baseNameLen, sizeof( int ) ) );
#else
	fs_headerLongs = Z_Malloc( ( filecount + 1 ) * sizeof( fs_headerLongs[0] ) );
#endif

	fs_numHeaderLongs = 0;
	fs_headerLongs[ fs_numHeaderLongs++ ] = LittleLong( fs_checksumFeed );

	Com_Memcpy( pack->pakFilename, zipfile, fileNameLen );
	Com_Memcpy( pack->pakBasename, basename, baseNameLen );

	// strip .pk3 if needed
	FS_StripExt( pack->pakBasename, ".pk3" );

	unzGoToFirstFile( uf );
	curFile = pack->buildBuffer;
	for ( i = 0; i < gi.number_entry; i++ )
	{
		err = unzGetCurrentFileInfo( uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0 );
		filename_inzip[sizeof(filename_inzip)-1] = '\0';
		if (err != UNZ_OK) {
			break;
		}
		if ( file_info.compression_method != 0 && file_info.compression_method != 8 /*Z_DEFLATED*/ ) {
			unzGoToNextFile( uf );
			continue;
		} 
		if ( file_info.uncompressed_size > 0 ) {
			fs_headerLongs[fs_numHeaderLongs++] = LittleLong( file_info.crc );
		}

		Q_strlwr( filename_inzip );

		// store the file position in the zip
		unzGetCurrentFileInfoPosition( uf, &curFile->pos );
		curFile->size = file_info.uncompressed_size;
		curFile->name = namePtr;
		strcpy( curFile->name, filename_inzip );
		namePtr += strlen( filename_inzip ) + 1;

		// update hash table
		hash = FS_HashFileName( filename_inzip, pack->hashSize );
		curFile->next = pack->hashTable[ hash ];
		pack->hashTable[ hash ] = curFile;
		unzGoToNextFile( uf );
		curFile++;
	}

	pack->checksum = Com_BlockChecksum( fs_headerLongs + 1, sizeof( fs_headerLongs[0] ) * ( fs_numHeaderLongs - 1 ) );
	pack->checksum = LittleLong( pack->checksum );

	pack->pure_checksum = Com_BlockChecksum( fs_headerLongs, sizeof( fs_headerLongs[0] ) * fs_numHeaderLongs );
	pack->pure_checksum = LittleLong( pack->pure_checksum );

#ifdef USE_PK3_CACHE
	pack->headerLongs = fs_headerLongs;
	pack->numHeaderLongs = fs_numHeaderLongs;
	pack->checksumFeed = fs_checksumFeed;
#else
	Z_Free( fs_headerLongs );
#endif

	if ( !fs_locked->integer )
	{
		unzClose( pack->handle );
		pack->handle = NULL;
	}

#ifdef USE_PK3_CACHE
	FS_InsertPK3ToCache( pack );
#ifdef USE_PK3_CACHE_FILE
	fs_paksReaded++;
#endif
#endif

	return pack;
}


/*
=================
FS_FreePak

Frees a pak structure and releases all associated resources
=================
*/
static void FS_FreePak( pack_t *pak )
{
	if ( pak->handle )
	{
		unzClose( pak->handle );
		pak->handle = NULL;
	}

	Z_Free( pak );
}


/*
=================
FS_CompareZipChecksum

Compares whether the given pak file matches a referenced checksum
=================
*/
qboolean FS_CompareZipChecksum(const char *zipfile)
{
	pack_t *thepak;
	int index, checksum;
	
	thepak = FS_LoadZipFile( zipfile );
	
	if ( !thepak )
		return qfalse;
	
	checksum = thepak->checksum;
#ifndef USE_PK3_CACHE
	FS_FreePak(thepak);
#endif
	
	for(index = 0; index < fs_numServerReferencedPaks; index++)
	{
		if(checksum == fs_serverReferencedPaks[index])
			return qtrue;
	}
	
	return qfalse;
}


/*
=================
FS_GetZipChecksum
=================
*/
int FS_GetZipChecksum( const char *zipfile ) 
{
	pack_t *pak;
	int checksum;
	
	pak = FS_LoadZipFile( zipfile );
	
	if ( !pak )
		return 0xFFFFFFFF;
	
	checksum = pak->checksum;
#ifndef USE_PK3_CACHE
	FS_FreePak( pak );
#endif

	return checksum;
} 


/*
=================================================================================

DIRECTORY SCANNING FUNCTIONS

=================================================================================
*/

static int FS_ReturnPath( const char *zname, char *zpath, int *depth ) {
	int len, at, newdep;

	newdep = 0;
	zpath[0] = '\0';
	len = 0;
	at = 0;

	while(zname[at] != 0)
	{
		if (zname[at]=='/' || zname[at]=='\\') {
			len = at;
			newdep++;
		}
		at++;
	}
	strcpy(zpath, zname);
	zpath[len] = '\0';
	*depth = newdep;

	return len;
}


char *FS_CopyString( const char *in ) {
	char *out;
	//out = S_Malloc( strlen( in ) + 1 );
	out = Z_Malloc( strlen( in ) + 1 );
	strcpy( out, in );
	return out;
}


/*
==================
FS_AddFileToList
==================
*/
static int FS_AddFileToList( const char *name, char **list, int nfiles ) {
	int		i;

	if ( nfiles == MAX_FOUND_FILES - 1 ) {
		return nfiles;
	}
	for ( i = 0 ; i < nfiles ; i++ ) {
		if ( !Q_stricmp( name, list[i] ) ) {
			return nfiles; // already in list
		}
	}
	list[ nfiles ] = FS_CopyString( name );
	nfiles++;

	return nfiles;
}


/*
===============
FS_AllowListExternal
===============
*/
static qboolean FS_AllowListExternal( const char *extension ) 
{
	if ( !extension )
		return qfalse;

	// allow scanning directories
	if ( !strcmp( extension, "/" ) )
		return qtrue;

	if ( !Q_stricmp( extension, ".cfg" ) )
		return qtrue;

	if ( !Q_stricmp( extension, ".txt" ) )
		return qtrue;
	
	if ( !Q_stricmp( extension, ".dat" ) )
		return qtrue;

	if ( !Q_stricmp( extension, ".menu" ) )
		return qtrue;

	if ( !Q_stricmp( extension, ".game" ) )
		return qtrue;

	return qfalse;
}

static fnamecallback_f fnamecallback = NULL;

void FS_SetFilenameCallback( fnamecallback_f func ) 
{
	fnamecallback = func;
}


/*
===============
FS_ListFilteredFiles

Returns a uniqued list of files that match the given criteria
from all search paths
===============
*/
char **FS_ListFilteredFiles( const char *path, const char *extension, const char *filter, int *numfiles, int flags ) {
	int				nfiles;
	char			**listCopy;
	char			*list[MAX_FOUND_FILES];
	searchpath_t	*search;
	int				i;
	int				pathLength;
	int				extLen;
	int				length, pathDepth, temp;
	pack_t			*pak;
	fileInPack_t	*buildBuffer;
	char			zpath[MAX_ZPATH];
	qboolean		hasPatterns;
	const char		*x;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if  ( fs_numServerPaks && !( flags & FS_MATCH_STICK ) ) {
		flags &= ~FS_MATCH_UNPURE;
		if ( !FS_AllowListExternal( extension ) )
			flags &= ~FS_MATCH_EXTERN;
	}

	if ( !path ) {
		*numfiles = 0;
		return NULL;
	}

	if ( !extension ) {
		extension = "";
	}

	extLen = (int)strlen( extension );
	hasPatterns = Com_HasPatterns( extension );
	if ( hasPatterns && extension[0] == '.' && extension[1] != '\0' ) {
		extension++;
	}

	pathLength = strlen( path );
	if ( path[pathLength-1] == '\\' || path[pathLength-1] == '/' ) {
		pathLength--;
	}
	nfiles = 0;
	FS_ReturnPath(path, zpath, &pathDepth);

	//
	// search through the path, one element at a time, adding to list
	//
	for (search = fs_searchpaths ; search ; search = search->next) {
		// is the element a pak file?
		if ( search->pack && ( flags & FS_MATCH_PK3s ) ) {

			//ZOID:  If we are pure, don't search for files on paks that
			// aren't on the pure list
			if ( !FS_PakIsPure( search->pack ) && !( flags & FS_MATCH_UNPURE ) ) {
				continue;
			}

			// look through all the pak file elements
			pak = search->pack;
			buildBuffer = pak->buildBuffer;
			for (i = 0; i < pak->numfiles; i++) {
				const char *name;
				int zpathLen, depth;

				// check for directory match
				name = buildBuffer[i].name;
				//
				if ( filter ) {
					// case insensitive
					if ( !Com_FilterPath( filter, name ) )
						continue;
					// unique the match
					nfiles = FS_AddFileToList( name, list, nfiles );
				}
				else {

					zpathLen = FS_ReturnPath(name, zpath, &depth);

					if ( (depth-pathDepth)>2 || pathLength > zpathLen || Q_stricmpn( name, path, pathLength ) ) {
						continue;
					}

					// check for extension match
					length = (int)strlen( name );

					if ( fnamecallback ) {
						// use custom filter
						if ( !fnamecallback( name, length ) )
							continue;
					} else {
						if ( length < extLen )
							continue;
						if ( *extension ) {
							if ( hasPatterns ) {
								x = strrchr( name, '.' );
								if ( !x || !Com_FilterExt( extension, x+1 ) ) {
									continue;
								}
							} else {
								if ( Q_stricmp( name + length - extLen, extension ) ) {
									continue;
								}
							}
						}
					}
					// unique the match

					temp = pathLength;
					if (pathLength) {
						temp++;		// include the '/'
					}
					nfiles = FS_AddFileToList( name + temp, list, nfiles );
				}
			}
		} else if ( search->dir && ( flags & FS_MATCH_EXTERN ) && search->policy != DIR_DENY ) { // scan for files in the filesystem
			const char *netpath;
			int		numSysFiles;
			char	**sysFiles;
			const char *name;

			netpath = FS_BuildOSPath( search->dir->path, search->dir->gamedir, path );
			sysFiles = Sys_ListFiles( netpath, extension, filter, &numSysFiles, qfalse );
			for ( i = 0 ; i < numSysFiles ; i++ ) {
				// unique the match
				name = sysFiles[ i ];
				length = strlen( name );
				if ( fnamecallback ) {
					// use custom filter
					if ( !fnamecallback( name, length ) )
						continue;
				} // else - should be already filtered by Sys_ListFiles

				nfiles = FS_AddFileToList( name, list, nfiles );
			}
			Sys_FreeFileList( sysFiles );
		}		
	}

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = Z_Malloc( ( nfiles + 1 ) * sizeof( listCopy[0] ) );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
}


/*
=================
FS_ListFiles
=================
*/
char **FS_ListFiles( const char *path, const char *extension, int *numfiles ) 
{
	return FS_ListFilteredFiles( path, extension, NULL, numfiles, FS_MATCH_ANY );
}


/*
=================
FS_FreeFileList
=================
*/
void FS_FreeFileList( char **list ) {
	int		i;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}


/*
================
FS_GetFileList
================
*/
int	FS_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize ) {
	int		nFiles, i, nTotal, nLen;
	char **pFiles = NULL;

	*listbuf = '\0';
	nFiles = 0;
	nTotal = 0;

	if (Q_stricmp(path, "$modlist") == 0) {
		return FS_GetModList(listbuf, bufsize);
	}

	pFiles = FS_ListFiles(path, extension, &nFiles);

	for (i =0; i < nFiles; i++) {
		nLen = strlen(pFiles[i]) + 1;
		if (nTotal + nLen + 1 < bufsize) {
			strcpy(listbuf, pFiles[i]);
			listbuf += nLen;
			nTotal += nLen;
		}
		else {
			nFiles = i;
			break;
		}
	}

	FS_FreeFileList(pFiles);

	return nFiles;
}


/*
=======================
Sys_ConcatenateFileLists

mkv: Naive implementation. Concatenates three lists into a
     new list, and frees the old lists from the heap.
bk001129 - from cvs1.17 (mkv)

FIXME TTimo those two should move to common.c next to Sys_ListFiles
=======================
 */
static unsigned int Sys_CountFileList( char **list )
{
	int i = 0;

	if ( list )
	{
		while ( *list )
		{
			list++;
			i++;
		}
	}

	return i;
}


static char** Sys_ConcatenateFileLists( char **list0, char **list1 )
{
	int totalLength;
	char **src, **dst, **cat;

	totalLength = Sys_CountFileList( list0 );
	totalLength += Sys_CountFileList( list1 );

	/* Create new list. */
	dst = cat = Z_Malloc( ( totalLength + 1 ) * sizeof( char* ) );

	/* Copy over lists. */
	if ( list0 )
	{
		for (src = list0; *src; src++, dst++)
			*dst = *src;
	}

	if ( list1 )
	{
		for ( src = list1; *src; src++, dst++ )
			*dst = *src;
	}

	// Terminate the list
	*dst = NULL;

	// Free our old lists.
	// NOTE: not freeing their content, it's been merged in dst and still being used
	if ( list0 ) Z_Free( list0 );
	if ( list1 ) Z_Free( list1 );

	return cat;
}


/*
================
FS_GetModDescription
================
*/
static void FS_GetModDescription( const char *modDir, char *description, int descriptionLen ) {
	fileHandle_t	descHandle;
	char			descPath[MAX_QPATH];
	int				nDescLen;

	Com_sprintf( descPath, sizeof ( descPath ), "%s%cdescription.txt", modDir, PATH_SEP );
	FS_ReplaceSeparators( descPath );
	nDescLen = FS_SV_FOpenFileRead( descPath, &descHandle );

	if ( descHandle != FS_INVALID_HANDLE ) {
		if ( nDescLen > 0 ) {
			if ( nDescLen > descriptionLen - 1 )
				nDescLen = descriptionLen - 1;
			nDescLen = FS_Read( description, nDescLen, descHandle );
			if ( nDescLen >= 0 ) {
				description[ nDescLen ] = '\0';
			}
		} else {
			Q_strncpyz( description, modDir, descriptionLen );
		}
		FS_FCloseFile( descHandle ); 
	} else {
		Q_strncpyz( description, modDir, descriptionLen );
	}
}


/*
================
FS_GetModList

Returns a list of mod directory names
A mod directory is a peer to baseq3 with a pk3 in it
================
*/
static int FS_GetModList( char *listbuf, int bufsize ) {
	int i, j, k;
	int	nMods, nTotal, nLen, nPaks, nPotential, nDescLen;
	int nDirs, nPakDirs;
	char **pFiles = NULL;
	char **pPaks = NULL;
	char **pDirs = NULL;
	const char *name, *path;
	char description[ MAX_OSPATH ];

	int dummy;
	char **pFiles0 = NULL;
	qboolean bDrop = qfalse;

	// paths to search for mods
	cvar_t *const *paths[] = { &fs_basepath, &fs_homepath, &fs_steampath };

	*listbuf = '\0';
	nMods = nTotal = 0;

	// iterate through paths and get list of potential mods
	for (i = 0; i < ARRAY_LEN( paths ); i++) {
		if ( !*paths[ i ] || !(*paths[i])->string[0] )
			continue;
		pFiles0 = Sys_ListFiles( (*paths[i])->string, NULL, NULL, &dummy, qtrue );
		// Sys_ConcatenateFileLists frees the lists so Sys_FreeFileList isn't required
		pFiles = Sys_ConcatenateFileLists( pFiles, pFiles0 );
	}

	nPotential = Sys_CountFileList( pFiles );

	for ( i = 0 ; i < nPotential ; i++ ) {
		name = pFiles[i];
		// NOTE: cleaner would involve more changes
		// ignore duplicate mod directories
		if ( i != 0 ) {
			bDrop = qfalse;
			for ( j = 0; j < i; j++ ) {
				if ( Q_stricmp( pFiles[j], name ) == 0 ) {
					// this one can be dropped
					bDrop = qtrue;
					break;
				}
			}
		}
		// we also drop "baseq3" "." and ".."
		if ( bDrop || Q_stricmp( name, fs_basegame->string ) == 0 || Q_stricmpn(name, ".", 1) == 0 ) {
			continue;
		}

		// in order to be a valid mod the directory must contain at least one .pk3
		// we didn't keep the information when we merged the directory names, as to what OS Path it was found under
		// so we will try each of them here
		nPaks = nPakDirs = 0;
		for ( j = 0; j < ARRAY_LEN( paths ); j++ ) {
			if ( !*paths[ j ] || !(*paths[ j ])->string[0] )
				break;
			path = FS_BuildOSPath( (*paths[j])->string, name, NULL );

			nPaks = nDirs = nPakDirs = 0;
			pPaks = Sys_ListFiles( path, ".pk3", NULL, &nPaks, qfalse );
			pDirs = Sys_ListFiles( path, "/", NULL, &nDirs, qfalse );
			for ( k = 0; k < nDirs; k++ ) {
				// we only want to count directories ending with ".pk3dir"
				if ( FS_IsExt( pDirs[k], ".pk3dir", strlen( pDirs[k] ) ) ) {
					nPakDirs++;
				}
			}

			// we only use Sys_ListFiles to check whether files are present
			Sys_FreeFileList( pDirs );
			Sys_FreeFileList( pPaks );
			if ( nPaks > 0 || nPakDirs > 0 ) {
				break;
			}
		}

		if ( nPaks > 0 || nPakDirs > 0 ) {
			nLen = strlen( name ) + 1;
			// nLen is the length of the mod path
			// we need to see if there is a description available
			FS_GetModDescription( name, description, sizeof( description ) );
			nDescLen = strlen( description ) + 1;

			if ( nTotal + nLen + 1 + nDescLen + 1 < bufsize ) {
				strcpy( listbuf, name );
				listbuf += nLen;
				strcpy( listbuf, description );
				listbuf += nDescLen;
				nTotal += nLen + nDescLen;
				nMods++;
			} else {
				break;
			}
		}
	}
	Sys_FreeFileList( pFiles );

	return nMods;
}


//============================================================================

/*
================
FS_Dir_f
================
*/
static void FS_Dir_f( void ) {
	const char *path;
	const char *extension;
	char **dirnames;
	int ndirs;
	int i;

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 ) {
		Com_Printf( "usage: dir <directory> [extension]\n" );
		return;
	}

	if ( Cmd_Argc() == 2 ) {
		path = Cmd_Argv( 1 );
		extension = "";
	} else {
		path = Cmd_Argv( 1 );
		extension = Cmd_Argv( 2 );
	}

	Com_Printf( "Directory of %s %s\n", path, extension );
	Com_Printf( "---------------\n" );

	dirnames = FS_ListFiles( path, extension, &ndirs );

	for ( i = 0; i < ndirs; i++ ) {
		Com_Printf( "%s\n", dirnames[i] );
	}
	FS_FreeFileList( dirnames );
}


/*
===========
FS_ConvertPath
===========
*/
static void FS_ConvertPath( char *s ) {
	while (*s) {
		if ( *s == '\\' || *s == ':' ) {
			*s = '/';
		}
		s++;
	}
}


/*
===========
FS_PathCmp

Ignore case and seprator char distinctions
===========
*/
static int FS_PathCmp( const char *s1, const char *s2 ) {
	int		c1, c2;
	
	do {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 >= 'a' && c1 <= 'z') {
			c1 -= ('a' - 'A');
		}
		if (c2 >= 'a' && c2 <= 'z') {
			c2 -= ('a' - 'A');
		}

		if ( c1 == '\\' || c1 == ':' ) {
			c1 = '/';
		}
		if ( c2 == '\\' || c2 == ':' ) {
			c2 = '/';
		}
		
		if (c1 < c2) {
			return -1;		// strings not equal
		}
		if (c1 > c2) {
			return 1;
		}
	} while (c1);
	
	return 0;		// strings are equal
}


/*
================
FS_SortFileList
================
*/
static void FS_SortFileList( char **list, int n ) {
	const char *m;
	char *temp;
	int i, j;
	i = 0;
	j = n;
	m = list[ n >> 1 ];
	do {
		while ( FS_PathCmp( list[i], m ) < 0 ) i++;
		while ( FS_PathCmp( list[j], m ) > 0 ) j--;
		if ( i <= j ) {
			temp = list[i];
			list[i] = list[j];
			list[j] = temp;
			i++; 
			j--;
		}
	} while ( i <= j );
	if ( j > 0 ) FS_SortFileList( list, j );
	if ( n > i ) FS_SortFileList( list+i, n-i );
}


/*
================
FS_NewDir_f
================
*/
static void FS_NewDir_f( void ) {
	const char *filter;
	char	**dirnames;
	char	dirname[ MAX_STRING_CHARS ];
	int		ndirs;
	int		i;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: fdir <filter>\n" );
		Com_Printf( "example: fdir *q3dm*.bsp\n");
		return;
	}

	filter = Cmd_Argv( 1 );

	Com_Printf( "---------------\n" );

	dirnames = FS_ListFilteredFiles( "", "", filter, &ndirs, FS_MATCH_ANY );

	if ( ndirs >= 2 )
		FS_SortFileList( dirnames, ndirs - 1 );

	for ( i = 0; i < ndirs; i++ ) {
		Q_strncpyz( dirname, dirnames[i], sizeof( dirname ) );
		FS_ConvertPath( dirname );
		Com_Printf( "%s\n", dirname );
	}

	Com_Printf( "%d files listed\n", ndirs );
	FS_FreeFileList( dirnames );
}


/*
============
FS_Path_f
============
*/
static void FS_Path_f( void ) {
	const searchpath_t *s;
	int i;

	Com_Printf( "Current search path:\n" );
	for ( s = fs_searchpaths; s; s = s->next ) {
		if ( s->pack ) {
			Com_Printf( "%s (%i files)\n", s->pack->pakFilename, s->pack->numfiles );
			if ( fs_numServerPaks ) {
				if ( !FS_PakIsPure( s->pack ) ) {
					Com_Printf( S_COLOR_YELLOW "    not on the pure list\n" );
				} else {
					Com_Printf( "    on the pure list\n" );
				}
			}
		} else {
			Com_Printf( "%s%c%s\n", s->dir->path, PATH_SEP, s->dir->gamedir );
		}
	}

	Com_Printf( "\n" );
	for ( i = 1 ; i < MAX_FILE_HANDLES ; i++ ) {
		if ( fsh[i].handleFiles.file.o ) {
			Com_Printf( "handle %i: %s\n", i, fsh[i].name );
		}
	}
}


/*
============
FS_TouchFile_f

The only purpose of this function is to allow game script files to copy
arbitrary files furing an "fs_copyfiles 1" run.
============
*/
static void FS_TouchFile_f( void ) {
	fileHandle_t	f;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: touchFile <file>\n" );
		return;
	}

	FS_FOpenFileRead( Cmd_Argv( 1 ), &f, qfalse );
	if ( f != FS_INVALID_HANDLE ) {
		FS_FCloseFile( f );
	}
}


/*
============
FS_CompleteFileName
============
*/
static void FS_CompleteFileName( char *args, int argNum ) {
	if( argNum == 2 ) {
		Field_CompleteFilename( "", "", qfalse, FS_MATCH_ANY );
	}
}


/*
============
FS_Which_f
============
*/
static void FS_Which_f( void ) {
	const searchpath_t *search;
	char			*netpath;
	pack_t			*pak;
	fileInPack_t	*pakFile;
	directory_t		*dir;
	long			hash;
	FILE			*temp;
	char			*filename;
	char			buf[ MAX_OSPATH*2 + 1 ];
	int				numfound;

	hash = 0;
	numfound = 0;
	filename = Cmd_Argv(1);

	if ( !filename[0] ) {
		Com_Printf( "Usage: which <file>\n" );
		return;
	}

	// qpaths are not supposed to have a leading slash
	if ( filename[0] == '/' || filename[0] == '\\' ) {
		filename++;
	}

	// just wants to see if file is there
	for ( search = fs_searchpaths ; search ; search = search->next ) {
		if ( search->pack ) {
			hash = FS_HashFileName(filename, search->pack->hashSize);
		}
		// is the element a pak file?
		if ( search->pack && search->pack->hashTable[hash] ) {
			// look through all the pak file elements
			pak = search->pack;
			pakFile = pak->hashTable[hash];
			do {
				// case and separator insensitive comparisons
				if ( !FS_FilenameCompare( pakFile->name, filename ) ) {
					// found it!
					Com_Printf( "File \"%s\" found in \"%s\"\n", filename, pak->pakFilename );
					if ( ++numfound >= 32 ) {
						return;
					}
				}
				pakFile = pakFile->next;
			} while(pakFile != NULL);
		} else if ( search->dir ) {
			dir = search->dir;

			netpath = FS_BuildOSPath( dir->path, dir->gamedir, filename );
			temp = Sys_FOpen( netpath, "rb" );
			if ( !temp ) {
				continue;
			}
			fclose(temp);
			Com_sprintf( buf, sizeof( buf ), "%s%c%s", dir->path, PATH_SEP, dir->gamedir );
			FS_ReplaceSeparators( buf );
			Com_Printf( "File \"%s\" found at \"%s\"\n", filename, buf );
			if ( ++numfound >= 32 ) {
				return;
			}
		}
	}

	if ( !numfound ) {
		Com_Printf( "File not found: \"%s\"\n", filename );
	}
}


//===========================================================================

/*
================
FS_AddGameDirectory

Sets fs_gamedir, adds the directory to the head of the path,
then loads the zip headers
================
*/
static void FS_AddGameDirectory( const char *path, const char *dir ) {
	const searchpath_t *sp;
	int				len;
	searchpath_t	*search;
	const char		*gamedir;
	pack_t			*pak;
	char			curpath[MAX_OSPATH*2 + 1];
	char			*pakfile;
	int				numfiles;
	char			**pakfiles;
	int				pakfilesi;
	int				numdirs;
	char			**pakdirs;
	int				pakdirsi;
	int				pakwhich;
	int				path_len;
	int				dir_len;

	for ( sp = fs_searchpaths ; sp ; sp = sp->next ) {
		if ( sp->dir && !Q_stricmp( sp->dir->path, path ) && !Q_stricmp( sp->dir->gamedir, dir )) {
			return;	// we've already got this one
		}
	}
	
	Q_strncpyz( fs_gamedir, dir, sizeof( fs_gamedir ) );

	//
	// add the directory to the search path
	//
	path_len = (int) strlen( path ) + 1;
	path_len = PAD( path_len, sizeof( int ) );
	dir_len = (int) strlen( dir ) + 1;
	dir_len = PAD( dir_len, sizeof( int ) );
	len = sizeof( *search ) + sizeof( *search->dir ) + path_len + dir_len;

	search = Z_TagMalloc( len, TAG_SEARCH_PATH );
	Com_Memset( search, 0, len );
	search->dir = (directory_t*)( search + 1 );
	search->dir->path = (char*)( search->dir + 1 );
	search->dir->gamedir = (char*)( search->dir->path + path_len );

	strcpy( search->dir->path, path );
	strcpy( search->dir->gamedir, dir );
	gamedir = search->dir->gamedir;

	search->next = fs_searchpaths;
	fs_searchpaths = search;

	// find all pak files in this directory
	Q_strncpyz( curpath, FS_BuildOSPath( path, dir, NULL ), sizeof( curpath ) );

	// Get .pk3 files
	pakfiles = Sys_ListFiles(curpath, ".pk3", NULL, &numfiles, qfalse);

	if ( numfiles >= 2 )
		FS_SortFileList( pakfiles, numfiles - 1 );

	pakfilesi = 0;
	pakdirsi = 0;

	if ( fs_numServerPaks ) {
		numdirs = 0;
		pakdirs = NULL;
	} else {
		// Get top level directories (we'll filter them later since the Sys_ListFiles filtering is terrible)
		pakdirs = Sys_ListFiles( curpath, "/", NULL, &numdirs, qfalse );
		if ( numdirs >= 2 ) {
			FS_SortFileList( pakdirs, numdirs - 1 );
		}
	}

	while (( pakfilesi < numfiles) || (pakdirsi < numdirs) ) 
	{
		// Check if a pakfile or pakdir comes next
		if (pakfilesi >= numfiles) {
			// We've used all the pakfiles, it must be a pakdir.
			pakwhich = 0;
		}
		else if (pakdirsi >= numdirs) {
			// We've used all the pakdirs, it must be a pakfile.
			pakwhich = 1;
		}
		else {
			// Could be either, compare to see which name comes first
			pakwhich = (FS_PathCmp( pakfiles[pakfilesi], pakdirs[pakdirsi] ) < 0);
		}

		if ( pakwhich ) {

			len = strlen( pakfiles[pakfilesi] );
			if ( !FS_IsExt( pakfiles[pakfilesi], ".pk3", len ) ) {
				// not a pk3 file
				pakfilesi++;
				continue;
			}

			// The next .pk3 file is before the next .pk3dir
			pakfile = FS_BuildOSPath( path, dir, pakfiles[pakfilesi] );
			if ( (pak = FS_LoadZipFile( pakfile ) ) == NULL ) {
				// This isn't a .pk3! Next!
				pakfilesi++;
				continue;
			}

			// store the game name for downloading
			pak->pakGamename = gamedir;

			pak->index = fs_packCount;
			pak->referenced = 0;

			fs_packFiles += pak->numfiles;
			fs_packCount++;

			search = Z_TagMalloc( sizeof( *search ), TAG_SEARCH_PACK );
			Com_Memset( search, 0, sizeof( *search ) );
			search->pack = pak;

			search->next = fs_searchpaths;
			fs_searchpaths = search;

			pakfilesi++;
		} else {

			len = strlen(pakdirs[pakdirsi]);

			// The next .pk3dir is before the next .pk3 file
			// But wait, this could be any directory, we're filtering to only ending with ".pk3dir" here.
			if (!FS_IsExt(pakdirs[pakdirsi], ".pk3dir", len)) {
				// This isn't a .pk3dir! Next!
				pakdirsi++;
				continue;
			}

			// add the directory to the search path
			path_len = (int) strlen( curpath ) + 1; 
			path_len = PAD( path_len, sizeof( int ) );
			dir_len = PAD( len + 1, sizeof( int ) );
			len = sizeof( *search ) + sizeof( *search->dir ) + path_len + dir_len;

			search = Z_TagMalloc( len, TAG_SEARCH_DIR );
			Com_Memset( search, 0, len );
			search->dir = (directory_t*)(search + 1);
			search->dir->path = (char*)( search->dir + 1 );
			search->dir->gamedir = (char*)( search->dir->path + path_len );
			search->policy = DIR_ALLOW;

			strcpy( search->dir->path, curpath );				// c:\quake3\baseq3
			strcpy( search->dir->gamedir, pakdirs[ pakdirsi ] );// mypak.pk3dir

			search->next = fs_searchpaths;
			fs_searchpaths = search;

			pakdirsi++;
		}
	}

	// done
	Sys_FreeFileList( pakdirs );
	Sys_FreeFileList( pakfiles );
}


/*
================
FS_idPak
================
*/
qboolean FS_idPak(const char *pak, const char *base, int numPaks)
{
	int i;

	for (i = 0; i < NUM_ID_PAKS; i++) {
		if ( !FS_FilenameCompare(pak, va("%s/pak%d", base, i)) ) {
			break;
		}
	}
	if (i < numPaks) {
		return qtrue;
	}
	return qfalse;
}


/*
================
FS_InvalidGameDir
return true if path is a reference to current directory or directory traversal
or a sub-directory
================
*/
qboolean FS_InvalidGameDir( const char *gamedir ) 
{
	if ( !strcmp( gamedir, "." ) || !strcmp( gamedir, ".." )
		|| strchr( gamedir, '/' ) || strchr( gamedir, '\\' ) ) {
		return qtrue;
	}

	return qfalse;
}


/*
================
FS_ComparePaks

----------------
dlstring == qtrue

Returns a list of pak files that we should download from the server. They all get stored
in the current gamedir and an FS_Restart will be fired up after we download them all.

The string is the format:

@remotename@localname [repeat]

static int		fs_numServerReferencedPaks;
static int		fs_serverReferencedPaks[MAX_REF_PAKS];
static char		*fs_serverReferencedPakNames[MAX_REF_PAKS];

----------------
dlstring == qfalse

we are not interested in a download string format, we want something human-readable
(this is used for diagnostics while connecting to a pure server)

================
*/
qboolean FS_ComparePaks( char *neededpaks, int len, qboolean dlstring ) {
	searchpath_t	*sp;
	qboolean havepak;
	char *origpos = neededpaks;
	int i;

	if (!fs_numServerReferencedPaks)
		return qfalse; // Server didn't send any pack information along

	*neededpaks = '\0';

	for ( i = 0 ; i < fs_numServerReferencedPaks ; i++ )
	{
		// Ok, see if we have this pak file
		havepak = qfalse;

		// never autodownload any of the id paks
		if ( FS_idPak(fs_serverReferencedPakNames[i], BASEGAME, NUM_ID_PAKS) || FS_idPak(fs_serverReferencedPakNames[i], BASETA, NUM_TA_PAKS) ) {
			continue;
		}

		// Make sure the server cannot make us write to non-quake3 directories.
		if(FS_CheckDirTraversal(fs_serverReferencedPakNames[i]))
                {
			Com_Printf("WARNING: Invalid download name %s\n", fs_serverReferencedPakNames[i]);
                        continue;
                }

		for ( sp = fs_searchpaths ; sp ; sp = sp->next ) {
			if ( sp->pack && sp->pack->checksum == fs_serverReferencedPaks[i] ) {
				havepak = qtrue; // This is it!
				break;
			}
		}

		if ( !havepak && fs_serverReferencedPakNames[i] && *fs_serverReferencedPakNames[i] ) { 
			// Don't got it

      if (dlstring)
      {
		// We need this to make sure we won't hit the end of the buffer or the server could
		// overwrite non-pk3 files on clients by writing so much crap into neededpaks that
		// Q_strcat cuts off the .pk3 extension.
	
		origpos += strlen(origpos);
	
        // Remote name
        Q_strcat( neededpaks, len, "@");
        Q_strcat( neededpaks, len, fs_serverReferencedPakNames[i] );
        Q_strcat( neededpaks, len, ".pk3" );

        // Local name
        Q_strcat( neededpaks, len, "@");
        // Do we have one with the same name?
        if ( FS_SV_FileExists( va( "%s.pk3", fs_serverReferencedPakNames[i] ) ) )
        {
          char st[MAX_ZPATH];
          // We already have one called this, we need to download it to another name
          // Make something up with the checksum in it
          Com_sprintf( st, sizeof( st ), "%s.%08x.pk3", fs_serverReferencedPakNames[i], fs_serverReferencedPaks[i] );
          Q_strcat( neededpaks, len, st );
        } else
        {
          Q_strcat( neededpaks, len, fs_serverReferencedPakNames[i] );
          Q_strcat( neededpaks, len, ".pk3" );
        }
        
        // Find out whether it might have overflowed the buffer and don't add this file to the
        // list if that is the case.
        if(strlen(origpos) + (origpos - neededpaks) >= len - 1)
	{
		*origpos = '\0';
		break;
	}
      }
      else
      {
        Q_strcat( neededpaks, len, fs_serverReferencedPakNames[i] );
			  Q_strcat( neededpaks, len, ".pk3" );
        // Do we have one with the same name?
        if ( FS_SV_FileExists( va( "%s.pk3", fs_serverReferencedPakNames[i] ) ) )
        {
          Q_strcat( neededpaks, len, " (local file exists with wrong checksum)");
        }
        Q_strcat( neededpaks, len, "\n");
      }
		}
	}

	if ( *neededpaks ) {
		return qtrue;
	}

	return qfalse; // We have them all
}


/*
================
FS_Shutdown

Frees all resources.
================
*/
void FS_Shutdown( qboolean closemfp )
{
	searchpath_t	*p, *next;
	int i;

	// close opened files
	if ( closemfp ) 
	{
		for ( i = 1; i < MAX_FILE_HANDLES; i++ )
		{
			if ( !fsh[i].handleFiles.file.v  )
				continue;

			FS_FCloseFile( i );
		}
	}

#ifdef DELAY_WRITECONFIG
	if ( fs_searchpaths )
	{
		Com_WriteConfiguration();
	}
#endif

#ifdef USE_PK3_CACHE
	FS_ResetCacheReferences();
#endif

	// free everything
	for( p = fs_searchpaths; p; p = next )
	{
		next = p->next;

		if ( p->pack )
		{
#ifndef USE_PK3_CACHE
			FS_FreePak( p->pack );
#endif
			p->pack = NULL;
		}

		Z_Free( p );
	}

	// any FS_ calls will now be an error until reinitialized
	fs_searchpaths = NULL;

	Cmd_RemoveCommand( "path" );
	Cmd_RemoveCommand( "dir" );
	Cmd_RemoveCommand( "fdir" );
	Cmd_RemoveCommand( "touchFile" );
	Cmd_RemoveCommand( "which" );
	Cmd_RemoveCommand( "lsof" );
	Cmd_RemoveCommand( "fs_restart" );
}

 
/*
================
FS_ReorderPurePaks
NOTE TTimo: the reordering that happens here is not reflected in the cvars (\cvarlist *pak*)
  this can lead to misleading situations, see https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=540
================
*/
static void FS_ReorderPurePaks( void )
{
	searchpath_t *s;
	int i;
	searchpath_t **p_insert_index, // for linked list reordering
		**p_previous; // when doing the scan
	
	fs_reordered = qfalse;

	// only relevant when connected to pure server
	if ( !fs_numServerPaks )
		return;
	
	p_insert_index = &fs_searchpaths; // we insert in order at the beginning of the list 
	for ( i = 0 ; i < fs_numServerPaks ; i++ ) {
		p_previous = p_insert_index; // track the pointer-to-current-item
		for (s = *p_insert_index; s; s = s->next) {
			// the part of the list before p_insert_index has been sorted already
			if (s->pack && fs_serverPaks[i] == s->pack->checksum) {
				fs_reordered = qtrue;
				// move this element to the insert list
				*p_previous = s->next;
				s->next = *p_insert_index;
				*p_insert_index = s;
				// increment insert list
				p_insert_index = &s->next;
				break; // iterate to next server pack
			}
			p_previous = &s->next;
		}
	}
}


/*
================
FS_OwnerName
================
*/
static const char *FS_OwnerName( handleOwner_t owner ) 
{
	static const char *s[4]= { "SY", "QA", "CG", "UI" };
	if ( owner < H_SYSTEM || owner > H_Q3UI )
		return "??";
	return s[owner];
}


/*
================
FS_ListOpenFiles
================
*/
static void FS_ListOpenFiles_f( void ) {
	int i;
	fileHandleData_t *fh;
	fh = fsh;
	for ( i = 0; i < MAX_FILE_HANDLES; i++, fh++ ) {
		if ( !fh->handleFiles.file.v )
			continue;
		Com_Printf( "%2i %2s %s\n", i, FS_OwnerName(fh->owner), fh->name );
	}
}


/*
=====================
FS_LoadedPakPureChecksums
=====================
*/
static int fs_numPureChecksums;
static int fs_pureChecksum[ MAX_FOUND_FILES ];

static void FS_LoadedPakPureChecksums( void )
{
	const searchpath_t *search;

	fs_numPureChecksums = 0;
	for ( search = fs_searchpaths ; search ; search = search->next ) {
		if ( search->pack ) {
			if ( fs_numPureChecksums >= ARRAY_LEN( fs_pureChecksum ) ) {
				Com_DPrintf( "WARNING: pure checksums overflowed\n" );
				fs_numPureChecksums = 0;
				return;
			}
			fs_pureChecksum[ fs_numPureChecksums ] = search->pack->pure_checksum;
			fs_numPureChecksums++;
		}
	}
}


/*
================
FS_IsPureChecksum
================
*/
qboolean FS_IsPureChecksum( int sum )
{
	int i;

	if ( fs_numPureChecksums == 0 )
		return qtrue;
	
	for ( i = 0; i < fs_numPureChecksums; i++ )
		if ( fs_pureChecksum[i] == sum )
			return qtrue;

	return qfalse;
}


/*
================
FS_Startup
================
*/
static void FS_Startup( void ) {
	const char *homePath;
	int start, end;

	Com_Printf( "----- FS_Startup -----\n" );

	fs_packFiles = 0;
	fs_packCount = 0;

	fs_debug = Cvar_Get( "fs_debug", "0", 0 );
	fs_copyfiles = Cvar_Get( "fs_copyfiles", "0", CVAR_INIT );
	fs_basepath = Cvar_Get( "fs_basepath", Sys_DefaultBasePath(), CVAR_INIT | CVAR_PROTECTED | CVAR_PRIVATE );
	fs_basegame = Cvar_Get( "fs_basegame", BASEGAME, CVAR_INIT | CVAR_PROTECTED );
	fs_steampath = Cvar_Get( "fs_steampath", Sys_SteamPath(), CVAR_INIT | CVAR_PROTECTED | CVAR_PRIVATE );

	fs_locked = Cvar_Get( "fs_locked", "0", CVAR_INIT );
	Cvar_SetDescription( fs_locked, "Set file handle policy for pk3 files:\n"
		" 0 - release after use, unlimited number of pk3 files can be loaded\n"
		" 1 - keep file handle locked, more consistent, total pk3 files count limited to ~1k-4k\n" );

	if ( !fs_basegame->string[0] )
		Com_Error( ERR_FATAL, "* fs_basegame is not set *" );
	
	homePath = Sys_DefaultHomePath();
	if ( !homePath || !homePath[0] ) {
		homePath = fs_basepath->string;
	}

	fs_homepath = Cvar_Get( "fs_homepath", homePath, CVAR_INIT | CVAR_PROTECTED | CVAR_PRIVATE );
	fs_gamedirvar = Cvar_Get( "fs_game", "", CVAR_INIT | CVAR_SYSTEMINFO );
	Cvar_CheckRange( fs_gamedirvar, NULL, NULL, CV_FSPATH );

	if ( !Q_stricmp( fs_basegame->string, fs_gamedirvar->string ) ) {
		Cvar_ForceReset( "fs_game" );
	}

	start = Sys_Milliseconds();

#ifdef USE_PK3_CACHE
#ifdef USE_PK3_CACHE_FILE
	FS_LoadCache();
#endif
#endif

	// add search path elements in reverse priority order
	if ( fs_steampath->string[0] ) {
		FS_AddGameDirectory( fs_steampath->string, fs_basegame->string );
	}

	if ( fs_basepath->string[0] ) {
		FS_AddGameDirectory( fs_basepath->string, fs_basegame->string );
	}

	// fs_homepath is somewhat particular to *nix systems, only add if relevant
	// NOTE: same filtering below for mods and basegame
	if ( fs_homepath->string[0] && Q_stricmp( fs_homepath->string, fs_basepath->string ) ) {
		FS_AddGameDirectory( fs_homepath->string, fs_basegame->string );
	}

	// check for additional game folder for mods
	if ( fs_gamedirvar->string[0] && Q_stricmp( fs_gamedirvar->string, fs_basegame->string ) ) {
		if ( fs_steampath->string[0] ) {
			FS_AddGameDirectory( fs_steampath->string, fs_gamedirvar->string );
		}
		if ( fs_basepath->string[0] ) {
			FS_AddGameDirectory( fs_basepath->string, fs_gamedirvar->string );
		}
		if ( fs_homepath->string[0] && Q_stricmp( fs_homepath->string, fs_basepath->string ) ) {
			FS_AddGameDirectory( fs_homepath->string, fs_gamedirvar->string );
		}
	}

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=506
	// reorder the pure pk3 files according to server order
	FS_ReorderPurePaks();

	// get the pure checksums of the pk3 files loaded by the server
	FS_LoadedPakPureChecksums();

	end = Sys_Milliseconds();

	Com_ReadCDKey( fs_basegame->string );

	if ( fs_gamedirvar->string[0] && Q_stricmp( fs_gamedirvar->string, fs_basegame->string ) ) {
		Com_AppendCDKey( fs_gamedirvar->string );
	}

	// add our commands
	Cmd_AddCommand( "path", FS_Path_f );
	Cmd_AddCommand( "dir", FS_Dir_f );
	Cmd_AddCommand( "fdir", FS_NewDir_f );
	Cmd_AddCommand( "touchFile", FS_TouchFile_f );
	Cmd_AddCommand( "lsof", FS_ListOpenFiles_f );
 	Cmd_AddCommand( "which", FS_Which_f );
	Cmd_SetCommandCompletionFunc( "which", FS_CompleteFileName );
	Cmd_AddCommand( "fs_restart", FS_Reload );

	// print the current search paths
	//FS_Path_f();
	Com_Printf( "...loaded in %i milliseconds\n", end - start );

	Com_Printf( "----------------------\n" );
	Com_Printf( "%d files in %d pk3 files\n", fs_packFiles, fs_packCount );

	fs_gamedirvar->modified = qfalse; // We just loaded, it's not modified

	// check original q3a files
	if ( !Q_stricmp( fs_basegame->string, BASEGAME ) || !Q_stricmp( fs_basegame->string, BASEDEMO ) )
		FS_CheckIdPaks();

#ifdef FS_MISSING
	if (missingFiles == NULL) {
		missingFiles = Sys_FOpen( "\\missing.txt", "ab" );
	}
#endif

#ifdef USE_PK3_CACHE
	FS_FreeUnusedCache();
#ifdef USE_PK3_CACHE_FILE
	FS_SaveCache();
#endif
#endif
}


/*
===================
FS_CheckIdPaks

Checks that pak0.pk3 is present and its checksum is correct
Note: If you're building a game that doesn't depend on the
Q3 media pak0.pk3, you'll want to remove this function
===================
*/
static void FS_CheckIdPaks( void )
{
	searchpath_t	*path;
	qboolean founddemo = qfalse;
	unsigned foundPak = 0;

	for( path = fs_searchpaths; path; path = path->next )
	{
		const char* pakBasename;

		if ( !path->pack )
			continue;

		pakBasename = path->pack->pakBasename;

		if(!Q_stricmpn( path->pack->pakGamename, BASEDEMO, MAX_OSPATH )
		   && !Q_stricmpn( pakBasename, "pak0", MAX_OSPATH ))
		{
			founddemo = qtrue;

			if( path->pack->checksum == DEMO_PAK0_CHECKSUM )
			{
				Com_Printf( "\n\n"
						"**************************************************\n"
						"WARNING: It looks like you're using pak0.pk3\n"
						"from the demo. This may work fine, but it is not\n"
						"guaranteed or supported.\n"
						"**************************************************\n\n\n" );
			}
		}

		else if(!Q_stricmpn( path->pack->pakGamename, BASEGAME, MAX_OSPATH )
			&& strlen(pakBasename) == 4 && !Q_stricmpn( pakBasename, "pak", 3 )
			&& pakBasename[3] >= '0' && pakBasename[3] <= '8')
		{
			if( (unsigned int)path->pack->checksum != pak_checksums[pakBasename[3]-'0'] )
			{
				if(pakBasename[3] == '0')
				{
					Com_Printf("\n\n"
						"**************************************************\n"
						"ERROR: pak0.pk3 is present but its checksum (%u)\n"
						"is not correct. Please re-copy pak0.pk3 from your\n"
						"legitimate Q3 CDROM.\n"
						"**************************************************\n\n\n",
						path->pack->checksum );
				}
				else
				{
					Com_Printf("\n\n"
						"**************************************************\n"
						"ERROR: pak%d.pk3 is present but its checksum (%u)\n"
						"is not correct. Please re-install Quake 3 Arena \n"
						"Point Release v1.32 pk3 files\n"
						"**************************************************\n\n\n",
						pakBasename[3]-'0', path->pack->checksum );
				}
				Com_Error(ERR_FATAL, "\n* You need to install correct Quake III Arena files in order to play *");
			}

			foundPak |= 1<<(pakBasename[3]-'0');
		}
	}

	if(!founddemo && (foundPak & 0x1ff) != 0x1ff )
	{
		if((foundPak&1) != 1 )
		{
			Com_Printf("\n\n"
			"pak0.pk3 is missing. Please copy it\n"
			"from your legitimate Q3 CDROM.\n");
		}

		if((foundPak&0x1fe) != 0x1fe )
		{
			Com_Printf("\n\n"
			"Point Release files are missing. Please\n"
			"re-install the 1.32 point release.\n");
		}

		Com_Printf("\n\n"
			"Also check that your Q3 executable is in\n"
			"the correct place and that every file\n"
			"in the %s directory is present and readable.\n", BASEGAME);

		if(!fs_gamedirvar->string[0]
		|| !Q_stricmp( fs_gamedirvar->string, BASEGAME )
		|| !Q_stricmp( fs_gamedirvar->string, BASETA ))
			Com_Error(ERR_FATAL, "\n*** you need to install Quake III Arena in order to play ***");
	}
}


/*
=====================
FS_LoadedPakChecksums

Returns a space separated string containing the checksums of all loaded pk3 files.
Servers with sv_pure set will get this string and pass it to clients.
=====================
*/
const char *FS_LoadedPakChecksums( qboolean *overflowed ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t *search;
	char buf[ 32 ];
	char *s, *max;
	int len;

	s = info;
	info[0] = '\0';
	max = &info[sizeof(info)-1];
	*overflowed = qfalse;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( !search->pack )
			continue;

		if ( info[0] )
			len = sprintf( buf, " %i", search->pack->checksum );
		else
			len = sprintf( buf, "%i", search->pack->checksum );

		if ( s + len > max ) {
			*overflowed = qtrue;
			break;
		}

		s = Q_stradd( s, buf );
	}

	return info;
}


/*
=====================
FS_LoadedPakNames

Returns a space separated string containing the names of all loaded pk3 files.
Servers with sv_pure set will get this string and pass it to clients.
=====================
*/
#ifndef DEDICATED
const char *FS_LoadedPakNames( void ) {
	static char	info[BIG_INFO_STRING];
	const searchpath_t *search;
	char *s, *max;
	int len;

	s = info;
	info[0] = '\0';
	max = &info[sizeof(info)-1];

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( !search->pack )
			continue;

		len = (int)strlen( search->pack->pakBasename );
		if ( info[0] )
			len++;

		if ( s + len > max )
			break;

		if ( info[0] )
			s = Q_stradd( s, " " );

		s = Q_stradd( s, search->pack->pakBasename );
	}

	return info;
}
#endif


/*
=====================
FS_ReferencedPakChecksums

Returns a space separated string containing the checksums of all referenced pk3 files.
The server will send this to the clients so they can check which files should be auto-downloaded. 
=====================
*/
const char *FS_ReferencedPakChecksums( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t *search;
	size_t len;

	info[0] = '\0';
	len = strlen( fs_basegame->string );

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( search->pack ) {
			if ( search->pack->referenced || Q_stricmpn( search->pack->pakGamename, fs_basegame->string, len ) ) {
				Q_strcat( info, sizeof( info ), va( "%i ", search->pack->checksum ) );
			}
		}
	}

	return info;
}


/*
=====================
FS_ReferencedPakPureChecksums

Returns a space separated string containing the pure checksums of all referenced pk3 files.
Servers with sv_pure set will get this string back from clients for pure validation 

The string has a specific order, "cgame ui @ ref1 ref2 ref3 ..."
=====================
*/
const char *FS_ReferencedPakPureChecksums( int maxlen ) {
	static char	info[ MAX_STRING_CHARS*2 ];
	char *s, *max;
	searchpath_t	*search;
	int nFlags, numPaks, checksum;

	max = info + maxlen; // maxlen is always smaller than MAX_STRING_CHARS so we can overflow a bit
	s = info;
	*s = '\0';

	checksum = fs_checksumFeed;
	numPaks = 0;
	for ( nFlags = FS_CGAME_REF; nFlags; nFlags = nFlags >> 1 ) {
		if ( nFlags & FS_GENERAL_REF ) {
			// add a delimter between must haves and general refs
			s = Q_stradd( s, "@ " );
			if ( s > max ) // client-side overflow
				break;
		}
		for ( search = fs_searchpaths ; search ; search = search->next ) {
			// is the element a pak file and has it been referenced based on flag?
			if ( search->pack && (search->pack->referenced & nFlags)) {
				s = Q_stradd( s, va( "%i ", search->pack->pure_checksum ) );
				if ( s > max ) // client-side overflow
					break;
				if ( nFlags & (FS_CGAME_REF | FS_UI_REF) ) {
					break;
				}
				checksum ^= search->pack->pure_checksum;
				numPaks++;
			}
		}
	}

	// last checksum is the encoded number of referenced pk3s
	checksum ^= numPaks;
	s = Q_stradd( s, va( "%i ", checksum ) );
	if ( s > max ) { 
		// client-side overflow
		Com_Printf( S_COLOR_YELLOW "WARNING: pure checksum list is too long (%i), you might be not able to play on remote server!\n", (int)(s - info) );
		*max = '\0';
	}
	
	return info;
}


/*
=====================
FS_ReferencedPakNames

Returns a space separated string containing the names of all referenced pk3 files.
The server will send this to the clients so they can check which files should be auto-downloaded. 
=====================
*/
const char *FS_ReferencedPakNames( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;
	size_t	len;

	info[0] = 0;
	len = strlen( fs_basegame->string );

	// we want to return ALL pk3's from the fs_game path
	// and referenced one's from baseq3
	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( search->pack ) {
			if ( search->pack->referenced || Q_stricmpn( search->pack->pakGamename, fs_basegame->string, len ) ) {
				if ( *info ) {
					Q_strcat( info, sizeof( info ), " " );
				}
				Q_strcat( info, sizeof( info ), search->pack->pakGamename );
				Q_strcat( info, sizeof( info ), "/" );
				Q_strcat( info, sizeof( info ), search->pack->pakBasename );
			}
		}
	}

	return info;
}


/*
=====================
FS_ClearPakReferences
=====================
*/
void FS_ClearPakReferences( int flags ) {
	searchpath_t *search;

	if ( !flags ) {
		flags = -1;
	}
	for ( search = fs_searchpaths; search; search = search->next ) {
		// is the element a pak file and has it been referenced?
		if ( search->pack ) {
			search->pack->referenced &= ~flags;
		}
	}
}


/*
=====================
FS_ApplyDirPolicy

Set access rights for non-regular (pk3dir) directories
=====================
*/
static void FS_SetDirPolicy( dirPolicy_t policy ) {
	searchpath_t	*search;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		if ( search->dir && search->policy != DIR_STATIC ) {
			search->policy = policy;
		}
	}
}


/*
=====================
FS_PureServerSetLoadedPaks

If the string is empty, all data sources will be allowed.
If not empty, only pk3 files that match one of the space
separated checksums will be checked for files, with the
exception of .cfg and .dat files.
=====================
*/
void FS_PureServerSetLoadedPaks( const char *pakSums, const char *pakNames ) {
	int		i, c, d;

	Cmd_TokenizeString( pakSums );

	c = Cmd_Argc();
	if ( c > ARRAY_LEN( fs_serverPaks ) ) {
		c = ARRAY_LEN( fs_serverPaks );
	}

	fs_numServerPaks = c;

	FS_SetDirPolicy( c ? DIR_DENY : DIR_ALLOW );

	for ( i = 0 ; i < c ; i++ ) {
		fs_serverPaks[i] = atoi( Cmd_Argv( i ) );
	}

	if ( fs_numServerPaks ) {
		Com_DPrintf( "Connected to a pure server.\n" );
	}
	else
	{
		if ( fs_reordered )
		{
			// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=540
			// force a restart to make sure the search order will be correct
			Com_DPrintf( "FS search reorder is required\n" );
			FS_Restart( fs_checksumFeed );
			return;
		}
	}

	for ( i = 0 ; i < ARRAY_LEN( fs_serverPakNames ) ; i++ ) {
		if ( fs_serverPakNames[i] ) {
			Z_Free( fs_serverPakNames[i] );
		}
		fs_serverPakNames[i] = NULL;
	}

	if ( pakNames && *pakNames ) {
		Cmd_TokenizeString( pakNames );

		d = Cmd_Argc();
		if ( d > ARRAY_LEN( fs_serverPakNames ) ) {
			d = ARRAY_LEN( fs_serverPakNames );
		}

		for ( i = 0 ; i < d ; i++ ) {
			fs_serverPakNames[i] = FS_CopyString( Cmd_Argv( i ) );
		}
	}
}


/*
=====================
FS_PureServerSetReferencedPaks

The checksums and names of the pk3 files referenced at the server
are sent to the client and stored here. The client will use these
checksums to see if any pk3 files need to be auto-downloaded. 
=====================
*/
void FS_PureServerSetReferencedPaks( const char *pakSums, const char *pakNames ) {
	int		i, c, d = 0;

	Cmd_TokenizeString( pakSums );

	c = Cmd_Argc();
	if ( c > ARRAY_LEN( fs_serverReferencedPaks ) ) {
		c = ARRAY_LEN( fs_serverReferencedPaks );
	}

	for ( i = 0 ; i < c ; i++ ) {
		fs_serverReferencedPaks[i] = atoi( Cmd_Argv( i ) );
	}

	for ( i = 0 ; i < ARRAY_LEN( fs_serverReferencedPakNames ); i++ ) {
		if ( fs_serverReferencedPakNames[i] )
			Z_Free( fs_serverReferencedPakNames[i] );
		fs_serverReferencedPakNames[i] = NULL;
	}

	if ( pakNames && *pakNames ) {
		Cmd_TokenizeString( pakNames );

		d = Cmd_Argc();

		if ( d > c )
			d = c;

		for ( i = 0 ; i < d ; i++ ) {

			// Too long pak name may lose its extension during further processing
			if ( strlen( Cmd_Argv( i ) ) >= MAX_OSPATH-13 ) // + ".00000000.pk3"
				Com_Error( ERR_DROP, "Referenced pak name is too long: %s", Cmd_Argv( i ) );

			fs_serverReferencedPakNames[i] = FS_CopyString( Cmd_Argv( i ) );
		}
	}

	// ensure that there are as many checksums as there are pak names.
	if ( d < c )
		c = d;

	fs_numServerReferencedPaks = c;	
}


/*
================
FS_InitFilesystem

Called only at inital startup, not when the filesystem
is resetting due to a game change
================
*/
void FS_InitFilesystem( void ) {
	// allow command line parms to override our defaults
	// we have to specially handle this, because normal command
	// line variable sets don't happen until after the filesystem
	// has already been initialized
	Com_StartupVariable( "fs_basepath" );
	Com_StartupVariable( "fs_homepath" );
	Com_StartupVariable( "fs_game" );
	Com_StartupVariable( "fs_basegame" );
	Com_StartupVariable( "fs_copyfiles" );
	Com_StartupVariable( "fs_restrict" );
	Com_StartupVariable( "fs_locked" );

#ifdef _WIN32
 	_setmaxstdio( 2048 );
#endif

	// try to start up normally
	FS_Restart( 0 );
}


/*
================
FS_Restart
================
*/
void FS_Restart( int checksumFeed ) {

	// last valid game folder used
	static char lastValidBase[MAX_OSPATH];
	static char lastValidGame[MAX_OSPATH];

	static qboolean execConfig = qfalse;

	// free anything we currently have loaded
	FS_Shutdown( qfalse );

	// set the checksum feed
	fs_checksumFeed = checksumFeed;

	// try to start up normally
	FS_Startup();

	// if we can't find default.cfg, assume that the paths are
	// busted and error out now, rather than getting an unreadable
	// graphics screen when the font fails to load
	if ( FS_ReadFile( "default.cfg", NULL ) <= 0 ) {
		// this might happen when connecting to a pure server not using BASEGAME/pak0.pk3
		// (for instance a TA demo server)
		if (lastValidBase[0]) {
			FS_PureServerSetLoadedPaks("", "");
			Cvar_Set( "fs_basepath", lastValidBase );
			Cvar_Set( "fs_game", lastValidGame );
			lastValidBase[0] = '\0';
			lastValidGame[0] = '\0';
			Cvar_Set( "fs_restrict", "0" );
			execConfig = qtrue;
			FS_Restart( checksumFeed );
			Com_Error( ERR_DROP, "Invalid game folder" );
			return;
		}
		Com_Error( ERR_FATAL, "Couldn't load default.cfg" );
	}

	// new check before safeMode
	if ( Q_stricmp(fs_gamedirvar->string, lastValidGame) && execConfig ) {
		// skip the q3config.cfg if "safe" is on the command line
		if ( !Com_SafeMode() ) {
			Cbuf_AddText( "exec " Q3CONFIG_CFG "\n" );
		}
	}
	execConfig = qfalse;

	Q_strncpyz( lastValidBase, fs_basepath->string, sizeof( lastValidBase ) );
	Q_strncpyz( lastValidGame, fs_gamedirvar->string, sizeof( lastValidGame ) );
}


/*
=================
FS_Reload
=================
*/
void FS_Reload( void ) 
{
	FS_Restart( fs_checksumFeed );
}


/*
=================
FS_ConditionalRestart
restart if necessary
=================
*/
qboolean FS_ConditionalRestart( int checksumFeed, qboolean clientRestart )
{
	if ( fs_gamedirvar->modified )
	{
		Com_GameRestart( checksumFeed, clientRestart );
		return qtrue;
	}
	else if ( checksumFeed != fs_checksumFeed )
	{
		FS_Restart( checksumFeed );
		return qtrue;
	}
	else if( fs_numServerPaks && !fs_reordered ) 
	{
		FS_ReorderPurePaks();
	}
	
	return qfalse;
}


/*
========================================================================================

Handle based file calls for virtual machines

========================================================================================
*/

int	FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
	int		r;
	qboolean	sync;
	fileHandleData_t *fhd;

	if ( !qpath || !*qpath ) {
		if ( f ) 
			*f = FS_INVALID_HANDLE;
		return -1;
	}

	r = 0;	// file size
	sync = qfalse;

	switch( mode ) {
	case FS_READ:
		r = FS_FOpenFileRead( qpath, f, qtrue );
		break;
	case FS_WRITE:
		if ( f == NULL )
			return -1;
		*f = FS_FOpenFileWrite( qpath );
		break;
	case FS_APPEND_SYNC:
		sync = qtrue;
	case FS_APPEND:
		if ( f == NULL )
			return -1;
		*f = FS_FOpenFileAppend( qpath );
		break;
	default:
		Com_Error( ERR_FATAL, "FSH_FOpenFile: bad mode %i", mode );
		return -1;
	}

	if ( !f )
		return r;

	if ( *f == FS_INVALID_HANDLE ) {
		return -1;
	}

	fhd = &fsh[ *f ];

	fhd->handleSync = sync;

	return r;
}


int FS_FTell( fileHandle_t f ) {
	int pos;
	if ( fsh[f].zipFile ) {
		pos = unztell( fsh[f].handleFiles.file.z );
	} else {
		pos = ftell( fsh[f].handleFiles.file.o );
	}
	return pos;
}


void FS_Flush( fileHandle_t f ) 
{
	fflush( fsh[f].handleFiles.file.o );
}


void	FS_FilenameCompletion( const char *dir, const char *ext,
		qboolean stripExt, void(*callback)(const char *s), int flags ) {
	char	filename[ MAX_STRING_CHARS ];
	char	**filenames;
	int		nfiles;
	int		i;

	filenames = FS_ListFilteredFiles( dir, ext, NULL, &nfiles, flags );

	if ( nfiles >= 2 )
		FS_SortFileList( filenames, nfiles-1 );

	for( i = 0; i < nfiles; i++ ) {

		Q_strncpyz( filename, filenames[ i ], sizeof( filename ) );
		FS_ConvertPath( filename );

		if ( stripExt ) {
			COM_StripExtension( filename, filename, sizeof( filename ) );
		}

		callback( filename );
	}
	FS_FreeFileList( filenames );
}


/*
	Secure VM functions
*/

int FS_VM_OpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode, handleOwner_t owner ) {
	int r;

	r = FS_FOpenFileByMode( qpath, f, mode );

	if ( f && *f != FS_INVALID_HANDLE )
		fsh[ *f ].owner = owner;

	return r;
}


int FS_VM_ReadFile( void *buffer, int len, fileHandle_t f, handleOwner_t owner ) {

	if ( f <= 0 || f >= MAX_FILE_HANDLES )
		return 0;

	if ( fsh[f].owner != owner || !fsh[f].handleFiles.file.v )
		return 0; 

	return FS_Read( buffer, len, f );
}


void FS_VM_WriteFile( void *buffer, int len, fileHandle_t f, handleOwner_t owner ) {

	if ( f <= 0 || f >= MAX_FILE_HANDLES )
		return;

	if ( fsh[f].owner != owner || !fsh[f].handleFiles.file.v )
		return;

	FS_Write( buffer, len, f );
}


int FS_VM_SeekFile( fileHandle_t f, long offset, fsOrigin_t origin, handleOwner_t owner ) {
	int r;

	if ( f <= 0 || f >= MAX_FILE_HANDLES )
		return -1;

	if ( fsh[f].owner != owner || !fsh[f].handleFiles.file.v )
		return -1;

	r = FS_Seek( f, offset, origin );
	return r;
}


void FS_VM_CloseFile( fileHandle_t f, handleOwner_t owner ) {

	if ( f <= 0 || f >= MAX_FILE_HANDLES )
		return;

	if ( fsh[f].owner != owner || !fsh[f].handleFiles.file.v )
		return;

	FS_FCloseFile( f );
}


void FS_VM_CloseFiles( handleOwner_t owner ) 
{
	int i;
	for ( i = 1; i < MAX_FILE_HANDLES; i++ ) 
	{
		if ( fsh[i].owner != owner )
			continue;
		Com_Printf( S_COLOR_YELLOW"%s:%i:%s leaked filehandle\n", 
			FS_OwnerName( owner ), i, fsh[i].name );
		FS_FCloseFile( i );
	}
}


const char *FS_GetCurrentGameDir( void )
{
	if ( fs_gamedirvar->string[0] )
		return fs_gamedirvar->string;

	return fs_basegame->string;
}


const char *FS_GetBaseGameDir( void )
{
	return fs_basegame->string;
}


const char *FS_GetBasePath( void )
{
	if ( fs_basepath && fs_basepath->string[0] )
		return fs_basepath->string;
	else
		return "";
}


const char *FS_GetHomePath( void )
{
	if ( fs_homepath && fs_homepath->string[0] )
		return fs_homepath->string;
	else
		return FS_GetBasePath();
}


const char *FS_GetGamePath( void )
{
	static char buffer[ MAX_OSPATH + MAX_CVAR_VALUE_STRING + 1 ];
	if ( fs_gamedirvar && fs_gamedirvar->string[0] ) {
		Com_sprintf( buffer, sizeof( buffer ), "%s%c%s", FS_GetHomePath(), 
			PATH_SEP, fs_gamedirvar->string );
		return buffer;
	} else {
		buffer[0] = '\0';
		return buffer;
	}
}


fileHandle_t FS_PipeOpenWrite( const char *cmd, const char *filename ) {
	fileHandleData_t *fd;
	fileHandle_t f;
	const char *ospath;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_PipeOpenWrite: %s\n", ospath );
	}

	FS_CheckFilenameIsNotAllowed( ospath, __func__, qfalse );

	f = FS_HandleForFile();
	fd = &fsh[ f ];
	FS_InitHandle( fd );

	if ( FS_CreatePath( ospath ) ) {
		return FS_INVALID_HANDLE;
	}

#ifdef _WIN32
	fd->handleFiles.file.o = _popen( cmd, "wb" );
#else
	fd->handleFiles.file.o = popen( cmd, "w" );
#endif

	if ( fd->handleFiles.file.o == NULL ) {
		return FS_INVALID_HANDLE;
	}

	Q_strncpyz( fd->name, filename, sizeof( fd->name ) );
	fd->handleSync = qfalse;
	fd->zipFile = qfalse;

	return f;
}


void FS_PipeClose( fileHandle_t f )
{
	if ( !fs_searchpaths )
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );

	if ( fsh[f].zipFile )
		return;

	if ( fsh[f].handleFiles.file.o ) {
#ifdef _WIN32
		_pclose( fsh[f].handleFiles.file.o );
#else
		pclose( fsh[f].handleFiles.file.o );
#endif
	}

	Com_Memset( &fsh[f], 0, sizeof( fsh[f] ) );
}
