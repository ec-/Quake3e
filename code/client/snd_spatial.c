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
/*
 * snd_spatial.c -- "spatial" sound backend (s_backend spatial)
 *
 * A replacement for the classic mixer in snd_dma.c/snd_mix.c, built so that
 * per-voice spatial processing (HRTF, occlusion, reflections) can be added.
 * It implements soundInterface_t and reuses the platform device layer
 * (SNDDMA_*) and the decoders (S_Codec*).
 *
 * Differences from the classic mixer:
 *  - Forward-only mixing. The classic mixer re-mixes the next s_mixAhead
 *    seconds from scratch every frame. That cannot work with stateful
 *    filters (HRTF convolution), so here every output frame is mixed exactly
 *    once, in fixed blocks of SP_BLOCK frames, with a small lead over the
 *    device playback position (s_spatialMixAhead).
 *  - Floating point mixing, output at >= 44.1 kHz (48 kHz requested).
 *  - Sounds are resampled to the output rate once, at load time, with a
 *    windowed-sinc filter (the classic loader uses nearest-sample, which
 *    adds aliasing above the source Nyquist frequency).
 *  - Gains are ramped across each block, so moving sounds do not click.
 *  - All positional processing goes through SP_Spatialize() and
 *    SP_MixVoice(). With Steam Audio loaded (snd_steamaudio.c) each
 *    positioned voice gets its own HRTF filter, so height and front/back
 *    are audible on headphones. Without it, the classic left/right pan law.
 *
 * Quake's up axis is Z. The listener axis is forward, left, up.
 */

#include "snd_local.h"
#include "snd_codec.h"
#include "client.h"

// =======================================================================
// Constants
// =======================================================================

#define SP_BLOCK			256			// frames per mix block (fixed, HRTF-friendly)
#define SP_MAX_SFX			4096
#define SP_SFX_HASH			256
#define SP_RAW_FRAMES		32768		// power of two
#define SP_RAW_AHEAD		16384		// how far ahead music is decoded
#define SP_MIN_SPEED		44100

// classic loudness model, kept so the backends match in level
#define SP_FULLVOLUME		80.0f
#define SP_ATTENUATE		0.0008f
#define SP_MASTER_VOL		127.0f
#define SP_SPHERE_VOL		90.0f

// HRTF mode gain, relative to masterVol * attenuation. Calibrated with
// s_spatialTest so a sound straight ahead has the same per-ear RMS level as
// with the classic pan law (Steam Audio 4.8.1 default HRTF, RMS normalised).
#define SP_HRTF_GAIN		0.566f

#ifdef _WIN32
#define SP_STEAMAUDIO_LIB	"phonon.dll"
#else
#define SP_STEAMAUDIO_LIB	"libphonon" DLL_EXT
#endif

// resampler
#define SP_RS_HALFWIDTH		12			// input samples each side of the centre
#define SP_RS_MAXPHASES		4096		// filter phases for unusual rate ratios
#define SP_RS_BETA			8.0			// Kaiser window shape

// =======================================================================
// Types
// =======================================================================

typedef struct spSfx_s {
	char			name[MAX_QPATH];
	short			*data;			// interleaved, at dma.speed
	int				frames;
	int				channels;
	qboolean		defaultSound;
	int				lastTimeUsed;
	struct spSfx_s	*hashNext;
} spSfx_t;

typedef struct {
	vec3_t	origin;
	vec3_t	axis[3];				// forward, left, up
	int		entnum;
} spListener_t;

// per-voice spatial state
typedef struct {
	float		target[2];			// per-ear gains wanted after the next block (pan mode, head-locked sounds)
	float		current[2];			// per-ear gains applied at the end of the last block
	float		monoTarget;			// distance gain for HRTF mode
	float		monoCurrent;
	vec3_t		dir;				// direction to the source in the listener frame (forward, left, up)
	qboolean	headLocked;			// the listener's own sounds: not positioned
	qboolean	primed;				// current values are valid
	void		*hrtf;				// binaural state, NULL without Steam Audio
	qboolean	tail;				// binaural state may still have output after the input stopped
} spSpatial_t;

typedef struct {
	spSfx_t		*sfx;
	int			allocTime;
	int			entnum;
	int			entchannel;
	qboolean	fixedOrigin;
	vec3_t		origin;
	float		masterVol;			// 0..255 before spatialization
	int			pos;				// next frame to play
	spSpatial_t	spatial;
} spChannel_t;

typedef struct {
	// set by the game every frame
	vec3_t		origin;				// also the entity's position for entity-following sounds
	vec3_t		velocity;
	spSfx_t		*sfx;
	qboolean	active;
	qboolean	kill;				// AddLoopingSound (cleared each frame) vs AddRealLoopingSound
	qboolean	doppler;
	float		dopplerScale;
	int			framenum;
	// mixer state
	spSfx_t		*playing;
	double		phase;
	spSpatial_t	spatial;
} spEntity_t;

// =======================================================================
// State
// =======================================================================

static qboolean		sp_started;
static qboolean		sp_muted;

static spSfx_t		sp_sfx[SP_MAX_SFX];
static int			sp_numSfx;
static spSfx_t		*sp_sfxHash[SP_SFX_HASH];

static spChannel_t	sp_channels[MAX_CHANNELS];
static spEntity_t	sp_entities[MAX_GENTITIES];
static spListener_t	sp_listener;

// sample-frame clocks
static int			sp_soundtime;		// device playback position
static int			sp_cursor;			// next frame to be written to the device ring
static int			sp_mixtime;			// next frame to be mixed (>= sp_cursor)
static int			sp_dmaBuffers;
static int			sp_oldSamplePos;
static int			sp_clearedTo;		// ring frames before this were zeroed after playback
static int			sp_underruns;
static qboolean		sp_clockValid;

// mixed but not yet delivered output (one partial block at most)
static float		sp_stage[SP_BLOCK * 2];
static int			sp_stageStart;		// first undelivered frame in sp_stage
static int			sp_stageEnd;		// frames valid in sp_stage

// streamed audio (music, cinematics, VoIP), already at dma.speed
static float		sp_raw[SP_RAW_FRAMES][2];
static int			sp_rawEnd;
static double		sp_rawFrac;
static float		sp_rawPrev[2];

static snd_stream_t	*sp_music;
static char			sp_musicLoop[MAX_QPATH];

static cvar_t		*s_spatialMixAhead;
static cvar_t		*s_spatialShow;
static cvar_t		*s_spatialHRTF;
static cvar_t		*s_steamAudioLib;
extern cvar_t		*s_khz;
extern cvar_t		*s_show;

// =======================================================================
// Resampling (load time only)
// =======================================================================

static float	*sp_rsPoly;				// [phases][taps]
static int		sp_rsPhases, sp_rsTaps, sp_rsHalf;
static int		sp_rsIn = -1, sp_rsOut = -1;

static double SP_BesselI0( double x ) {
	double sum = 1.0, term = 1.0;
	int k;
	for ( k = 1; k < 32; k++ ) {
		term *= ( x / ( 2.0 * k ) ) * ( x / ( 2.0 * k ) );
		sum += term;
		if ( term < sum * 1e-12 )
			break;
	}
	return sum;
}


static int SP_Gcd( int a, int b ) {
	while ( b ) {
		const int t = a % b;
		a = b;
		b = t;
	}
	return a;
}


