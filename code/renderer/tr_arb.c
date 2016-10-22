#include "tr_local.h"
#include "tr_common.h"

#ifdef USE_PMLIGHT

enum {
	VP_GLOBAL_EYEPOS,
	VP_GLOBAL_MAX,
};

#if (VP_GLOBAL_MAX > 96)
#error VP_GLOBAL_MAX > MAX_PROGRAM_ENV_PARAMETERS_ARB
#endif

enum {
	PR_VERTEX,
	PR_FRAGMENT,
	PR_COUNT
};

GLuint programs[ PR_COUNT ];

static	qboolean programAvail	= qfalse;
static	qboolean programEnabled	= qfalse;

void ( APIENTRY * qglGenProgramsARB )( GLsizei n, GLuint *programs );
void ( APIENTRY * qglDeleteProgramsARB)( GLsizei n, const GLuint *programs );
void ( APIENTRY * qglProgramStringARB )( GLenum target, GLenum format, GLsizei len, const GLvoid *string );
void ( APIENTRY * qglBindProgramARB )( GLenum target, GLuint program );
void ( APIENTRY * qglProgramLocalParameter4fARB )( GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w );
void ( APIENTRY * qglProgramEnvParameter4fARB )( GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w );


void GL_ProgramDisable( void )
{
	if ( programEnabled )
	{
		qglDisable( GL_VERTEX_PROGRAM_ARB );
		qglDisable( GL_FRAGMENT_PROGRAM_ARB );
		programEnabled = qfalse;
	}
}


void GL_ProgramEnable( void )
{
	if ( !programAvail )
		return;

	if ( !programEnabled )
	{
		qglEnable( GL_VERTEX_PROGRAM_ARB );
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, programs[ PR_VERTEX ] );
		qglEnable( GL_FRAGMENT_PROGRAM_ARB );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, programs[ PR_FRAGMENT ] );
		programEnabled = qtrue;
	}
}


static void ARB_Lighting( const shaderStage_t* pStage )
{
	const dlight_t* dl;
	byte clipBits[ SHADER_MAX_VERTEXES ];
	unsigned hitIndexes[ SHADER_MAX_INDEXES ];
	int numIndexes;
	int i;
	int clip;

	backEnd.pc.c_lit_vertices_lateculltest += tess.numVertexes;

	dl = tess.light;

	for ( i = 0; i < tess.numVertexes; ++i ) {
		vec3_t dist;
		VectorSubtract( dl->transformed, tess.xyz[i], dist );

		if ( DotProduct( dist, tess.normal[i] ) <= 0.0f ) {
			clipBits[ i ] = 63;
			continue;
		}

		clip = 0;
		if ( dist[0] > dl->radius ) {
			clip |= 1;
		} else if ( dist[0] < -dl->radius ) {
			clip |= 2;
		}
		if ( dist[1] > dl->radius ) {
			clip |= 4;
		} else if ( dist[1] < -dl->radius ) {
			clip |= 8;
		}
		if ( dist[2] > dl->radius ) {
			clip |= 16;
		} else if ( dist[2] < -dl->radius ) {
			clip |= 32;
		}

		clipBits[i] = clip;
	}

	// build a list of triangles that need light
	numIndexes = 0;

	for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
		int		a, b, c;

		a = tess.indexes[i];
		b = tess.indexes[i+1];
		c = tess.indexes[i+2];
		if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
			continue;	// not lighted
		}
		hitIndexes[numIndexes] = a;
		hitIndexes[numIndexes+1] = b;
		hitIndexes[numIndexes+2] = c;
		numIndexes += 3;
	}

	backEnd.pc.c_lit_indices_latecull_in += numIndexes;
	backEnd.pc.c_lit_indices_latecull_out += tess.numIndexes - numIndexes;

	if ( !numIndexes )
		return;

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );

	GL_SelectTexture( 0 );
	R_BindAnimatedImage( &pStage->bundle[ 0 ] );

	R_DrawElements( numIndexes, hitIndexes );
}


