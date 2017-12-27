#include "../client/client.h"
#include "unix_glw.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>

#include <X11/extensions/xf86vmode.h>

// gamma value of the X display before we start playing with it
static XF86VidModeGamma vidmode_InitialGamma;

static XF86VidModeModeInfo **vidmodes = NULL;

static void *v_lib = NULL;

Bool (*_XF86VidModeQueryExtension)( Display *dpy, int *event_base, int *error_base );
Bool (*_XF86VidModeQueryVersion)( Display *dpy, int *majorVersion, int *minorVersion );
Bool (*_XF86VidModeGetModeLine)( Display *dpy, int screen, int *dotclock, XF86VidModeModeLine *modeline );
Bool (*_XF86VidModeGetAllModeLines)( Display *dpy, int screen, int *modecount, XF86VidModeModeInfo ***modelinesPtr );
Bool (*_XF86VidModeSwitchToMode)( Display *dpy, int screen, XF86VidModeModeInfo *modeline );
Bool (*_XF86VidModeSetViewPort)( Display *dpy, int screen, int x, int y );
Bool (*_XF86VidModeGetGamma)( Display *dpy, int screen, XF86VidModeGamma *Gamma );
Bool (*_XF86VidModeSetGamma)( Display *dpy, int screen, XF86VidModeGamma *Gamma );
Bool (*_XF86VidModeGetGammaRampSize)( Display *dpy, int screen, int *size );
Bool (*_XF86VidModeSetGammaRamp)( Display *dpy, int screen, int size, unsigned short *r, unsigned short *g, unsigned short *b );

static sym_t v_list[] =
{
	{ (void**)&_XF86VidModeQueryExtension, "XF86VidModeQueryExtension" },
	{ (void**)&_XF86VidModeQueryVersion, "XF86VidModeQueryVersion" },
	{ (void**)&_XF86VidModeGetModeLine, "XF86VidModeGetModeLine" },
	{ (void**)&_XF86VidModeGetAllModeLines, "XF86VidModeGetAllModeLines" },
	{ (void**)&_XF86VidModeSwitchToMode, "XF86VidModeSwitchToMode" },
	{ (void**)&_XF86VidModeSetViewPort, "XF86VidModeSetViewPort" },
	{ (void**)&_XF86VidModeGetGamma, "XF86VidModeGetGamma" },
	{ (void**)&_XF86VidModeSetGamma, "XF86VidModeSetGamma" },
	{ (void**)&_XF86VidModeGetGammaRampSize, "XF86VidModeGetGammaRampSize" },
	{ (void**)&_XF86VidModeSetGammaRamp, "XF86VidModeSetGammaRamp" }
};

qboolean VidMode_Init( void )
{
	int ver_major = 0;
	int ver_minor = 0;
	int event_base, error_base;
	int i;

	glw_state.vidmode_ext = qfalse;
	glw_state.vidmode_gamma = qfalse;

	if ( !dpy )
		return qfalse;

	if ( v_lib == NULL )
	{
		v_lib = Sys_LoadLibrary( "libXxf86vm.so.1" );
		if ( v_lib == NULL )
		{
			v_lib = Sys_LoadLibrary( "libXxf86vm.so" );
		}
		if ( v_lib == NULL )
		{
			Com_Printf( "...error loading libXxf86vm\n" );
			goto __fail;
		}
	}

	for ( i = 0 ; i < ARRAY_LEN( v_list ); i++ )
	{
		*v_list[ i ].symbol = Sys_LoadFunction( v_lib, v_list[ i ].name );
		if ( *v_list[ i ].symbol == NULL )
		{
			Com_Printf( "...couldn't find '%s' in libXxf86vm\n", v_list[ i ].name );
			goto __fail;
		}
	}
	
	if ( !_XF86VidModeQueryExtension( dpy, &event_base, &error_base ) || !_XF86VidModeQueryVersion( dpy, &ver_major, &ver_minor ) )
	{
		Com_Printf( "...VidMode extension is not available.\n" );
		goto __fail;
	}

	Com_Printf( "...VidMode extension version %i.%i detected.\n", ver_major, ver_minor );

	glw_state.vidmode_ext = qtrue;
	
	if ( glw_state.desktop_ok == qfalse )
	{
		XF86VidModeModeLine c;
		int n;
		if ( _XF86VidModeGetModeLine( dpy, scrnum, &n, &c ) )
		{
			glw_state.desktop_width = c.hdisplay;
			glw_state.desktop_height = c.vdisplay;
			glw_state.desktop_ok = qtrue;
		}
		else
		{
			Com_Printf( "XF86VidModeGetModeLine failed.\n" );
		}
	}

	Com_Printf( "desktop width:%i height:%i\n",
		glw_state.desktop_width, glw_state.desktop_height );

	/* Minimum extension version required */
	#define GAMMA_MINMAJOR 2
	#define GAMMA_MINMINOR 0

	if ( ver_major < GAMMA_MINMAJOR || (ver_major == GAMMA_MINMAJOR && ver_minor < GAMMA_MINMINOR) )
	{
		Com_Printf( "...VidMode gamma extension not supported.\n" );
	}
	else
	{
		_XF86VidModeGetGamma( dpy, scrnum, &vidmode_InitialGamma );
		Com_Printf( "...using VidMode gamma extension.\n" );
		glw_state.vidmode_gamma = qtrue;
	}

	return qtrue;

__fail:
	VidMode_Done();
	return qfalse;
}


