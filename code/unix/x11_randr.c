#include "../client/client.h"
#include "unix_glw.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>

#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h>

static Display *dpy;

#define MAX_MONITORS 16

typedef struct
{
	int x, y;
	int w, h;
	RROutput outputn;
	RRCrtc crtcn;
	RRMode curMode;
	RRMode oldMode;
	char name[32];
} monitor_t;

monitor_t monitors[ MAX_MONITORS ];
monitor_t *current_monitor;
monitor_t desktop_monitor;

unsigned short old_gamma[3][4096]; // backup
int old_gamma_size;

static qboolean BackupMonitorGamma( void );

// we resolve all functions dynamically

static void *r_lib = NULL;

Bool (*_XRRQueryExtension)( Display *dpy, int *event_base_return, int *error_base_return );
Status (*_XRRQueryVersion)( Display *dpy, int *major_version_return, int *minor_version_return );
XRRScreenResources* (*_XRRGetScreenResources)( Display *dpy, Window window );
void (*_XRRFreeScreenResources)( XRRScreenResources *resources );
XRROutputInfo *(*_XRRGetOutputInfo)( Display *dpy, XRRScreenResources *resources, RROutput output );
void (*_XRRFreeOutputInfo)( XRROutputInfo *outputInfo );
XRRCrtcInfo *(*_XRRGetCrtcInfo)( Display *dpy, XRRScreenResources *resources, RRCrtc crtc );
void (*_XRRFreeCrtcInfo)( XRRCrtcInfo *crtcInfo );
Status (*_XRRSetCrtcConfig)( Display *dpy, XRRScreenResources *resources, RRCrtc crtc,
		Time timestamp, int x, int y, RRMode mode, Rotation rotation,
		RROutput *outputs, int noutputs );
int (*_XRRGetCrtcGammaSize)( Display *dpy, RRCrtc crtc );
XRRCrtcGamma *(*_XRRGetCrtcGamma)( Display *dpy, RRCrtc crtc );
XRRCrtcGamma *(*_XRRAllocGamma)( int size );
void (*_XRRSetCrtcGamma)( Display *dpy, RRCrtc crtc, XRRCrtcGamma *gamma );
void (*_XRRFreeGamma)( XRRCrtcGamma *gamma );

static sym_t r_list[] =
{
	{ (void**)&_XRRQueryExtension, "XRRQueryExtension" },
	{ (void**)&_XRRQueryVersion, "XRRQueryVersion" },
	{ (void**)&_XRRGetScreenResources, "XRRGetScreenResources" },
	{ (void**)&_XRRFreeScreenResources, "XRRFreeScreenResources" },
	{ (void**)&_XRRGetOutputInfo, "XRRGetOutputInfo" },
	{ (void**)&_XRRFreeOutputInfo, "XRRFreeOutputInfo" },
	{ (void**)&_XRRGetCrtcInfo, "XRRGetCrtcInfo" },
	{ (void**)&_XRRFreeCrtcInfo, "XRRFreeCrtcInfo" },
	{ (void**)&_XRRSetCrtcConfig, "XRRSetCrtcConfig" },
	{ (void**)&_XRRGetCrtcGammaSize, "XRRGetCrtcGammaSize" },
	{ (void**)&_XRRGetCrtcGamma, "XRRGetCrtcGamma" },
	{ (void**)&_XRRAllocGamma, "XRRAllocGamma" },
	{ (void**)&_XRRSetCrtcGamma, "XRRSetCrtcGamma" },
	{ (void**)&_XRRFreeGamma, "XRRFreeGamma" },
};


static qboolean monitor_in_list( int x, int y, int w, int h, RROutput outputn, RRCrtc crtcn )
{
	int i;

	for ( i = 0; i < glw_state.monitorCount; i++ )
	{
		if ( monitors[i].x != x || monitors[i].y != y )
			continue;
		if ( monitors[i].w != w || monitors[i].h != h )
			continue;
		if ( monitors[i].outputn != outputn )
			continue;
		if ( monitors[i].crtcn != crtcn )
			continue;

		return qtrue;
	}

	return qfalse;
}


void monitor_add( int x, int y, int w, int h, const char *name, RROutput outputn, RRCrtc crtcn, RRMode mode )
{
	monitor_t *m;
	
	if ( glw_state.monitorCount >= MAX_MONITORS )
		return;

	if ( monitor_in_list( x, y, w, h, outputn, crtcn ) )
		return;

	m = monitors + glw_state.monitorCount;

	m->x = x; m->y = y;
	m->w = w; m->h = h;

	Q_strncpyz( m->name, name, sizeof( m->name ) );

	m->outputn = outputn;
	m->crtcn = crtcn;

	m->curMode = mode;
	m->oldMode = mode;

	glw_state.monitorCount++;
}