void ARB_SetupLightParams( void )
{
	const dlight_t *dl;
	vec3_t lightRGB;
	float radius;

	if ( !programAvail )
		return;

	GL_ProgramEnable();

	dl = tess.light;

	if ( !glConfig.deviceSupportsGamma )
		VectorScale( dl->color, 2 * pow( r_intensity->value, r_gamma->value ), lightRGB );
	else
		VectorCopy( dl->color, lightRGB );

	radius = dl->radius * r_dlightScale->value;
	if ( r_greyscale->value > 0 ) {
		float luminance;
		luminance = LUMA( lightRGB[0], lightRGB[1], lightRGB[2] );
		lightRGB[0] = LERP( lightRGB[0], luminance, r_greyscale->value );
		lightRGB[1] = LERP( lightRGB[1], luminance, r_greyscale->value );
		lightRGB[2] = LERP( lightRGB[2], luminance, r_greyscale->value );
	}

	qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 0, dl->transformed[0], dl->transformed[1], dl->transformed[2], 0 );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, lightRGB[0], lightRGB[1], lightRGB[2], 1.0 );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, 1.0 / Square( radius ), 0, 0, 0 );

	qglProgramEnvParameter4fARB( GL_VERTEX_PROGRAM_ARB, VP_GLOBAL_EYEPOS,
		backEnd.or.viewOrigin[0], backEnd.or.viewOrigin[1], backEnd.or.viewOrigin[2], 0 );
}


void ARB_LightingPass( void )
{
	const shaderStage_t* pStage;

	if ( !programAvail )
		return;

	RB_DeformTessGeometry();

	GL_Cull( tess.shader->cullType );

	pStage = tess.xstages[ tess.shader->lightingStage ];

	R_ComputeTexCoords( pStage );

	// since this is guaranteed to be a single pass, fill and lock all the arrays

	qglDisableClientState( GL_COLOR_ARRAY );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );

	qglEnableClientState( GL_NORMAL_ARRAY );
	qglNormalPointer( GL_FLOAT, 16, tess.normal );

	qglVertexPointer( 3, GL_FLOAT, 16, tess.xyz );

	if ( qglLockArraysEXT )
		qglLockArraysEXT( 0, tess.numVertexes );

	ARB_Lighting( pStage );

	if ( qglUnlockArraysEXT )
		qglUnlockArraysEXT();

	qglDisableClientState( GL_NORMAL_ARRAY );
}

// welding these into the code to avoid having a pk3 dependency in the engine

static const char *VP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"PARAM posEye = program.env[0]; \n"
	"PARAM posLight = program.local[0]; \n"
	"OUTPUT lv = result.texcoord[4]; \n"
	"OUTPUT ev = result.texcoord[5]; \n"
	"OUTPUT n = result.texcoord[6]; \n"
	"MOV result.texcoord[0], vertex.texcoord; \n"
	"MOV n, vertex.normal; \n"
	"SUB ev, posEye, vertex.position; \n"
	"SUB lv, posLight, vertex.position; \n"
	"END \n" 
};

static const char *FPfmt = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM lightRGB = program.local[0]; \n"
	"PARAM lightRange2recip = program.local[1]; \n"
	"TEMP base; TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"ATTRIB dnLV = fragment.texcoord[4]; \n"
	"ATTRIB dnEV = fragment.texcoord[5]; \n"
	"ATTRIB n = fragment.texcoord[6]; \n"
	"TEMP tmp, lv; \n"
	"DP3 tmp, dnLV, dnLV; \n"
	"RSQ lv.w, tmp.w; \n"
	"MUL lv.xyz, dnLV, lv.w; \n"
	"TEMP light; \n"
	"MUL tmp.x, tmp.w, lightRange2recip; \n"
	"SUB tmp.x, 1.0, tmp.x; \n"
	"MUL light.rgb, lightRGB, tmp.x; \n"
	"PARAM specRGB = 0.25; \n"
	"PARAM specEXP = %1.1f; \n" // r_dlightSpecExp->value
	"TEMP ev; \n"
	"DP3 ev, dnEV, dnEV; \n"
	"RSQ ev.w, ev.w; \n"
	"MUL ev.xyz, dnEV, ev.w; \n"
	"ADD tmp, lv, ev; \n"
	"DP3 tmp.w, tmp, tmp; \n"
	"RSQ tmp.w, tmp.w; \n"
	"MUL tmp.xyz, tmp, tmp.w; \n"
	"DP3_SAT tmp.w, n, tmp; \n"
	"POW tmp.w, tmp.w, specEXP.w; \n"
	"TEMP spec; \n"
	"MUL spec.rgb, specRGB, tmp.w; \n"
	"TEMP bump; \n"
	"DP3_SAT bump.w, n, lv; \n"
	"MAD base, base, bump.w, spec; \n"
	"MUL result.color.rgb, base, light; \n"
	"END \n"
};

