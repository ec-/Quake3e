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

#ifdef USE_CURL
#include "client.h"
cvar_t *cl_cURLLib;

#ifdef USE_CURL_DLOPEN
//#include "../sys/sys_loadlib.h"

char* (*qcurl_version)(void);

CURL* (*qcurl_easy_init)(void);
CURLcode (*qcurl_easy_setopt)(CURL *curl, CURLoption option, ...);
CURLcode (*qcurl_easy_perform)(CURL *curl);
void (*qcurl_easy_cleanup)(CURL *curl);
CURLcode (*qcurl_easy_getinfo)(CURL *curl, CURLINFO info, ...);
CURL* (*qcurl_easy_duphandle)(CURL *curl);
void (*qcurl_easy_reset)(CURL *curl);
const char *(*qcurl_easy_strerror)(CURLcode);

CURLM* (*qcurl_multi_init)(void);
CURLMcode (*qcurl_multi_add_handle)(CURLM *multi_handle,
                                                CURL *curl_handle);
CURLMcode (*qcurl_multi_remove_handle)(CURLM *multi_handle,
                                                CURL *curl_handle);
CURLMcode (*qcurl_multi_fdset)(CURLM *multi_handle,
                                                fd_set *read_fd_set,
                                                fd_set *write_fd_set,
                                                fd_set *exc_fd_set,
                                                int *max_fd);
CURLMcode (*qcurl_multi_perform)(CURLM *multi_handle,
                                                int *running_handles);
CURLMcode (*qcurl_multi_cleanup)(CURLM *multi_handle);
CURLMsg *(*qcurl_multi_info_read)(CURLM *multi_handle,
                                                int *msgs_in_queue);
const char *(*qcurl_multi_strerror)(CURLMcode);

static void *cURLLib = NULL;

/*
=================
GPA
=================
*/
static void *GPA(char *str)
{
	void *rv;

	rv = Sys_LoadFunction(cURLLib, str);
	if(!rv)
	{
		Com_Printf("Can't load symbol %s\n", str);
		clc.cURLEnabled = qfalse;
		return NULL;
	}
	else
	{
		Com_DPrintf("Loaded symbol %s (0x%p)\n", str, rv);
        return rv;
	}
}
#endif /* USE_CURL_DLOPEN */

/*
=================
CL_cURL_Init
=================
*/
qboolean CL_cURL_Init()
{
#ifdef USE_CURL_DLOPEN
	if(cURLLib)
		return qtrue;


	Com_Printf("Loading \"%s\"...", cl_cURLLib->string);
	if( (cURLLib = Sys_LoadLibrary(cl_cURLLib->string)) == 0 )
	{
#ifdef _WIN32
		return qfalse;
#else
		char fn[1024];

		Q_strncpyz( fn, Sys_Cwd( ), sizeof( fn ) );
		strncat(fn, "/", sizeof(fn)-strlen(fn)-1);
		strncat(fn, cl_cURLLib->string, sizeof(fn)-strlen(fn)-1);

		if((cURLLib = Sys_LoadLibrary(fn)) == 0)
		{
#ifdef ALTERNATE_CURL_LIB
			// On some linux distributions there is no libcurl.so.3, but only libcurl.so.4. That one works too.
			if( (cURLLib = Sys_LoadLibrary(ALTERNATE_CURL_LIB)) == 0 )
			{
				return qfalse;
			}
#else
			return qfalse;
#endif
		}
#endif /* _WIN32 */
	}

	clc.cURLEnabled = qtrue;

	qcurl_version = GPA("curl_version");

	qcurl_easy_init = GPA("curl_easy_init");
	qcurl_easy_setopt = GPA("curl_easy_setopt");
	qcurl_easy_perform = GPA("curl_easy_perform");
	qcurl_easy_cleanup = GPA("curl_easy_cleanup");
	qcurl_easy_getinfo = GPA("curl_easy_getinfo");
	qcurl_easy_duphandle = GPA("curl_easy_duphandle");
	qcurl_easy_reset = GPA("curl_easy_reset");
	qcurl_easy_strerror = GPA("curl_easy_strerror");
	
	qcurl_multi_init = GPA("curl_multi_init");
	qcurl_multi_add_handle = GPA("curl_multi_add_handle");
	qcurl_multi_remove_handle = GPA("curl_multi_remove_handle");
	qcurl_multi_fdset = GPA("curl_multi_fdset");
	qcurl_multi_perform = GPA("curl_multi_perform");
	qcurl_multi_cleanup = GPA("curl_multi_cleanup");
	qcurl_multi_info_read = GPA("curl_multi_info_read");
	qcurl_multi_strerror = GPA("curl_multi_strerror");

	if(!clc.cURLEnabled)
	{
		CL_cURL_Shutdown();
		Com_Printf("FAIL One or more symbols not found\n");
		return qfalse;
	}
	Com_Printf("OK\n");

	return qtrue;
#else
	clc.cURLEnabled = qtrue;
	return qtrue;
#endif /* USE_CURL_DLOPEN */
}

