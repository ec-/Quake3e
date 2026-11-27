/*
===========================================================================
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
/*
 * snd_steamaudio.c -- HRTF through Steam Audio, for the spatial backend
 *
 * The only file that knows about Steam Audio. The library is loaded at
 * runtime (s_steamAudioLib), so the engine builds and runs without it.
 * The mixer sees a small voice API (declared in snd_local.h): one binaural
 * filter state per playing sound, processed in fixed blocks of mono audio.
 *
 * Reflections: the level's triangles go into a Steam Audio scene, and a
 * worker thread keeps re-simulating how sound from each slot (a sound
 * position) bounces around it to the listener. Each slot's audio goes
 * through a hybrid reverb: convolution with the simulated impulse response
 * for the early part (first-order Ambisonics, so reflections keep their
 * direction), then a parametric tail matched to it. All slots are summed,
 * spread into a diffuse field and decoded once per block to headphones
 * with the same HRTF.
 */

#include "snd_local.h"
#include "../steamaudio/include/phonon.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

typedef IPLerror				(IPLCALL *pContextCreate_t)( IPLContextSettings*, IPLContext* );
typedef void					(IPLCALL *pContextRelease_t)( IPLContext* );
typedef IPLerror				(IPLCALL *pHRTFCreate_t)( IPLContext, IPLAudioSettings*, IPLHRTFSettings*, IPLHRTF* );
typedef void					(IPLCALL *pHRTFRelease_t)( IPLHRTF* );
typedef IPLerror				(IPLCALL *pBinauralCreate_t)( IPLContext, IPLAudioSettings*, IPLBinauralEffectSettings*, IPLBinauralEffect* );
typedef void					(IPLCALL *pBinauralRelease_t)( IPLBinauralEffect* );
typedef void					(IPLCALL *pBinauralReset_t)( IPLBinauralEffect );
typedef IPLAudioEffectState		(IPLCALL *pBinauralApply_t)( IPLBinauralEffect, IPLBinauralEffectParams*, IPLAudioBuffer*, IPLAudioBuffer* );
typedef IPLAudioEffectState		(IPLCALL *pBinauralGetTail_t)( IPLBinauralEffect, IPLAudioBuffer* );
typedef IPLerror				(IPLCALL *pEmbreeCreate_t)( IPLContext, void*, IPLEmbreeDevice* );
typedef void					(IPLCALL *pEmbreeRelease_t)( IPLEmbreeDevice* );
typedef IPLerror				(IPLCALL *pSceneCreate_t)( IPLContext, IPLSceneSettings*, IPLScene* );
typedef void					(IPLCALL *pSceneRelease_t)( IPLScene* );
typedef void					(IPLCALL *pSceneCommit_t)( IPLScene );
typedef IPLerror				(IPLCALL *pStaticMeshCreate_t)( IPLScene, IPLStaticMeshSettings*, IPLStaticMesh* );
typedef void					(IPLCALL *pStaticMeshRelease_t)( IPLStaticMesh* );
typedef void					(IPLCALL *pStaticMeshAdd_t)( IPLStaticMesh, IPLScene );
typedef IPLerror				(IPLCALL *pSimulatorCreate_t)( IPLContext, IPLSimulationSettings*, IPLSimulator* );
typedef void					(IPLCALL *pSimulatorRelease_t)( IPLSimulator* );
typedef void					(IPLCALL *pSimulatorSetScene_t)( IPLSimulator, IPLScene );
typedef void					(IPLCALL *pSimulatorSetSharedInputs_t)( IPLSimulator, IPLSimulationFlags, IPLSimulationSharedInputs* );
typedef void					(IPLCALL *pSimulatorCommit_t)( IPLSimulator );
typedef void					(IPLCALL *pSimulatorRunReflections_t)( IPLSimulator );
typedef IPLerror				(IPLCALL *pSourceCreate_t)( IPLSimulator, IPLSourceSettings*, IPLSource* );
typedef void					(IPLCALL *pSourceRelease_t)( IPLSource* );
typedef void					(IPLCALL *pSourceAdd_t)( IPLSource, IPLSimulator );
typedef void					(IPLCALL *pSourceSetInputs_t)( IPLSource, IPLSimulationFlags, IPLSimulationInputs* );
typedef void					(IPLCALL *pSourceGetOutputs_t)( IPLSource, IPLSimulationFlags, IPLSimulationOutputs* );
typedef IPLerror				(IPLCALL *pReflectionCreate_t)( IPLContext, IPLAudioSettings*, IPLReflectionEffectSettings*, IPLReflectionEffect* );
typedef void					(IPLCALL *pReflectionRelease_t)( IPLReflectionEffect* );
typedef void					(IPLCALL *pReflectionReset_t)( IPLReflectionEffect );
typedef IPLAudioEffectState		(IPLCALL *pReflectionApply_t)( IPLReflectionEffect, IPLReflectionEffectParams*, IPLAudioBuffer*, IPLAudioBuffer*, IPLReflectionMixer );
typedef IPLerror				(IPLCALL *pDecodeCreate_t)( IPLContext, IPLAudioSettings*, IPLAmbisonicsDecodeEffectSettings*, IPLAmbisonicsDecodeEffect* );
typedef void					(IPLCALL *pDecodeRelease_t)( IPLAmbisonicsDecodeEffect* );
typedef IPLAudioEffectState		(IPLCALL *pDecodeApply_t)( IPLAmbisonicsDecodeEffect, IPLAmbisonicsDecodeEffectParams*, IPLAudioBuffer*, IPLAudioBuffer* );

static struct {
	void				*lib;
	IPLContext			context;
	IPLHRTF				hrtf;
	IPLAudioSettings	audio;
	char				desc[MAX_OSPATH + 64];

	pContextCreate_t	ContextCreate;
	pContextRelease_t	ContextRelease;
	pHRTFCreate_t		HRTFCreate;
	pHRTFRelease_t		HRTFRelease;
	pBinauralCreate_t	BinauralCreate;
	pBinauralRelease_t	BinauralRelease;
	pBinauralReset_t	BinauralReset;
	pBinauralApply_t	BinauralApply;
	pBinauralGetTail_t	BinauralGetTail;

