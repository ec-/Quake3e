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
#ifndef __Q_PLATFORM_H
#define __Q_PLATFORM_H

#define QDECL

#define id386 0
#define idx64 0

//================================================================= WIN32 ===

#ifdef _WIN32

#undef QDECL
#define QDECL __cdecl
#define Q_NEWLINE "\r\n"

#if defined (_WIN32_WINNT)
#if _WIN32_WINNT < 0x0501
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#else
#define _WIN32_WINNT 0x0501
#endif

#if defined( _MSC_VER ) && _MSC_VER >= 1400 // MSVC++ 8.0 at least
#define OS_STRING "win_msvc"
#elif defined __MINGW32__
#define OS_STRING "win_mingw"
#elif defined __MINGW64__
#define OS_STRING "win_mingw64"
#else
#error "Compiler not supported"
#endif

#define ID_INLINE __inline
#define PATH_SEP '\\'
#define DLL_EXT ".dll"

#if defined( _M_IX86 )
#define ARCH_STRING "x86"
#define Q3_LITTLE_ENDIAN
#undef id386
#define id386 1
#endif

#if defined( _M_AMD64 )
#define ARCH_STRING "x86_64"
#define Q3_LITTLE_ENDIAN
#undef idx64
#define idx64 1
//#define UNICODE
#endif

#else // !defined _WIN32

#define Q_NEWLINE "\n"

#endif

//================================================================= LINUX ===

#ifdef __linux__

#include <endian.h>

#define OS_STRING "linux"
#define ID_INLINE inline
#define PATH_SEP '/'
#define DLL_EXT ".so"

#if defined __i386__
#define ARCH_STRING "i386"
#define Q3_LITTLE_ENDIAN
#undef id386
#define id386 1
#endif

#if defined __x86_64__
#define ARCH_STRING "x86_64"
#define Q3_LITTLE_ENDIAN
#undef idx64
#define idx64 1
#endif

#endif

//=============================================================== FreeBSD ===

#ifdef __FreeBSD__ // rb010123

#include <sys/types.h>
#include <machine/endian.h>

#define OS_STRING "freebsd"
#define ID_INLINE inline
#define PATH_SEP '/'

#ifdef __i386__
#define ARCH_STRING "i386"
#elif defined __axp__
#define ARCH_STRING "alpha"
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define Q3_BIG_ENDIAN
#else
#define Q3_LITTLE_ENDIAN
#endif

#define DLL_EXT ".so"

#endif

//================================================================ NetBSD ===

// This is very much like the FreeBSD one and can probably be merged
#ifdef __NetBSD__

#include <sys/types.h>
#include <sys/types.h>
#include <machine/endian.h>

#define OS_STRING "netbsd"
#define ID_INLINE inline
#define PATH_SEP '/'

#ifdef __i386__
#define ARCH_STRING "i386"
// Netbsd has alot of platforms
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define Q3_BIG_ENDIAN
#else
#define Q3_LITTLE_ENDIAN
#endif

#define DLL_EXT ".so"

#endif

//================================================================== Q3VM ===

#ifdef Q3_VM

#define OS_STRING "q3vm"
#define ID_INLINE
#define PATH_SEP '/'

#define ARCH_STRING "bytecode"

#define DLL_EXT ".qvm"

#endif

//===========================================================================

//catch missing defines in above blocks
#if !defined( OS_STRING )
#error "Operating system not supported"
#endif

#if !defined( ARCH_STRING )
#error "Architecture not supported"
#endif

#ifndef ID_INLINE
#error "ID_INLINE not defined"
#endif

#ifndef PATH_SEP
#error "PATH_SEP not defined"
#endif

#ifndef DLL_EXT
#error "DLL_EXT not defined"
#endif

// Endianess

#if defined( Q3_BIG_ENDIAN ) && defined( Q3_LITTLE_ENDIAN )

#error "Endianness defined as both big and little"

#elif defined( Q3_BIG_ENDIAN )

#define CopyLittleShort(dest, src) CopyShortSwap(dest, src)
#define CopyLittleLong(dest, src) CopyLongSwap(dest, src)
#define LittleShort(x) ShortSwap(x)
#define LittleLong(x) LongSwap(x)
#define LittleFloat(x) FloatSwap(&x)
#define BigShort
#define BigLong
#define BigFloat

#elif defined( Q3_LITTLE_ENDIAN ) || defined ( Q3_VM )

#define CopyLittleShort(dest, src) Com_Memcpy(dest, src, 2)
#define CopyLittleLong(dest, src) Com_Memcpy(dest, src, 4)
#define LittleShort
#define LittleLong
#define LittleFloat
#define BigShort(x) ShortSwap(x)
#define BigLong(x) LongSwap(x)
#define BigFloat(x) FloatSwap(&x)

#else

#error "Endianness not defined"

#endif


//platform string
#ifdef NDEBUG
#define PLATFORM_STRING OS_STRING "-" ARCH_STRING
#else
#define PLATFORM_STRING OS_STRING "-" ARCH_STRING "-debug"
#endif


#endif