/*
=================
CL_cURL_Shutdown
=================
*/
void CL_cURL_Shutdown( void )
{
	CL_cURL_Cleanup();
#ifdef USE_CURL_DLOPEN
	if(cURLLib)
	{
		Sys_UnloadLibrary(cURLLib);
		cURLLib = NULL;
	}
	qcurl_easy_init = NULL;
	qcurl_easy_setopt = NULL;
	qcurl_easy_perform = NULL;
	qcurl_easy_cleanup = NULL;
	qcurl_easy_getinfo = NULL;
	qcurl_easy_duphandle = NULL;
	qcurl_easy_reset = NULL;

	qcurl_multi_init = NULL;
	qcurl_multi_add_handle = NULL;
	qcurl_multi_remove_handle = NULL;
	qcurl_multi_fdset = NULL;
	qcurl_multi_perform = NULL;
	qcurl_multi_cleanup = NULL;
	qcurl_multi_info_read = NULL;
	qcurl_multi_strerror = NULL;
#endif /* USE_CURL_DLOPEN */
}

void CL_cURL_Cleanup(void)
{
	if(clc.downloadCURLM) {
		if(clc.downloadCURL) {
			qcurl_multi_remove_handle(clc.downloadCURLM,
				clc.downloadCURL);
			qcurl_easy_cleanup(clc.downloadCURL);
		}
		qcurl_multi_cleanup(clc.downloadCURLM);
		clc.downloadCURLM = NULL;
		clc.downloadCURL = NULL;
	}
	else if(clc.downloadCURL) {
		qcurl_easy_cleanup(clc.downloadCURL);
		clc.downloadCURL = NULL;
	}
}

static int CL_cURL_CallbackProgress( void *dummy, double dltotal, double dlnow,
	double ultotal, double ulnow )
{
	clc.downloadSize = (int)dltotal;
	Cvar_SetValue( "cl_downloadSize", clc.downloadSize );
	clc.downloadCount = (int)dlnow;
	Cvar_SetValue( "cl_downloadCount", clc.downloadCount );
	return 0;
}

static size_t CL_cURL_CallbackWrite(void *buffer, size_t size, size_t nmemb,
	void *stream)
{
	FS_Write( buffer, size*nmemb, ((fileHandle_t*)stream)[0] );
	return size*nmemb;
}

