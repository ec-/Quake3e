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

#include "../client/snd_local.h"
#include "win_local.h"

extern cvar_t *s_khz;

static qboolean	dsound_init;
static qboolean SNDDMA_InitDS( void );

// Visual Studio 2012+ or MINGW
#if ( _MSC_VER >= 1700 ) || defined(MINGW)
#ifndef USE_WASAPI
#define USE_WASAPI 1
#endif
#endif

#if USE_WASAPI
static qboolean wasapi_init;

#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

// Ugly hack to detect Win10 without manifest
// http://www.codeproject.com/Articles/678606/Part-Overcoming-Windows-s-deprecation-of-GetVe?msg=5080848#xx5080848xx
typedef LONG( WINAPI *RtlGetVersionPtr )( RTL_OSVERSIONINFOEXW* );
static qboolean IsWindows7OrGreater( void ) {
	RtlGetVersionPtr rtl_get_version_f = NULL;
	HMODULE ntdll = GetModuleHandle( T( "ntdll" ) );
	RTL_OSVERSIONINFOEXW osver;

	if ( !ntdll )
		return qfalse; // will never happen

	rtl_get_version_f = (RtlGetVersionPtr)GetProcAddress( ntdll, "RtlGetVersion" );

	if ( !rtl_get_version_f )
		return qfalse; // will never happen

	osver.dwOSVersionInfoSize = sizeof( RTL_OSVERSIONINFOEXW );

	if ( rtl_get_version_f( &osver ) == 0 ) {
		if ( osver.dwMajorVersion >= 7 )
			return qtrue;
	}

	return qfalse;
}


UINT32				bufferFrameCount;
UINT32				bufferPosition; // in fullsamples
UINT32				bufferSampleSize;

static int			inPlay;
static HANDLE		hEvent;
static HANDLE		hThread;

static CRITICAL_SECTION cs; // to lock mixer thread during buffer painting

#ifndef AUDCLNT_STREAMFLAGS_RATEADJUST
#define AUDCLNT_STREAMFLAGS_RATEADJUST  0x00100000
#endif