/*
================
SP_BuildPolyphase

Kaiser-windowed sinc filter bank for inRate -> outRate. For a rational
ratio in/out = M/L every output sample falls on one of L fractional input
positions, so the filter is precomputed per position ("phase") and each
output sample is a plain dot product. Rows are normalised to unity DC gain.
Downsampling widens the kernel so the cutoff follows the output Nyquist.
================
*/
static void SP_BuildPolyphase( int inRate, int outRate ) {
	const double scale = ( outRate < inRate ) ? (double)outRate / inRate : 1.0;
	const double fc = 0.97 * scale;		// cutoff relative to the input Nyquist frequency
	const double i0beta = SP_BesselI0( SP_RS_BETA );
	int L, p, j;

	if ( sp_rsPoly && sp_rsIn == inRate && sp_rsOut == outRate )
		return;

	L = outRate / SP_Gcd( inRate, outRate );
	sp_rsPhases = ( L > SP_RS_MAXPHASES ) ? SP_RS_MAXPHASES : L;
	sp_rsHalf = (int)ceil( SP_RS_HALFWIDTH / scale );
	sp_rsTaps = sp_rsHalf * 2;

	free( sp_rsPoly );
	sp_rsPoly = malloc( sp_rsPhases * sp_rsTaps * sizeof( float ) );

	for ( p = 0; p < sp_rsPhases; p++ ) {
		float *row = sp_rsPoly + p * sp_rsTaps;
		const double frac = (double)p / sp_rsPhases;
		double sum = 0.0;
		for ( j = 0; j < sp_rsTaps; j++ ) {
			// distance from the output position to input sample (centre - half + 1 + j)
			const double t = frac + ( sp_rsHalf - 1 - j );
			const double r = t / sp_rsHalf;
			double v = 0.0;
			if ( r > -1.0 && r < 1.0 ) {
				const double s = ( t == 0.0 ) ? 1.0 : sin( M_PI * fc * t ) / ( M_PI * fc * t );
				v = fc * s * SP_BesselI0( SP_RS_BETA * sqrt( 1.0 - r * r ) ) / i0beta;
			}
			row[j] = (float)v;
			sum += v;
		}
		for ( j = 0; j < sp_rsTaps; j++ )
			row[j] = (float)( row[j] / sum );
	}

	sp_rsIn = inRate;
	sp_rsOut = outRate;
}


/*
================
SP_Resample

in: interleaved floats at inRate. out: interleaved shorts at outRate.
================
*/
static void SP_Resample( const float *in, int inFrames, int channels, int inRate, int outRate, short *out, int outFrames ) {
	float *padded;
	int o, c, j;

	SP_BuildPolyphase( inRate, outRate );

	// zero padding on both sides removes all bounds checks from the inner loop
	padded = calloc( ( inFrames + sp_rsTaps + 2 ) * channels, sizeof( float ) );
	Com_Memcpy( padded + sp_rsHalf * channels, in, inFrames * channels * sizeof( float ) );

	for ( o = 0; o < outFrames; o++ ) {
		const long long num = (long long)o * inRate;
		int centre = (int)( num / outRate );
		int phase = (int)( (double)( num % outRate ) * sp_rsPhases / outRate + 0.5 );
		const float *row;
		const float *src;

		if ( phase >= sp_rsPhases ) {
			phase = 0;
			centre++;
		}
		row = sp_rsPoly + phase * sp_rsTaps;
		// padded index of input sample (centre - half + 1) is centre + 1
		src = padded + ( centre + 1 ) * channels;

		for ( c = 0; c < channels; c++ ) {
			float acc = 0.0f;
			int s;
			for ( j = 0; j < sp_rsTaps; j++ )
				acc += row[j] * src[j * channels + c];
			s = (int)lrintf( acc * 32767.0f );
			if ( s > 32767 ) s = 32767;
			else if ( s < -32768 ) s = -32768;
			out[o * channels + c] = (short)s;
		}
	}

	free( padded );
}

// =======================================================================
// Sound effects
// =======================================================================

static unsigned int SP_HashName( const char *name ) {
	unsigned int hash = 0;
	int i;
	for ( i = 0; name[i]; i++ ) {
		char c = tolower( name[i] );
		if ( c == '.' ) break;
		if ( c == '\\' ) c = '/';
		hash += (unsigned int)c * ( i + 119 );
	}
	return hash & ( SP_SFX_HASH - 1 );
}


static qboolean SP_LoadSfx( spSfx_t *sfx ) {
	snd_info_t info;
	byte *data;
	float *pcm;
	int i, n, outFrames;

	data = S_CodecLoad( sfx->name, &info );
	if ( !data )
		return qfalse;

	if ( ( info.width != 1 && info.width != 2 ) || info.channels < 1 || info.channels > 2 || info.samples <= 0 ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: %s: unsupported format (%d channels, %d bytes per sample)\n",
			sfx->name, info.channels, info.width );
		Hunk_FreeTempMemory( data );
		return qfalse;
	}

	n = info.samples * info.channels;
	pcm = malloc( n * sizeof( float ) );
	for ( i = 0; i < n; i++ ) {
		if ( info.width == 2 )
			pcm[i] = ((short *)( data + info.dataofs ))[i] * ( 1.0f / 32768.0f );
		else
			pcm[i] = ( (int)( data + info.dataofs )[i] - 128 ) * ( 1.0f / 128.0f );
	}
	Hunk_FreeTempMemory( data );

	outFrames = (int)( (double)info.samples * dma.speed / info.rate );
	if ( outFrames < 1 )
		outFrames = 1;

	sfx->data = malloc( outFrames * info.channels * sizeof( short ) );
	sfx->frames = outFrames;
	sfx->channels = info.channels;

	if ( info.rate == dma.speed ) {
		for ( i = 0; i < n; i++ )
			sfx->data[i] = (short)lrintf( pcm[i] * 32767.0f );
	} else {
		SP_Resample( pcm, info.samples, info.channels, info.rate, dma.speed, sfx->data, outFrames );
	}

	free( pcm );
	return qtrue;
}


static spSfx_t *SP_FindSfx( const char *name ) {
	unsigned int hash;
	spSfx_t *sfx;
	int i;

	if ( !name || !name[0] ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Sound name is empty\n" );
		return NULL;
	}
	if ( strlen( name ) >= MAX_QPATH ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Sound name is too long: %s\n", name );
		return NULL;
	}
	if ( name[0] == '*' ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Tried to load player sound directly: %s\n", name );
		return NULL;
	}

	hash = SP_HashName( name );
	for ( sfx = sp_sfxHash[hash]; sfx; sfx = sfx->hashNext ) {
		if ( !Q_stricmp( sfx->name, name ) )
			return sfx;
	}

	if ( sp_numSfx >= SP_MAX_SFX ) {
		Com_Error( ERR_FATAL, "SP_FindSfx: out of sfx slots" );
	}

	i = sp_numSfx++;
	sfx = &sp_sfx[i];
	Com_Memset( sfx, 0, sizeof( *sfx ) );
	Q_strncpyz( sfx->name, name, sizeof( sfx->name ) );
	sfx->hashNext = sp_sfxHash[hash];
	sp_sfxHash[hash] = sfx;

	if ( !SP_LoadSfx( sfx ) ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: couldn't load sound: %s\n", sfx->name );
		sfx->defaultSound = qtrue;
	}

	return sfx;
}


static void SP_FreeAllSfx( void ) {
	int i;
	for ( i = 0; i < sp_numSfx; i++ ) {
		free( sp_sfx[i].data );
	}
	Com_Memset( sp_sfx, 0, sizeof( sp_sfx ) );
	Com_Memset( sp_sfxHash, 0, sizeof( sp_sfxHash ) );
	sp_numSfx = 0;
}

// =======================================================================
// Spatialization
//
// Everything that decides how a positioned sound reaches the two ears lives
// here. With Steam Audio loaded (and s_spatialHRTF 1) every positioned voice
// goes through its own HRTF filter, which carries height and front/back.
// Otherwise the classic pan law is used (left/right only).
// =======================================================================