void CL_cURL_BeginDownload( const char *localName, const char *remoteURL )
{
	clc.cURLUsed = qtrue;
	Com_Printf("URL: %s\n", remoteURL);
	Com_DPrintf("***** CL_cURL_BeginDownload *****\n"
		"Localname: %s\n"
		"RemoteURL: %s\n"
		"****************************\n", localName, remoteURL);
	CL_cURL_Cleanup();
	Q_strncpyz(clc.downloadURL, remoteURL, sizeof(clc.downloadURL));
	Q_strncpyz(clc.downloadName, localName, sizeof(clc.downloadName));
	Com_sprintf(clc.downloadTempName, sizeof(clc.downloadTempName),
		"%s.tmp", localName);

	// Set so UI gets access to it
	Cvar_Set("cl_downloadName", localName);
	Cvar_Set("cl_downloadSize", "0");
	Cvar_Set("cl_downloadCount", "0");
	Cvar_SetValue("cl_downloadTime", cls.realtime);

	clc.downloadBlock = 0; // Starting new file
	clc.downloadCount = 0;

	clc.downloadCURL = qcurl_easy_init();
	if(!clc.downloadCURL) {
		Com_Error(ERR_DROP, "CL_cURL_BeginDownload: qcurl_easy_init() "
			"failed\n");
		return;
	}
	clc.download = FS_SV_FOpenFileWrite(clc.downloadTempName);
	if(!clc.download) {
		Com_Error(ERR_DROP, "CL_cURL_BeginDownload: failed to open "
			"%s for writing\n", clc.downloadTempName);
		return;
	}
	FS_LockHandle( clc.download ); // -EC-

	if ( com_developer->integer )
		qcurl_easy_setopt( clc.downloadCURL, CURLOPT_VERBOSE, 1 );
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_URL, clc.downloadURL);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_TRANSFERTEXT, 0);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_REFERER, va("ioQ3://%s",
		NET_AdrToString(clc.serverAddress)));
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_USERAGENT, Q3_VERSION);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_WRITEFUNCTION,
		CL_cURL_CallbackWrite);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_WRITEDATA, &clc.download);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_NOPROGRESS, 0);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_PROGRESSFUNCTION,
		CL_cURL_CallbackProgress);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_PROGRESSDATA, NULL);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_FAILONERROR, 1);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_FOLLOWLOCATION, 1);
	qcurl_easy_setopt(clc.downloadCURL, CURLOPT_MAXREDIRS, 5);
	clc.downloadCURLM = qcurl_multi_init();	
	if(!clc.downloadCURLM) {
		qcurl_easy_cleanup(clc.downloadCURL);
		clc.downloadCURL = NULL;
		Com_Error(ERR_DROP, "CL_cURL_BeginDownload: qcurl_multi_init() "
			"failed\n");
		return;
	}
	qcurl_multi_add_handle(clc.downloadCURLM, clc.downloadCURL);

	if(!(clc.sv_allowDownload & DLF_NO_DISCONNECT) &&
		!clc.cURLDisconnected) {

		CL_AddReliableCommand("disconnect", qtrue);
		CL_WritePacket();
		CL_WritePacket();
		CL_WritePacket();
		clc.cURLDisconnected = qtrue;
	}
}

void CL_cURL_PerformDownload(void)
{
	CURLMcode res;
	CURLMsg *msg;
	int c;
	int i = 0;

	res = qcurl_multi_perform(clc.downloadCURLM, &c);
	while(res == CURLM_CALL_MULTI_PERFORM && i < 100) {
		res = qcurl_multi_perform(clc.downloadCURLM, &c);
		i++;
	}
	if(res == CURLM_CALL_MULTI_PERFORM)
		return;
	msg = qcurl_multi_info_read(clc.downloadCURLM, &c);
	if(msg == NULL) {
		return;
	}
	FS_FCloseFile( clc.download );
	if(msg->msg == CURLMSG_DONE && msg->data.result == CURLE_OK) {
		FS_SV_Rename(clc.downloadTempName, clc.downloadName);
		clc.downloadRestart = qtrue;
	}
	else {
		long code;

		qcurl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE,
			&code);	
		Com_Error(ERR_DROP, "Download Error: %s Code: %ld URL: %s",
			qcurl_easy_strerror(msg->data.result),
			code, clc.downloadURL);
	}

	CL_NextDownload();
}