const GUID IID_IUnknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const GUID IID_IAudioClient = { 0x1CB9AD4C, 0xDBFA, 0x4c32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
const GUID IID_IAudioRenderClient = { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };
const GUID CLSID_MMDeviceEnumerator = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
const GUID IID_IMMNotificationClient = { 0x7991EEC9, 0x7E89, 0x4D85, { 0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0 } };
const GUID IID_IMMDeviceEnumerator = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
const GUID PcmSubformatGuid = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
const GUID FloatSubformatGuid = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

static LPWSTR DeviceID = NULL;
static qboolean doSndRestart = qfalse;

static IAudioRenderClient	*iAudioRenderClient = NULL;
static IAudioClient			*iAudioClient = NULL; 
static IMMDeviceEnumerator	*pEnumerator = NULL;
static IMMDevice			*iMMDevice = NULL;

static void initFormat( WAVEFORMATEXTENSIBLE *wave, int nChannels, int nSamples, int nBits )
{
	Com_Memset( wave, 0, sizeof( *wave ) );

	// wave->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wave->Format.wFormatTag = WAVE_FORMAT_PCM;
	wave->Format.nChannels = nChannels;
	wave->Format.nSamplesPerSec = nSamples;
	wave->Format.nBlockAlign = (nChannels * nBits) / 8;
	wave->Format.nAvgBytesPerSec = nSamples * ( nChannels * nBits ) / 8;
	wave->Format.wBitsPerSample = nBits;

	if ( wave->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE )
	{
		wave->Format.cbSize = sizeof( WAVEFORMATEXTENSIBLE ) - sizeof( WAVEFORMATEX );
		wave->Samples.wValidBitsPerSample = nBits;
		if ( nBits == 32 )
			memcpy( &wave->SubFormat, &FloatSubformatGuid, sizeof( GUID ) );
		else
			memcpy( &wave->SubFormat, &PcmSubformatGuid, sizeof( GUID ) );
	}
}


// Sound mixer thread
static DWORD WINAPI ThreadProc( HANDLE hInited )
{
	HANDLE( WINAPI *pAvSetMmThreadCharacteristicsW )( _In_ LPCWSTR TaskName, _Inout_ LPDWORD TaskIndex );
	BOOL( WINAPI *pAvRevertMmThreadCharacteristics )( _In_ HANDLE AvrtHandle );
	BYTE	*pData;
	DWORD	taskIndex;
	HANDLE	th;
	DWORD	dwOffset;
	DWORD	dwRes;
	UINT32	samples, n;
	HRESULT hr;
	UINT32	numFramesAvailable;
	HMODULE hAVRT;

	// execution starts in main thread context

	// Ask MMCSS to temporarily boost our thread priority to reduce glitches while the low-latency stream plays
	th = NULL;
	taskIndex = 0;
	pAvSetMmThreadCharacteristicsW = NULL;
	pAvRevertMmThreadCharacteristics = NULL;
	hAVRT = LoadLibraryW( L"avrt" );
	if ( hAVRT )
	{
		pAvSetMmThreadCharacteristicsW = (void*)GetProcAddress( hAVRT, "AvSetMmThreadCharacteristicsW" );
		pAvRevertMmThreadCharacteristics = (void*)GetProcAddress( hAVRT, "AvRevertMmThreadCharacteristics" );
		if ( pAvRevertMmThreadCharacteristics && pAvSetMmThreadCharacteristicsW )
		{
			th = pAvSetMmThreadCharacteristicsW( L"Pro Audio", &taskIndex );
			if ( th == NULL )
			{
				Com_Printf( S_COLOR_YELLOW "WASAPI: thread priority setup failed\n" );
				goto err_exit;
			}
		}
		else
		{
			Com_Printf( S_COLOR_RED "WASAPI: failed to load avrt.dll\n" );
		}
	}

	if ( com_developer->integer )
	{
		REFERENCE_TIME streamLatency;
		if ( iAudioClient->lpVtbl->GetStreamLatency( iAudioClient, &streamLatency ) != S_OK )
		{
			Com_Printf( S_COLOR_YELLOW "WASAPI: GetStreamLatency() failed\n" );
			goto err_exit;
		}
		Com_Printf( S_COLOR_CYAN "WASAPI stream latency: %ims\n", (int)( streamLatency / 10000 ) );
	}

	inPlay = 1;
	bufferPosition = 0;
	numFramesAvailable = bufferFrameCount;

	if ( iAudioRenderClient->lpVtbl->GetBuffer( iAudioRenderClient, numFramesAvailable, &pData ) != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI GetBuffer failed\n" );
		goto err_exit;
	}

	if ( iAudioRenderClient->lpVtbl->ReleaseBuffer( iAudioRenderClient, numFramesAvailable, AUDCLNT_BUFFERFLAGS_SILENT ) != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI ReleaseBuffer failed\n" );
		goto err_exit;
	}

	// Start audio playback
	if ( iAudioClient->lpVtbl->Start( iAudioClient ) != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI playback start failed\n" );
		goto err_exit;
	}

	// return control to the main thread
	SetEvent( hInited ); hInited = NULL;

	// execution continues in async mixer thread, we can't use Com_Printf anymore

	for ( ;; )
	{
		dwRes = WaitForSingleObject( hEvent, INFINITE );
		if ( !inPlay || dwRes != WAIT_OBJECT_0 )
			break;

		if ( iAudioClient->lpVtbl->GetCurrentPadding( iAudioClient, &numFramesAvailable ) != S_OK )
			continue;

		numFramesAvailable = bufferFrameCount - numFramesAvailable;
		if ( numFramesAvailable == 0 )
			continue;

		hr = iAudioRenderClient->lpVtbl->GetBuffer( iAudioRenderClient, numFramesAvailable, &pData );
		if ( hr == S_OK )
		{
			dwOffset = 0;
			samples = numFramesAvailable;

			EnterCriticalSection( &cs );

			// fill pData with numFramesAvailable
			do
			{
				if ( bufferPosition + samples > dma.fullsamples )
					n = dma.fullsamples - bufferPosition;
				else
					n = samples;

				Com_Memcpy( pData + dwOffset, dma.buffer + bufferPosition * bufferSampleSize, n * bufferSampleSize );

				dwOffset += n * bufferSampleSize;
				bufferPosition = ( bufferPosition + n ) & ( dma.fullsamples - 1 );
				samples -= n;
			}
			while ( samples );

			LeaveCriticalSection( &cs );

			iAudioRenderClient->lpVtbl->ReleaseBuffer( iAudioRenderClient, numFramesAvailable, 0 );
		}
	}

	iAudioClient->lpVtbl->Stop( iAudioClient );

err_exit:
	if ( hAVRT )
	{
		if ( pAvRevertMmThreadCharacteristics && th != NULL )
			pAvRevertMmThreadCharacteristics( th );

		FreeLibrary( hAVRT );
	}

	inPlay = 0;
	bufferPosition = 0;

	if ( hInited )
		SetEvent( hInited );

	return 0;
}


static BOOL ValidFormat( const WAVEFORMATEXTENSIBLE *format, const WORD wFormatTag, const GUID *SubFormat ) {
	
	if ( format->Format.wFormatTag == wFormatTag )
	{
		return TRUE;
	}

	if ( format->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE )
	{
		if ( memcmp( &format->SubFormat, SubFormat, sizeof( GUID ) ) == 0 )
		{
			return TRUE;
		}
	}

	return FALSE;
}


typedef struct NotificationClient_s
{
	const IMMNotificationClientVtbl *lpVtbl;
	LONG refcount;
}
NotificationClient_t;

static HRESULT STDMETHODCALLTYPE QueryInterface( IMMNotificationClient *this, REFIID riid, VOID **ppvInterface )
{
	if ( !memcmp( riid, &IID_IUnknown, sizeof( GUID ) ) || !memcmp( riid, &IID_IMMNotificationClient, sizeof( GUID ) ) ) 
	{
		*ppvInterface = (void**)this;
		this->lpVtbl->AddRef( this );
		return S_OK;
	}
	else
	{
		*ppvInterface = NULL;
		return E_NOINTERFACE;
	}
}

static ULONG STDMETHODCALLTYPE AddRef( IMMNotificationClient *this )
{
	NotificationClient_t *cl = (NotificationClient_t *) this;
	return InterlockedIncrement( &cl->refcount );
}

static ULONG STDMETHODCALLTYPE Release( IMMNotificationClient *this )
{
	NotificationClient_t *cl = (NotificationClient_t *) this;
	return InterlockedDecrement( &cl->refcount );
}

static HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged( IMMNotificationClient *this, EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId )
{
	if ( flow == eRender && role == eMultimedia )
	{
		doSndRestart = qtrue;
	}
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE OnDeviceAdded( IMMNotificationClient *this, LPCWSTR pwstrDeviceId )
{
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE OnDeviceRemoved( IMMNotificationClient *this, LPCWSTR pwstrDeviceId )
{
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE OnDeviceStateChanged( IMMNotificationClient *this, LPCWSTR pwstrDeviceId, DWORD dwNewState )
{
	if ( DeviceID && wcscmp( DeviceID, pwstrDeviceId ) == 0 )
	{
		if ( dwNewState == DEVICE_STATE_ACTIVE )
		{
			doSndRestart = qtrue;
		}
		else // DEVICE_STATE_DISABLED, DEVICE_STATE_NOTPRESENT, DEVICE_STATE_UNPLUGGED
		{
			inPlay = 0; // do not waste CPU cycles, terminate mixer thread
		}
	}
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE OnPropertyValueChanged( IMMNotificationClient *this, LPCWSTR pwstrDeviceId, const PROPERTYKEY key )
{
	//MessageBox( 0, "PropertyValueChanged", "", MB_ICONWARNING );
	return S_OK;
}

static const IMMNotificationClientVtbl notification_client_vtbl = {
	QueryInterface,
	AddRef,
	Release,
	OnDeviceStateChanged,
	OnDeviceAdded,
	OnDeviceRemoved,
	OnDefaultDeviceChanged,
	OnPropertyValueChanged
};

static NotificationClient_t notification_client = { &notification_client_vtbl, 1 };


static qboolean SNDDMA_InitWASAPI( void )
{
	static byte				buffer[ 64 * 1024 ];
	DWORD					dwStreamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
	WAVEFORMATEXTENSIBLE	desiredFormat;
	WAVEFORMATEXTENSIBLE	*closest = NULL;
	DWORD					dwThreadID;
	HANDLE					hInited;
	qboolean				isfloat;
	HRESULT					hr;

	InitializeCriticalSection( &cs );

	hr = CoCreateInstance( &CLSID_MMDeviceEnumerator, 0, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **) &pEnumerator );
	if ( hr != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: CoCreateInstance() failed\n" );
		goto error1;
	}

	hr = pEnumerator->lpVtbl->RegisterEndpointNotificationCallback( pEnumerator, (IMMNotificationClient*) &notification_client );
	if ( hr != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: RegisterEndpointNotificationCallback() failed\n" );
		goto error2;
	}

	hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint( pEnumerator, eRender, eMultimedia, &iMMDevice );
	if ( hr != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: GetDefaultAudioEndpoint() failed\n" );
		goto error2;
	}

	if ( DeviceID ) // release old device id if exists
	{
		CoTaskMemFree( DeviceID );
		DeviceID = NULL;
	}

	iMMDevice->lpVtbl->GetId( iMMDevice, &DeviceID );

	hr = iMMDevice->lpVtbl->Activate( iMMDevice, &IID_IAudioClient, CLSCTX_ALL, 0, (void **)&iAudioClient );
	if ( hr != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: audio client activation failed\n" );
		goto error3;
	}

	dma.channels = 2;
	dma.samplebits = 16;

	switch ( s_khz->integer ) {
		case 48: dma.speed = 48000; break;
		case 44: dma.speed = 44100; break;
		case 11: dma.speed = 11025; break;
		case 22:
		default: dma.speed = 22050; break;
	};

	initFormat( &desiredFormat, dma.channels, dma.speed, dma.samplebits );

#if 0
	iAudioClient->lpVtbl->GetMixFormat( iAudioClient, (WAVEFORMATEX**) &mixFormat );
	if ( mixFormat )
	{
		Com_Printf( "MIX FORMAT\n" );
		Com_Printf( "subformat: %x-%x-%x-%x\n", mixFormat->SubFormat.Data1, mixFormat->SubFormat.Data2, mixFormat->SubFormat.Data3, mixFormat->SubFormat.Data4 );
		Com_Printf( "channels: %i\n", mixFormat->Format.nChannels );
		Com_Printf( "samples per sec: %i\n", mixFormat->Format.nSamplesPerSec );
		Com_Printf( "bits per sample: %i\n", mixFormat->Format.wBitsPerSample );
	}
#endif

	hr = iAudioClient->lpVtbl->IsFormatSupported( iAudioClient, AUDCLNT_SHAREMODE_SHARED, (const WAVEFORMATEX *) &desiredFormat, (WAVEFORMATEX **) &closest );
	if ( hr != S_OK )
	{
		if ( closest )
		{
			Com_Memcpy( &desiredFormat, closest,
				closest->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE ? sizeof( WAVEFORMATEXTENSIBLE ) : sizeof( WAVEFORMATEX ) );
			CoTaskMemFree( closest );
		}
		else
		{
			Com_Printf( S_COLOR_YELLOW "WASAPI: desired format is not supported\n" );
			goto error3;
		}
	}

	// check if format is supported
	if ( desiredFormat.Format.nChannels != 1 && desiredFormat.Format.nChannels != 2 )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: unsupported channel count %i\n", desiredFormat.Format.nChannels );
		goto error3;
	}

	switch ( desiredFormat.Format.wBitsPerSample )
	{
		case 8:
		case 16:
			if ( !ValidFormat( &desiredFormat, WAVE_FORMAT_PCM, &PcmSubformatGuid ) )
			{
				Com_Printf( S_COLOR_YELLOW "WASAPI: unsupported format for %i-bit samples\n", desiredFormat.Format.wBitsPerSample );
				goto error3;
			}
			isfloat = qfalse;
			break;
		case 32:
			if ( !ValidFormat( &desiredFormat, WAVE_FORMAT_IEEE_FLOAT, &FloatSubformatGuid ) )
			{
				Com_Printf( S_COLOR_YELLOW "WASAPI: unsupported format for %i-bit samples\n", desiredFormat.Format.wBitsPerSample );
				goto error3;
			}
			isfloat = qtrue;
			break;
		default:
			Com_Printf( S_COLOR_YELLOW "WASAPI: unsupported sample count %i\n", desiredFormat.Format.wBitsPerSample );
			goto error3;
	}

	if ( desiredFormat.Format.nSamplesPerSec != (DWORD) dma.speed )
	{
		if ( !IsWindows7OrGreater() )
		{
			// Windows7+ is required for AUDCLNT_STREAMFLAGS_RATEADJUST
			// we don't bother about Vista support and fall back to DirectSound
			goto error3;
		}

		// use wasapi resampler
		Com_DPrintf( "WASAPI resample from %iHz to %iHz\n", dma.speed, (int)desiredFormat.Format.nSamplesPerSec );
		desiredFormat.Format.nSamplesPerSec = dma.speed;
		desiredFormat.Format.nAvgBytesPerSec = dma.speed * desiredFormat.Format.nBlockAlign;
		dwStreamFlags |= AUDCLNT_STREAMFLAGS_RATEADJUST;
	}

	if ( com_developer->integer )
	{
		// this is only for information, we will not use returned value in any way
		// because we will call Initialize() with hnsBufferDuration=0 to select minimal buffer size
		REFERENCE_TIME defDuration;
		iAudioClient->lpVtbl->GetDevicePeriod( iAudioClient, &defDuration, NULL );
		Com_Printf( S_COLOR_CYAN "WASAPI buffer duration: %i.%i millisecons\n", 
			(int)(defDuration / 10000), (int)(( ( defDuration + 500 ) / 1000 ) % 10) );
	}

	// initialize sound device with desired format in shared mode
	hr = iAudioClient->lpVtbl->Initialize( iAudioClient, AUDCLNT_SHAREMODE_SHARED, dwStreamFlags, 0, 0, (WAVEFORMATEX *) &desiredFormat, 0 );
	if ( hr != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: Initialize() failed\n" );
		goto error4;
	}

	hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
	if ( hEvent == NULL )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: CreateEvent( hEvent ) failed\n" );
		goto error4;
	}

	// get the actual size of the audio buffer
	if ( iAudioClient->lpVtbl->GetBufferSize( iAudioClient, &bufferFrameCount ) != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: GetBufferSize() failed\n" );
		goto error5;
	}

	Com_DPrintf( "WASAPI buffer frame count: %i\n", bufferFrameCount );
	
	dma.submission_chunk = 1;
	dma.buffer = buffer;
	dma.isfloat = isfloat;
	dma.channels = desiredFormat.Format.nChannels;
	dma.speed = desiredFormat.Format.nSamplesPerSec;
	dma.samplebits = desiredFormat.Format.wBitsPerSample;

	dma.fullsamples = log2pad( bufferFrameCount * 8, 1 );
	while ( dma.fullsamples * desiredFormat.Format.nBlockAlign > sizeof( buffer ) )
		dma.fullsamples >>= 1;
	if ( dma.fullsamples < bufferFrameCount )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: static sound buffer is too small\n" );
		goto error5;
	}
	dma.samples = dma.fullsamples * dma.channels;

	bufferPosition = 0; // in fullsamples
	bufferSampleSize = desiredFormat.Format.nBlockAlign;

	if ( iAudioClient->lpVtbl->SetEventHandle( iAudioClient, hEvent ) != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: SetEventHandle() failed\n" );
		goto error5;
	}

	if ( iAudioClient->lpVtbl->GetService( iAudioClient, &IID_IAudioRenderClient, (void**)&iAudioRenderClient ) != S_OK )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: GetService() failed\n" );
		iAudioRenderClient = NULL;
		goto error5;
	}

	// additional event to synchronize thread creation
	hInited = CreateEvent( NULL, FALSE, FALSE, NULL );
	if ( hInited == NULL )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: CreateEvent( hInited ) failed\n" );
		goto error6;
	}

	hThread = CreateThread( NULL, 4096, (LPTHREAD_START_ROUTINE)ThreadProc, hInited, 0, &dwThreadID );
	if ( hThread == NULL )
	{
		Com_Printf( S_COLOR_YELLOW "WASAPI: CreateThread( hThread ) failed\n" );
		goto error7;
	}

	WaitForSingleObject( hInited, INFINITE );
	CloseHandle( hInited ); hInited = NULL;

	if ( inPlay )
		return qtrue;

	Com_Printf( S_COLOR_YELLOW "WASAPI: mixer thread startup failed\n" );