/*
================
SP_Spatialize

Sets the targets for the next block of a voice of volume masterVol (0..255)
at origin. Head-locked voices (the listener's own sounds) are centred at
full volume, as in the classic mixer.
================
*/
static void SP_Spatialize( spSpatial_t *sp, const spListener_t *listener, const vec3_t origin, float masterVol, qboolean headLocked ) {
	vec3_t dir;
	float dist, atten, lscale, rscale, dot;

	sp->headLocked = headLocked;

	if ( headLocked ) {
		sp->target[0] = sp->target[1] = masterVol / 256.0f;
		sp->monoTarget = 0.0f;
		VectorSet( sp->dir, 1.0f, 0.0f, 0.0f );
	} else {
		VectorSubtract( origin, listener->origin, dir );
		dist = VectorNormalize( dir );

		if ( dist < 1.0f ) {
			// on top of the listener: no meaningful direction
			VectorSet( sp->dir, 1.0f, 0.0f, 0.0f );
		} else {
			VectorRotate( dir, listener->axis, sp->dir );
		}

		dist -= SP_FULLVOLUME;
		if ( dist < 0.0f )
			dist = 0.0f;
		atten = 1.0f - dist * SP_ATTENUATE;
		if ( atten < 0.0f )
			atten = 0.0f;

		dot = -sp->dir[1];	// +1 = fully right, -1 = fully left
		if ( dma.channels == 1 ) {
			lscale = rscale = 1.0f;
		} else {
			rscale = 0.5f * ( 1.0f + dot );
			lscale = 0.5f * ( 1.0f - dot );
			if ( rscale < 0.0f ) rscale = 0.0f;
			if ( lscale < 0.0f ) lscale = 0.0f;
		}

		sp->target[0] = masterVol * atten * lscale / 256.0f;
		sp->target[1] = masterVol * atten * rscale / 256.0f;
		sp->monoTarget = masterVol * atten * SP_HRTF_GAIN / 256.0f;
	}

	if ( !sp->primed ) {
		sp->current[0] = sp->target[0];
		sp->current[1] = sp->target[1];
		sp->monoCurrent = sp->monoTarget;
		sp->primed = qtrue;
	}
}


static qboolean SP_UseHRTF( const spSpatial_t *sp ) {
	return sp->hrtf && !sp->headLocked && s_spatialHRTF->integer && dma.channels == 2;
}


/*
================
SP_Read

Reads up to SP_BLOCK frames of a voice from *phase, advancing by rate per
frame, into left/right (or mono when right is NULL, stereo sounds are
averaged). Unread frames are zeroed. Returns the frames read (less than
SP_BLOCK only for a one-shot that ended).
================
*/
static int SP_Read( const spSfx_t *sfx, double *phase, double rate, qboolean loop, float *left, float *right ) {
	const short *data = sfx->data;
	const int frames = sfx->frames;
	const float scale = 1.0f / 32768.0f;
	int i, n = 0;

	if ( data && frames > 0 && rate == 1.0 && !loop ) {
		// one-shot at native rate: integer stepping
		const int p = (int)*phase;
		n = frames - p;
		if ( n > SP_BLOCK ) n = SP_BLOCK;
		if ( n < 0 ) n = 0;
		for ( i = 0; i < n; i++ ) {
			float l, r;
			if ( sfx->channels == 2 ) {
				l = data[( p + i ) * 2] * scale;
				r = data[( p + i ) * 2 + 1] * scale;
			} else {
				l = r = data[p + i] * scale;
			}
			if ( right ) {
				left[i] = l;
				right[i] = r;
			} else {
				left[i] = 0.5f * ( l + r );
			}
		}
		*phase = p + n;
	} else if ( data && frames > 0 ) {
		// looping and/or resampled (doppler): linear interpolation
		for ( i = 0; i < SP_BLOCK; i++ ) {
			double ph = *phase;
			int a, b;
			float f, l, r;

			if ( ph >= frames ) {
				if ( !loop )
					break;
				ph = fmod( ph, frames );
			}
			a = (int)ph;
			b = a + 1;
			if ( b >= frames )
				b = loop ? 0 : a;
			f = (float)( ph - a );

			if ( sfx->channels == 2 ) {
				l = ( data[a * 2] + f * ( data[b * 2] - data[a * 2] ) ) * scale;
				r = ( data[a * 2 + 1] + f * ( data[b * 2 + 1] - data[a * 2 + 1] ) ) * scale;
			} else {
				l = r = ( data[a] + f * ( data[b] - data[a] ) ) * scale;
			}
			if ( right ) {
				left[i] = l;
				right[i] = r;
			} else {
				left[i] = 0.5f * ( l + r );
			}
			*phase = ph + rate;
			n++;
		}
	}

	for ( i = n; i < SP_BLOCK; i++ ) {
		left[i] = 0.0f;
		if ( right )
			right[i] = 0.0f;
	}
	return n;
}


/*
================
SP_MixVoice

Mixes one block of a voice into out (interleaved stereo), ramping gains
from the previous block's values to the targets. Returns the frames read.
================
*/
static int SP_MixVoice( const spSfx_t *sfx, double *phase, double rate, qboolean loop, spSpatial_t *sp, float *out ) {
	float a[SP_BLOCK], b[SP_BLOCK];
	const float step = 1.0f / SP_BLOCK;
	int i, n;

	if ( SP_UseHRTF( sp ) ) {
		float l[SP_BLOCK], r[SP_BLOCK];
		const float g0 = sp->monoCurrent, dg = ( sp->monoTarget - sp->monoCurrent ) * step;
		n = SP_Read( sfx, phase, rate, loop, a, NULL );
		for ( i = 0; i < SP_BLOCK; i++ )
			a[i] *= g0 + dg * i;
		SPH_Process( sp->hrtf, a, sp->dir, l, r );
		for ( i = 0; i < SP_BLOCK; i++ ) {
			out[i * 2] += l[i];
			out[i * 2 + 1] += r[i];
		}
		sp->tail = qtrue;
	} else {
		const float l0 = sp->current[0], dl = ( sp->target[0] - sp->current[0] ) * step;
		const float r0 = sp->current[1], dr = ( sp->target[1] - sp->current[1] ) * step;
		n = SP_Read( sfx, phase, rate, loop, a, b );
		for ( i = 0; i < SP_BLOCK; i++ ) {
			out[i * 2] += a[i] * ( l0 + dl * i );
			out[i * 2 + 1] += b[i] * ( r0 + dr * i );
		}
	}

	sp->current[0] = sp->target[0];
	sp->current[1] = sp->target[1];
	sp->monoCurrent = sp->monoTarget;
	return n;
}


/*
================
SP_MixTail

Mixes the remaining output of a voice's HRTF filter after its sound ended.
================
*/
static void SP_MixTail( spSpatial_t *sp, float *out ) {
	float l[SP_BLOCK], r[SP_BLOCK];
	int i;
	if ( !sp->hrtf || !sp->tail )
		return;
	sp->tail = SPH_Tail( sp->hrtf, l, r );
	for ( i = 0; i < SP_BLOCK; i++ ) {
		out[i * 2] += l[i];
		out[i * 2 + 1] += r[i];
	}
}

// =======================================================================
// Mixing
// =======================================================================

// one binaural state per channel slot, kept across sounds so tails can finish
static spSpatial_t	sp_chanTail[MAX_CHANNELS];
static void			*sp_chanHRTF[MAX_CHANNELS];

static void SP_ChannelFree( spChannel_t *ch ) {
	const int i = ch - sp_channels;
	// hand the filter over so its tail can play out
	sp_chanTail[i].hrtf = ch->spatial.hrtf;
	sp_chanTail[i].tail = ch->spatial.tail;
	Com_Memset( ch, 0, sizeof( *ch ) );
}


