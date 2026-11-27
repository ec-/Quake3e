/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)

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

#include "client.h"
#include "snd_codec.h"
#include "snd_local.h"
#include "snd_public.h"

cvar_t *s_volume;
cvar_t *s_musicVolume;
cvar_t *s_doppler;
cvar_t *s_muteWhenMinimized;
cvar_t *s_muteWhenUnfocused;

static soundInterface_t si;

dmaHints_t dmaHints;

/*
=================
S_ValidateInterface
=================
*/
static qboolean S_ValidSoundInterface( const soundInterface_t *s )
{
	if( !s->Shutdown ) return qfalse;
	if( !s->StartSound ) return qfalse;
	if( !s->StartLocalSound ) return qfalse;
	if( !s->StartBackgroundTrack ) return qfalse;
	if( !s->StopBackgroundTrack ) return qfalse;
	if( !s->RawSamples ) return qfalse;
	if( !s->StopAllSounds ) return qfalse;
	if( !s->ClearLoopingSounds ) return qfalse;
	if( !s->AddLoopingSound ) return qfalse;
	if( !s->AddRealLoopingSound ) return qfalse;
	if( !s->StopLoopingSound ) return qfalse;
	if( !s->Respatialize ) return qfalse;
	if( !s->UpdateEntityPosition ) return qfalse;
	if( !s->Update ) return qfalse;
	if( !s->DisableSounds ) return qfalse;
	if( !s->BeginRegistration ) return qfalse;
	if( !s->RegisterSound ) return qfalse;
	if( !s->ClearSoundBuffer ) return qfalse;
	if( !s->SoundInfo ) return qfalse;
	if( !s->SoundList ) return qfalse;

	return qtrue;
}


/*
=================
S_StartSound
=================
*/
void S_StartSound( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx )
{
	if( si.StartSound ) {
		si.StartSound( origin, entnum, entchannel, sfx );
	}
}


/*
=================
S_StartLocalSound
=================
*/
void S_StartLocalSound( sfxHandle_t sfx, int channelNum )
{
	if( si.StartLocalSound ) {
		si.StartLocalSound( sfx, channelNum );
	}
}


/*
=================
S_StartBackgroundTrack
=================
*/
void S_StartBackgroundTrack( const char *intro, const char *loop )
{
	if( si.StartBackgroundTrack ) {
		si.StartBackgroundTrack( intro, loop );
	}
}


/*
=================
S_StopBackgroundTrack
=================
*/
void S_StopBackgroundTrack( void )
{
	if( si.StopBackgroundTrack ) {
		si.StopBackgroundTrack( );
	}
}


/*
=================
S_RawSamples
=================
*/
void S_RawSamples (int samples, int rate, int width, int channels,
		   const byte *data, float volume)
{
	if( si.RawSamples ) {
		si.RawSamples( samples, rate, width, channels, data, volume );
	}
}


/*
=================
S_StopAllSounds
=================
*/
void S_StopAllSounds( void )
{
	if( si.StopAllSounds ) {
		si.StopAllSounds();
	}
}


/*
=================
S_ClearLoopingSounds
=================
*/
void S_ClearLoopingSounds( qboolean killall )
{
	if( si.ClearLoopingSounds ) {
		si.ClearLoopingSounds( killall );
	}
}


/*
=================
S_AddLoopingSound
=================
*/
void S_AddLoopingSound( int entityNum, const vec3_t origin,
		const vec3_t velocity, sfxHandle_t sfx )
{
	if( si.AddLoopingSound ) {
		si.AddLoopingSound( entityNum, origin, velocity, sfx );
	}
}


/*
=================
S_AddRealLoopingSound
=================
*/
void S_AddRealLoopingSound( int entityNum, const vec3_t origin,
		const vec3_t velocity, sfxHandle_t sfx )
{
	if( si.AddRealLoopingSound ) {
		si.AddRealLoopingSound( entityNum, origin, velocity, sfx );
	}
}