error7:
	if ( hInited )
		CloseHandle( hInited );
	hInited = NULL;

error6:
	iAudioRenderClient->lpVtbl->Release( iAudioRenderClient ); iAudioRenderClient = NULL;

error5:
	CloseHandle( hEvent ); hEvent = NULL;

error4:
	iAudioClient->lpVtbl->Release( iAudioClient ); iAudioClient = NULL;

error3:
	iMMDevice->lpVtbl->Release( iMMDevice ); iMMDevice = NULL;

error2:
	if ( DeviceID )
		CoTaskMemFree( DeviceID );
	DeviceID = NULL;

	if ( notification_client.lpVtbl->QueryInterface ) {
		pEnumerator->lpVtbl->UnregisterEndpointNotificationCallback( pEnumerator, (IMMNotificationClient *)&notification_client );
	}

	pEnumerator->lpVtbl->Release( pEnumerator ); pEnumerator = NULL;

error1:
	DeleteCriticalSection( &cs );

	Com_Memset( &dma, 0, sizeof( dma ) );

	dma.channels = 1; // to avoid division-by-zero in S_GetSoundtime()

	return qfalse;
}


static void Done_WASAPI( void )
{
	inPlay = 0; // break mixer loop

	if ( hEvent )
		SetEvent( hEvent );

	if ( hThread )
	{
		WaitForSingleObject( hThread, INFINITE ); CloseHandle( hThread ); hThread = NULL;
	}

//error6:
	iAudioRenderClient->lpVtbl->Release( iAudioRenderClient ); iAudioRenderClient = NULL;
//error5:
	if ( hEvent )
		CloseHandle( hEvent );
	hEvent = NULL;
//error4:
	iAudioClient->lpVtbl->Release( iAudioClient ); iAudioClient = NULL;
//error3:
	iMMDevice->lpVtbl->Release( iMMDevice ); iMMDevice = NULL;
//error2:
	if ( DeviceID )
		CoTaskMemFree( DeviceID );
	DeviceID = NULL;

	pEnumerator->lpVtbl->UnregisterEndpointNotificationCallback( pEnumerator, (IMMNotificationClient *) &notification_client );
	pEnumerator->lpVtbl->Release( pEnumerator ); pEnumerator = NULL;

// error1:
	DeleteCriticalSection( &cs );
}
#endif // USE_WASAPI