/*
================
SP_MixBlock

Mixes exactly SP_BLOCK frames starting at sp_mixtime into out.
================
*/
static void SP_MixBlock( float *out ) {
	// classic mixer: sample * vol(0..255) * (s_volume * 255) / 65536
	const float master = s_volume->value * 255.0f / 256.0f;
	spChannel_t *ch;
	spEntity_t *ent;
	int i, t;

	Com_Memset( out, 0, SP_BLOCK * 2 * sizeof( float ) );

	// streamed audio is not scaled by s_volume (matches the classic mixer)
	for ( t = sp_mixtime, i = 0; i < SP_BLOCK; i++, t++ ) {
		if ( t - sp_rawEnd >= 0 )
			break;
		out[i * 2] = sp_raw[t & ( SP_RAW_FRAMES - 1 )][0];
		out[i * 2 + 1] = sp_raw[t & ( SP_RAW_FRAMES - 1 )][1];
	}

	if ( master > 0.0f ) {
		float tmp[SP_BLOCK * 2];
		Com_Memset( tmp, 0, sizeof( tmp ) );

		// one-shot sounds
		for ( i = 0, ch = sp_channels; i < MAX_CHANNELS; i++, ch++ ) {
			double phase;
			if ( !ch->sfx ) {
				SP_MixTail( &sp_chanTail[i], tmp );
				continue;
			}
			phase = ch->pos;
			SP_MixVoice( ch->sfx, &phase, 1.0, qfalse, &ch->spatial, tmp );
			ch->pos = (int)phase;
			if ( ch->pos >= ch->sfx->frames )
				SP_ChannelFree( ch );
		}

		// looping sounds
		for ( i = 0, ent = sp_entities; i < MAX_GENTITIES; i++, ent++ ) {
			const double rate = ent->doppler ? ent->dopplerScale : 1.0;
			if ( !ent->playing )
				continue;
			if ( !ent->active ) {
				ent->spatial.target[0] = ent->spatial.target[1] = 0.0f;
				ent->spatial.monoTarget = 0.0f;
			}
			SP_MixVoice( ent->playing, &ent->phase, rate, qtrue, &ent->spatial, tmp );
			if ( !ent->active && ent->spatial.current[0] == 0.0f && ent->spatial.current[1] == 0.0f
				&& ent->spatial.monoCurrent == 0.0f ) {
				// faded out
				ent->playing = NULL;
				ent->spatial.primed = qfalse;
			}
		}

		for ( i = 0; i < SP_BLOCK * 2; i++ )
			out[i] += tmp[i] * master;
	}

	if ( s_testsound->integer ) {
		for ( i = 0; i < SP_BLOCK; i++ )
			out[i * 2] = out[i * 2 + 1] = sin( ( sp_mixtime + i ) * 0.1 ) * ( 20000.0f / 32768.0f );
	}

	sp_mixtime += SP_BLOCK;
}


/*
================
SP_Render

Produces exactly 'frames' frames of output, mixing whole blocks as needed.
================
*/
static void SP_Render( float *out, int frames ) {
	while ( frames > 0 ) {
		int n;
		if ( sp_stageStart >= sp_stageEnd ) {
			SP_MixBlock( sp_stage );
			sp_stageStart = 0;
			sp_stageEnd = SP_BLOCK;
		}
		n = sp_stageEnd - sp_stageStart;
		if ( n > frames )
			n = frames;
		Com_Memcpy( out, sp_stage + sp_stageStart * 2, n * 2 * sizeof( float ) );
		sp_stageStart += n;
		out += n * 2;
		frames -= n;
	}
}


/*
================
SP_Convert

Converts interleaved stereo float to the device sample format.
Writes dma.channels samples per frame.
================
*/
static void SP_Convert( const float *in, int frames, byte *out, qboolean mute ) {
	int i, c;
	for ( i = 0; i < frames; i++ ) {
		float v[2];
		v[0] = mute ? 0.0f : in[i * 2];
		v[1] = mute ? 0.0f : in[i * 2 + 1];
		if ( dma.channels == 1 )
			v[0] = 0.5f * ( v[0] + v[1] );
		for ( c = 0; c < (int)dma.channels; c++ ) {
			float s = ( c < 2 ) ? v[c] : 0.0f;
			if ( s > 1.0f ) s = 1.0f;
			else if ( s < -1.0f ) s = -1.0f;
			if ( dma.isfloat && dma.samplebits == 32 ) {
				( (float *)out )[i * dma.channels + c] = s;
			} else if ( dma.samplebits == 16 ) {
				( (short *)out )[i * dma.channels + c] = (short)lrintf( s * 32767.0f );
			} else {
				out[i * dma.channels + c] = (byte)( lrintf( s * 127.0f ) + 128 );
			}
		}
	}
}

// =======================================================================
// Device output
// =======================================================================

static void SP_RingWrite( int start, const byte *src, int frames ) {
	const int bpf = dma.channels * dma.samplebits / 8;
	while ( frames > 0 ) {
		const int pos = start % dma.fullsamples;
		int n = dma.fullsamples - pos;
		if ( n > frames )
			n = frames;
		if ( src )
			Com_Memcpy( dma.buffer + pos * bpf, src, n * bpf );
		else
			Com_Memset( dma.buffer + pos * bpf, ( dma.samplebits == 8 ) ? 0x80 : 0, n * bpf );
		if ( src )
			src += n * bpf;
		start += n;
		frames -= n;
	}
}


static void SP_ClearRing( void ) {
	if ( dma.buffer )
		Com_Memset( dma.buffer, ( dma.samplebits == 8 ) ? 0x80 : 0, dma.samples * dma.samplebits / 8 );
}


static void SP_StopAll( void );

static void SP_GetSoundtime( void ) {
	const int samplepos = SNDDMA_GetDMAPos();

	if ( samplepos < sp_oldSamplePos ) {
		sp_dmaBuffers++;
		if ( sp_soundtime > 0x40000000 ) {
			// avoid 32 bit overflow of the frame clocks
			sp_dmaBuffers = 0;
			sp_oldSamplePos = samplepos;
			sp_soundtime = sp_cursor = sp_mixtime = sp_clearedTo = samplepos / dma.channels;
			sp_rawEnd = 0;
			sp_stageStart = sp_stageEnd = 0;
			SP_StopAll();
			return;
		}
	}
	sp_oldSamplePos = samplepos;
	sp_soundtime = sp_dmaBuffers * dma.fullsamples + samplepos / dma.channels;

	if ( !sp_clockValid ) {
		sp_cursor = sp_clearedTo = sp_soundtime;
		sp_clockValid = qtrue;
	}
}


static void SP_WriteVideoFrame( void ) {
	// fixed amount of audio per video frame, so captures are deterministic
	const float duration = MAX( (float)dma.speed / cl_aviFrameRate->value, 1.0f );
	const float frameDuration = duration + clc.aviSoundFrameRemainder;
	const int n = (int)frameDuration;
	const int bpf = dma.channels * dma.samplebits / 8;
	static float fbuf[4096 * 2];
	static byte pbuf[4096 * 2 * 4];
	int done;

	clc.aviSoundFrameRemainder = frameDuration - n;

	for ( done = 0; done < n; ) {
		int k = n - done;
		if ( k > 4096 )
			k = 4096;
		SP_Render( fbuf, k );
		SP_Convert( fbuf, k, pbuf, qfalse );
		CL_WriteAVIAudioFrame( pbuf, k * bpf );
		done += k;
	}

	sp_cursor += n;
	sp_soundtime = sp_cursor;
	clc.aviFrameEndTime = sp_cursor;

	// the device clock kept running independently: re-sync when recording stops
	sp_clockValid = qfalse;

	// keep the device quiet while recording
	SP_ClearRing();
}

// =======================================================================
// Music and raw samples
// =======================================================================

