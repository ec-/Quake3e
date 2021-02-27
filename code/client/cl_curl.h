/*
===========================================================================
Copyright (C) 2006 Tony J. White (tjw@tjw.org)

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


#ifndef __QCURL_H__
#define __QCURL_H__

extern cvar_t *cl_cURLLib;

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef _WIN32
#define DEFAULT_CURL_LIB "libcurl-3.dll"
#elif defined(__APPLE__)
#define DEFAULT_CURL_LIB "libcurl.dylib"
#else
#define DEFAULT_CURL_LIB "libcurl.so.4"
#define ALTERNATE_CURL_LIB "libcurl.so.3"
#endif

#ifdef USE_LOCAL_HEADERS
  #include "curl/curl.h"
#else
  #include <curl/curl.h>
#endif


#ifdef USE_CURL_DLOPEN
extern char* (*qcurl_version)(void);

extern CURL* (*qcurl_easy_init)(void);
extern CURLcode (*qcurl_easy_setopt)(CURL *curl, CURLoption option, ...);
extern CURLcode (*qcurl_easy_perform)(CURL *curl);
extern void (*qcurl_easy_cleanup)(CURL *curl);
extern CURLcode (*qcurl_easy_getinfo)(CURL *curl, CURLINFO info, ...);
extern void (*qcurl_easy_reset)(CURL *curl);
extern const char *(*qcurl_easy_strerror)(CURLcode);

extern CURLM* (*qcurl_multi_init)(void);
extern CURLMcode (*qcurl_multi_add_handle)(CURLM *multi_handle, CURL *curl_handle);
extern CURLMcode (*qcurl_multi_remove_handle)(CURLM *multi_handle, CURL *curl_handle);
extern CURLMcode (*qcurl_multi_fdset)(CURLM *multi_handle,
						fd_set *read_fd_set,
						fd_set *write_fd_set,
						fd_set *exc_fd_set,
						int *max_fd);
extern CURLMcode (*qcurl_multi_perform)(CURLM *multi_handle, int *running_handles);
extern CURLMcode (*qcurl_multi_cleanup)(CURLM *multi_handle);
extern CURLMsg *(*qcurl_multi_info_read)(CURLM *multi_handle, int *msgs_in_queue);
extern const char *(*qcurl_multi_strerror)(CURLMcode);
#else
#define qcurl_version curl_version

#define qcurl_easy_init curl_easy_init
#define qcurl_easy_setopt curl_easy_setopt
#define qcurl_easy_perform curl_easy_perform
#define qcurl_easy_cleanup curl_easy_cleanup
#define qcurl_easy_getinfo curl_easy_getinfo
#define qcurl_easy_duphandle curl_easy_duphandle
#define qcurl_easy_reset curl_easy_reset
#define qcurl_easy_strerror curl_easy_strerror

#define qcurl_multi_init curl_multi_init
#define qcurl_multi_add_handle curl_multi_add_handle
#define qcurl_multi_remove_handle curl_multi_remove_handle
#define qcurl_multi_fdset curl_multi_fdset
#define qcurl_multi_perform curl_multi_perform
#define qcurl_multi_cleanup curl_multi_cleanup
#define qcurl_multi_info_read curl_multi_info_read
#define qcurl_multi_strerror curl_multi_strerror
#endif

qboolean CL_cURL_Init( void );
void CL_cURL_Shutdown( void );
void CL_cURL_BeginDownload( const char *localName, const char *remoteURL );
void CL_cURL_PerformDownload( void );
void CL_cURL_Cleanup( void );

typedef struct download_s {
	char		URL[MAX_OSPATH];
	char		TempName[MAX_OSPATH+4];
	char		Name[MAX_OSPATH];
	char		gameDir[MAX_OSPATH];
	char		progress[MAX_OSPATH+64];
	CURL		*cURL;
	CURLM		*cURLM;
	fileHandle_t fHandle;
	int			Size;
	int			Count;
	qboolean	headerCheck;
	qboolean	mapAutoDownload;

	struct func_s {
		char*		(*version)(void);
		char *		(*easy_escape)(CURL *curl, const char *string, int length);
		void		(*free)(char *ptr);

		CURL*		(*easy_init)(void);
		CURLcode	(*easy_setopt)(CURL *curl, CURLoption option, ...);
		CURLcode	(*easy_perform)(CURL *curl);
		void		(*easy_cleanup)(CURL *curl);
		CURLcode	(*easy_getinfo)(CURL *curl, CURLINFO info, ...);
		const char *(*easy_strerror)(CURLcode);

		CURLM*		(*multi_init)(void);
		CURLMcode	(*multi_add_handle)(CURLM *multi_handle, CURL *curl_handle);
		CURLMcode	(*multi_remove_handle)(CURLM *multi_handle, CURL *curl_handle);
		CURLMcode	(*multi_perform)(CURLM *multi_handle, int *running_handles);
		CURLMcode	(*multi_cleanup)(CURLM *multi_handle);
		CURLMsg		*(*multi_info_read)(CURLM *multi_handle, int *msgs_in_queue);
		const char	*(*multi_strerror)(CURLMcode);

		void		*lib;
	} func;
} download_t;

#endif	// __QCURL_H__