HRESULT (WINAPI *pDirectSoundCreate)(GUID FAR *lpGUID, LPDIRECTSOUND FAR *lplpDS, IUnknown FAR *pUnkOuter);
#define iDirectSoundCreate(a,b,c)	pDirectSoundCreate(a,b,c)

#define SECONDARY_BUFFER_SIZE	0x10000

static int		sample16;
static DWORD	gSndBufSize;
static DWORD	locksize;
static LPDIRECTSOUND pDS;
static LPDIRECTSOUNDBUFFER pDSBuf, pDSPBuf;
static HINSTANCE hInstDS;

static const char *DSoundError( int error ) {
	switch ( error ) {
	case DSERR_BUFFERLOST:
		return "DSERR_BUFFERLOST";
	case DSERR_INVALIDCALL:
		return "DSERR_INVALIDCALLS";
	case DSERR_INVALIDPARAM:
		return "DSERR_INVALIDPARAM";
	case DSERR_PRIOLEVELNEEDED:
		return "DSERR_PRIOLEVELNEEDED";
	}

	return "unknown";
}


/*
==================
SNDDMA_Shutdown
==================
*/
void SNDDMA_Shutdown( void ) {
	Com_DPrintf( "Shutting down sound system\n" );
#if USE_WASAPI
	if ( wasapi_init ) {
		Done_WASAPI();
	}
#endif
	if ( pDS ) {
		Com_DPrintf( "Destroying DS buffers\n" );
		if ( pDS ) {
			Com_DPrintf( "...setting NORMAL coop level\n" );
			pDS->lpVtbl->SetCooperativeLevel( pDS, g_wv.hWnd, DSSCL_PRIORITY );
		}

		if ( pDSBuf ) {
			Com_DPrintf( "...stopping and releasing sound buffer\n" );
			pDSBuf->lpVtbl->Stop( pDSBuf );
			pDSBuf->lpVtbl->Release( pDSBuf );
		}

		// only release primary buffer if it's not also the mixing buffer we just released
		if ( pDSPBuf && ( pDSBuf != pDSPBuf ) ) {
			Com_DPrintf( "...releasing primary buffer\n" );
			pDSPBuf->lpVtbl->Release( pDSPBuf );
		}
		pDSBuf = NULL;
		pDSPBuf = NULL;

		Com_DPrintf( "...releasing DS object\n" );
		pDS->lpVtbl->Release( pDS );
	}

	if ( hInstDS ) {
		Com_DPrintf( "...freeing DSOUND.DLL\n" );
		FreeLibrary( hInstDS );
		hInstDS = NULL;
	}

	pDS = NULL;
	pDSBuf = NULL;
	pDSPBuf = NULL;

	dsound_init = qfalse;
#if USE_WASAPI
	wasapi_init = qfalse;
#endif
	memset( &dma, 0, sizeof( dma ) );

	CoUninitialize();
}