static void SP_RawSamples( int samples, int rate, int width, int n_channels, const byte *data, float volume ) {
	const double step = (double)rate / dma.speed;
	int i;

	if ( !sp_started || sp_muted )
		return;

	if ( sp_rawEnd - sp_mixtime < 0 ) {
		sp_rawEnd = sp_mixtime;
		sp_rawFrac = 0.0;
		sp_rawPrev[0] = sp_rawPrev[1] = 0.0f;
	}

	// streaming linear interpolation between the previous and the current input frame
	for ( i = 0; i < samples; i++ ) {
		float cur[2];
		if ( width == 2 ) {
			cur[0] = ((const short *)data)[i * n_channels] * ( 1.0f / 32768.0f );
			cur[1] = ( n_channels == 2 ) ? ((const short *)data)[i * 2 + 1] * ( 1.0f / 32768.0f ) : cur[0];
		} else {
			cur[0] = ( (int)data[i * n_channels] - 128 ) * ( 1.0f / 128.0f );
			cur[1] = ( n_channels == 2 ) ? ( (int)data[i * 2 + 1] - 128 ) * ( 1.0f / 128.0f ) : cur[0];
		}
		cur[0] *= volume;
		cur[1] *= volume;

		while ( sp_rawFrac < 1.0 ) {
			const float f = (float)sp_rawFrac;
			float *dst = sp_raw[sp_rawEnd & ( SP_RAW_FRAMES - 1 )];
			dst[0] = sp_rawPrev[0] + f * ( cur[0] - sp_rawPrev[0] );
			dst[1] = sp_rawPrev[1] + f * ( cur[1] - sp_rawPrev[1] );
			sp_rawEnd++;
			sp_rawFrac += step;
		}
		sp_rawFrac -= 1.0;
		sp_rawPrev[0] = cur[0];
		sp_rawPrev[1] = cur[1];
	}

	if ( sp_rawEnd - sp_mixtime > SP_RAW_FRAMES ) {
		Com_DPrintf( "S_RawSamples: overflowed %i > %i\n", sp_rawEnd, sp_mixtime );
	}
}


static void SP_StopBackgroundTrack( void ) {
	if ( !sp_music )
		return;
	S_CodecCloseStream( sp_music );
	sp_music = NULL;
	sp_rawEnd = 0;
}


static void SP_OpenBackgroundStream( const char *filename ) {
	if ( sp_music ) {
		S_CodecCloseStream( sp_music );
		sp_music = NULL;
	}
	sp_music = S_CodecOpenStream( filename );
	if ( !sp_music ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: couldn't open music file %s\n", filename );
	}
}


static void SP_StartBackgroundTrack( const char *intro, const char *loop ) {
	if ( !intro )
		intro = "";
	if ( !loop || !loop[0] )
		loop = intro;
	Com_DPrintf( "S_StartBackgroundTrack( %s, %s )\n", intro, loop );

	if ( !*intro ) {
		SP_StopBackgroundTrack();
		return;
	}
	Q_strncpyz( sp_musicLoop, loop, sizeof( sp_musicLoop ) );
	SP_OpenBackgroundStream( intro );
}


static void SP_UpdateBackgroundTrack( void ) {
	byte raw[30000];

	if ( !sp_music || s_musicVolume->value == 0.0f )
		return;

	if ( sp_rawEnd - sp_mixtime < 0 )
		sp_rawEnd = sp_mixtime;

	while ( sp_rawEnd - sp_mixtime < SP_RAW_AHEAD ) {
		const int frameBytes = sp_music->info.width * sp_music->info.channels;
		int fileSamples = ( SP_RAW_AHEAD - ( sp_rawEnd - sp_mixtime ) ) * sp_music->info.rate / dma.speed;
		int fileBytes, r;

		if ( fileSamples <= 0 )
			return;
		fileBytes = fileSamples * frameBytes;
		if ( fileBytes > (int)sizeof( raw ) ) {
			fileBytes = sizeof( raw ) - sizeof( raw ) % frameBytes;
		}

		r = S_CodecReadStream( sp_music, fileBytes, raw );
		if ( r > 0 ) {
			SP_RawSamples( r / frameBytes, sp_music->info.rate, sp_music->info.width,
				sp_music->info.channels, raw, s_musicVolume->value );
		} else if ( sp_musicLoop[0] ) {
			SP_OpenBackgroundStream( sp_musicLoop );
			if ( !sp_music )
				return;
		} else {
			SP_StopBackgroundTrack();
			return;
		}
	}
}

// =======================================================================
// soundInterface_t implementation
// =======================================================================

static void SP_StopAll( void ) {
	int i;
	for ( i = 0; i < MAX_CHANNELS; i++ ) {
		SP_ChannelFree( &sp_channels[i] );
		sp_chanTail[i].tail = qfalse;
		SPH_ResetVoice( sp_chanHRTF[i] );
	}
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		sp_entities[i].active = qfalse;
		sp_entities[i].kill = qfalse;
		sp_entities[i].playing = NULL;
		sp_entities[i].spatial.primed = qfalse;
	}
	sp_rawEnd = 0;
}


static void S_Spatial_ClearSoundBuffer( void ) {
	if ( !sp_started )
		return;
	SP_StopAll();
	SNDDMA_BeginPainting();
	SP_ClearRing();
	SNDDMA_Submit();
}


static void S_Spatial_StopAllSounds( void ) {
	if ( !sp_started )
		return;
	SP_StopBackgroundTrack();
	S_Spatial_ClearSoundBuffer();
}


static void S_Spatial_StartSound( const vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfxHandle ) {
	spChannel_t *ch;
	spSfx_t *sfx;
	int i, oldest, chosen, startTime, inplay, allowed;

	if ( !sp_started || sp_muted )
		return;

	if ( !origin && ( entityNum < 0 || entityNum >= MAX_GENTITIES ) ) {
		Com_Error( ERR_DROP, "S_StartSound: bad entitynum %i", entityNum );
	}
	if ( sfxHandle < 0 || sfxHandle >= sp_numSfx ) {
		Com_Printf( S_COLOR_YELLOW "S_StartSound: handle %i out of range\n", sfxHandle );
		return;
	}

	sfx = &sp_sfx[sfxHandle];
	if ( !sfx->data )
		return;

	if ( s_show->integer == 1 ) {
		Com_Printf( "%i : %s\n", sp_mixtime, sfx->name );
	}

	startTime = sp_soundtime;

	// the same entity starting the same sound twice in one frame only makes it louder
	if ( entityNum != ENTITYNUM_WORLD ) {
		for ( i = 0, ch = sp_channels; i < MAX_CHANNELS; i++, ch++ ) {
			if ( ch->sfx == sfx && ch->entnum == entityNum && ch->allocTime == startTime ) {
				sfx->lastTimeUsed = startTime;
				return;
			}
		}
	}

	// limit duplication of the same sound on one entity
	allowed = ( entityNum == sp_listener.entnum ) ? 16 : 8;
	inplay = 0;
	for ( i = 0, ch = sp_channels; i < MAX_CHANNELS; i++, ch++ ) {
		if ( ch->sfx == sfx && ch->entnum == entityNum ) {
			if ( startTime - ch->allocTime < 20 ) {
				return;
			}
			inplay++;
		}
	}
	if ( inplay > allowed ) {
		return;
	}

	sfx->lastTimeUsed = startTime;

	// find a free channel, otherwise steal the oldest, preferring sounds
	// from the same entity and never the listener's own or announcer sounds
	chosen = -1;
	for ( i = 0; i < MAX_CHANNELS; i++ ) {
		if ( !sp_channels[i].sfx ) {
			chosen = i;
			break;
		}
	}
	if ( chosen == -1 ) {
		oldest = startTime;
		for ( i = 0, ch = sp_channels; i < MAX_CHANNELS; i++, ch++ ) {
			if ( ch->entnum != sp_listener.entnum && ch->entnum == entityNum && ch->allocTime - oldest < 0 && ch->entchannel != CHAN_ANNOUNCER ) {
				oldest = ch->allocTime;
				chosen = i;
			}
		}
		if ( chosen == -1 ) {
			for ( i = 0, ch = sp_channels; i < MAX_CHANNELS; i++, ch++ ) {
				if ( ch->entnum != sp_listener.entnum && ch->allocTime - oldest < 0 && ch->entchannel != CHAN_ANNOUNCER ) {
					oldest = ch->allocTime;
					chosen = i;
				}
			}
		}
		if ( chosen == -1 && sp_channels[0].entnum == sp_listener.entnum ) {
			for ( i = 0, ch = sp_channels; i < MAX_CHANNELS; i++, ch++ ) {
				if ( ch->allocTime - oldest < 0 ) {
					oldest = ch->allocTime;
					chosen = i;
				}
			}
		}
		if ( chosen == -1 ) {
			Com_DPrintf( S_COLOR_YELLOW "S_StartSound: No more channels free for %s\n", sfx->name );
			return;
		}
	}

	ch = &sp_channels[chosen];
	Com_Memset( ch, 0, sizeof( *ch ) );
	ch->sfx = sfx;
	ch->allocTime = startTime;
	ch->entnum = entityNum;
	ch->entchannel = entchannel;
	ch->masterVol = SP_MASTER_VOL;
	if ( origin ) {
		VectorCopy( origin, ch->origin );
		ch->fixedOrigin = qtrue;
	}

	// this slot's HRTF filter: drop any tail from its previous sound
	ch->spatial.hrtf = sp_chanHRTF[chosen];
	sp_chanTail[chosen].tail = qfalse;
	SPH_ResetVoice( ch->spatial.hrtf );

	// spatialize now so the first block has the right gains
	SP_Spatialize( &ch->spatial, &sp_listener, ch->fixedOrigin ? ch->origin : sp_entities[entityNum].origin,
		ch->masterVol, entityNum == sp_listener.entnum );
}