	pEmbreeCreate_t				EmbreeCreate;
	pEmbreeRelease_t			EmbreeRelease;
	pSceneCreate_t				SceneCreate;
	pSceneRelease_t				SceneRelease;
	pSceneCommit_t				SceneCommit;
	pStaticMeshCreate_t			StaticMeshCreate;
	pStaticMeshRelease_t		StaticMeshRelease;
	pStaticMeshAdd_t			StaticMeshAdd;
	pSimulatorCreate_t			SimulatorCreate;
	pSimulatorRelease_t			SimulatorRelease;
	pSimulatorSetScene_t		SimulatorSetScene;
	pSimulatorSetSharedInputs_t	SimulatorSetSharedInputs;
	pSimulatorCommit_t			SimulatorCommit;
	pSimulatorRunReflections_t	SimulatorRunReflections;
	pSourceCreate_t				SourceCreate;
	pSourceRelease_t			SourceRelease;
	pSourceAdd_t				SourceAdd;
	pSourceSetInputs_t			SourceSetInputs;
	pSourceGetOutputs_t			SourceGetOutputs;
	pReflectionCreate_t			ReflectionCreate;
	pReflectionRelease_t		ReflectionRelease;
	pReflectionReset_t			ReflectionReset;
	pReflectionApply_t			ReflectionApply;
	pDecodeCreate_t				DecodeCreate;
	pDecodeRelease_t			DecodeRelease;
	pDecodeApply_t				DecodeApply;
} sa;


static void *SA_Load( const char *name ) {
	void *lib = Sys_LoadLibrary( name );
	if ( !lib && !strchr( name, '/' ) && !strchr( name, '\\' ) ) {
		// also look next to the executable
		lib = Sys_LoadLibrary( va( "%s%c%s", Sys_Pwd(), PATH_SEP, name ) );
	}
	return lib;
}


static qboolean SA_FileExists( const char *path ) {
	FILE *f = Sys_FOpen( path, "rb" );
	if ( !f )
		return qfalse;
	fclose( f );
	return qtrue;
}


/*
================
SPH_Init

Loads Steam Audio and creates an HRTF for the given output rate and block
size: from sofaPath (an OS path to a SimpleFreeFieldHRIR .sofa file) when
given, otherwise Steam Audio's built-in one. Returns qfalse (and leaves
HRTF disabled) on any failure.
================
*/
qboolean SPH_Init( const char *libName, const char *sofaPath, int rate, int frameSize ) {
	IPLContextSettings cs;
	IPLHRTFSettings hs;
	IPLerror err;

	SPH_Shutdown();

	if ( !libName || !libName[0] )
		return qfalse;

	sa.lib = SA_Load( libName );
	if ( !sa.lib ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: couldn't load %s, HRTF disabled (set s_steamAudioLib)\n", libName );
		return qfalse;
	}

	sa.ContextCreate = (pContextCreate_t)Sys_LoadFunction( sa.lib, "iplContextCreate" );
	sa.ContextRelease = (pContextRelease_t)Sys_LoadFunction( sa.lib, "iplContextRelease" );
	sa.HRTFCreate = (pHRTFCreate_t)Sys_LoadFunction( sa.lib, "iplHRTFCreate" );
	sa.HRTFRelease = (pHRTFRelease_t)Sys_LoadFunction( sa.lib, "iplHRTFRelease" );
	sa.BinauralCreate = (pBinauralCreate_t)Sys_LoadFunction( sa.lib, "iplBinauralEffectCreate" );
	sa.BinauralRelease = (pBinauralRelease_t)Sys_LoadFunction( sa.lib, "iplBinauralEffectRelease" );
	sa.BinauralReset = (pBinauralReset_t)Sys_LoadFunction( sa.lib, "iplBinauralEffectReset" );
	sa.BinauralApply = (pBinauralApply_t)Sys_LoadFunction( sa.lib, "iplBinauralEffectApply" );
	sa.BinauralGetTail = (pBinauralGetTail_t)Sys_LoadFunction( sa.lib, "iplBinauralEffectGetTail" );

	// reflections (optional: checked in SPH_ReflectionsInit)
	sa.EmbreeCreate = (pEmbreeCreate_t)Sys_LoadFunction( sa.lib, "iplEmbreeDeviceCreate" );
	sa.EmbreeRelease = (pEmbreeRelease_t)Sys_LoadFunction( sa.lib, "iplEmbreeDeviceRelease" );
	sa.SceneCreate = (pSceneCreate_t)Sys_LoadFunction( sa.lib, "iplSceneCreate" );
	sa.SceneRelease = (pSceneRelease_t)Sys_LoadFunction( sa.lib, "iplSceneRelease" );
	sa.SceneCommit = (pSceneCommit_t)Sys_LoadFunction( sa.lib, "iplSceneCommit" );
	sa.StaticMeshCreate = (pStaticMeshCreate_t)Sys_LoadFunction( sa.lib, "iplStaticMeshCreate" );
	sa.StaticMeshRelease = (pStaticMeshRelease_t)Sys_LoadFunction( sa.lib, "iplStaticMeshRelease" );
	sa.StaticMeshAdd = (pStaticMeshAdd_t)Sys_LoadFunction( sa.lib, "iplStaticMeshAdd" );
	sa.SimulatorCreate = (pSimulatorCreate_t)Sys_LoadFunction( sa.lib, "iplSimulatorCreate" );
	sa.SimulatorRelease = (pSimulatorRelease_t)Sys_LoadFunction( sa.lib, "iplSimulatorRelease" );
	sa.SimulatorSetScene = (pSimulatorSetScene_t)Sys_LoadFunction( sa.lib, "iplSimulatorSetScene" );
	sa.SimulatorSetSharedInputs = (pSimulatorSetSharedInputs_t)Sys_LoadFunction( sa.lib, "iplSimulatorSetSharedInputs" );
	sa.SimulatorCommit = (pSimulatorCommit_t)Sys_LoadFunction( sa.lib, "iplSimulatorCommit" );
	sa.SimulatorRunReflections = (pSimulatorRunReflections_t)Sys_LoadFunction( sa.lib, "iplSimulatorRunReflections" );
	sa.SourceCreate = (pSourceCreate_t)Sys_LoadFunction( sa.lib, "iplSourceCreate" );
	sa.SourceRelease = (pSourceRelease_t)Sys_LoadFunction( sa.lib, "iplSourceRelease" );
	sa.SourceAdd = (pSourceAdd_t)Sys_LoadFunction( sa.lib, "iplSourceAdd" );
	sa.SourceSetInputs = (pSourceSetInputs_t)Sys_LoadFunction( sa.lib, "iplSourceSetInputs" );
	sa.SourceGetOutputs = (pSourceGetOutputs_t)Sys_LoadFunction( sa.lib, "iplSourceGetOutputs" );
	sa.ReflectionCreate = (pReflectionCreate_t)Sys_LoadFunction( sa.lib, "iplReflectionEffectCreate" );
	sa.ReflectionRelease = (pReflectionRelease_t)Sys_LoadFunction( sa.lib, "iplReflectionEffectRelease" );
	sa.ReflectionReset = (pReflectionReset_t)Sys_LoadFunction( sa.lib, "iplReflectionEffectReset" );
	sa.ReflectionApply = (pReflectionApply_t)Sys_LoadFunction( sa.lib, "iplReflectionEffectApply" );
	sa.DecodeCreate = (pDecodeCreate_t)Sys_LoadFunction( sa.lib, "iplAmbisonicsDecodeEffectCreate" );
	sa.DecodeRelease = (pDecodeRelease_t)Sys_LoadFunction( sa.lib, "iplAmbisonicsDecodeEffectRelease" );
	sa.DecodeApply = (pDecodeApply_t)Sys_LoadFunction( sa.lib, "iplAmbisonicsDecodeEffectApply" );

	if ( !sa.ContextCreate || !sa.ContextRelease || !sa.HRTFCreate || !sa.HRTFRelease || !sa.BinauralCreate
		|| !sa.BinauralRelease || !sa.BinauralReset || !sa.BinauralApply || !sa.BinauralGetTail ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: %s is missing functions, HRTF disabled\n", libName );
		SPH_Shutdown();
		return qfalse;
	}

	Com_Memset( &cs, 0, sizeof( cs ) );
	cs.version = STEAMAUDIO_VERSION;
	cs.simdLevel = IPL_SIMDLEVEL_AVX2;
	err = sa.ContextCreate( &cs, &sa.context );
	if ( err != IPL_STATUS_SUCCESS ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: context creation failed (%i); library version may not match headers %i.%i.%i\n",
			err, STEAMAUDIO_VERSION_MAJOR, STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH );
		SPH_Shutdown();
		return qfalse;
	}

	sa.audio.samplingRate = rate;
	sa.audio.frameSize = frameSize;

	Com_Memset( &hs, 0, sizeof( hs ) );
	hs.volume = 1.0f;
	hs.normType = IPL_HRTFNORMTYPE_RMS;		// similar loudness from every direction

	err = IPL_STATUS_FAILURE;
	if ( sofaPath && sofaPath[0] ) {
		if ( !SA_FileExists( sofaPath ) ) {
			Com_Printf( S_COLOR_YELLOW "Steam Audio: HRTF file %s not found, using the built-in HRTF\n", sofaPath );
		} else {
			hs.type = IPL_HRTFTYPE_SOFA;
			hs.sofaFileName = sofaPath;
			err = sa.HRTFCreate( sa.context, &sa.audio, &hs, &sa.hrtf );
			if ( err != IPL_STATUS_SUCCESS ) {
				Com_Printf( S_COLOR_YELLOW "Steam Audio: couldn't load HRTF file %s (%i), using the built-in HRTF\n", sofaPath, err );
				sa.hrtf = NULL;
			}
		}
	}
	if ( err != IPL_STATUS_SUCCESS ) {
		hs.type = IPL_HRTFTYPE_DEFAULT;
		hs.sofaFileName = NULL;
		sofaPath = NULL;
		err = sa.HRTFCreate( sa.context, &sa.audio, &hs, &sa.hrtf );
	}
	if ( err != IPL_STATUS_SUCCESS ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: HRTF creation failed (%i)\n", err );
		SPH_Shutdown();
		return qfalse;
	}

	Com_sprintf( sa.desc, sizeof( sa.desc ), "Steam Audio %i.%i.%i, %s HRTF, %i Hz, %i-frame blocks",
		STEAMAUDIO_VERSION_MAJOR, STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH,
		sofaPath ? COM_SkipPath( (char *)sofaPath ) : "built-in", rate, frameSize );
	Com_Printf( "%s\n", sa.desc );
	return qtrue;
}


