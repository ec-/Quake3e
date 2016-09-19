#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

#include "../client/snd_local.h"
#include "../qcommon/q_shared.h"


#define NUM_SAMPLES 8192
#define NUM_PERIODS 3
#define PERIOD_TIME 20000

/* engine variables */

extern cvar_t *s_khz;

/* pthreads  private variables */

#ifdef USE_ALSA_STATIC

#define _pthread_mutex_init pthread_mutex_init
#define _pthread_mutex_destroy pthread_mutex_destroy
#define	_pthread_mutex_lock pthread_mutex_lock
#define	_pthread_mutex_unlock pthread_mutex_unlock
#define	_pthread_join pthread_join
#define	_pthread_create pthread_create
#define	_pthread_exit pthread_exit

#define	_snd_strerror snd_strerror
#define	_snd_pcm_open snd_pcm_open
#define	_snd_pcm_drop snd_pcm_drop
#define	_snd_pcm_close snd_pcm_close
#define	_snd_pcm_hw_params_sizeof snd_pcm_hw_params_sizeof
#define	_snd_pcm_sw_params_sizeof snd_pcm_sw_params_sizeof
#define	_snd_pcm_hw_params_any snd_pcm_hw_params_any
#define	_snd_pcm_hw_params_set_rate_resample snd_pcm_hw_params_set_rate_resample
#define	_snd_pcm_hw_params_set_access snd_pcm_hw_params_set_access
#define	_snd_pcm_hw_params_set_format snd_pcm_hw_params_set_format
#define	_snd_pcm_hw_params_set_channels snd_pcm_hw_params_set_channels
#define	_snd_pcm_hw_params_set_rate_near snd_pcm_hw_params_set_rate_near
#define	_snd_pcm_hw_params_set_period_time_near snd_pcm_hw_params_set_period_time_near
#define	_snd_pcm_hw_params_set_periods snd_pcm_hw_params_set_periods
#define	_snd_pcm_hw_params_get_period_size snd_pcm_hw_params_get_period_size
#define	_snd_pcm_hw_params snd_pcm_hw_params
#define	_snd_pcm_sw_params_current snd_pcm_sw_params_current
#define	_snd_pcm_sw_params_set_start_threshold snd_pcm_sw_params_set_start_threshold
#define	_snd_pcm_sw_params_set_stop_threshold snd_pcm_sw_params_set_stop_threshold
#define	_snd_pcm_sw_params_set_avail_min snd_pcm_sw_params_set_avail_min
#define	_snd_pcm_sw_params snd_pcm_sw_params
#define	_snd_pcm_start snd_pcm_start
#define	_snd_pcm_prepare snd_pcm_prepare
#define	_snd_pcm_resume snd_pcm_resume
#define	_snd_pcm_wait snd_pcm_wait
#define	__snd_pcm_state snd_pcm_state
#define	_snd_pcm_avail_update snd_pcm_avail_update
#define	_snd_pcm_mmap_begin snd_pcm_mmap_begin
#define	_snd_pcm_mmap_commit snd_pcm_mmap_commit
#define _snd_pcm_writei snd_pcm_writei

#else

static void *t_lib = NULL;

static int (*_pthread_mutex_init)(pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr);
static int (*_pthread_mutex_destroy)(pthread_mutex_t *mutex);
static int (*_pthread_mutex_lock)(pthread_mutex_t *mutex);
static int (*_pthread_mutex_unlock)(pthread_mutex_t *mutex);
static int (*_pthread_join)(pthread_t __th, void **__thread_return);
static int (*_pthread_create)(pthread_t *thread, const pthread_attr_t *attr, void *(*func) (void *), void *arg);
static void (*_pthread_exit)(void *retval);

/* alsa private variables */

static void *a_lib = NULL;