static void S_Spatial_StartLocalSound( sfxHandle_t sfxHandle, int channelNum ) {
	if ( !sp_started || sp_muted )
		return;
	if ( sfxHandle < 0 || sfxHandle >= sp_numSfx ) {
		Com_Printf( S_COLOR_YELLOW "S_StartLocalSound: handle %i out of range\n", sfxHandle );
		return;
	}
	S_Spatial_StartSound( NULL, sp_listener.entnum, channelNum, sfxHandle );
}


static void S_Spatial_StopLoopingSound( int entityNum ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "S_StopLoopingSound: bad entitynum %i", entityNum );
	}
	sp_entities[entityNum].active = qfalse;
	sp_entities[entityNum].kill = qfalse;
}


static void S_Spatial_ClearLoopingSounds( qboolean killall ) {
	int i;
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		if ( killall || sp_entities[i].kill ) {
			S_Spatial_StopLoopingSound( i );
		}
	}
}


static void SP_AddLoop( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle, qboolean kill, const char *caller ) {
	spEntity_t *ent;
	spSfx_t *sfx;

	if ( !sp_started || sp_muted )
		return;
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "%s: bad entitynum %i", caller, entityNum );
	}
	if ( sfxHandle < 0 || sfxHandle >= sp_numSfx ) {
		Com_Printf( S_COLOR_YELLOW "%s: handle %i out of range\n", caller, sfxHandle );
		return;
	}
	sfx = &sp_sfx[sfxHandle];
	if ( !sfx->data || sfx->frames <= 0 ) {
		Com_Error( ERR_DROP, "%s has length 0", sfx->name );
	}

	ent = &sp_entities[entityNum];
	VectorCopy( origin, ent->origin );
	VectorCopy( velocity, ent->velocity );
	ent->sfx = sfx;
	ent->active = qtrue;
	ent->kill = kill;
	ent->doppler = qfalse;
	ent->dopplerScale = 1.0f;

	// classic doppler model for fast-moving loops (projectiles)
	if ( kill && s_doppler->integer && VectorLengthSquared( velocity ) > 0.0f ) {
		vec3_t out;
		float lena, lenb;
		lena = DistanceSquared( sp_entities[sp_listener.entnum].origin, ent->origin );
		VectorAdd( ent->origin, ent->velocity, out );
		lenb = DistanceSquared( sp_entities[sp_listener.entnum].origin, out );
		if ( lena > 0.0f ) {
			ent->dopplerScale = lenb / ( lena * 100.0f );
			if ( ent->dopplerScale > 1.0f ) {
				ent->doppler = qtrue;
				if ( ent->dopplerScale > MAX_DOPPLER_SCALE )
					ent->dopplerScale = MAX_DOPPLER_SCALE;
			}
		}
	}
	ent->framenum = cls.framecount;
}


static void S_Spatial_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle ) {
	SP_AddLoop( entityNum, origin, velocity, sfxHandle, qtrue, "S_AddLoopingSound" );
}


static void S_Spatial_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle ) {
	SP_AddLoop( entityNum, origin, velocity, sfxHandle, qfalse, "S_AddRealLoopingSound" );
}


static void S_Spatial_UpdateEntityPosition( int entityNum, const vec3_t origin ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "S_UpdateEntityPosition: bad entitynum %i", entityNum );
	}
	VectorCopy( origin, sp_entities[entityNum].origin );
}


static void S_Spatial_Respatialize( int entityNum, const vec3_t head, vec3_t axis[3], int inwater ) {
	spChannel_t *ch;
	spEntity_t *ent;
	int i;

	if ( !sp_started || sp_muted )
		return;
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "S_Respatialize: bad entitynum %i", entityNum );
	}

	sp_listener.entnum = entityNum;
	VectorCopy( head, sp_listener.origin );
	VectorCopy( axis[0], sp_listener.axis[0] );
	VectorCopy( axis[1], sp_listener.axis[1] );
	VectorCopy( axis[2], sp_listener.axis[2] );

	for ( i = 0, ch = sp_channels; i < MAX_CHANNELS; i++, ch++ ) {
		if ( !ch->sfx )
			continue;
		// the listener's own sounds are always full volume and centred
		SP_Spatialize( &ch->spatial, &sp_listener, ch->fixedOrigin ? ch->origin : sp_entities[ch->entnum].origin,
			ch->masterVol, ch->entnum == entityNum );
	}

	for ( i = 0, ent = sp_entities; i < MAX_GENTITIES; i++, ent++ ) {
		if ( ent->active ) {
			if ( ent->playing != ent->sfx ) {
				// new or changed loop: start on the shared cycle, like the classic mixer
				ent->playing = ent->sfx;
				ent->phase = sp_mixtime % ent->sfx->frames;
				ent->spatial.primed = qfalse;
				if ( !ent->spatial.hrtf )
					ent->spatial.hrtf = SPH_CreateVoice();
				SPH_ResetVoice( ent->spatial.hrtf );
			}
			SP_Spatialize( &ent->spatial, &sp_listener, ent->origin, ent->kill ? SP_MASTER_VOL : SP_SPHERE_VOL, qfalse );
		} else if ( ent->playing ) {
			// fade out over the next block instead of cutting off
			ent->spatial.target[0] = ent->spatial.target[1] = 0.0f;
			ent->spatial.monoTarget = 0.0f;
		}
	}
}


static void S_Spatial_Update( int msec ) {
	static float fbuf[SP_RAW_FRAMES * 2];
	static byte pbuf[SP_RAW_FRAMES * 2 * 4];
	qboolean mute;
	int end, n, i;

	if ( !sp_started || sp_muted )
		return;

	if ( s_show->integer == 2 ) {
		int total = 0;
		for ( i = 0; i < MAX_CHANNELS; i++ ) {
			if ( sp_channels[i].sfx ) {
				Com_Printf( "%.3f %.3f %s\n", sp_channels[i].spatial.target[0], sp_channels[i].spatial.target[1], sp_channels[i].sfx->name );
				total++;
			}
		}
		Com_Printf( "----(%i)---- mixed: %i\n", total, sp_mixtime );
	}

	if ( CL_VideoRecording() ) {
		SP_UpdateBackgroundTrack();
		SP_WriteVideoFrame();
		return;
	}

	SP_GetSoundtime();

	// device consumed what we gave it: skip ahead rather than play stale data
	if ( sp_cursor - sp_soundtime < 0 ) {
		sp_underruns++;
		if ( s_spatialShow->integer )
			Com_Printf( "spatial: underrun %i (%i frames late)\n", sp_underruns, sp_soundtime - sp_cursor );
		sp_cursor = sp_soundtime;
	}

	SP_UpdateBackgroundTrack();

	end = sp_soundtime + (int)( s_spatialMixAhead->value * dma.speed );
	if ( end - sp_soundtime > dma.fullsamples - SP_BLOCK )
		end = sp_soundtime + dma.fullsamples - SP_BLOCK;
	n = end - sp_cursor;
	if ( n > SP_RAW_FRAMES )
		n = SP_RAW_FRAMES;

	mute = ( !gw_active && !gw_minimized && s_muteWhenUnfocused->integer ) || ( gw_minimized && s_muteWhenMinimized->integer );

	if ( n > 0 ) {
		SP_Render( fbuf, n );
		SP_Convert( fbuf, n, pbuf, mute );
	}

	SNDDMA_BeginPainting();
	// zero what has already been played, so an underrun plays silence, not old audio
	if ( sp_soundtime - sp_clearedTo > 0 ) {
		int c = sp_soundtime - sp_clearedTo;
		if ( c > dma.fullsamples )
			c = dma.fullsamples;
		SP_RingWrite( sp_soundtime - c, NULL, c );
		sp_clearedTo = sp_soundtime;
	}
	if ( n > 0 ) {
		SP_RingWrite( sp_cursor, pbuf, n );
		sp_cursor += n;
	}
	SNDDMA_Submit();
}