void SPH_Shutdown( void ) {
	SPH_ReflectionsShutdown();
	if ( sa.hrtf && sa.HRTFRelease )
		sa.HRTFRelease( &sa.hrtf );
	if ( sa.context && sa.ContextRelease )
		sa.ContextRelease( &sa.context );
	if ( sa.lib )
		Sys_UnloadLibrary( sa.lib );
	Com_Memset( &sa, 0, sizeof( sa ) );
}


qboolean SPH_Active( void ) {
	return sa.hrtf != NULL;
}


const char *SPH_Describe( void ) {
	return sa.hrtf ? sa.desc : "off";
}


void *SPH_CreateVoice( void ) {
	IPLBinauralEffectSettings es;
	IPLBinauralEffect effect = NULL;
	if ( !sa.hrtf )
		return NULL;
	es.hrtf = sa.hrtf;
	if ( sa.BinauralCreate( sa.context, &sa.audio, &es, &effect ) != IPL_STATUS_SUCCESS )
		return NULL;
	return effect;
}


void SPH_ReleaseVoice( void **voice ) {
	if ( *voice && sa.BinauralRelease ) {
		IPLBinauralEffect effect = (IPLBinauralEffect)*voice;
		sa.BinauralRelease( &effect );
	}
	*voice = NULL;
}


void SPH_ResetVoice( void *voice ) {
	if ( voice )
		sa.BinauralReset( (IPLBinauralEffect)voice );
}


/*
================
SPH_Process

Spatializes one block of mono audio. dir is the direction to the source in
the listener's frame (forward, left, up), unit length. Writes (not adds)
frameSize samples to outL and outR.
================
*/
void SPH_Process( void *voice, float *mono, const vec3_t dir, float *outL, float *outR ) {
	IPLBinauralEffectParams params;
	IPLAudioBuffer in, out;
	float *inData[1], *outData[2];

	inData[0] = mono;
	outData[0] = outL;
	outData[1] = outR;
	in.numChannels = 1;
	in.numSamples = sa.audio.frameSize;
	in.data = inData;
	out.numChannels = 2;
	out.numSamples = sa.audio.frameSize;
	out.data = outData;

	// Steam Audio: right-handed, +x right, +y up, -z ahead
	Com_Memset( &params, 0, sizeof( params ) );
	params.direction.x = -dir[1];
	params.direction.y = dir[2];
	params.direction.z = -dir[0];
	params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
	params.spatialBlend = 1.0f;
	params.hrtf = sa.hrtf;
	params.peakDelays = NULL;

	sa.BinauralApply( (IPLBinauralEffect)voice, &params, &in, &out );
}


/*
================
SPH_Tail

Writes the next block of the effect's remaining output after its input
stopped. Returns qtrue while more tail remains after this block.
================
*/
qboolean SPH_Tail( void *voice, float *outL, float *outR ) {
	IPLAudioBuffer out;
	float *outData[2];
	outData[0] = outL;
	outData[1] = outR;
	out.numChannels = 2;
	out.numSamples = sa.audio.frameSize;
	out.data = outData;
	return sa.BinauralGetTail( (IPLBinauralEffect)voice, &out ) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
}