qboolean ARB_UpdatePrograms( void )
{
	const char *FP;
	GLint errorPos;
	cvar_t	*specExp;

	if ( !qglGenProgramsARB )
		return qfalse;

	if ( programAvail ) // delete old programs
	{
		programEnabled = qtrue; // force disable
		GL_ProgramDisable();
		qglDeleteProgramsARB( PR_COUNT, programs );
		Com_Memset( programs, 0, sizeof( programs ) );
		programAvail = qfalse;
	}

	qglGenProgramsARB( PR_COUNT, programs );

	qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, programs[ PR_VERTEX ] );
	qglProgramStringARB( GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, strlen( VP ), VP );
	qglGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &errorPos );

	if ( qglGetError() != GL_NO_ERROR || errorPos != -1 ) {
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "VP Compile Error: %s", qglGetString( GL_PROGRAM_ERROR_STRING_ARB ) );
		qglDeleteProgramsARB( PR_COUNT, programs );
		Com_Memset( programs, 0, sizeof( programs ) );
		return qfalse;
	}

	// fetch latest values
	specExp = ri.Cvar_Get( "r_dlightSpecExp", "16.0", CVAR_ARCHIVE );
	specExp->modified = qfalse;

	FP = va( FPfmt, specExp->value ); // apply custom parameters

	qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, programs[ PR_FRAGMENT ] );
	qglProgramStringARB( GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, strlen( FP ), FP );
	qglGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &errorPos );

	if ( qglGetError() != GL_NO_ERROR || errorPos != -1 ) {
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "FP Compile Error: %s", qglGetString( GL_PROGRAM_ERROR_STRING_ARB ) );
		qglDeleteProgramsARB( PR_COUNT, programs );
		Com_Memset( programs, 0, sizeof( programs ) );
		return qfalse;
	}

	programAvail = qtrue;

	return qtrue;
}



#ifdef _MSC_VER
#	pragma warning (disable : 4113 4133 4047 )
#endif

qboolean QGL_InitARB( void )
{
	programAvail = qfalse;

	if ( atof( (const char *)qglGetString( GL_VERSION ) ) < 1.4 ) {
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "...OpenGL 1.4 is not available\n" );
		goto __fail;
	}

	if ( !GLimp_HaveExtension( "GL_ARB_vertex_program" ) ) {
		goto __fail;
	}

	if ( !GLimp_HaveExtension( "GL_ARB_fragment_program" ) ) {
		goto __fail;
	}

#define GPA(fn) q##fn = qwglGetProcAddress( #fn ); if ( !q##fn ) goto __fail;
	GPA( glGenProgramsARB );
	GPA( glBindProgramARB );
	GPA( glProgramStringARB );
	GPA( glDeleteProgramsARB );
	GPA( glProgramLocalParameter4fARB );
	GPA( glProgramEnvParameter4fARB );
#undef GPA

	if ( ARB_UpdatePrograms() )
	{
		//programAvail = qtrue;
		programEnabled = qtrue; // force disable
		GL_ProgramDisable();
		ri.Printf( PRINT_ALL, "...using ARB shaders\n" );
		return qtrue;
	}

__fail:
	ri.Printf( PRINT_ALL, "...not using ARB shaders\n" );

	qglGenProgramsARB		= NULL;
	qglDeleteProgramsARB	= NULL;
	qglProgramStringARB		= NULL;
	qglBindProgramARB		= NULL;
	qglProgramLocalParameter4fARB = NULL;
	qglProgramEnvParameter4fARB = NULL;

	return qfalse;
}


void QGL_DoneARB( void )
{
	if ( programAvail )
	{
		programEnabled = qtrue; // force disable
		GL_ProgramDisable();
		qglDeleteProgramsARB( PR_COUNT, programs );
		Com_Memset( programs, 0, sizeof( programs ) );
	}
	programAvail = qfalse;

	qglGenProgramsARB		= NULL;
	qglDeleteProgramsARB	= NULL;
	qglProgramStringARB		= NULL;
	qglBindProgramARB		= NULL;
	qglProgramLocalParameter4fARB = NULL;
	qglProgramEnvParameter4fARB = NULL;
}

#ifdef _MSC_VER
#pragma warning (default : 4113 4133 4047 )
#endif

#endif // USE_PMLIGHT