/*
=================
S_StopLoopingSound
=================
*/
void S_StopLoopingSound( int entityNum )
{
	if( si.StopLoopingSound ) {
		si.StopLoopingSound( entityNum );
	}
}


/*
=================
S_Respatialize
=================
*/
void S_Respatialize( int entityNum, const vec3_t origin,
		vec3_t axis[3], int inwater )
{
	if( si.Respatialize ) {
		si.Respatialize( entityNum, origin, axis, inwater );
	}
}


/*
=================
S_UpdateEntityPosition
=================
*/
void S_UpdateEntityPosition( int entityNum, const vec3_t origin )
{
	if( si.UpdateEntityPosition ) {
		si.UpdateEntityPosition( entityNum, origin );
	}
}


/*
=================
S_Update
=================
*/
void S_Update( int msec )
{
	if ( si.Update ) {
		si.Update( msec );
	}
}


/*
=================
S_DisableSounds
=================
*/
void S_DisableSounds( void )
{
	if( si.DisableSounds ) {
		si.DisableSounds();
	}
}


/*
=================
S_BeginRegistration
=================
*/
void S_BeginRegistration( void )
{
	if ( si.BeginRegistration ) {
		si.BeginRegistration();
	}
}


/*
=================
S_RegisterSound
=================
*/
sfxHandle_t	S_RegisterSound( const char *sample, qboolean compressed )
{
	if ( !sample || !*sample ) {
		Com_Printf( "NULL sound\n" );
		return 0;
	}

	if( si.RegisterSound ) {
		return si.RegisterSound( sample, compressed );
	} else {
		return 0;
	}
}


/*
=================
S_ClearSoundBuffer
=================
*/
void S_ClearSoundBuffer( void )
{
	if( si.ClearSoundBuffer ) {
		si.ClearSoundBuffer();
	}
}


/*
=================
S_SoundInfo
=================
*/
static void S_SoundInfo( void )
{
	if( si.SoundInfo ) {
		si.SoundInfo();
	}
}


/*
=================
S_SoundList
=================
*/
static void S_SoundList( void )
{
	if( si.SoundList ) {
		si.SoundList();
	}
}

//=============================================================================

/*
=================
S_Play_f
=================
*/
static void S_Play_f( void ) {
	int 		i;
	int			c;
	sfxHandle_t	h;

	if( !si.RegisterSound || !si.StartLocalSound ) {
		return;
	}

	c = Cmd_Argc();

	if( c < 2 ) {
		Com_Printf ("Usage: play <sound filename> [sound filename] [sound filename] ...\n");
		return;
	}

	for( i = 1; i < c; i++ ) {
		h = si.RegisterSound( Cmd_Argv(i), qfalse );

		if( h ) {
			si.StartLocalSound( h, CHAN_LOCAL_SOUND );
		}
	}
}


/*
=================
S_Music_f
=================
*/
static void S_Music_f( void ) {
	int		c;

	if( !si.StartBackgroundTrack ) {
		return;
	}

	c = Cmd_Argc();

	if ( c == 2 ) {
		si.StartBackgroundTrack( Cmd_Argv(1), NULL );
	} else if ( c == 3 ) {
		si.StartBackgroundTrack( Cmd_Argv(1), Cmd_Argv(2) );
	} else {
		Com_Printf ("Usage: music <musicfile> [loopfile]\n");
		return;
	}

}


/*
=================
S_StopMusic_f
=================
*/
static void S_StopMusic_f( void )
{
	if ( !si.StopBackgroundTrack )
		return;

	si.StopBackgroundTrack();
}


//=============================================================================

/*
=================
S_Init
=================
*/
/*
=================
Sound profiles

Named sets of sound cvars, from the classic mixer up to full reflections.
Settings that need a restart are applied with snd_restart afterwards.
Paths (s_steamAudioLib, s_hrtfFile) are never changed by a profile.
=================
*/
typedef struct {
	const char	*name;
	const char	*description;
	const char	*cvars;			// "name value name value ..."
} soundProfile_t;