/*
================
SPH_FrontResponse

Measures how loud the loaded HRTF makes a sound straight ahead: the average
per-ear RMS of the output divided by the RMS of the input, for pink noise
(game audio has most of its energy in the low and mid range). Datasets are
normalised differently, so the mixer uses this to keep loudness consistent.
Deterministic. Returns 0 if HRTF is not active.
================
*/
float SPH_FrontResponse( void ) {
	const int blocks = 64, warmup = 4, n = sa.audio.frameSize;
	const vec3_t front = { 1.0f, 0.0f, 0.0f };
	unsigned int seed = 12345;
	float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
	double inSum = 0.0, outSum = 0.0;
	float *in, *l, *r;
	void *voice;
	int b, i;

	if ( !sa.hrtf || n <= 0 )
		return 0.0f;

	voice = SPH_CreateVoice();
	if ( !voice )
		return 0.0f;

	in = malloc( n * 3 * sizeof( float ) );
	l = in + n;
	r = l + n;

	for ( b = 0; b < blocks; b++ ) {
		for ( i = 0; i < n; i++ ) {
			float white;
			seed = seed * 1664525u + 1013904223u;
			white = ( (int)( seed >> 8 ) - ( 1 << 23 ) ) / (float)( 1 << 23 );
			// Paul Kellet's pink noise filter
			b0 = 0.99886f * b0 + white * 0.0555179f;
			b1 = 0.99332f * b1 + white * 0.0750759f;
			b2 = 0.96900f * b2 + white * 0.1538520f;
			b3 = 0.86650f * b3 + white * 0.3104856f;
			b4 = 0.55000f * b4 + white * 0.5329522f;
			b5 = -0.7616f * b5 - white * 0.0168980f;
			in[i] = ( b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f ) * 0.1f;
			b6 = white * 0.115926f;
		}
		if ( b >= warmup ) {
			for ( i = 0; i < n; i++ )
				inSum += in[i] * in[i];
		}
		SPH_Process( voice, in, front, l, r );
		if ( b >= warmup ) {
			for ( i = 0; i < n; i++ )
				outSum += 0.5 * ( l[i] * l[i] + r[i] * r[i] );
		}
	}

	free( in );
	SPH_ReleaseVoice( &voice );

	return ( inSum > 0.0 ) ? (float)sqrt( outSum / inSum ) : 0.0f;
}


// =======================================================================
// Reflections
// =======================================================================

#define SA_UNITS_PER_METRE	32.0f		// a player (56 units) is about 1.75 m
#define SA_ORDER			1			// Ambisonics order of the reflections
#define SA_CHANNELS			( ( SA_ORDER + 1 ) * ( SA_ORDER + 1 ) )
#define SA_MAX_SLOTS		64
#define SA_DIFFUSE_SAMPLES	32
#define SA_SIM_THREADS		2
#define SA_HYBRID_TRANSITION	0.2f	// seconds of convolution before the parametric tail
#define SA_DIFFUSE_LEN		1024		// decorrelation filter length (about 21 ms at 48 kHz)
#define SA_DIFFUSE_TAPS		40			// velvet noise impulses per filter
#define SA_DIFFUSE_GAIN		1.0f		// how much of W is spread into X, Y, Z

