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
// snd_local.h -- private sound definitions


#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "snd_public.h"

#define	PAINTBUFFER_SIZE		4096					// this is in samples

#define SND_CHUNK_SIZE			1024					// samples
#define SND_CHUNK_SIZE_FLOAT	(SND_CHUNK_SIZE/2)		// floats
#define SND_CHUNK_SIZE_BYTE		(SND_CHUNK_SIZE*2)		// floats

typedef struct {
	int			left;	// the final values will be clamped to +/- 0x00ffff00 and shifted down
	int			right;
} portable_samplepair_t;

typedef struct adpcm_state {
	short	sample;		/* Previous output value */
	char	index;		/* Index into stepsize table */
} adpcm_state_t;

typedef	struct sndBuffer_s {
	short					sndChunk[SND_CHUNK_SIZE];
	struct sndBuffer_s		*next;
	int						size;
	adpcm_state_t			adpcm;
} sndBuffer;

typedef struct sfx_s {
	sndBuffer		*soundData;
	qboolean		defaultSound;			// couldn't be loaded, so use buzz
	qboolean		inMemory;				// not in Memory
	qboolean		soundCompressed;		// not in Memory
	int				soundCompressionMethod;
	int 			soundLength;
	int				soundChannels;
	char 			soundName[MAX_QPATH];
	int				lastTimeUsed;
	struct sfx_s	*next;
} sfx_t;

typedef struct {
	unsigned int channels;
	unsigned int samples;				// mono samples in buffer
	int			fullsamples;			// samples with all channels in buffer (samples divided by channels)
	int			submission_chunk;		// don't mix less than this #
	int			samplebits;
	int			isfloat;
	int			speed;
	byte		*buffer;
	const char	*driver;
} dma_t;

extern byte *dma_buffer2;

#define START_SAMPLE_IMMEDIATE	0x7fffffff

#define MAX_DOPPLER_SCALE 50.0f //arbitrary

typedef struct loopSound_s {
	vec3_t		origin;
	vec3_t		velocity;
	sfx_t		*sfx;
	int			mergeFrame;
	qboolean	active;
	qboolean	kill;
	qboolean	doppler;
	float		dopplerScale;
	float		oldDopplerScale;
	int			framenum;
} loopSound_t;

typedef struct
{
	int			allocTime;
	int			startSample;	// START_SAMPLE_IMMEDIATE = set immediately on next mix
	int			entnum;			// to allow overriding a specific sound
	int			entchannel;		// to allow overriding a specific sound
	int			leftvol;		// 0-255 volume after spatialization
	int			rightvol;		// 0-255 volume after spatialization
	int			master_vol;		// 0-255 volume before spatialization
	float		dopplerScale;
	float		oldDopplerScale;
	vec3_t		origin;			// only use if fixed_origin is set
	qboolean	fixed_origin;	// use origin instead of fetching entnum's origin
	sfx_t		*thesfx;		// sfx structure
	qboolean	doppler;
} channel_t;


#define WAV_FORMAT_PCM			0x0001
#define WAVE_FORMAT_IEEE_FLOAT	0x0003

typedef struct {
	int			format;
	int			rate;
	int			width;
	int			channels;
	int			samples;
	int			dataofs;		// chunk starts this many bytes from file start
} wavinfo_t;