static const soundProfile_t s_profiles[] = {
	{ "classic", "original Quake 3 mixer",
		"s_backend base" },
	{ "pan", "new mixer, stereo panning only (no Steam Audio needed)",
		"s_backend spatial s_spatialHRTF 0 s_occlusion 0 s_reflections 0" },
	{ "hrtf", "3D positioning with HRTF, occlusion, no reflections",
		"s_backend spatial s_spatialHRTF 1 s_occlusion 1 s_reflections 0" },
	{ "reflections-low", "HRTF, occlusion, light reflections (fewer positions, shorter tails)",
		"s_backend spatial s_spatialHRTF 1 s_occlusion 1 s_reflections 1 s_reflectionType convolution "
		"s_reflectionRays 1024 s_reflectionBounces 4 s_reflectionDuration 0.8 s_reflectionInterval 150 s_reflectionSources 6" },
	{ "reflections", "HRTF, occlusion, reflections (default balance)",
		"s_backend spatial s_spatialHRTF 1 s_occlusion 1 s_reflections 1 s_reflectionType convolution "
		"s_reflectionRays 2048 s_reflectionBounces 8 s_reflectionDuration 1.0 s_reflectionInterval 100 s_reflectionSources 12" },
	{ "reflections-high", "HRTF, occlusion, detailed reflections and long tails (heavy)",
		"s_backend spatial s_spatialHRTF 1 s_occlusion 1 s_reflections 1 s_reflectionType convolution "
		"s_reflectionRays 4096 s_reflectionBounces 16 s_reflectionDuration 1.5 s_reflectionInterval 50 s_reflectionSources 24" },
};


static qboolean S_ProfileMatches( const soundProfile_t *p ) {
	char buf[512];
	const char *s, *name, *value;
	Q_strncpyz( buf, p->cvars, sizeof( buf ) );
	s = buf;
	while ( ( name = COM_ParseExt( &s, qfalse ) ) != NULL && name[0] ) {
		char n[MAX_CVAR_VALUE_STRING];
		Q_strncpyz( n, name, sizeof( n ) );
		value = COM_ParseExt( &s, qfalse );
		// a latched value waiting for snd_restart counts as set
		if ( Q_stricmp( Cvar_VariableString( n ), value ) ) {
			cvar_t *cv;
			if ( Cvar_Flags( n ) == CVAR_NONEXISTENT )
				return qfalse;
			cv = Cvar_Get( n, "", 0 );		// exists: just looks it up
			if ( !cv->latchedString || Q_stricmp( cv->latchedString, value ) )
				return qfalse;
		}
	}
	return qtrue;
}


static void S_Profile_f( void ) {
	const soundProfile_t *p = NULL;
	char buf[512];
	const char *s, *name;
	int i;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: s_profile <name>\n" );
		for ( i = 0; i < ARRAY_LEN( s_profiles ); i++ )
			Com_Printf( "%s %-17s %s\n", S_ProfileMatches( &s_profiles[i] ) ? "*" : " ",
				s_profiles[i].name, s_profiles[i].description );
		return;
	}
	for ( i = 0; i < ARRAY_LEN( s_profiles ); i++ ) {
		if ( !Q_stricmp( Cmd_Argv( 1 ), s_profiles[i].name ) )
			p = &s_profiles[i];
	}
	if ( !p ) {
		Com_Printf( "unknown sound profile \"%s\"; s_profile lists them\n", Cmd_Argv( 1 ) );
		return;
	}

	Q_strncpyz( buf, p->cvars, sizeof( buf ) );
	s = buf;
	while ( ( name = COM_ParseExt( &s, qfalse ) ) != NULL && name[0] ) {
		char n[MAX_CVAR_VALUE_STRING];
		Q_strncpyz( n, name, sizeof( n ) );
		Cvar_Set( n, COM_ParseExt( &s, qfalse ) );
	}
	Com_Printf( "sound profile %s: %s\n", p->name, p->description );
	Cbuf_InsertText( "snd_restart\n" );
}