/*
==================
SNDDMA_Init

Initialize direct sound
Returns false if failed
==================
*/
qboolean SNDDMA_Init( void ) {

#if USE_WASAPI
	const char *defdrv;
	cvar_t *s_driver;

	if ( IsWindows7OrGreater() )
		defdrv = "wasapi";
	else
		defdrv = "dsound";

	s_driver = Cvar_Get( "s_driver", defdrv, CVAR_LATCH | CVAR_ARCHIVE_ND );

	Cvar_SetDescription( s_driver, "Specify sound subsystem in win32 environment:\n"
		" dsound - DirectSound\n"
		" wasapi - WASAPI\n" );
#endif

	memset( &dma, 0, sizeof( dma ) );

	dsound_init = qfalse;
#if USE_WASAPI
	wasapi_init = qfalse;
#endif
	if ( CoInitialize( NULL ) != S_OK ) {
		return qfalse;
	}
#if USE_WASAPI
	if ( Q_stricmp( s_driver->string, "wasapi" ) == 0 && SNDDMA_InitWASAPI() ) {
		dma.driver = "WASAPI";
		wasapi_init = qtrue;
		return qtrue;
	}
#endif
	if ( SNDDMA_InitDS() ) {
		dma.driver = "DirectSound";
		dsound_init = qtrue;
		return qtrue;
	} else {
		dma.channels = 1; // to avoid division-by-zero in S_GetSoundTime()
	}

	Com_DPrintf( "Failed\n" );

	return qfalse;
}