static int getRefreshRate( const XRRModeInfo *mode_info )
{
	if ( mode_info->hTotal && mode_info->vTotal )
		return ( (double)mode_info->dotClock / ( (double)mode_info->hTotal * (double)mode_info->vTotal ) );
	else
		return 0;
}


static const XRRModeInfo* getModeInfo( const XRRScreenResources* sr, RRMode id )
{
	int i;

	for ( i = 0; i < sr->nmode; i++ )
		if ( sr->modes[ i ].id == id )
			return sr->modes + i;

	return NULL;
}


qboolean RandR_SetMode( int *width, int *height, int *rate )
{
	monitor_t *m = &desktop_monitor;
	XRRScreenResources *sr;
	const XRRModeInfo *mode_info;
	XRROutputInfo *output_info;
	XRRCrtcInfo *crtc_info;
	RRMode newMode;
	int best_fit, best_dist, best_rate;
	int dist, r, rr;
	int x, y, w, h;
	int n;

	glw_state.randr_active = qfalse;

	if ( !glw_state.randr_ext )
		return glw_state.randr_active;

	if ( *width == m->w && *height == m->h )
	{
		Com_Printf( "...using desktop display mode\n" );
		glw_state.randr_active = qtrue;
		return glw_state.randr_active;
	}
	
	sr = _XRRGetScreenResources( dpy, DefaultRootWindow( dpy ) );

	output_info = _XRRGetOutputInfo( dpy, sr, m->outputn );
	crtc_info = _XRRGetCrtcInfo( dpy, sr, m->crtcn );

	best_rate = 999999999;
	best_dist = 999999999;
	best_fit = -1;

	// find best-matching mode from available
	for ( n = 0; n < output_info->nmode; n++ )
	{
		mode_info = getModeInfo( sr, output_info->modes[ n ] );

		if ( !mode_info || ( mode_info->modeFlags & RR_Interlace ) )
			continue;
	
		// change original policy, i.e. allow selecting lower resolution modes
		// as it is very unlikely that current mode is lower than mode you want to set
		if ( mode_info->width > *width || mode_info->height > *height )
			continue;
		x = *width - mode_info->width;
		y = *height - mode_info->height;
		dist = ( x * x ) + ( y * y );

		if ( *rate ) {
			r = *rate - getRefreshRate( mode_info );
			r = ( r * r );
		} else {
			r = best_rate;
		}
	
		if ( dist < best_dist || ( dist == best_dist && r < best_rate ) )
		{
			best_dist = dist;
			best_rate = r;
			best_fit = n;
			newMode = output_info->modes[ n ];
			w = mode_info->width; // save adjusted with
			h = mode_info->height; // save adjusted height
			rr = getRefreshRate( mode_info );
			
		}
		//fprintf( stderr, "mode[%i]: %i x %i @ %iHz.\n", i, mode_info->width, mode_info->height, getRefreshRate( mode_info ) );
	}
	
	if ( best_fit != -1 )
	{
		//Com_Printf( "...setting new mode 0x%x via xrandr \n", (int)newMode );
		_XRRSetCrtcConfig( dpy, sr, m->crtcn, CurrentTime, crtc_info->x, crtc_info->y,
			newMode, crtc_info->rotation, crtc_info->outputs, crtc_info->noutput );

		m->curMode = newMode;
		glw_state.randr_active = qtrue;
		*width = w;
		*height = h;
		*rate = rr;
	}

	_XRRFreeCrtcInfo( crtc_info );
	_XRRFreeOutputInfo( output_info );
	_XRRFreeScreenResources( sr );

	return glw_state.randr_active;
}


void RandR_RestoreMode( void )
{
	monitor_t *m = &desktop_monitor;
	XRRScreenResources *sr;
	XRROutputInfo *output_info;
	XRRCrtcInfo *crtc_info;
	
	if ( !glw_state.randr_ext || !glw_state.randr_active )
		return;

	glw_state.randr_active = qfalse;

	if ( m->curMode == m->oldMode )
		return;

	Com_Printf( "...restoring desktop display mode\n" );

	sr = _XRRGetScreenResources( dpy, DefaultRootWindow( dpy ) );

	output_info = _XRRGetOutputInfo( dpy, sr, m->outputn );
	crtc_info = _XRRGetCrtcInfo( dpy, sr, m->crtcn );

	_XRRSetCrtcConfig( dpy, sr, m->crtcn, CurrentTime, crtc_info->x, crtc_info->y,
		m->oldMode, crtc_info->rotation, crtc_info->outputs, crtc_info->noutput );

	_XRRFreeCrtcInfo( crtc_info );
	_XRRFreeOutputInfo( output_info );
	_XRRFreeScreenResources( sr );

	m->curMode = m->oldMode;

	current_monitor = NULL;
}