static void S_Spatial_DisableSounds( void ) {
	S_Spatial_StopAllSounds();
	sp_muted = qtrue;
}


static sfxHandle_t S_Spatial_RegisterSound( const char *name, qboolean compressed ) {
	spSfx_t *sfx;

	if ( !sp_started )
		return 0;
	if ( strlen( name ) >= MAX_QPATH ) {
		Com_Printf( "Sound name exceeds MAX_QPATH\n" );
		return 0;
	}

	sfx = SP_FindSfx( name );
	if ( !sfx )
		return 0;
	if ( sfx->defaultSound ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: could not find %s - using default\n", sfx->name );
		return 0;
	}
	return sfx - sp_sfx;
}


static void S_Spatial_BeginRegistration( void ) {
	sp_muted = qfalse;
	if ( sp_numSfx )
		return;
	// handle 0 is the fallback sound
	S_Spatial_RegisterSound( "sound/feedback/hit.wav", qfalse );
}


static void S_Spatial_SoundInfo( void ) {
	Com_Printf( "----- Sound Info -----\n" );
	if ( !sp_started ) {
		Com_Printf( "sound system not started\n" );
	} else {
		Com_Printf( "backend: spatial\n" );
		Com_Printf( "HRTF: %s%s\n", SPH_Describe(), ( SPH_Active() && !s_spatialHRTF->integer ) ? " (disabled by s_spatialHRTF 0)" : "" );
		Com_Printf( "%5d channels\n", dma.channels );
		Com_Printf( "%5d samples\n", dma.samples );
		Com_Printf( "%5d samplebits (%s)\n", dma.samplebits, dma.isfloat ? "float" : "int" );
		Com_Printf( "%5d speed\n", dma.speed );
		Com_Printf( "%5d mix block (frames)\n", SP_BLOCK );
		Com_Printf( "%5d mix ahead (frames)\n", (int)( s_spatialMixAhead->value * dma.speed ) );
		Com_Printf( "%5d underruns\n", sp_underruns );
		if ( dma.driver )
			Com_Printf( "Using %s subsystem\n", dma.driver );
		Com_Printf( sp_music ? "Background file: %s\n" : "No background file.\n", sp_musicLoop );
	}
	Com_Printf( "----------------------\n" );
}


static void S_Spatial_SoundList( void ) {
	int i, total = 0;
	for ( i = 0; i < sp_numSfx; i++ ) {
		const int size = sp_sfx[i].frames * sp_sfx[i].channels * (int)sizeof( short );
		total += size;
		Com_Printf( "%8i %s%s\n", size, sp_sfx[i].name, sp_sfx[i].defaultSound ? " [missing]" : "" );
	}
	Com_Printf( "Total resident: %i\n", total );
}


static void S_Spatial_Shutdown( void ) {
	if ( !sp_started )
		return;

	SNDDMA_Shutdown();
	{
		int i;
		for ( i = 0; i < MAX_CHANNELS; i++ ) {
			SPH_ReleaseVoice( &sp_chanHRTF[i] );
			sp_chanTail[i].hrtf = NULL;
			sp_chanTail[i].tail = qfalse;
		}
		for ( i = 0; i < MAX_GENTITIES; i++ )
			SPH_ReleaseVoice( &sp_entities[i].spatial.hrtf );
	}
	SPH_Shutdown();
	SP_FreeAllSfx();
	free( sp_rsPoly );
	sp_rsPoly = NULL;
	sp_rsIn = sp_rsOut = -1;
	sp_started = qfalse;

	Cmd_RemoveCommand( "s_spatialTest" );

	cls.soundRegistered = qfalse;
}

// =======================================================================
// Offline test render
// =======================================================================

typedef struct {
	const char	*name;
	vec3_t		dir;		// listener frame: forward, left, up
} spPlacement_t;

static void SP_PutLE( byte *p, unsigned int v, int bytes ) {
	int i;
	for ( i = 0; i < bytes; i++ )
		p[i] = ( v >> ( 8 * i ) ) & 0xff;
}

static void SP_WriteWavHeader( fileHandle_t f, int frames, int rate ) {
	byte h[44];
	const int dataBytes = frames * 2 * 2;
	Com_Memcpy( h, "RIFF", 4 );
	SP_PutLE( h + 4, 36 + dataBytes, 4 );
	Com_Memcpy( h + 8, "WAVEfmt ", 8 );
	SP_PutLE( h + 16, 16, 4 );		// fmt chunk size
	SP_PutLE( h + 20, 1, 2 );		// PCM
	SP_PutLE( h + 22, 2, 2 );		// stereo
	SP_PutLE( h + 24, rate, 4 );
	SP_PutLE( h + 28, rate * 4, 4 );
	SP_PutLE( h + 32, 4, 2 );
	SP_PutLE( h + 34, 16, 2 );
	Com_Memcpy( h + 36, "data", 4 );
	SP_PutLE( h + 40, dataBytes, 4 );
	FS_Write( h, sizeof( h ), f );
}