/*  
==================================

Common CURL downloading functions

==================================
*/

/*
==================================
stristr

case-insensitive sub-string search
==================================
*/
char* stristr( char *source, char *target ) 
{
	char *p0, *p1, *p2, *pn;
	char c1, c2;

	if ( *target == '\0' )  
	{
		return source;
	}

	pn = source;
	p1 = source;
	p2 = target;
	
	while ( *++p2 )
	{
	    pn++;
	}

	while ( *pn != '\0' ) 
	{

    	p0 = p1;
	    p2 = target;

    	while ( (c1 = *p1) != '\0' && (c2 = *p2) != '\0' )
		{
				if ( c1 <= 'Z' && c1 >= 'A' )
					c1 += ('a' - 'A');

				if ( c2 <= 'Z' && c2 >= 'A' )
					c2 += ('a' - 'A');

				if ( c1 != c2 ) 
				{
					break;
				}

				p1++;
				p2++;
		}

		if ( *p2 == '\0' )  
		{
			return p0;
		}

		p1 = p0 + 1;
	    pn++;
  }

  return NULL;
}

/*
==================================
replace
==================================
*/
int replace1( const char src, const char dst, char *str ) 
{
	int count;

	if ( !str ) 
		return 0;

	count = 0;

	while ( *str != '\0' ) 
	{
		if ( *str == src )	
		{
			*str = dst;
			count++;
		}
		str++;
	}

	return count;
}



void Com_DL_Cleanup( download_t *dl )
{
	if( dl->cURLM ) 
	{
		if ( dl->cURL ) 
		{
			qcurl_multi_remove_handle( dl->cURLM, dl->cURL );
			qcurl_easy_cleanup( dl->cURL );
		}
		qcurl_multi_cleanup( dl->cURLM );
		dl->cURLM = NULL;
		dl->cURL = NULL;
	}
	else if( dl->cURL ) 
	{
		qcurl_easy_cleanup( dl->cURL );
		dl->cURL = NULL;
	}
	if ( dl->fHandle != FS_INVALID_HANDLE ) 
	{
		FS_FCloseFile( dl->fHandle );
		dl->fHandle = FS_INVALID_HANDLE;
	}

	dl->Block = 0;
	dl->Count = 0;
	dl->Size = 0;

	dl->URL[0] = '\0';
	dl->Name[0] = '\0';
	dl->TempName[0] = '\0';
}


static int Com_DL_CallbackProgress( void *data, double dltotal, double dlnow, double ultotal, double ulnow )
{
	download_t *dl = (download_t *)data;
	
	dl->Size = (int)dltotal;
	dl->Count = (int)dlnow;
	
	if ( dl->clientUI ) 
	{
		Cvar_SetValue( "cl_downloadSize", dl->Size );
		Cvar_SetValue( "cl_downloadCount", dl->Count );
	}

	return 0;
}


static size_t Com_DL_CallbackWrite( void *ptr, size_t size, size_t nmemb, void *userdata )
{
	download_t *dl;

	dl = (download_t *)userdata;

	if ( dl->fHandle == FS_INVALID_HANDLE ) 
		return (size_t)-1;

	FS_Write( ptr, size*nmemb, dl->fHandle );

	return (size * nmemb);
}