static void BuildMonitorList( void )
{
	XRRScreenResources *sr;
	XRRCrtcInfo *crtc_info;
	XRROutputInfo *info;
	int outn;

	glw_state.monitorCount = 0;

	sr = _XRRGetScreenResources( dpy, DefaultRootWindow( dpy ) );

	if ( !sr )
		return;

	for ( outn = 0; outn < sr->noutput; outn++ )
	{
		info = _XRRGetOutputInfo( dpy, sr, sr->outputs[ outn ] );
		if ( info )
		{
			if ( info->connection == RR_Connected )
			{
				crtc_info = _XRRGetCrtcInfo( dpy, sr, info->crtc );
				if ( crtc_info )
				{
					//fprintf( stderr, "%ix%i @%ix%i outn:%i (crtc:%i) %s\n",
					//		crtc_info->width, crtc_info->height,
					//		crtc_info->x, crtc_info->y,
					//		(int)outn, (int)info->crtc, info->name );
					if ( crtc_info->width && crtc_info->height )
					{
						monitor_add( crtc_info->x, crtc_info->y, crtc_info->width, crtc_info->height,
							info->name, sr->outputs[ outn ], info->crtc, crtc_info->mode );
					}
					_XRRFreeCrtcInfo( crtc_info );
				}
			}
			_XRRFreeOutputInfo( info );
		}
	}
	_XRRFreeScreenResources( sr );
}


static monitor_t *FindNearestMonitor( int x, int y, int w, int h )
{
	monitor_t *m, *found;
	unsigned long dx, dy, dist, nearest;
	int cx, cy;
	int i;

	found = NULL;
	nearest = 0xFFFFFFFF;

	cx = x + w/2;
	cy = y + h/2;

	for ( i = 0; i < glw_state.monitorCount; i++ )
	{
		m = &monitors[ i ];
		// window center intersection
		if ( cx >= m->x && cx < (m->x + m->w) && cy >= m->y && cy < (m->y + m->h) )
		{
			//Com_Printf( "match by center on %s\n", m->name );
			return m;
		}
		// nearest distance
		//dx = MIN( abs( m->x - ( x + w ) ), abs( x - ( m->x + m->w ) ) );
		//dy = MIN( abs( m->y - ( y + h ) ), abs( y - ( m->y + m->h ) ) );
		// nearest distance from window center to screen center
		dx = (m->x + m->w/2) - cx;
		dy = (m->y + m->h/2) - cy;
		dist = ( dx * dx ) + ( dy * dy );
		if ( nearest > dist )
		{
			nearest = dist;
			found = m;
		}
	}

	return found;
}


void RandR_UpdateMonitor( int x, int y, int w, int h )
{
	monitor_t *cm;
//	int i;
	
	if ( !glw_state.monitorCount || glw_state.cdsFullscreen )
		return;

	// try to find monitor to which input coordinates belongs to
	cm = FindNearestMonitor( x, y, w, h );

	if ( !cm )
		return;

	if ( cm != current_monitor )
	{
		qboolean gammaSet = glw_state.gammaSet;

		if ( glw_state.randr_gamma && gammaSet )
		{
			RandR_RestoreGamma();
		}

		// save new monitor
		current_monitor = cm;
		memcpy( &desktop_monitor, cm, sizeof( desktop_monitor ) );

		glw_state.desktop_x = cm->x;
		glw_state.desktop_y = cm->y;
		glw_state.desktop_width = cm->w;
		glw_state.desktop_height = cm->h;

		glw_state.desktop_ok = qtrue;

		Com_Printf( "...current monitor: %ix%i@%i,%i %s\n",
			glw_state.desktop_width, glw_state.desktop_height,
			glw_state.desktop_x, glw_state.desktop_y,
			desktop_monitor.name );

		BackupMonitorGamma();

		if ( glw_state.randr_gamma && gammaSet && re.SetColorMappings )
		{
			re.SetColorMappings();
		}
	}
}