#undef DEFINE_GUID

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        const GUID name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

// DirectSound Component GUID {47D4D946-62E8-11CF-93BC-444553540000}
DEFINE_GUID(CLSID_DirectSound, 0x47d4d946, 0x62e8, 0x11cf, 0x93, 0xbc, 0x44, 0x45, 0x53, 0x54, 0x0, 0x0);

// DirectSound 8.0 Component GUID {3901CC3F-84B5-4FA4-BA35-AA8172B8A09B}
DEFINE_GUID(CLSID_DirectSound8, 0x3901cc3f, 0x84b5, 0x4fa4, 0xba, 0x35, 0xaa, 0x81, 0x72, 0xb8, 0xa0, 0x9b);

DEFINE_GUID(IID_IDirectSound8, 0xC50A7E93, 0xF395, 0x4834, 0x9E, 0xF6, 0x7F, 0xA9, 0x9D, 0xE5, 0x09, 0x66);
DEFINE_GUID(IID_IDirectSound, 0x279AFA83, 0x4981, 0x11CE, 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60);


static qboolean SNDDMA_InitDS( void )
{
	HRESULT			hresult;
	DSBUFFERDESC	dsbuf;
	DSBCAPS			dsbcaps;
	WAVEFORMATEX	format;
	int				use8;

	Com_Printf( "Initializing DirectSound\n" );

	use8 = 1;
	// Create IDirectSound using the primary sound device
	if( FAILED( hresult = CoCreateInstance(&CLSID_DirectSound8, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectSound8, (void **)&pDS))) {
		use8 = 0;
		if( FAILED( hresult = CoCreateInstance(&CLSID_DirectSound, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectSound, (void **)&pDS))) {
			Com_Printf ("failed\n");
			SNDDMA_Shutdown();
			return qfalse;
		}
	}

	hresult = pDS->lpVtbl->Initialize( pDS, NULL);

	Com_DPrintf( "ok\n" );

	Com_DPrintf("...setting DSSCL_PRIORITY coop level: " );

	if ( DS_OK != pDS->lpVtbl->SetCooperativeLevel( pDS, g_wv.hWnd, DSSCL_PRIORITY ) )	{
		Com_Printf ("failed\n");
		SNDDMA_Shutdown();
		return qfalse;
	}
	Com_DPrintf("ok\n" );

	// create the secondary buffer we'll actually work with
	dma.channels = 2;
	dma.samplebits = 16;

	switch ( s_khz->integer ) {
		case 48: dma.speed = 48000; break;
		case 44: dma.speed = 44100; break;
		case 11: dma.speed = 11025; break;
		case 22:
		default: dma.speed = 22050; break;
	};

	memset( &format, 0, sizeof( format ) );
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = dma.channels;
	format.wBitsPerSample = dma.samplebits;
	format.nSamplesPerSec = dma.speed;
	format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
	format.cbSize = 0;
	format.nAvgBytesPerSec = format.nSamplesPerSec*format.nBlockAlign; 

	memset( &dsbuf, 0, sizeof( dsbuf ) );
	dsbuf.dwSize = sizeof(DSBUFFERDESC);

	// Micah: take advantage of 2D hardware.if available.
	dsbuf.dwFlags = DSBCAPS_LOCHARDWARE | DSBCAPS_GLOBALFOCUS;
	if (use8) {
		dsbuf.dwFlags |= DSBCAPS_GETCURRENTPOSITION2;
	}
	dsbuf.dwBufferBytes = SECONDARY_BUFFER_SIZE;
	dsbuf.lpwfxFormat = &format;
	
	memset(&dsbcaps, 0, sizeof(dsbcaps));
	dsbcaps.dwSize = sizeof(dsbcaps);
	
	Com_DPrintf( "...creating secondary buffer: " );
	if (DS_OK == pDS->lpVtbl->CreateSoundBuffer(pDS, &dsbuf, &pDSBuf, NULL)) {
		Com_Printf( "locked hardware.  ok\n" );
	}
	else {
		// Couldn't get hardware, fallback to software.
		dsbuf.dwFlags = DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS;
		if (use8) {
			dsbuf.dwFlags |= DSBCAPS_GETCURRENTPOSITION2;
		}
		if (DS_OK != pDS->lpVtbl->CreateSoundBuffer(pDS, &dsbuf, &pDSBuf, NULL)) {
			Com_Printf( "failed\n" );
			SNDDMA_Shutdown();
			return qfalse;
		}
		Com_DPrintf( "forced to software.  ok\n" );
	}
		
	// Make sure mixer is active
	if ( DS_OK != pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING) ) {
		Com_Printf ("*** Looped sound play failed ***\n");
		SNDDMA_Shutdown();
		return qfalse;
	}

	// get the returned buffer size
	if ( DS_OK != pDSBuf->lpVtbl->GetCaps (pDSBuf, &dsbcaps) ) {
		Com_Printf ("*** GetCaps failed ***\n");
		SNDDMA_Shutdown();
		return qfalse;
	}
	
	gSndBufSize = dsbcaps.dwBufferBytes;

	dma.isfloat = qfalse;
	dma.channels = format.nChannels;
	dma.samplebits = format.wBitsPerSample;
	dma.speed = format.nSamplesPerSec;
	dma.samples = gSndBufSize/(dma.samplebits/8);
	dma.fullsamples = dma.samples / dma.channels;
	dma.submission_chunk = 1;
	dma.buffer = NULL;			// must be locked first

	sample16 = (dma.samplebits/8) - 1;

	SNDDMA_BeginPainting();

	if ( dma.buffer )
		memset( dma.buffer, 0, dma.samples * dma.samplebits/8 );
	
	SNDDMA_Submit();

	return qtrue;
}