void S_Init( void )
{
	cvar_t		*cv;
	qboolean	started = qfalse;

	Com_Printf( "------ Initializing Sound ------\n" );

	s_volume = Cvar_Get( "s_volume", "0.8", CVAR_ARCHIVE );
	Cvar_CheckRange( s_volume, "0", "1", CV_FLOAT );
	Cvar_SetDescription( s_volume, "Sets master volume for all game audio." );
	s_musicVolume = Cvar_Get( "s_musicVolume", "0.25", CVAR_ARCHIVE );
	Cvar_CheckRange( s_musicVolume, "0", "1", CV_FLOAT );
	Cvar_SetDescription( s_musicVolume, "Sets volume for in-game music only." );
	s_doppler = Cvar_Get( "s_doppler", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_doppler, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_doppler, "Enables doppler effect on moving projectiles." );
	s_muteWhenUnfocused = Cvar_Get( "s_muteWhenUnfocused", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( s_muteWhenUnfocused, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_muteWhenUnfocused, "Mutes all audio while game window is unfocused." );
	s_muteWhenMinimized = Cvar_Get( "s_muteWhenMinimized", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( s_muteWhenMinimized, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_muteWhenMinimized, "Mutes all audio while game is minimized." );

	cv = Cvar_Get( "s_initsound", "1", 0 );
	Cvar_SetDescription( cv, "Whether or not to startup the sound system." );
	if ( !cv->integer ) {
		Com_Printf( "Sound disabled.\n" );
	} else {

		S_CodecInit();

		Cmd_AddCommand( "play", S_Play_f );
		Cmd_AddCommand( "music", S_Music_f );
		Cmd_AddCommand( "stopmusic", S_StopMusic_f );
		Cmd_AddCommand( "s_list", S_SoundList );
		Cmd_AddCommand( "s_stop", S_StopAllSounds );
		Cmd_AddCommand( "s_info", S_SoundInfo );
		Cmd_AddCommand( "s_profile", S_Profile_f );

		cv = Cvar_Get( "s_backend", "base", CVAR_ARCHIVE_ND | CVAR_LATCH );
		Cvar_SetDescription( cv, "Sound backend:\n"
			" base    - classic Quake 3 mixer\n"
			" spatial - new mixer that 3D sound processing is being built on (headphones)\n"
			"Requires snd_restart." );

		if ( !Q_stricmp( cv->string, "spatial" ) ) {
			started = S_Spatial_Init( &si );
			if ( !started ) {
				Com_Printf( S_COLOR_YELLOW "Spatial sound backend failed to start, falling back to base\n" );
			}
		}

		if ( !started ) {
			started = S_Base_Init( &si );
		}

		if ( started ) {
			if( !S_ValidSoundInterface( &si ) ) {
				Com_Error( ERR_FATAL, "Sound interface invalid" );
			}

			S_SoundInfo();
			Com_Printf( "Sound initialization successful.\n" );
		} else {
			Com_Printf( "Sound initialization failed.\n" );
		}
	}

	Com_Printf( "--------------------------------\n");
}


/*
=================
S_Shutdown
=================
*/
void S_Shutdown( void )
{
	if ( si.StopAllSounds ) {
		si.StopAllSounds();
	}

	if ( si.Shutdown ) {
		si.Shutdown();
	}

	Com_Memset( &si, 0, sizeof( soundInterface_t ) );

	Cmd_RemoveCommand( "play" );
	Cmd_RemoveCommand( "music");
	Cmd_RemoveCommand( "stopmusic");
	Cmd_RemoveCommand( "s_list" );
	Cmd_RemoveCommand( "s_stop" );
	Cmd_RemoveCommand( "s_info" );
	Cmd_RemoveCommand( "s_profile" );

	S_CodecShutdown();

	cls.soundStarted = qfalse;
}