static qboolean BackupMonitorGamma( void )
{
	XRRCrtcGamma* gamma;
	int gammaRampSize;

	if ( !glw_state.monitorCount || !glw_state.randr_gamma )
	{
		return qfalse;
	}

	gammaRampSize = _XRRGetCrtcGammaSize( dpy, desktop_monitor.crtcn );
	if ( gammaRampSize < 256 || gammaRampSize > 4096 )
	{
		glw_state.randr_gamma = qfalse;
		fprintf( stderr, "...unsupported gamma ramp size: %i\n", gammaRampSize );
		return qfalse;
	}

	gamma = _XRRGetCrtcGamma( dpy, desktop_monitor.crtcn );

	if ( gamma )
	{
		memcpy( old_gamma[0], gamma->red,   gammaRampSize * sizeof( unsigned short ) );
		memcpy( old_gamma[1], gamma->green, gammaRampSize * sizeof( unsigned short ) );
		memcpy( old_gamma[2], gamma->blue,  gammaRampSize * sizeof( unsigned short ) );
		old_gamma_size = gammaRampSize;

		_XRRFreeGamma( gamma );
		
		return qtrue;
	}

	return qfalse;
}


static void SetMonitorGamma( unsigned short *red, unsigned short *green, unsigned short *blue, int size )
{
	XRRCrtcGamma* gamma;

	gamma = _XRRAllocGamma( size );
	if ( gamma )
	{
		memcpy( gamma->red,   red,   size * sizeof( unsigned short ) );
		memcpy( gamma->green, green, size * sizeof( unsigned short ) );
		memcpy( gamma->blue,  blue,  size * sizeof( unsigned short ) );
		_XRRSetCrtcGamma( dpy, desktop_monitor.crtcn, gamma );
		_XRRFreeGamma( gamma );
	}
}

void RandR_RestoreGamma( void )
{
	if ( glw_state.randr_gamma && old_gamma_size )
		SetMonitorGamma( old_gamma[0], old_gamma[1], old_gamma[2], old_gamma_size );

	old_gamma_size = 0;
}


void RandR_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
	unsigned short table[3][4096];
	
	if ( BuildGammaRampTable( red, green, blue, old_gamma_size, table ) )
	{
		SetMonitorGamma( table[0], table[1], table[2], old_gamma_size );
		glw_state.gammaSet = qtrue;
	}
}


qboolean RandR_Init( Display *_dpy, int x, int y, int w, int h )
{
	int event_base, error_base;
	int ver_major = 1, ver_minor = 2;
	int i;

	glw_state.randr_ext = qfalse;
	glw_state.randr_active = qfalse;
	glw_state.randr_gamma = qfalse;

	glw_state.monitorCount = 0;
	current_monitor = NULL;
	memset( monitors, 0, sizeof( monitors ) );
	memset( &desktop_monitor, 0, sizeof( desktop_monitor ) );

	if ( r_lib == NULL )
	{
		r_lib = Sys_LoadLibrary( "libXrandr.so.2" );
		if ( r_lib == NULL )
		{
			r_lib = Sys_LoadLibrary( "libXrandr.so" );
		}
		if ( r_lib == NULL )
		{
			Com_Printf( "...error loading libXrandr\n" );
			goto __fail;
		}
	}

	for ( i = 0 ; i < ARRAY_LEN( r_list ); i++ )
	{
		*r_list[ i ].symbol = Sys_LoadFunction( r_lib, r_list[ i ].name );
		if ( *r_list[ i ].symbol == NULL )
		{
			Com_Printf( "...couldn't find '%s' in libXrandr\n", r_list[ i ].name );
			goto __fail;
		}
	}

	dpy = _dpy;

	if ( !_XRRQueryExtension( dpy, &event_base, &error_base ) || !_XRRQueryVersion( dpy, &ver_major, &ver_minor ) )
	{
		Com_Printf( "...RandR extension is not available.\n" );
		goto __fail;
	}

	Com_Printf( "...RandR extension version %i.%i detected.\n", ver_major, ver_minor );

	glw_state.randr_ext = qtrue;

	glw_state.randr_gamma = qtrue; // this will be reset by BackupMonitorGamma() if gamma is not available

	BuildMonitorList();

	RandR_UpdateMonitor( x, y, w, h );

	return qtrue;

__fail:
	RandR_Done();
	return qfalse;
}


void RandR_Done( void )
{
	if ( r_lib )
	{
		Sys_UnloadLibrary( r_lib );
		r_lib = NULL;
	}
	glw_state.randr_ext = qfalse;
	glw_state.randr_gamma = qfalse;
}