static size_t Com_DL_HeaderCallback( void *ptr, size_t size, size_t nmemb, void *userdata ) 
{
	char name[MAX_CVAR_VALUE_STRING];
	char header[1024], *s, quote, *d;
	download_t *dl;
	int len;

	if ( size*nmemb >= sizeof( header ) ) 
	{
		Com_Printf( S_COLOR_RED "Com_DL_HeaderCallback: header is too large." );
		return (size_t)-1;
	}

	dl = (download_t *)userdata;
	if ( dl->fHandle != FS_INVALID_HANDLE ) 
	{
		//Com_Printf( "Already created file?\n" );
		return size*nmemb; // already created file?
	}

	memcpy( header, ptr, size*nmemb+1 );
	header[ size*nmemb ] = '\0';

	//Com_Printf( "h: %s\n--------------------------\n", header );

	s = stristr( header, "content-disposition:" );
	if ( s ) 
	{
		s += 20; // strlen( "content-disposition:" )	
		s = stristr( s, "filename=" );
		if ( s ) 
		{
			s += 9; // strlen( "filename=" )
			
			d = name;
			replace1( '\r', '\0', s );
			replace1( '\n', '\0', s );

			// prevent overflow
			if ( strlen( s ) >= sizeof( name ) ) 
				s[ sizeof( name ) - 1 ] = '\0';

			if ( *s == '\'' || *s == '"' )
				quote = *s++;
			else
				quote = '\0';

			// copy filename
			while ( *s != '\0' && *s != quote ) 
				*d++ = *s++;
			len = d - name;
			*d++ = '\0';

			// validate
			if ( len < 5 || !stristr( name + len - 4, ".pk3" ) || strchr( name, '/' ) )
			{
				Com_Printf( "Com_DL_HeaderCallback: bad file name '%s'\n" );
				return (size_t)-1;
			}

			// store in
			Com_sprintf( dl->Name, sizeof( dl->Name ), "%s/%s", FS_GetCurrentGameDir(), name );

			Com_sprintf( dl->TempName, sizeof( dl->TempName ), "%s.tmp%04X", dl->Name, random() );

			//Com_Printf( S_COLOR_YELLOW "%s %s\n", dl->Name, dl->TempName );

			dl->fHandle = FS_SV_FOpenFileWrite( dl->TempName );
			if ( dl->fHandle == FS_INVALID_HANDLE ) 
			{
				Com_Printf( S_COLOR_RED "Com_DL_HeaderCallback: failed to open %s for writing\n", 
					dl->TempName );
				return (size_t)-1;
			}
			FS_LockHandle( dl->fHandle );
		}
	}
	
	return size*nmemb;
}