/*
==============
SNDDMA_GetDMAPos

return the current sample WRITE position (in mono samples)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
int SNDDMA_GetDMAPos( void ) {
#if USE_WASAPI
	if ( wasapi_init ) {
		// restart sound system if needed
		if ( doSndRestart ) {
			Done_WASAPI();
			Com_DPrintf( "WASAPI: restart due to device configuration changes\n" );
			wasapi_init = SNDDMA_InitWASAPI();
			doSndRestart = qfalse;
		}
		return ( bufferPosition * dma.channels ) & ( dma.samples - 1 );
	}
#endif
	if ( dsound_init ) {
		DWORD	dwWriteCursor;

		// write position is the only safe position to start update
		pDSBuf->lpVtbl->GetCurrentPosition( pDSBuf, NULL, &dwWriteCursor );

		return ( dwWriteCursor >> sample16 ) & ( dma.samples - 1 );
	}

	return 0;
}


/*
==============
SNDDMA_BeginPainting

Makes sure dma.buffer is valid
===============
*/
void SNDDMA_BeginPainting( void ) {
	int		reps;
	DWORD	dwSize2;
	DWORD	*pbuf, *pbuf2;
	HRESULT	hresult;
	DWORD	dwStatus;
#if USE_WASAPI
	if ( wasapi_init ) {
		EnterCriticalSection( &cs );
		return;
	}
#endif
	if ( !pDSBuf ) {
		return;
	}

	// if the buffer was lost or stopped, restore it and/or restart it
	if ( pDSBuf->lpVtbl->GetStatus (pDSBuf, &dwStatus) != DS_OK ) {
		Com_Printf ("Couldn't get sound buffer status\n");
	}
	
	if (dwStatus & DSBSTATUS_BUFFERLOST)
		pDSBuf->lpVtbl->Restore (pDSBuf);
	
	if (!(dwStatus & DSBSTATUS_PLAYING))
		pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);

	// lock the dsound buffer
	reps = 0;
	dma.buffer = NULL;

	while ((hresult = pDSBuf->lpVtbl->Lock(pDSBuf, 0, gSndBufSize, (LPVOID)&pbuf, &locksize, 
								   (LPVOID)&pbuf2, &dwSize2, 0)) != DS_OK)
	{
		if (hresult != DSERR_BUFFERLOST)
		{
			Com_Printf( "SNDDMA_BeginPainting: Lock failed with error '%s'\n", DSoundError( hresult ) );
			S_Shutdown();
			return;
		}
		else
		{
			pDSBuf->lpVtbl->Restore( pDSBuf );
		}

		if (++reps > 2)
			return;
	}
	dma.buffer = (byte *)pbuf;
}


/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
Also unlocks the dsound buffer
===============
*/
void SNDDMA_Submit( void ) {
#if USE_WASAPI
	if ( wasapi_init ) {
		LeaveCriticalSection( &cs );
		return;
	}
#endif
	// unlock the dsound buffer
	if ( pDSBuf ) {
		pDSBuf->lpVtbl->Unlock(pDSBuf, dma.buffer, locksize, NULL, 0);
	}
}


/*
=================
SNDDMA_Activate

When we change windows we need to do this
=================
*/
void SNDDMA_Activate( void ) {
#if USE_WASAPI
	if ( wasapi_init ) {
		if ( inPlay == 0 ) {
			doSndRestart = qtrue;
		}
		return;
	}
#endif
	if ( !pDS ) {
		return;
	}

	if ( DS_OK != pDS->lpVtbl->SetCooperativeLevel( pDS, g_wv.hWnd, DSSCL_PRIORITY ) )	{
		Com_Printf( "sound SetCooperativeLevel failed\n" );
		SNDDMA_Shutdown();
	}
}