#ifdef _WIN32
typedef HANDLE				saThread_t;
typedef CRITICAL_SECTION	saMutex_t;
static void SA_MutexInit( saMutex_t *m ) { InitializeCriticalSection( m ); }
static void SA_MutexDestroy( saMutex_t *m ) { DeleteCriticalSection( m ); }
static void SA_Lock( saMutex_t *m ) { EnterCriticalSection( m ); }
static void SA_Unlock( saMutex_t *m ) { LeaveCriticalSection( m ); }
static void SA_SleepMs( int ms ) { Sleep( ms ); }
static int SA_TimeMs( void ) { return (int)GetTickCount(); }
static double SA_TimeSec( void ) {
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency( &f );
	QueryPerformanceCounter( &c );
	return (double)c.QuadPart / (double)f.QuadPart;
}
#else
typedef pthread_t			saThread_t;
typedef pthread_mutex_t		saMutex_t;
static void SA_MutexInit( saMutex_t *m ) { pthread_mutex_init( m, NULL ); }
static void SA_MutexDestroy( saMutex_t *m ) { pthread_mutex_destroy( m ); }
static void SA_Lock( saMutex_t *m ) { pthread_mutex_lock( m ); }
static void SA_Unlock( saMutex_t *m ) { pthread_mutex_unlock( m ); }
static void SA_SleepMs( int ms ) {
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = ( ms % 1000 ) * 1000000L;
	nanosleep( &ts, NULL );
}
static int SA_TimeMs( void ) {
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (int)( ts.tv_sec * 1000 + ts.tv_nsec / 1000000 );
}
static double SA_TimeSec( void ) {
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

// absorption (low, mid, high), scattering, transmission (low, mid, high);
// Steam Audio's presets
static const IPLMaterial sa_materials[SP_NUM_MATERIALS] = {
	{ { 0.05f, 0.07f, 0.08f }, 0.05f, { 0.015f, 0.002f, 0.001f } },	// stone (concrete)
	{ { 0.20f, 0.07f, 0.06f }, 0.05f, { 0.200f, 0.025f, 0.010f } },	// metal
	{ { 0.11f, 0.07f, 0.06f }, 0.05f, { 0.070f, 0.014f, 0.005f } },	// wood
	{ { 0.06f, 0.03f, 0.02f }, 0.05f, { 0.060f, 0.044f, 0.011f } },	// glass
	{ { 0.01f, 0.02f, 0.02f }, 0.05f, { 0.060f, 0.044f, 0.011f } },	// tile (ceramic)
	{ { 0.60f, 0.70f, 0.80f }, 0.05f, { 0.031f, 0.012f, 0.008f } },	// soft (gravel)
	{ { 0.24f, 0.69f, 0.73f }, 0.05f, { 0.020f, 0.005f, 0.003f } },	// fabric (carpet)
};

static struct {
	qboolean			active;
	int					numSlots;
	float				duration;
	qboolean			convolution;
	IPLReflectionEffectType type;
	int					rays;
	IPLEmbreeDevice		embree;					// NULL: Steam Audio's own ray tracer
	int					bounces;				// live, under dataLock
	int					intervalMs;				// live, under dataLock
	int					irSize;
	char				desc[MAX_QPATH + 128];

	IPLSimulator		simulator;
	IPLSource			sources[SA_MAX_SLOTS];
	IPLReflectionEffect	effects[SA_MAX_SLOTS];
	IPLAmbisonicsDecodeEffect decode;
	float				*amb[SA_CHANNELS];		// summed Ambisonics for the current block
	float				*ambTmp[SA_CHANNELS];
	IPLCoordinateSpace3	listenerSpace;			// main thread's copy, for decoding

	// diffuse field: decorrelated copies of W for the directional channels
	int					diffusePos[SA_CHANNELS - 1][SA_DIFFUSE_TAPS];
	float				diffuseGain[SA_CHANNELS - 1][SA_DIFFUSE_TAPS];
	float				*wHistory;				// SA_DIFFUSE_LEN + frameSize samples of W

	// worker
	saThread_t			thread;
	qboolean			threadRunning;
	volatile int		quit;
	saMutex_t			simLock;				// held for a whole simulation step
	saMutex_t			dataLock;				// guards the fields below
	qboolean			sync;					// main thread drives simulation (capture, tests)
	spGeometry_t		*pendingGeom;
	qboolean			geomPending;
	IPLCoordinateSpace3	listener;
	IPLVector3			origins[SA_MAX_SLOTS];
	qboolean			slotActive[SA_MAX_SLOTS];
	int					slotGen[SA_MAX_SLOTS];		// bumped when a slot gets a new sound position
	int					slotDoneGen[SA_MAX_SLOTS];	// generation the last finished simulation used
	char				sceneName[MAX_QPATH];
	int					sceneTris;
	int					lastSimMs;					// duration of the last simulation step

	// audio-side cost, for s_info
	double				mixSeconds;					// spent in Process/Output since the last report
	int					mixBlocks;
	int					mixSlots;					// slot blocks processed

	// worker-owned
	IPLScene			scene;
	IPLStaticMesh		mesh;
} sar;


static IPLVector3 SA_Position( const vec3_t v ) {
	// game: +x forward, +y left, +z up (units); Steam Audio: right-handed, +y up (metres)
	IPLVector3 r;
	r.x = v[0] / SA_UNITS_PER_METRE;
	r.y = v[2] / SA_UNITS_PER_METRE;
	r.z = -v[1] / SA_UNITS_PER_METRE;
	return r;
}


static IPLVector3 SA_Direction( const vec3_t v ) {
	IPLVector3 r;
	r.x = v[0];
	r.y = v[2];
	r.z = -v[1];
	return r;
}


/*
Builds a Steam Audio scene from level triangles and makes the simulator use
it. Simulation thread only (or the main thread in sync mode, under simLock).
*/
static void SA_BuildScene( spGeometry_t *geom ) {
	IPLSceneSettings ss;
	IPLStaticMeshSettings ms;
	IPLScene scene = NULL;
	IPLStaticMesh mesh = NULL;
	IPLVector3 *verts;
	IPLTriangle *tris;
	IPLMaterial materials[SP_NUM_MATERIALS];
	int i;

	if ( geom ) {
		Com_Memset( &ss, 0, sizeof( ss ) );
		ss.type = sar.embree ? IPL_SCENETYPE_EMBREE : IPL_SCENETYPE_DEFAULT;
		ss.embreeDevice = sar.embree;
		if ( sa.SceneCreate( sa.context, &ss, &scene ) == IPL_STATUS_SUCCESS ) {
			verts = malloc( geom->numVerts * sizeof( *verts ) );
			tris = malloc( geom->numTris * sizeof( *tris ) );
			for ( i = 0; i < geom->numVerts; i++ )
				verts[i] = SA_Position( geom->verts[i] );
			for ( i = 0; i < geom->numTris; i++ ) {
				tris[i].indices[0] = geom->tris[i][0];
				tris[i].indices[1] = geom->tris[i][1];
				tris[i].indices[2] = geom->tris[i][2];
			}
			Com_Memcpy( materials, sa_materials, sizeof( materials ) );

			Com_Memset( &ms, 0, sizeof( ms ) );
			ms.numVertices = geom->numVerts;
			ms.numTriangles = geom->numTris;
			ms.numMaterials = SP_NUM_MATERIALS;
			ms.vertices = verts;
			ms.triangles = tris;
			ms.materialIndices = geom->materials;
			ms.materials = materials;
			if ( sa.StaticMeshCreate( scene, &ms, &mesh ) == IPL_STATUS_SUCCESS ) {
				sa.StaticMeshAdd( mesh, scene );
				sa.SceneCommit( scene );
			} else {
				sa.SceneRelease( &scene );
				scene = NULL;
			}
			free( verts );
			free( tris );
		}
	}

	sa.SimulatorSetScene( sar.simulator, scene );
	sa.SimulatorCommit( sar.simulator );

	if ( sar.mesh )
		sa.StaticMeshRelease( &sar.mesh );
	if ( sar.scene )
		sa.SceneRelease( &sar.scene );
	sar.scene = scene;
	sar.mesh = mesh;

	SA_Lock( &sar.dataLock );
	if ( scene ) {
		Q_strncpyz( sar.sceneName, geom->name, sizeof( sar.sceneName ) );
		sar.sceneTris = geom->numTris;
	} else {
		sar.sceneName[0] = '\0';
		sar.sceneTris = 0;
	}
	for ( i = 0; i < sar.numSlots; i++ )
		sar.slotDoneGen[i] = -1;	// old results are for another level
	SA_Unlock( &sar.dataLock );

	SP_FreeGeometry( geom );
}


/*
One simulation step: picks up a new level if there is one, then traces
reflections for every active slot. Caller holds simLock.
*/
static void SA_SimulateStep( void ) {
	IPLSimulationSharedInputs shared;
	IPLSimulationInputs inputs;
	IPLCoordinateSpace3 listener;
	IPLVector3 origins[SA_MAX_SLOTS];
	qboolean active[SA_MAX_SLOTS];
	int gen[SA_MAX_SLOTS];
	spGeometry_t *geom = NULL;
	qboolean newGeom;
	int i, start, bounces;

	SA_Lock( &sar.dataLock );
	newGeom = sar.geomPending;
	if ( newGeom ) {
		geom = sar.pendingGeom;
		sar.pendingGeom = NULL;
		sar.geomPending = qfalse;
	}
	SA_Unlock( &sar.dataLock );

	if ( newGeom )
		SA_BuildScene( geom );
	if ( !sar.scene )
		return;

	SA_Lock( &sar.dataLock );
	listener = sar.listener;
	bounces = sar.bounces;
	for ( i = 0; i < sar.numSlots; i++ ) {
		origins[i] = sar.origins[i];
		active[i] = sar.slotActive[i];
		gen[i] = sar.slotGen[i];
	}
	SA_Unlock( &sar.dataLock );

	start = SA_TimeMs();
	for ( i = 0; i < sar.numSlots; i++ ) {
		Com_Memset( &inputs, 0, sizeof( inputs ) );
		inputs.flags = active[i] ? IPL_SIMULATIONFLAGS_REFLECTIONS : 0;
		inputs.source.origin = origins[i];
		inputs.source.right.x = 1.0f;
		inputs.source.up.y = 1.0f;
		inputs.source.ahead.z = -1.0f;
		inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
		inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
		inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
		inputs.hybridReverbTransitionTime = SA_HYBRID_TRANSITION;
		inputs.hybridReverbOverlapPercent = 0.25f;
		sa.SourceSetInputs( sar.sources[i], IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs );
	}

	Com_Memset( &shared, 0, sizeof( shared ) );
	shared.listener = listener;
	shared.numRays = sar.rays;
	shared.numBounces = bounces;
	shared.duration = sar.duration;
	shared.order = SA_ORDER;
	shared.irradianceMinDistance = 1.0f;
	sa.SimulatorSetSharedInputs( sar.simulator, IPL_SIMULATIONFLAGS_REFLECTIONS, &shared );
	sa.SimulatorRunReflections( sar.simulator );

	SA_Lock( &sar.dataLock );
	for ( i = 0; i < sar.numSlots; i++ ) {
		if ( active[i] )
			sar.slotDoneGen[i] = gen[i];
	}
	sar.lastSimMs = SA_TimeMs() - start;
	SA_Unlock( &sar.dataLock );
}


#ifdef _WIN32
static DWORD WINAPI SA_Thread( LPVOID arg ) {
#else
static void *SA_Thread( void *arg ) {
#endif
	(void)arg;
	while ( !sar.quit ) {
		const int start = SA_TimeMs();
		int elapsed, interval;
		SA_Lock( &sar.simLock );
		if ( !sar.sync )
			SA_SimulateStep();
		SA_Unlock( &sar.simLock );
		SA_Lock( &sar.dataLock );
		interval = sar.intervalMs;
		SA_Unlock( &sar.dataLock );
		elapsed = SA_TimeMs() - start;
		SA_SleepMs( elapsed < interval ? interval - elapsed : 1 );
	}
	return 0;
}


/*
Steam Audio builds impulse responses from energy per direction, so a
diffuse late field ends up almost entirely in W and decodes to nearly the
same signal at both ears: a mono tail. Adding mutually decorrelated copies
of W (velvet noise filters) to X, Y and Z restores an isotropic field, and
the HRTF then decorrelates the ears the way a real room does.
*/
static void SA_InitDiffuse( void ) {
	unsigned int seed = 0x5eed1234u;
	int c, t;

	sar.wHistory = calloc( SA_DIFFUSE_LEN + sa.audio.frameSize, sizeof( float ) );
	for ( c = 0; c < SA_CHANNELS - 1; c++ ) {
		double sum = 0.0;
		for ( t = 0; t < SA_DIFFUSE_TAPS; t++ ) {
			// one impulse at a random spot in each of SA_DIFFUSE_TAPS equal segments
			const int seg = SA_DIFFUSE_LEN / SA_DIFFUSE_TAPS;
			float g;
			seed = seed * 1664525u + 1013904223u;
			sar.diffusePos[c][t] = t * seg + (int)( ( seed >> 8 ) % seg );
			seed = seed * 1664525u + 1013904223u;
			g = ( seed & 0x80000000u ) ? -1.0f : 1.0f;
			g *= expf( -2.3f * t / SA_DIFFUSE_TAPS );		// fall by 20 dB
			sar.diffuseGain[c][t] = g;
			sum += g * g;
		}
		for ( t = 0; t < SA_DIFFUSE_TAPS; t++ )
			sar.diffuseGain[c][t] *= SA_DIFFUSE_GAIN / sqrt( sum );
	}
}


static void SA_Diffuse( void ) {
	const int n = sa.audio.frameSize;
	float *h = sar.wHistory;
	int c, t, i;

	memmove( h, h + n, SA_DIFFUSE_LEN * sizeof( float ) );
	Com_Memcpy( h + SA_DIFFUSE_LEN, sar.amb[0], n * sizeof( float ) );
	for ( c = 0; c < SA_CHANNELS - 1; c++ ) {
		float *out = sar.amb[c + 1];
		for ( t = 0; t < SA_DIFFUSE_TAPS; t++ ) {
			const float g = sar.diffuseGain[c][t];
			const float *src = h + SA_DIFFUSE_LEN - sar.diffusePos[c][t];
			for ( i = 0; i < n; i++ )
				out[i] += g * src[i];
		}
	}
}


/*
================
SPH_ReflectionsInit

Needs SPH_Init to have succeeded. Bounces and the simulation interval are
set separately with SPH_ReflectionsSetLive and can change at any time.
================
*/
qboolean SPH_ReflectionsInit( const sphReflSettings_t *settings ) {
	IPLSimulationSettings sim;
	IPLSourceSettings src;
	IPLReflectionEffectSettings res;
	IPLAmbisonicsDecodeEffectSettings ds;
	int i;

	SPH_ReflectionsShutdown();
	if ( !sa.hrtf )
		return qfalse;
	if ( !sa.SceneCreate || !sa.SceneRelease || !sa.SceneCommit || !sa.StaticMeshCreate || !sa.StaticMeshRelease
		|| !sa.StaticMeshAdd || !sa.SimulatorCreate || !sa.SimulatorRelease || !sa.SimulatorSetScene
		|| !sa.SimulatorSetSharedInputs || !sa.SimulatorCommit || !sa.SimulatorRunReflections || !sa.SourceCreate
		|| !sa.SourceRelease || !sa.SourceAdd || !sa.SourceSetInputs || !sa.SourceGetOutputs || !sa.ReflectionCreate
		|| !sa.ReflectionRelease || !sa.ReflectionReset || !sa.ReflectionApply || !sa.DecodeCreate
		|| !sa.DecodeRelease || !sa.DecodeApply ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: library is missing reflection functions, reflections disabled\n" );
		return qfalse;
	}

	sar.numSlots = MIN( settings->numSlots, SA_MAX_SLOTS );
	sar.duration = settings->duration;
	sar.irSize = (int)( sar.duration * sa.audio.samplingRate );
	sar.convolution = settings->convolution;
	sar.type = settings->convolution ? IPL_REFLECTIONEFFECTTYPE_CONVOLUTION : IPL_REFLECTIONEFFECTTYPE_HYBRID;
	sar.rays = settings->rays;
	if ( settings->embree && sa.EmbreeCreate && sa.EmbreeRelease
		&& sa.EmbreeCreate( sa.context, NULL, &sar.embree ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: Embree unavailable, using the built-in ray tracer\n" );
		sar.embree = NULL;
	}
	sar.bounces = 4;
	sar.intervalMs = 100;

	Com_Memset( &sim, 0, sizeof( sim ) );
	sim.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
	sim.sceneType = sar.embree ? IPL_SCENETYPE_EMBREE : IPL_SCENETYPE_DEFAULT;
	sim.reflectionType = sar.type;
	sim.maxNumRays = sar.rays;
	sim.numDiffuseSamples = SA_DIFFUSE_SAMPLES;
	sim.maxDuration = sar.duration;
	sim.maxOrder = SA_ORDER;
	sim.maxNumSources = sar.numSlots;
	sim.numThreads = SA_SIM_THREADS;
	sim.rayBatchSize = 1;
	sim.samplingRate = sa.audio.samplingRate;
	sim.frameSize = sa.audio.frameSize;
	if ( sa.SimulatorCreate( sa.context, &sim, &sar.simulator ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: couldn't create the reflection simulator\n" );
		sar.simulator = NULL;
		SPH_ReflectionsShutdown();
		return qfalse;
	}

	Com_Memset( &res, 0, sizeof( res ) );
	res.type = sar.type;
	res.irSize = sar.irSize;
	res.numChannels = SA_CHANNELS;
	for ( i = 0; i < sar.numSlots; i++ ) {
		src.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
		if ( sa.SourceCreate( sar.simulator, &src, &sar.sources[i] ) != IPL_STATUS_SUCCESS
			|| sa.ReflectionCreate( sa.context, &sa.audio, &res, &sar.effects[i] ) != IPL_STATUS_SUCCESS ) {
			Com_Printf( S_COLOR_YELLOW "Steam Audio: couldn't create reflection sources\n" );
			SPH_ReflectionsShutdown();
			return qfalse;
		}
		sa.SourceAdd( sar.sources[i], sar.simulator );
		sar.slotDoneGen[i] = -1;
	}
	sa.SimulatorCommit( sar.simulator );

	Com_Memset( &ds, 0, sizeof( ds ) );
	ds.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
	ds.hrtf = sa.hrtf;
	ds.maxOrder = SA_ORDER;
	if ( sa.DecodeCreate( sa.context, &sa.audio, &ds, &sar.decode ) != IPL_STATUS_SUCCESS ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: couldn't create the Ambisonics decoder\n" );
		sar.decode = NULL;
		SPH_ReflectionsShutdown();
		return qfalse;
	}

	for ( i = 0; i < SA_CHANNELS; i++ ) {
		sar.amb[i] = calloc( sa.audio.frameSize, sizeof( float ) );
		sar.ambTmp[i] = calloc( sa.audio.frameSize, sizeof( float ) );
	}
	SA_InitDiffuse();
	sar.listener.right.x = sar.listenerSpace.right.x = 1.0f;
	sar.listener.up.y = sar.listenerSpace.up.y = 1.0f;
	sar.listener.ahead.z = sar.listenerSpace.ahead.z = -1.0f;

	SA_MutexInit( &sar.simLock );
	SA_MutexInit( &sar.dataLock );
	sar.quit = 0;
#ifdef _WIN32
	sar.thread = CreateThread( NULL, 0, SA_Thread, NULL, 0, NULL );
	sar.threadRunning = ( sar.thread != NULL );
#else
	sar.threadRunning = ( pthread_create( &sar.thread, NULL, SA_Thread, NULL ) == 0 );
#endif
	if ( !sar.threadRunning ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: couldn't start the reflection thread\n" );
		SA_MutexDestroy( &sar.simLock );
		SA_MutexDestroy( &sar.dataLock );
		SPH_ReflectionsShutdown();
		return qfalse;
	}

	sar.active = qtrue;
	Com_Printf( "Steam Audio reflections: %s, %i sound positions, %.1f s, order %i, %i rays, %s ray tracer\n",
		sar.convolution ? "convolution" : "hybrid", sar.numSlots, sar.duration, SA_ORDER, sar.rays,
		sar.embree ? "Embree" : "built-in" );
	return qtrue;
}


void SPH_ReflectionsShutdown( void ) {
	int i;

	if ( sar.threadRunning ) {
		sar.quit = 1;
#ifdef _WIN32
		WaitForSingleObject( sar.thread, INFINITE );
		CloseHandle( sar.thread );
#else
		pthread_join( sar.thread, NULL );
#endif
		sar.threadRunning = qfalse;
		SA_MutexDestroy( &sar.simLock );
		SA_MutexDestroy( &sar.dataLock );
	}

	if ( sar.pendingGeom )
		SP_FreeGeometry( sar.pendingGeom );
	for ( i = 0; i < SA_MAX_SLOTS; i++ ) {
		if ( sar.effects[i] )
			sa.ReflectionRelease( &sar.effects[i] );
		if ( sar.sources[i] )
			sa.SourceRelease( &sar.sources[i] );
	}
	if ( sar.decode )
		sa.DecodeRelease( &sar.decode );
	if ( sar.simulator )
		sa.SimulatorRelease( &sar.simulator );
	if ( sar.mesh )
		sa.StaticMeshRelease( &sar.mesh );
	if ( sar.scene )
		sa.SceneRelease( &sar.scene );
	if ( sar.embree )
		sa.EmbreeRelease( &sar.embree );
	for ( i = 0; i < SA_CHANNELS; i++ ) {
		free( sar.amb[i] );
		free( sar.ambTmp[i] );
	}
	free( sar.wHistory );
	Com_Memset( &sar, 0, sizeof( sar ) );
}


qboolean SPH_ReflectionsActive( void ) {
	return sar.active;
}


const char *SPH_ReflectionsDescribe( void ) {
	if ( !sar.active )
		return "off";
	SA_Lock( &sar.dataLock );
	if ( sar.sceneName[0] ) {
		const double audio = sar.mixBlocks ? (double)sar.mixBlocks * sa.audio.frameSize / sa.audio.samplingRate : 0.0;
		Com_sprintf( sar.desc, sizeof( sar.desc ), "%s%s, %i rays, %i bounces every %i ms; %s, %i triangles, last simulation %i ms, mixing %.1f%% of a core (%.1f positions)",
			sar.convolution ? "convolution" : "hybrid", sar.embree ? " (Embree)" : "", sar.rays, sar.bounces, sar.intervalMs,
			sar.sceneName, sar.sceneTris, sar.lastSimMs, audio > 0.0 ? 100.0 * sar.mixSeconds / audio : 0.0,
			sar.mixBlocks ? (double)sar.mixSlots / sar.mixBlocks : 0.0 );
		sar.mixSeconds = 0.0;
		sar.mixBlocks = sar.mixSlots = 0;
	}
	else
		Q_strncpyz( sar.desc, "no level loaded", sizeof( sar.desc ) );
	SA_Unlock( &sar.dataLock );
	return sar.desc;
}


void SPH_ReflectionsSetLive( int bounces, int intervalMs ) {
	if ( !sar.active )
		return;
	SA_Lock( &sar.dataLock );
	sar.bounces = bounces;
	sar.intervalMs = intervalMs;
	SA_Unlock( &sar.dataLock );
}


void SPH_ReflectionsSetGeometry( spGeometry_t *geom ) {
	if ( !sar.active ) {
		SP_FreeGeometry( geom );
		return;
	}
	SA_Lock( &sar.dataLock );
	if ( sar.pendingGeom )
		SP_FreeGeometry( sar.pendingGeom );
	sar.pendingGeom = geom;
	sar.geomPending = qtrue;
	SA_Unlock( &sar.dataLock );
}


/*
In sync mode the worker stays idle and the main thread runs every
simulation step with SPH_ReflectionsSimulate, so output is repeatable.
*/
void SPH_ReflectionsSetSync( qboolean sync ) {
	if ( !sar.active || sar.sync == sync )
		return;
	SA_Lock( &sar.simLock );		// waits for a running step to finish
	sar.sync = sync;
	SA_Unlock( &sar.simLock );
}


void SPH_ReflectionsSimulate( void ) {
	if ( !sar.active || !sar.sync )
		return;
	SA_Lock( &sar.simLock );
	SA_SimulateStep();
	SA_Unlock( &sar.simLock );
}


void SPH_ReflectionsUpdate( const vec3_t listener, vec3_t axis[3], const vec3_t *origins, const qboolean *active ) {
	IPLCoordinateSpace3 space;
	vec3_t right;
	int i;

	if ( !sar.active )
		return;

	VectorNegate( axis[1], right );
	space.origin = SA_Position( listener );
	space.right = SA_Direction( right );
	space.up = SA_Direction( axis[2] );
	space.ahead = SA_Direction( axis[0] );
	sar.listenerSpace = space;

	SA_Lock( &sar.dataLock );
	sar.listener = space;
	for ( i = 0; i < sar.numSlots; i++ ) {
		sar.slotActive[i] = active[i];
		if ( active[i] )
			sar.origins[i] = SA_Position( origins[i] );
	}
	SA_Unlock( &sar.dataLock );
}


void SPH_ReflectionsReset( int slot ) {
	if ( !sar.active || slot < 0 || slot >= sar.numSlots )
		return;
	sa.ReflectionReset( sar.effects[slot] );
	SA_Lock( &sar.dataLock );
	sar.slotGen[slot]++;
	SA_Unlock( &sar.dataLock );
}


/*
================
SPH_ReflectionsProcess

Runs one block of a slot's mono input (NULL = silence, to let the reverb
ring out) through its reflections into the shared Ambisonics mix. Until
the slot's own position has been simulated it borrows the result of the
nearest slot that has been. Returns qfalse once a silent slot has rung out.
================
*/
qboolean SPH_ReflectionsProcess( int slot, const float *mono ) {
	static float *silence;
	IPLSimulationOutputs out;
	IPLReflectionEffectParams params;
	IPLAudioBuffer in, amb;
	float *inData[1];
	IPLAudioEffectState state;
	const qboolean silent = ( mono == NULL );
	double start;
	int use = -1, i, c;

	if ( !sar.active || slot < 0 || slot >= sar.numSlots )
		return qfalse;

	SA_Lock( &sar.dataLock );
	if ( sar.slotDoneGen[slot] == sar.slotGen[slot] ) {
		use = slot;
	} else {
		float best = 1e30f;
		for ( i = 0; i < sar.numSlots; i++ ) {
			float dx, dy, dz, d;
			if ( i == slot || !sar.slotActive[i] || sar.slotDoneGen[i] != sar.slotGen[i] )
				continue;
			dx = sar.origins[i].x - sar.origins[slot].x;
			dy = sar.origins[i].y - sar.origins[slot].y;
			dz = sar.origins[i].z - sar.origins[slot].z;
			d = dx * dx + dy * dy + dz * dz;
			if ( d < best ) {
				best = d;
				use = i;
			}
		}
	}
	SA_Unlock( &sar.dataLock );
	if ( use < 0 )
		return !silent;		// not simulated yet: keep the slot
	start = SA_TimeSec();
	sar.mixSlots++;

	if ( !mono ) {
		if ( !silence )
			silence = calloc( sa.audio.frameSize, sizeof( float ) );
		mono = silence;
	}

	Com_Memset( &out, 0, sizeof( out ) );
	sa.SourceGetOutputs( sar.sources[use], IPL_SIMULATIONFLAGS_REFLECTIONS, &out );
	params = out.reflections;
	params.type = sar.type;
	params.numChannels = SA_CHANNELS;
	params.irSize = sar.irSize;

	inData[0] = (float *)mono;
	in.numChannels = 1;
	in.numSamples = sa.audio.frameSize;
	in.data = inData;
	amb.numChannels = SA_CHANNELS;
	amb.numSamples = sa.audio.frameSize;
	amb.data = sar.ambTmp;
	state = sa.ReflectionApply( sar.effects[slot], &params, &in, &amb, NULL );
	for ( c = 0; c < SA_CHANNELS; c++ )
		for ( i = 0; i < sa.audio.frameSize; i++ )
			sar.amb[c][i] += sar.ambTmp[c][i];
	sar.mixSeconds += SA_TimeSec() - start;
	return !silent || state == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
}


/*
================
SPH_ReflectionsOutput

Decodes this block's reflections to stereo (binaural with the HRTF, or
plain panning) into outL/outR, and clears the mix for the next block.
================
*/
void SPH_ReflectionsOutput( float *outL, float *outR, qboolean binaural ) {
	IPLAmbisonicsDecodeEffectParams params;
	IPLAudioBuffer in, out;
	float *outData[2];
	double start;
	int c;

	if ( !sar.active ) {
		Com_Memset( outL, 0, sa.audio.frameSize * sizeof( float ) );
		Com_Memset( outR, 0, sa.audio.frameSize * sizeof( float ) );
		return;
	}

	start = SA_TimeSec();
	outData[0] = outL;
	outData[1] = outR;
	in.numChannels = SA_CHANNELS;
	in.numSamples = sa.audio.frameSize;
	in.data = sar.amb;

	SA_Diffuse();

	out.numChannels = 2;
	out.numSamples = sa.audio.frameSize;
	out.data = outData;

	Com_Memset( &params, 0, sizeof( params ) );
	params.order = SA_ORDER;
	params.hrtf = sa.hrtf;
	params.orientation = sar.listenerSpace;
	params.binaural = binaural ? IPL_TRUE : IPL_FALSE;
	sa.DecodeApply( sar.decode, &params, &in, &out );
	for ( c = 0; c < SA_CHANNELS; c++ )
		Com_Memset( sar.amb[c], 0, sa.audio.frameSize * sizeof( float ) );
	sar.mixSeconds += SA_TimeSec() - start;
	sar.mixBlocks++;
}