/*
================
S_Spatial_Test_f

s_spatialTest <sound> <output.wav> [distance]

Renders <sound> at fixed positions around a listener at the origin facing
+X, through the same spatialization and voice code as the live mixer, and
writes the result as a 16-bit stereo WAV under the home path. Prints one
line per placement for automated checks. Independent of live sound state,
s_volume and the device clock, so the output is deterministic.
================
*/
static void S_Spatial_Test_f( void ) {
	static const spPlacement_t placements[] = {
		{ "front",		{  1,  0,  0 } },
		{ "right",		{  0, -1,  0 } },
		{ "back",		{ -1,  0,  0 } },
		{ "left",		{  0,  1,  0 } },
		{ "above",		{  0,  0,  1 } },
		{ "below",		{  0,  0, -1 } },
		{ "front_up",	{  0.7071f, 0,  0.7071f } },
		{ "front_down",	{  0.7071f, 0, -0.7071f } },
	};
	const int numPlacements = ARRAY_LEN( placements );
	spListener_t listener;
	spSfx_t *sfx;
	fileHandle_t f;
	void *hrtf;
	float distance, *buf;
	short *pcm;
	int gap, segment, total, p, i;

	if ( Cmd_Argc() < 3 ) {
		Com_Printf( "usage: s_spatialTest <sound> <output.wav> [distance]\n" );
		return;
	}
	if ( !sp_started ) {
		Com_Printf( "s_spatialTest: sound system not started\n" );
		return;
	}

	sfx = SP_FindSfx( Cmd_Argv( 1 ) );
	if ( !sfx || sfx->defaultSound || !sfx->data ) {
		Com_Printf( S_COLOR_YELLOW "s_spatialTest: couldn't load %s\n", Cmd_Argv( 1 ) );
		return;
	}
	distance = ( Cmd_Argc() > 3 ) ? atof( Cmd_Argv( 3 ) ) : 200.0f;

	Com_Memset( &listener, 0, sizeof( listener ) );
	listener.entnum = ENTITYNUM_NONE;
	listener.axis[0][0] = listener.axis[1][1] = listener.axis[2][2] = 1.0f;

	gap = dma.speed / 4;
	segment = ( ( sfx->frames + gap + SP_BLOCK - 1 ) / SP_BLOCK ) * SP_BLOCK;
	total = segment * numPlacements;

	buf = calloc( total * 2, sizeof( float ) );
	pcm = malloc( total * 2 * sizeof( short ) );

	hrtf = SPH_CreateVoice();

	for ( p = 0; p < numPlacements; p++ ) {
		spSpatial_t sp;
		vec3_t origin;
		double phase = 0.0;
		double sum[2] = { 0.0, 0.0 };
		float *seg = buf + p * segment * 2;
		int b;

		VectorScale( placements[p].dir, distance, origin );
		Com_Memset( &sp, 0, sizeof( sp ) );
		sp.hrtf = hrtf;
		SPH_ResetVoice( hrtf );
		SP_Spatialize( &sp, &listener, origin, SP_MASTER_VOL, qfalse );

		// keep processing after the sound ends so the filter tail is included
		for ( b = 0; b < segment; b += SP_BLOCK ) {
			SP_MixVoice( sfx, &phase, 1.0, qfalse, &sp, seg + b * 2 );
		}
		for ( i = 0; i < segment; i++ ) {
			sum[0] += seg[i * 2] * seg[i * 2];
			sum[1] += seg[i * 2 + 1] * seg[i * 2 + 1];
		}
		Com_Printf( "spatialtest: %s rmsL=%.6f rmsR=%.6f\n", placements[p].name,
			sqrt( sum[0] / sfx->frames ), sqrt( sum[1] / sfx->frames ) );
	}

	SPH_ReleaseVoice( &hrtf );

	for ( i = 0; i < total * 2; i++ ) {
		float s = buf[i];
		if ( s > 1.0f ) s = 1.0f;
		else if ( s < -1.0f ) s = -1.0f;
		pcm[i] = LittleShort( (short)lrintf( s * 32767.0f ) );
	}

	f = FS_FOpenFileWrite( Cmd_Argv( 2 ) );
	if ( f == FS_INVALID_HANDLE ) {
		Com_Printf( S_COLOR_YELLOW "s_spatialTest: couldn't open %s for writing\n", Cmd_Argv( 2 ) );
	} else {
		SP_WriteWavHeader( f, total, dma.speed );
		FS_Write( pcm, total * 2 * sizeof( short ), f );
		FS_FCloseFile( f );
		Com_Printf( "spatialtest: wrote %s (%i placements, %i Hz, distance %.0f, %s)\n", Cmd_Argv( 2 ), numPlacements, dma.speed, distance,
			( SPH_Active() && s_spatialHRTF->integer ) ? "HRTF" : "stereo pan" );
	}

	free( pcm );
	free( buf );
}

// =======================================================================
// Init
// =======================================================================

qboolean S_Spatial_Init( soundInterface_t *si ) {
	if ( !si )
		return qfalse;

	s_khz = Cvar_Get( "s_khz", "22", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_khz, "0", "48", CV_INTEGER );
	Cvar_SetDescription( s_khz, "Specifies the sound sampling rate, (8, 11, 22, 44, 48) in kHz. Default value is 22. The spatial backend always uses at least 44 kHz." );

	s_show = Cvar_Get( "s_show", "0", CVAR_CHEAT );
	Cvar_SetDescription( s_show, "Debugging output (used sound files)." );
	s_testsound = Cvar_Get( "s_testsound", "0", CVAR_CHEAT );
	Cvar_SetDescription( s_testsound, "Debugging tool that plays a simple sine wave tone to test the sound system." );

	s_spatialMixAhead = Cvar_Get( "s_spatialMixAhead", "0.04", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_spatialMixAhead, "0.01", "0.25", CV_FLOAT );
	Cvar_SetDescription( s_spatialMixAhead, "Spatial backend: seconds of audio mixed ahead of the playback position. Lower values reduce latency; raise it if you hear crackling (see s_info for underruns)." );
	s_spatialShow = Cvar_Get( "s_spatialShow", "0", 0 );
	Cvar_SetDescription( s_spatialShow, "Spatial backend: print underruns as they happen." );
	s_spatialHRTF = Cvar_Get( "s_spatialHRTF", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_spatialHRTF, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_spatialHRTF, "Spatial backend: 1 = 3D sound with HRTF (headphones, needs Steam Audio), 0 = classic left/right panning." );
	s_steamAudioLib = Cvar_Get( "s_steamAudioLib", SP_STEAMAUDIO_LIB, CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_steamAudioLib, "Spatial backend: Steam Audio library to load for HRTF, a file name (searched by the system and next to the executable) or a full path. Requires snd_restart." );

	// ask the device for a high rate and a small buffer; ignored by drivers that can't
	dmaHints.minSpeed = 48000;
	dmaHints.deviceSamples = 512;

	if ( !SNDDMA_Init() ) {
		Com_Memset( &dmaHints, 0, sizeof( dmaHints ) );
		return qfalse;
	}
	Com_Memset( &dmaHints, 0, sizeof( dmaHints ) );

	if ( dma.speed < SP_MIN_SPEED ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: spatial sound backend needs at least %i Hz, device gave %i Hz\n", SP_MIN_SPEED, dma.speed );
	}

	sp_started = qtrue;
	sp_muted = qtrue;
	sp_numSfx = 0;
	Com_Memset( sp_sfxHash, 0, sizeof( sp_sfxHash ) );
	Com_Memset( &sp_listener, 0, sizeof( sp_listener ) );
	sp_listener.axis[0][0] = sp_listener.axis[1][1] = sp_listener.axis[2][2] = 1.0f;

	sp_dmaBuffers = 0;
	sp_oldSamplePos = 0;
	sp_soundtime = sp_cursor = sp_mixtime = sp_clearedTo = 0;
	sp_stageStart = sp_stageEnd = 0;
	sp_underruns = 0;
	sp_clockValid = qfalse;

	if ( dma.channels == 2 && SPH_Init( s_steamAudioLib->string, dma.speed, SP_BLOCK ) ) {
		int i;
		for ( i = 0; i < MAX_CHANNELS; i++ )
			sp_chanHRTF[i] = SPH_CreateVoice();
	}

	S_Spatial_StopAllSounds();

	Cmd_AddCommand( "s_spatialTest", S_Spatial_Test_f );

	si->Shutdown = S_Spatial_Shutdown;
	si->StartSound = S_Spatial_StartSound;
	si->StartLocalSound = S_Spatial_StartLocalSound;
	si->StartBackgroundTrack = SP_StartBackgroundTrack;
	si->StopBackgroundTrack = SP_StopBackgroundTrack;
	si->RawSamples = SP_RawSamples;
	si->StopAllSounds = S_Spatial_StopAllSounds;
	si->ClearLoopingSounds = S_Spatial_ClearLoopingSounds;
	si->AddLoopingSound = S_Spatial_AddLoopingSound;
	si->AddRealLoopingSound = S_Spatial_AddRealLoopingSound;
	si->StopLoopingSound = S_Spatial_StopLoopingSound;
	si->Respatialize = S_Spatial_Respatialize;
	si->UpdateEntityPosition = S_Spatial_UpdateEntityPosition;
	si->Update = S_Spatial_Update;
	si->DisableSounds = S_Spatial_DisableSounds;
	si->BeginRegistration = S_Spatial_BeginRegistration;
	si->RegisterSound = S_Spatial_RegisterSound;
	si->ClearSoundBuffer = S_Spatial_ClearSoundBuffer;
	si->SoundInfo = S_Spatial_SoundInfo;
	si->SoundList = S_Spatial_SoundList;

	return qtrue;
}
