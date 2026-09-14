/* gcc -O2 -fno-strict-aliasing -DUSE_ALSA_STATIC
 * -Werror=incompatible-pointer-types -ffunction-sections -fdata-sections
 * tools/tests/audio_threads.c -Wl,--gc-sections -Wl,--wrap=snd_pcm_mmap_commit
 * -Wl,--wrap=snd_pcm_writei -lasound -pthread -o /tmp/audio-test
 * timeout 10 /tmp/audio-test
 */
#include "../../code/unix/linux_snd.c"
#include <assert.h>

typedef void *(*thread_callback_t)( void * );
static const thread_callback_t callbacks[] = { thread_proc_mmap, thread_proc_direct };

dma_t dma;
static cvar_t device, rate;
cvar_t *s_device = &device;
cvar_t *s_khz = &rate;
static unsigned int mmap_writes, direct_writes;

void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
snd_pcm_sframes_t __real_snd_pcm_mmap_commit( snd_pcm_t *, snd_pcm_uframes_t, snd_pcm_uframes_t );
snd_pcm_sframes_t __wrap_snd_pcm_mmap_commit( snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames )
{
	snd_pcm_sframes_t result = __real_snd_pcm_mmap_commit( pcm, offset, frames );
	if ( result > 0 )
		mmap_writes++;
	return result;
}
snd_pcm_sframes_t __real_snd_pcm_writei( snd_pcm_t *, const void *, snd_pcm_uframes_t );
snd_pcm_sframes_t __wrap_snd_pcm_writei( snd_pcm_t *pcm, const void *data, snd_pcm_uframes_t frames )
{
	snd_pcm_sframes_t result = __real_snd_pcm_writei( pcm, data, frames );
	if ( result > 0 )
		direct_writes++;
	return result;
}

int main( void )
{
	assert( callbacks[0] && callbacks[1] );
	device.string = (char *)"null";
	rate.integer = 22;
	assert( setup_ALSA( SND_MODE_MMAP ) );
	usleep( 50000 );
	assert( SNDDMA_GetDMAPos() >= 0 );
	SNDDMA_Shutdown();
	assert( mmap_writes > 0 );
	assert( setup_ALSA( SND_MODE_DIRECT ) );
	usleep( 50000 );
	assert( SNDDMA_GetDMAPos() >= 0 );
	SNDDMA_Shutdown();
	assert( direct_writes > 0 );
	puts( "PASS: ALSA null sink received MMAP and DIRECT samples; both threads joined" );
	return 0;
}
