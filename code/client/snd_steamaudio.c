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
 */

#include "snd_local.h"
#include "../steamaudio/include/phonon.h"

typedef IPLerror				(IPLCALL *pContextCreate_t)( IPLContextSettings*, IPLContext* );
typedef void					(IPLCALL *pContextRelease_t)( IPLContext* );
typedef IPLerror				(IPLCALL *pHRTFCreate_t)( IPLContext, IPLAudioSettings*, IPLHRTFSettings*, IPLHRTF* );
typedef void					(IPLCALL *pHRTFRelease_t)( IPLHRTF* );
typedef IPLerror				(IPLCALL *pBinauralCreate_t)( IPLContext, IPLAudioSettings*, IPLBinauralEffectSettings*, IPLBinauralEffect* );
typedef void					(IPLCALL *pBinauralRelease_t)( IPLBinauralEffect* );
typedef void					(IPLCALL *pBinauralReset_t)( IPLBinauralEffect );
typedef IPLAudioEffectState		(IPLCALL *pBinauralApply_t)( IPLBinauralEffect, IPLBinauralEffectParams*, IPLAudioBuffer*, IPLAudioBuffer* );
typedef IPLAudioEffectState		(IPLCALL *pBinauralGetTail_t)( IPLBinauralEffect, IPLAudioBuffer* );

static struct {
	void				*lib;
	IPLContext			context;
	IPLHRTF				hrtf;
	IPLAudioSettings	audio;
	char				desc[128];

	pContextCreate_t	ContextCreate;
	pContextRelease_t	ContextRelease;
	pHRTFCreate_t		HRTFCreate;
	pHRTFRelease_t		HRTFRelease;
	pBinauralCreate_t	BinauralCreate;
	pBinauralRelease_t	BinauralRelease;
	pBinauralReset_t	BinauralReset;
	pBinauralApply_t	BinauralApply;
	pBinauralGetTail_t	BinauralGetTail;
} sa;


static void *SA_Load( const char *name ) {
	void *lib = Sys_LoadLibrary( name );
	if ( !lib && !strchr( name, '/' ) && !strchr( name, '\\' ) ) {
		// also look next to the executable
		lib = Sys_LoadLibrary( va( "%s%c%s", Sys_Pwd(), PATH_SEP, name ) );
	}
	return lib;
}


/*
================
SPH_Init

Loads Steam Audio and creates the default HRTF for the given output rate
and block size. Returns qfalse (and leaves HRTF disabled) on any failure.
================
*/
qboolean SPH_Init( const char *libName, int rate, int frameSize ) {
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
	hs.type = IPL_HRTFTYPE_DEFAULT;
	hs.volume = 1.0f;
	hs.normType = IPL_HRTFNORMTYPE_RMS;		// similar loudness from every direction
	err = sa.HRTFCreate( sa.context, &sa.audio, &hs, &sa.hrtf );
	if ( err != IPL_STATUS_SUCCESS ) {
		Com_Printf( S_COLOR_YELLOW "Steam Audio: HRTF creation failed (%i)\n", err );
		SPH_Shutdown();
		return qfalse;
	}

	Com_sprintf( sa.desc, sizeof( sa.desc ), "Steam Audio %i.%i.%i, default HRTF, %i Hz, %i-frame blocks",
		STEAMAUDIO_VERSION_MAJOR, STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH, rate, frameSize );
	Com_Printf( "%s\n", sa.desc );
	return qtrue;
}


void SPH_Shutdown( void ) {
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