// Interface between Q3 sound "api" and the sound backend
typedef struct
{
	void (*Shutdown)(void);
	void (*StartSound)( const vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx );
	void (*StartLocalSound)( sfxHandle_t sfx, int channelNum );
	void (*StartBackgroundTrack)( const char *intro, const char *loop );
	void (*StopBackgroundTrack)( void );
	void (*RawSamples)(int samples, int rate, int width, int channels, const byte *data, float volume);
	void (*StopAllSounds)( void );
	void (*ClearLoopingSounds)( qboolean killall );
	void (*AddLoopingSound)( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
	void (*AddRealLoopingSound)( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
	void (*StopLoopingSound)(int entityNum );
	void (*Respatialize)( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater );
	void (*UpdateEntityPosition)( int entityNum, const vec3_t origin );
	void (*Update)( int msec );
	void (*DisableSounds)( void );
	void (*BeginRegistration)( void );
	sfxHandle_t (*RegisterSound)( const char *sample, qboolean compressed );
	void (*ClearSoundBuffer)( void );
	void (*SoundInfo)( void );
	void (*SoundList)( void );
} soundInterface_t;


/*
====================================================================

  SYSTEM SPECIFIC FUNCTIONS

====================================================================
*/

// initializes cycling through a DMA buffer and returns information on it
qboolean SNDDMA_Init(void);

// gets the current DMA position
int		SNDDMA_GetDMAPos(void);

// shutdown the DMA xfer.
void	SNDDMA_Shutdown(void);

void	SNDDMA_BeginPainting (void);

void	SNDDMA_Submit(void);

//====================================================================

#define	MAX_CHANNELS			96

extern	channel_t   s_channels[MAX_CHANNELS];
extern	channel_t   loop_channels[MAX_CHANNELS];
extern	int		numLoopChannels;

extern	int		s_soundtime;
extern	int		s_paintedtime;
extern	int		s_rawend;
extern	vec3_t	listener_forward;
extern	vec3_t	listener_right;
extern	vec3_t	listener_up;
extern	dma_t	dma;

#define	MAX_RAW_SAMPLES	16384
extern	portable_samplepair_t	s_rawsamples[MAX_RAW_SAMPLES];

extern cvar_t *s_volume;
extern cvar_t *s_musicVolume;
extern cvar_t *s_doppler;
extern cvar_t *s_muteWhenUnfocused;
extern cvar_t *s_muteWhenMinimized;

extern cvar_t *s_testsound;

qboolean S_LoadSound( sfx_t *sfx );

void		SND_free(sndBuffer *v);
sndBuffer*	SND_malloc( void );
void		SND_setup( void );
void		SND_shutdown( void );

void S_PaintChannels(int endtime);

// spatializes a channel
void S_Spatialize(channel_t *ch);

// adpcm functions
int  S_AdpcmMemoryNeeded( const wavinfo_t *info );
void S_AdpcmEncodeSound( sfx_t *sfx, short *samples );
void S_AdpcmGetSamples(sndBuffer *chunk, short *to);

// wavelet function

#define SENTINEL_MULAW_ZERO_RUN 127
#define SENTINEL_MULAW_FOUR_BIT_RUN 126

void S_FreeOldestSound( void );

#define	NXStream byte

void encodeWavelet(sfx_t *sfx, short *packets);
void decodeWavelet( sndBuffer *stream, short *packets);

void encodeMuLaw( sfx_t *sfx, short *packets);
extern short mulawToShort[256];

extern short *sfxScratchBuffer;
extern sfx_t *sfxScratchPointer;
extern int	   sfxScratchIndex;

qboolean S_Base_Init( soundInterface_t *si );
qboolean S_Spatial_Init( soundInterface_t *si );

// HRTF for the spatial backend (snd_steamaudio.c). A voice is one sound's
// binaural filter state; blocks are frameSize frames of mono input.
qboolean	SPH_Init( const char *libName, const char *sofaPath, int rate, int frameSize );
float		SPH_FrontResponse( void );
void		SPH_Shutdown( void );
qboolean	SPH_Active( void );
const char	*SPH_Describe( void );
void		*SPH_CreateVoice( void );
void		SPH_ReleaseVoice( void **voice );
void		SPH_ResetVoice( void *voice );
void		SPH_Process( void *voice, float *mono, const vec3_t dir, float *outL, float *outR );
qboolean	SPH_Tail( void *voice, float *outL, float *outR );

// level geometry for reflections (snd_geometry.c), in game coordinates
typedef enum {
	SP_MAT_STONE,			// default: concrete, brick, rock
	SP_MAT_METAL,
	SP_MAT_WOOD,
	SP_MAT_GLASS,
	SP_MAT_TILE,
	SP_MAT_SOFT,			// earth, grass, organic
	SP_MAT_FABRIC,
	SP_NUM_MATERIALS
} spMaterial_t;

typedef struct {
	char	name[MAX_QPATH];
	vec3_t	*verts;
	int		numVerts;
	int		(*tris)[3];			// wound so the right-hand normal faces out of the surface
	int		*materials;			// spMaterial_t per triangle
	int		numTris;
} spGeometry_t;

spGeometry_t	*SP_LoadMapGeometry( const char *bspName );
void			SP_FreeGeometry( spGeometry_t *geom );

// reflections through Steam Audio (snd_steamaudio.c). Each slot is one
// sound position; the mixer feeds each slot's mono input per block and
// gets one stereo reflection signal for all of them.
typedef struct {
	int			numSlots;			// sound positions simulated at once
	float		duration;			// seconds of impulse response simulated
	qboolean	convolution;		// convolve the whole response, or only the start (hybrid)
	int			rays;
	qboolean	embree;				// Intel Embree ray tracer instead of Steam Audio's own
} sphReflSettings_t;

qboolean	SPH_ReflectionsInit( const sphReflSettings_t *settings );
void		SPH_ReflectionsSetLive( int bounces, int intervalMs );
void		SPH_ReflectionsShutdown( void );
qboolean	SPH_ReflectionsActive( void );
const char	*SPH_ReflectionsDescribe( void );
void		SPH_ReflectionsSetGeometry( spGeometry_t *geom );	// takes ownership; NULL = no level
void		SPH_ReflectionsSetSync( qboolean sync );
void		SPH_ReflectionsSimulate( void );
void		SPH_ReflectionsUpdate( const vec3_t listener, vec3_t axis[3], const vec3_t *origins, const qboolean *active );
void		SPH_ReflectionsReset( int slot );
qboolean	SPH_ReflectionsProcess( int slot, const float *mono );
void		SPH_ReflectionsOutput( float *outL, float *outR, qboolean binaural );

// Optional requests from the active backend to the device layer, read by
// SNDDMA_Init. Zero means "no preference". Drivers may ignore them.
typedef struct {
	int		minSpeed;			// lowest acceptable sample rate in Hz
	int		deviceSamples;		// preferred device buffer size in frames
} dmaHints_t;

extern dmaHints_t dmaHints;