static const char *(*_snd_strerror)(int errnum);
static int (*_snd_pcm_open)(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode);
static int (*_snd_pcm_drop)(snd_pcm_t *pcm);
static int (*_snd_pcm_close)(snd_pcm_t *pcm);
static size_t (*_snd_pcm_hw_params_sizeof)(void);
static size_t (*_snd_pcm_sw_params_sizeof)(void);
static int (*_snd_pcm_hw_params_any)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
static int (*_snd_pcm_hw_params_set_rate_resample)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val);
static int (*_snd_pcm_hw_params_set_access)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_access_t _access);
static int (*_snd_pcm_hw_params_set_format)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_format_t val);
static int (*_snd_pcm_hw_params_set_channels)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val);
static int (*_snd_pcm_hw_params_set_rate_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*_snd_pcm_hw_params_set_period_time_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*_snd_pcm_hw_params_set_periods)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val, int dir);
static int (*_snd_pcm_hw_params_get_period_size)(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *frames, int *dir);
static int (*_snd_pcm_hw_params)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
static int (*_snd_pcm_sw_params_current)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
static int (*_snd_pcm_sw_params_set_start_threshold)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*_snd_pcm_sw_params_set_stop_threshold)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*_snd_pcm_sw_params_set_avail_min)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*_snd_pcm_sw_params)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
static int (*_snd_pcm_start)(snd_pcm_t *pcm);
static int (*_snd_pcm_prepare)(snd_pcm_t *pcm);
static int (*_snd_pcm_resume)(snd_pcm_t *pcm);
static int (*_snd_pcm_wait)(snd_pcm_t *pcm, int timeout);
static snd_pcm_state_t (*__snd_pcm_state)(snd_pcm_t *pcm);
static snd_pcm_sframes_t (*_snd_pcm_avail_update)(snd_pcm_t *pcm);
static int (*_snd_pcm_mmap_begin)(snd_pcm_t *pcm, const snd_pcm_channel_area_t **areas, snd_pcm_uframes_t *offset, snd_pcm_uframes_t *frames);
static snd_pcm_sframes_t (*_snd_pcm_mmap_commit)(snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames);
static snd_pcm_sframes_t (*_snd_pcm_writei)(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);

typedef struct {
	void **symbol;
	const char *name;
} sym_t;

sym_t t_list[] = {
	{ (void**)&_pthread_mutex_init, "pthread_mutex_init" },
	{ (void**)&_pthread_mutex_destroy, "pthread_mutex_destroy" },
	{ (void**)&_pthread_mutex_lock, "pthread_mutex_lock" },
	{ (void**)&_pthread_mutex_unlock, "pthread_mutex_unlock" },
	{ (void**)&_pthread_join, "pthread_join" },
	{ (void**)&_pthread_create, "pthread_create" },
	{ (void**)&_pthread_exit, "pthread_exit" }
};