qboolean Com_DL_Begin( download_t *dl, const char *localName, const char *remoteURL, qboolean checkHeader )
{
	if ( dl->cURL && dl->URL[0] ) 
	{
		Com_Printf( "already downloading %s\n", dl->URL );
		return qfalse;
	}

	Com_Printf( "URL: %s\n", remoteURL );

	Com_DL_Cleanup( dl );

	dl->cURL = qcurl_easy_init();
	if ( !dl->cURL ) 
	{
		Com_Printf( S_COLOR_RED "Com_BeginDownload: qcurl_easy_init() failed\n" );
		return qfalse;
	}

	Q_strncpyz( dl->URL, remoteURL, sizeof( dl->URL ) );
	
	dl->Count = 0; // Starting new file
	dl->Block = 0;
	
	if ( !checkHeader ) 
	{
		Q_strncpyz( dl->Name, localName, sizeof( dl->Name ) );
		Com_sprintf( dl->TempName, sizeof( dl->TempName ), "%s.tmp", localName );
		dl->fHandle = FS_SV_FOpenFileWrite( dl->TempName );
		if ( dl->fHandle == FS_INVALID_HANDLE ) 
		{
			Com_Printf( S_COLOR_RED "Com_DL_Begin: failed to open %s for writing\n", 
				dl->TempName );
			return qfalse;
		}
		FS_LockHandle( dl->fHandle );

		dl->clientUI = qtrue;
	}
	else 
	{
		dl->clientUI = qfalse;
	}

	if ( com_developer->integer )
		qcurl_easy_setopt( dl->cURL, CURLOPT_VERBOSE, 1 );

	qcurl_easy_setopt( dl->cURL, CURLOPT_URL, dl->URL );
	qcurl_easy_setopt( dl->cURL, CURLOPT_TRANSFERTEXT, 0 );
	qcurl_easy_setopt( dl->cURL, CURLOPT_REFERER, va( "ioQ3://%s", NET_AdrToString( clc.serverAddress ) ) );
	qcurl_easy_setopt( dl->cURL, CURLOPT_USERAGENT, Q3_VERSION );
	qcurl_easy_setopt( dl->cURL, CURLOPT_WRITEFUNCTION,	Com_DL_CallbackWrite );
	qcurl_easy_setopt( dl->cURL, CURLOPT_WRITEDATA, dl );
	if ( checkHeader ) 
	{
		qcurl_easy_setopt( dl->cURL, CURLOPT_HEADERFUNCTION, Com_DL_HeaderCallback );
		qcurl_easy_setopt( dl->cURL, CURLOPT_HEADERDATA, dl );
	}
	qcurl_easy_setopt( dl->cURL, CURLOPT_NOPROGRESS, 0 );
	qcurl_easy_setopt( dl->cURL, CURLOPT_PROGRESSFUNCTION, Com_DL_CallbackProgress );
	qcurl_easy_setopt( dl->cURL, CURLOPT_PROGRESSDATA, dl );
	qcurl_easy_setopt( dl->cURL, CURLOPT_FAILONERROR, 1 );
	qcurl_easy_setopt( dl->cURL, CURLOPT_FOLLOWLOCATION, 1 );
	qcurl_easy_setopt( dl->cURL, CURLOPT_MAXREDIRS, 5 );
	
	dl->cURLM = qcurl_multi_init();	
	
	if ( !dl->cURLM ) 
	{
		Com_DL_Cleanup( dl );	
		Com_Printf( S_COLOR_RED "Com_DL_Begin: qcurl_multi_init() failed\n" );
		return qfalse;
	}

	if ( qcurl_multi_add_handle( dl->cURLM, dl->cURL ) != CURLM_OK ) 
	{
		Com_DL_Cleanup( dl );
		Com_Printf( S_COLOR_RED "Com_DL_Begin: qcurl_multi_add_handle() failed\n" );
		return qfalse;
	}

	return qtrue;
}


qboolean Com_DL_Perform( download_t *dl )
{
	CURLMcode res;
	CURLMsg *msg;
	long code;
	int c, n;
	int i = 0;

	res = qcurl_multi_perform( dl->cURLM, &c );

	if ( cls.state == CA_DISCONNECTED )
		n = 64;
	else
		n = 32;

	while( res == CURLM_CALL_MULTI_PERFORM && i < n ) 
	{
		res = qcurl_multi_perform( dl->cURLM, &c );
		i++;
	}
	if( res == CURLM_CALL_MULTI_PERFORM ) 
	{
		return qtrue;
	}

	msg = qcurl_multi_info_read( dl->cURLM, &c );
	if( msg == NULL ) 
	{
		return qtrue;
	}

	FS_FCloseFile( dl->fHandle );
	dl->fHandle = FS_INVALID_HANDLE;

	if ( msg->msg == CURLMSG_DONE && msg->data.result == CURLE_OK ) 
	{
		if ( dl->TempName ) 
		{
			FS_SV_Rename( dl->TempName, dl->Name );
		}
		Com_Printf( "%s downloaded\n", dl->Name );
		Com_DL_Cleanup( dl );
		FS_Reload(); //clc.downloadRestart = qtrue;
		return qfalse;
	}
	else 
	{
		qcurl_easy_getinfo( msg->easy_handle, CURLINFO_RESPONSE_CODE, &code );	
		Com_Printf( S_COLOR_RED "Download Error: %s Code: %ld URL: %s",
			qcurl_easy_strerror( msg->data.result ), code, dl->URL );
		FS_Remove( dl->TempName ); 
		Com_DL_Cleanup( dl );
	}

	return qtrue;
}


#endif /* USE_CURL */