void VidMode_Done( void )
{
	if ( v_lib )
	{
		Sys_UnloadLibrary( v_lib );
		v_lib = NULL;
	}

	glw_state.vidmode_ext = qfalse;
	glw_state.vidmode_gamma = qfalse;
}


void VidMode_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
	unsigned short table[3][4096];
	int size;

	if ( !glw_state.vidmode_ext )
		return;

	_XF86VidModeGetGammaRampSize( dpy, scrnum, &size );

	if ( BuildGammaRampTable( red, green, blue, size, table ) )
	{
		_XF86VidModeSetGammaRamp( dpy, scrnum, size, table[0], table[1], table[2] );
		glw_state.gammaSet = qtrue;
	}
}


void VidMode_RestoreGamma( void )
{
	if ( !glw_state.vidmode_ext )
		return;

	if ( glw_state.vidmode_gamma )
	{
		_XF86VidModeSetGamma( dpy, scrnum, &vidmode_InitialGamma );
	}
}


qboolean VidMode_SetMode( int *width, int *height, int *rate )
{
	int best_fit, best_dist;
	int dist;
	int x, y; //, w, h;
	int num_vidmodes;
	int i;
	
	if ( !glw_state.vidmode_ext )
		return qfalse;

	if ( vidmodes )
	{
		free( vidmodes );
	}
	
	_XF86VidModeGetAllModeLines( dpy, scrnum, &num_vidmodes, &vidmodes );

	best_dist = 9999999;
	best_fit = -1;

	for ( i = 0; i < num_vidmodes; i++ )
	{
		// change original policy, i.e. allow selecting lower resolution modes
		// as it is very unlikely that current mode is lower than mode you want to set
		if ( vidmodes[i]->hdisplay > *width || vidmodes[i]->vdisplay > *height )
			continue;
		x = *width - vidmodes[i]->hdisplay;
		y = *height - vidmodes[i]->vdisplay;
		dist = (x * x) + (y * y);
		if ( dist < best_dist )
		{
			best_dist = dist;
			best_fit = i;
//			w = vidmodes[i]->hdisplay; // save adjusted with
//			h = vidmodes[i]->vdisplay; // save adjusted height
		}
	}

	if ( best_fit != -1 )
	{
		*width = vidmodes[ best_fit ]->hdisplay;
		*height = vidmodes[ best_fit ]->vdisplay;

		// change to the mode
		_XF86VidModeSwitchToMode( dpy, scrnum, vidmodes[ best_fit ] );
		XFlush( dpy );  // drakkar - man 3 XF86VidModeSwitchToMode
		glw_state.vidmode_active = qtrue;

		// Move the viewport to top left
		_XF86VidModeSetViewPort( dpy, scrnum, 0, 0 );

		Com_Printf( "XFree86-VidModeExtension Activated at %dx%d\n",
			*width, *height );

		return qtrue;
	}
	else
	{
		Com_Printf( "XFree86-VidModeExtension: No acceptable modes found\n" );
		return qfalse;
	}
}


void VidMode_RestoreMode( void )
{
	if ( !glw_state.vidmode_ext )
		return;

	if ( vidmodes )
	{
		if ( dpy )
		{
			_XF86VidModeSwitchToMode( dpy, scrnum, vidmodes[ 0 ] );
		}
		free( vidmodes );
		vidmodes = NULL;
	}
}