sym_t a_list[] = {
	{ (void**)&_snd_strerror, "snd_strerror" },
	{ (void**)&_snd_pcm_open, "snd_pcm_open" },
	{ (void**)&_snd_pcm_drop, "snd_pcm_drop" },
	{ (void**)&_snd_pcm_close, "snd_pcm_close" },
	{ (void**)&_snd_pcm_hw_params_sizeof, "snd_pcm_hw_params_sizeof" },
	{ (void**)&_snd_pcm_sw_params_sizeof, "snd_pcm_sw_params_sizeof" },
	{ (void**)&_snd_pcm_hw_params_any, "snd_pcm_hw_params_any" },
	{ (void**)&_snd_pcm_hw_params_set_rate_resample, "snd_pcm_hw_params_set_rate_resample" },
	{ (void**)&_snd_pcm_hw_params_set_access, "snd_pcm_hw_params_set_access" },
	{ (void**)&_snd_pcm_hw_params_set_format, "snd_pcm_hw_params_set_format" },
	{ (void**)&_snd_pcm_hw_params_set_channels, "snd_pcm_hw_params_set_channels" },
	{ (void**)&_snd_pcm_hw_params_set_rate_near, "snd_pcm_hw_params_set_rate_near" },
	{ (void**)&_snd_pcm_hw_params_set_period_time_near, "snd_pcm_hw_params_set_period_time_near" },
	{ (void**)&_snd_pcm_hw_params_set_periods, "snd_pcm_hw_params_set_periods" },
	{ (void**)&_snd_pcm_hw_params_get_period_size, "snd_pcm_hw_params_get_period_size" },
	{ (void**)&_snd_pcm_hw_params, "snd_pcm_hw_params" },
	{ (void**)&_snd_pcm_sw_params_current, "snd_pcm_sw_params_current" },
	{ (void**)&_snd_pcm_sw_params_set_start_threshold, "snd_pcm_sw_params_set_start_threshold" },
	{ (void**)&_snd_pcm_sw_params_set_stop_threshold, "snd_pcm_sw_params_set_stop_threshold" },
	{ (void**)&_snd_pcm_sw_params_set_avail_min, "snd_pcm_sw_params_set_avail_min" },
	{ (void**)&_snd_pcm_sw_params, "snd_pcm_sw_params" },
	{ (void**)&_snd_pcm_start, "snd_pcm_start" },
	{ (void**)&_snd_pcm_prepare, "snd_pcm_prepare" },
	{ (void**)&_snd_pcm_resume, "snd_pcm_resume" },
	{ (void**)&_snd_pcm_wait, "snd_pcm_wait" },
	{ (void**)&__snd_pcm_state, "snd_pcm_state" },
	{ (void**)&_snd_pcm_avail_update, "snd_pcm_avail_update" },
	{ (void**)&_snd_pcm_mmap_begin, "snd_pcm_mmap_begin" },
	{ (void**)&_snd_pcm_mmap_commit, "snd_pcm_mmap_commit" },
	{ (void**)&_snd_pcm_writei, "snd_pcm_writei" }
};

#endif

qboolean alsa_used = qfalse; /* will be checked in oss engine */

pthread_t thread;

static qboolean snd_inited = qfalse;

static const char *device = "default"; /* playback device */

/* we will use static dma buffer */
static unsigned char buffer[NUM_SAMPLES*4];

static unsigned int periods = NUM_PERIODS;

static unsigned int period_time = PERIOD_TIME;  // wishable latency

static snd_pcm_t *handle;

static pthread_mutex_t mutex;

static volatile qboolean snd_loop;

static snd_pcm_sframes_t buffer_pos; // buffer position

void Snd_Memset( void* dest, const int val, const size_t count )
{
    Com_Memset( dest, val, count );
}

void SNDDMA_BeginPainting( void )
{

}

void SNDDMA_Submit( void )
{

}

static void thread_proc_mmap( void );
static void thread_proc_direct( void );

static void UnloadLibs( void )
{
#ifndef USE_ALSA_STATIC
	Sys_UnloadLibrary( a_lib );
	Sys_UnloadLibrary( t_lib );
	a_lib = NULL;
	t_lib = NULL;
#endif
}


qboolean SNDDMA_Init( void )
{
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t period_size;
	unsigned int speed, rrate;
	int err, dir, bps, channels;
	qboolean use_mmap;
	int i;

	if ( snd_inited == qtrue ) 
	{
		return qtrue;
	}

	alsa_used = qfalse;

#ifndef USE_ALSA_STATIC
	if ( t_lib == NULL )
	{
		t_lib = Sys_LoadLibrary( "libpthread.so.0" );
		if ( t_lib == NULL )
		{
			t_lib = Sys_LoadLibrary( "libpthread.so" );
		}
		if ( t_lib == NULL )
		{
			Com_Printf( "Error loading pthread library, disabling ALSA support.\n" );
			return qfalse;
		}
	}

	for ( i = 0 ; i < sizeof(t_list)/sizeof(t_list[0]) ; i++ )
	{
		*t_list[i].symbol = Sys_LoadFunction( t_lib, t_list[i].name );
		if ( *t_list[i].symbol == NULL )
		{
			Com_Printf( "Couldn't find '%s' symbol, disabling ALSA support.\n",
				t_list[i].name );
			UnloadLibs();
			return qfalse;
		}
	}

	if ( a_lib == NULL )
	{
		a_lib = Sys_LoadLibrary( "libasound.so.2" );
		if ( a_lib == NULL )
		{
			a_lib = Sys_LoadLibrary( "libasound.so" );
		}
		if ( a_lib == NULL )
		{
			Com_Printf( "Error loading ALSA library.\n" );
			UnloadLibs();
			return qfalse;
		}
	}

	for ( i = 0 ; i < sizeof(a_list)/sizeof(a_list[0]) ; i++ )
	{
		*a_list[i].symbol = Sys_LoadFunction( a_lib, a_list[i].name );
		if ( *a_list[i].symbol == NULL )
		{
			Com_Printf( "Couldn't find '%s' symbol, disabling ALSA support.\n",
				a_list[i].name );
			UnloadLibs();
			return qfalse;
		}
	}
#endif

    err = _snd_pcm_open( &handle, device, SND_PCM_STREAM_PLAYBACK,
		SND_PCM_NONBLOCK );

    if ( err < 0 )
    {
        Com_Printf( "Playback device open error: %s\n",
			_snd_strerror( err ) );
		UnloadLibs();
        return qfalse;
    }

	hwparams = alloca( _snd_pcm_hw_params_sizeof() );
	swparams = alloca( _snd_pcm_hw_params_sizeof() );

	err = _snd_pcm_hw_params_any( handle, hwparams );
	if ( err < 0 )
	{
		Com_Printf( "Broken configuration for playback: " \
			"no configurations available: %s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	/* set the interleaved read/write format */
	err = _snd_pcm_hw_params_set_access( handle,
		hwparams, SND_PCM_ACCESS_MMAP_INTERLEAVED );
	if ( err < 0 ) {
  	  err = _snd_pcm_hw_params_set_access( handle,
  		hwparams, SND_PCM_ACCESS_RW_INTERLEAVED );
  	  use_mmap = qfalse;
  	} else {
	  use_mmap = qtrue;
  	}

	if ( err < 0 )
	{
		Com_Printf("Access type not available for playback: " \
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}
	
	if ( use_mmap ) {
	  Com_Printf( "Use mmap access\n" );
	} else {
	  Com_Printf( "Use direct access\n" );
	}

    /* set hw resampling */
	err = _snd_pcm_hw_params_set_rate_resample( handle, hwparams, 1 );
	if ( err < 0 )
	{
		Com_Printf( "Resampling setup failed for playback: " \
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	/* set the sample format */
	bps = 16;
	err = _snd_pcm_hw_params_set_format( handle, hwparams, SND_PCM_FORMAT_S16 );
	if ( err < 0 )
	{
		bps = 8;
		err = _snd_pcm_hw_params_set_format( handle, hwparams, SND_PCM_FORMAT_S8 );
	}
	if ( err < 0 )
	{
		Com_Printf( "Sample format not available for playback: " \
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	channels = 2;
	/* set the count of channels */
	err = _snd_pcm_hw_params_set_channels( handle, hwparams, channels );
	if ( err < 0 )
	{
		channels = 1;
		err = _snd_pcm_hw_params_set_channels( handle, hwparams, channels );
	}
	if ( err < 0 )
	{
		err = _snd_pcm_hw_params_set_channels( handle, hwparams, channels );
		Com_Printf( "Channels count (%i) not available for playbacks: " \
			"%s\n", channels, _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	switch ( s_khz->integer )
	{
		case 48: speed = 48000; break;
		case 44: speed = 44100; break;
		case 11: speed = 11025; break;
		case 22:
		default: speed = 22050; break;
	};

	rrate = speed;
	err = _snd_pcm_hw_params_set_rate_near( handle, hwparams, &rrate, 0 );
	if ( err < 0 )
	{
		Com_Printf("Rate %iHz not available for playback: " \
			"%s\n", speed, _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}
	if ( rrate != speed )
	{
		Com_Printf( "Rate doesn't match (requested %iHz, get %iHz)\n",
			speed, err );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	/* set the period time */
	err = _snd_pcm_hw_params_set_period_time_near( handle, hwparams,
			&period_time, &dir );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set period time %i for playback: " \
			"%s\n", period_time, _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	err = _snd_pcm_hw_params_set_periods( handle, hwparams, periods, 0 );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set periods (%i): %s",
			periods, _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	/* get period size */
	err = _snd_pcm_hw_params_get_period_size( hwparams, &period_size, &dir );
	if ( err < 0 )
	{
		Com_Printf("Unable to get period size for playback: %s\n",
			_snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	/* write the parameters to device */
	err = _snd_pcm_hw_params( handle, hwparams );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set hw params for playback: " \
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	err = _snd_pcm_sw_params_current( handle, swparams );
	if ( err < 0 )
	{
		Com_Printf( "Unable to determine current swparams for playback: "\
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	err = _snd_pcm_sw_params_set_start_threshold( handle,
		swparams, 1 /*period_size*/ );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set start threshold mode for playback: " \
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	/* disable XRUN */
	err = _snd_pcm_sw_params_set_stop_threshold( handle, swparams, -1 );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set stop threshold for playback: " \
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	err = _snd_pcm_sw_params_set_avail_min( handle, swparams, period_size );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set avail min for playback: " \
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

	err = _snd_pcm_sw_params( handle, swparams );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set sw params for playback: " \
			"%s\n", _snd_strerror( err ) );
		_snd_pcm_close( handle );
		UnloadLibs();
		return qfalse;
	}

#ifdef INT
	Com_Printf( "period_time=%i\n", period_time );
	Com_Printf( "period_size=%i\n", (int)period_size );
#endif

    dma.channels = channels;
    dma.speed = speed;
    dma.samples = NUM_SAMPLES;
    dma.samplebits = bps;
    dma.submission_chunk = 1;
    dma.buffer = buffer;

    memset( buffer, 0, sizeof( buffer ) );

    _pthread_mutex_init( &mutex, NULL );

    snd_inited = qtrue;
    snd_loop = qtrue;

     /* will be unlocked after thread creation */
    _pthread_mutex_lock( &mutex );
    
	if ( use_mmap ==  qfalse )
  	  err = _pthread_create( &thread, NULL, (void*)&thread_proc_direct, NULL );
  	else
  	  err = _pthread_create( &thread, NULL, (void*)&thread_proc_mmap, NULL );

    if ( err != 0 )
    {
	Com_Printf( "Error creating sound thread (%i)\n", err );
	_pthread_mutex_unlock( &mutex );
	_snd_pcm_close( handle );
	UnloadLibs();
	return qfalse;
    }

    /* wait for thread creation */
    _pthread_mutex_lock( &mutex );
    _pthread_mutex_unlock( &mutex );

    alsa_used = qtrue;

    return qtrue;
}

void SNDDMA_Shutdown( void )
{
	if ( snd_inited == qfalse )
		return;

	snd_loop = qfalse;

	/* wait for thread loop exit */
	_pthread_join( thread, NULL );

    _snd_pcm_drop( handle );
    _snd_pcm_close( handle );

	snd_inited = qfalse;

	_pthread_mutex_destroy( &mutex );

	UnloadLibs();
}

void print_state( snd_pcm_state_t state )
{
	switch( state )
	{
	case(SND_PCM_STATE_OPEN):     Com_Printf("SND_PCM_STATE_OPEN\n");     break;
	case(SND_PCM_STATE_SETUP):    Com_Printf("SND_PCM_STATE_SETUP\n");    break;
	case(SND_PCM_STATE_PREPARED): Com_Printf("SND_PCM_STATE_PREPARED\n"); break;
	case(SND_PCM_STATE_RUNNING):  Com_Printf("SND_PCM_STATE_RUNNING\n");  break;
	case(SND_PCM_STATE_XRUN): 	  Com_Printf("SND_PCM_STATE_XRUN\n");     break;
	case(SND_PCM_STATE_DRAINING): Com_Printf("SND_PCM_STATE_DRAINING\n"); break;
	case(SND_PCM_STATE_PAUSED):   Com_Printf("SND_PCM_STATE_PAUSED\n");   break;
	case(SND_PCM_STATE_SUSPENDED):Com_Printf("SND_PCM_STATE_SUSPENDED\n");break;
	case(SND_PCM_STATE_DISCONNECTED):Com_Printf("SND_PCM_STATE_DISCONNECTED\n");break;
	};
}

static int xrun_recovery( snd_pcm_t *handle, int err )
{
	if ( err == -EPIPE ) /* underrun */
	{
		err = _snd_pcm_prepare( handle );
		if ( err < 0 )
		{
			Com_Printf( "Can't recovery from underrun, prepare failed: " \
				"%s\n", _snd_strerror( err ) );
			return err;
		}
		return 0;
	}
	else if ( err == -ESTRPIPE )
	{
		int tries = 0;
		/* wait until the suspend flag is released */
		while ( ( err = _snd_pcm_resume( handle ) ) == -EAGAIN ) 
		{
			usleep( period_time );
			if ( tries++ < 16 )
			{
				break;
			}
		}
		if ( err < 0 )
		{
			err = _snd_pcm_prepare( handle );
			if ( err < 0 )
			{
				Com_Printf( "Can't recovery from suspend, prepare failed: " \
					"%s\n", _snd_strerror( err ) );
				return err;
			}
		}
		return 0;
	}
//	Com_Printf( "error: %i\n", err );
	return err;
}


static int restore_transfer( void )
{
	snd_pcm_state_t state;
	int err;

	state = __snd_pcm_state( handle );

	if ( state == SND_PCM_STATE_XRUN )
	{
		//print_state( state );
		err = xrun_recovery( handle, -EPIPE );
		if ( err < 0 )
		{
			Com_Printf( "XRUN recovery failed: %s\n",
				_snd_strerror( err ) );
			return err;
		}
		buffer_pos = 0;
	}
	else if ( state == SND_PCM_STATE_SUSPENDED )
	{
		print_state( state );
		err = xrun_recovery( handle, -ESTRPIPE );
		if ( err < 0 )
		{
			Com_Printf( "SUSPEND recovery failed: %s\n", 
				_snd_strerror( err ) );
			return err;
		}
		buffer_pos = 0;
	}
	return 0;
};

/*
==============
SNDDMA_GetDMAPos
return the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
int SNDDMA_GetDMAPos( void )
{
	int samples;

	if ( snd_inited == qfalse )
		return 0;

	_pthread_mutex_lock( &mutex );

	if ( dma.samples )
		samples = (buffer_pos) % dma.samples;
	else
		samples = 0;

	_pthread_mutex_unlock( &mutex );

    return samples;
}

static void thread_proc_mmap( void )
{
	const snd_pcm_channel_area_t *areas;
	snd_pcm_sframes_t commitres;
	snd_pcm_uframes_t frames;
	snd_pcm_uframes_t offset;
	snd_pcm_sframes_t avail;
	snd_pcm_state_t state;
	unsigned char *addr;
	int frame_sz;	// in bytes
	int buffer_sz;	// in bytes
	int sz0, sz1;
	int err, p;

	frame_sz = dma.channels * dma.samplebits / 8;
	buffer_sz = dma.samples * dma.samplebits / 8;

	buffer_pos = 0;

	_pthread_mutex_unlock( &mutex ); // thread is running

	while ( snd_loop == qtrue )
	{
		if ( restore_transfer() < 0 )
			break;

		_snd_pcm_wait( handle, period_time * 2 );
		avail = _snd_pcm_avail_update( handle );

		if ( avail < 0 )
			continue;

		state = __snd_pcm_state( handle );

		if ( state == SND_PCM_STATE_PREPARED )
		{
			_snd_pcm_start( handle );
			avail = _snd_pcm_avail_update( handle );
			buffer_pos = 0;
		}

		if ( avail < 0 )
			continue;

		_pthread_mutex_lock( &mutex );

		frames = avail;
		err = _snd_pcm_mmap_begin( handle, &areas, &offset, &frames );

		if ( err < 0 )
		{
			if ( (err = xrun_recovery( handle, err ) ) < 0 )
			{
				Com_Printf( "MMAP begin error: %s\n",
				_snd_strerror( err ) );
				_pthread_mutex_unlock( &mutex );
				continue;
			}
		}

		addr = areas[0].addr;
		addr += offset * frame_sz;
		sz0 = frames * frame_sz;

		p = buffer_pos * (dma.samplebits / 8);
		while ( sz0 > 0 )
		{
			sz1 = sz0;
			if ( p + sz1 > buffer_sz )
				sz1 = buffer_sz - p;
			memcpy( addr, dma.buffer + p, sz1 );
			p = (p + sz1) % buffer_sz;
			addr += sz1;
			sz0 -= sz1;
		}
		buffer_pos = p / ( dma.samplebits / 8 );

		commitres = _snd_pcm_mmap_commit( handle, offset, frames );
		if ( commitres < 0 || commitres != frames )
		{
			if ( ( err = xrun_recovery( handle, commitres >= 0 ? -EPIPE : commitres ) ) < 0 )
			{
				Com_Printf( "MMAP commit error: %s\n", _snd_strerror( err ) );
				_pthread_mutex_unlock( &mutex );
				break;
			}
		}
		_pthread_mutex_unlock( &mutex );
	}

	_pthread_exit( 0 );
}

static void thread_proc_direct( void )
{
    snd_pcm_uframes_t size;
    snd_pcm_uframes_t pos;
    snd_pcm_sframes_t avail, x;
    snd_pcm_state_t state;

    int frame_sz;
    int err;

    /* buffer size in full samples */
    size = dma.samples / dma.channels;
    frame_sz = dma.samplebits / 8 * dma.channels;

    buffer_pos = 0;

    _pthread_mutex_unlock( &mutex ); /* thread is running */

    while ( snd_loop == qtrue )
    {
	if ( restore_transfer() < 0 )
	    break;

		_snd_pcm_wait( handle, period_time * 2 );
		avail = _snd_pcm_avail_update( handle );

		if ( avail < 0 )
			continue;

//		Com_Printf( "write\n" );

		if ( snd_loop == qfalse )
			break;

		state = __snd_pcm_state( handle );

		if ( state == SND_PCM_STATE_PREPARED )
		{
			_snd_pcm_start( handle );
			avail = _snd_pcm_avail_update( handle );
			//_snd_pcm_writei( handle, dma.buffer, size );
			buffer_pos = 0;
		}

		if ( avail < 0 )
		{
			continue;
		}

		if ( avail == 0 )
		{
		  continue;
		}

		_pthread_mutex_lock( &mutex );

		// buffer position in full samples
		pos = buffer_pos / dma.channels;

		while ( avail > 0 ) {
			x = avail;
			if ( pos + x > size  ) {
				x =  size - pos;
			}
			err = _snd_pcm_writei( handle, dma.buffer + pos * frame_sz, x );
			if ( err >= 0 ) {
				pos = (pos + x) % size;
				avail -= x;
			} else {
				avail = 0;
			}
		}

		// buffer pos in mono samples again
		buffer_pos = pos * dma.channels;

		_pthread_mutex_unlock( &mutex );
	}

	_pthread_exit( 0 );
}
