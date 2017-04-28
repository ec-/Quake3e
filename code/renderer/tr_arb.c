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
	DLIGHT_VERTEX,
	DLIGHT_FRAGMENT,
	SPRITE_VERTEX,
	SPRITE_FRAGMENT,
	PROGRAM_COUNT
};

typedef enum {
	Vertex,
	Fragment
} programType;

static GLuint programs[ PROGRAM_COUNT ];
static GLuint current_vp;
static GLuint current_fp;

static	qboolean programAvail	= qfalse;
static	qboolean programEnabled	= qfalse;

void ( APIENTRY * qglGenProgramsARB )( GLsizei n, GLuint *programs );
void ( APIENTRY * qglDeleteProgramsARB)( GLsizei n, const GLuint *programs );
void ( APIENTRY * qglProgramStringARB )( GLenum target, GLenum format, GLsizei len, const GLvoid *string );
void ( APIENTRY * qglBindProgramARB )( GLenum target, GLuint program );
void ( APIENTRY * qglProgramLocalParameter4fARB )( GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w );
void ( APIENTRY * qglProgramEnvParameter4fARB )( GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w );

qboolean GL_ProgramAvailable( void ) 
{
	return programAvail;
}


void GL_ProgramDisable( void )
{
	if ( programEnabled )
	{
		qglDisable( GL_VERTEX_PROGRAM_ARB );
		qglDisable( GL_FRAGMENT_PROGRAM_ARB );
		programEnabled = qfalse;
		current_vp = 0;
		current_fp = 0;
	}
}


void GL_ProgramEnable( void ) 
{
	if ( !programAvail )
		return;

	if ( current_vp != programs[ SPRITE_VERTEX ] ) {
		qglEnable( GL_VERTEX_PROGRAM_ARB );
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, programs[ SPRITE_VERTEX ] );
		current_vp = programs[ SPRITE_VERTEX ];
	}

	if ( current_fp != programs[ SPRITE_FRAGMENT ] ) {
		qglEnable( GL_FRAGMENT_PROGRAM_ARB );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, programs[ SPRITE_FRAGMENT ] );
		current_fp = programs[ SPRITE_FRAGMENT ];
	}
	programEnabled = qtrue;
}


static void GL_DlightProgramEnable( void )
{
	if ( !programAvail )
		return;

	if ( current_vp != programs[ DLIGHT_VERTEX ] ) {
		qglEnable( GL_VERTEX_PROGRAM_ARB );
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, programs[ DLIGHT_VERTEX ] );
		current_vp = programs[ DLIGHT_VERTEX ];
	}

	if ( current_fp != programs[ DLIGHT_FRAGMENT ] ) {
		qglEnable( GL_FRAGMENT_PROGRAM_ARB );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, programs[ DLIGHT_FRAGMENT ] );
		current_fp = programs[ DLIGHT_FRAGMENT ];
	}

	programEnabled = qtrue;
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

		if ( tess.surfType != SF_GRID && DotProduct( dist, tess.normal[i] ) <= 0.0f ) {
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

	if ( tess.shader->sort < SS_OPAQUE ) {
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	}

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

	GL_DlightProgramEnable();

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

	//if ( tess.shader->lightingStage == -1 )
	//	return;

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

	//if ( qglLockArraysEXT )
	//		qglLockArraysEXT( 0, tess.numVertexes );

	ARB_Lighting( pStage );

	//if ( qglUnlockArraysEXT )
	//		qglUnlockArraysEXT();

	qglDisableClientState( GL_NORMAL_ARRAY );
}

extern cvar_t *r_dlightSpecPower;
extern cvar_t *r_dlightSpecColor;

// welding these into the code to avoid having a pk3 dependency in the engine

static const char *dlightVP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"PARAM posEye = program.env[0]; \n"
	"PARAM posLight = program.local[0]; \n"
	"OUTPUT lv = result.texcoord[1]; \n" // 1
	"OUTPUT ev = result.texcoord[2]; \n" // 2
	"OUTPUT n = result.texcoord[3]; \n"  // 3
	"MOV result.texcoord[0], vertex.texcoord; \n"
	"MOV n, vertex.normal; \n"
	"SUB ev, posEye, vertex.position; \n"
	"SUB lv, posLight, vertex.position; \n"
	"END \n" 
};


// dynamically apply custom parameters
static const char *ARB_BuildDlightFragmentProgram( void  )
{
	static char program[1024];
	
	program[0] = '\0';
	strcat( program, 
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM lightRGB = program.local[0]; \n"
	"PARAM lightRange2recip = program.local[1]; \n"
	"TEMP base; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"ATTRIB dnLV = fragment.texcoord[1]; \n" // 1
	"ATTRIB dnEV = fragment.texcoord[2]; \n" // 2
	"ATTRIB n = fragment.texcoord[3]; \n"    // 3
	
	// normalize light vector
	"TEMP tmp, lv; \n"
	"DP3 tmp.w, dnLV, dnLV; \n"
	"RSQ lv.w, tmp.w; \n"
	"MUL lv.xyz, dnLV, lv.w; \n"

	// calculate light intensity
	"TEMP light; \n"
	"MUL tmp.x, tmp.w, lightRange2recip; \n"
	"SUB tmp.x, 1.0, tmp.x; \n"
	"MUL light, lightRGB, tmp.x; \n" // light.rgb
	);

	if ( r_dlightSpecColor->value > 0 ) {
		strcat( program, va( "PARAM specRGB = %1.2f; \n", r_dlightSpecColor->value ) );
	}

	strcat( program, va( "PARAM specEXP = %1.2f; \n", r_dlightSpecPower->value ) );

	strcat( program,
	// normalize eye vector
	"TEMP ev; \n"
	"DP3 ev.w, dnEV, dnEV; \n"
	"RSQ ev.w, ev.w; \n"
	"MUL ev.xyz, dnEV, ev.w; \n"

	// normalize (eye + light) vector
	"ADD tmp, lv, ev; \n"
	"DP3 tmp.w, tmp, tmp; \n"
	"RSQ tmp.w, tmp.w; \n"
	"MUL tmp.xyz, tmp, tmp.w; \n"

	// modulate specular strength
	"DP3_SAT tmp.w, n, tmp; \n"
	"POW tmp.w, tmp.w, specEXP.w; \n"
	"TEMP spec; \n" );
	if ( r_dlightSpecColor->value > 0 ) {
		// by constant
		strcat( program, "MUL spec, specRGB, tmp.w; \n" );
	} else {
		// by texture
		strcat( program, va( "MUL tmp.w, tmp.w, %1.2f; \n", -r_dlightSpecColor->value ) );
		strcat( program, "MUL spec, base, tmp.w; \n" );
	}

	strcat( program, 
	// bump color
	"TEMP bump; \n"
	"DP3_SAT bump.w, n, lv; \n"

	"MAD base, base, bump.w, spec; \n"
	"MUL result.color, base, light; \n"
	"END \n" 
	);
	
	r_dlightSpecColor->modified = qfalse;
	r_dlightSpecPower->modified = qfalse;

	return program;
}

static const char *spriteVP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"MOV result.texcoord[0], vertex.texcoord; \n"
	"END \n" 
};

static const char *spriteFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"TEMP base; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"TEMP test; \n"
	"SUB test.a, base.a, 0.85; \n"
	"KIL test.a; \n"
	"MOV base, 0.0; \n"
	"MOV result.color, base; \n"
	"MOV result.depth, fragment.position.z; \n"
	"END \n" 
};


static void ARB_DeletePrograms( void ) 
{
	qglDeleteProgramsARB( ARRAY_LEN( programs ), programs );
	Com_Memset( programs, 0, sizeof( programs ) );
}


static qboolean ARB_CompileProgram( programType ptype, const char *text, GLuint program ) 
{
	GLint errorPos;
	int kind;

	if ( ptype == Fragment )
		kind = GL_FRAGMENT_PROGRAM_ARB;
	else
		kind = GL_VERTEX_PROGRAM_ARB;

	qglBindProgramARB( kind, program );
	qglProgramStringARB( kind, GL_PROGRAM_FORMAT_ASCII_ARB, strlen( text ), text );
	qglGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &errorPos );
	if ( qglGetError() != GL_NO_ERROR || errorPos != -1 ) {
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "%s Compile Error: %s", (ptype == Fragment) ? "FP" : "VP", 
			qglGetString( GL_PROGRAM_ERROR_STRING_ARB ) );
		ARB_DeletePrograms();
		return qfalse;
	}

	return qtrue;
}


qboolean ARB_UpdatePrograms( void )
{
	const char *dlightFP;

	if ( !qglGenProgramsARB )
		return qfalse;

	if ( programAvail ) // delete old programs
	{
		programEnabled = qtrue; // force disable
		GL_ProgramDisable();
		ARB_DeletePrograms();
		programAvail = qfalse;
	}

	qglGenProgramsARB( ARRAY_LEN( programs ), programs );

	if ( !ARB_CompileProgram( Vertex, dlightVP, programs[ DLIGHT_VERTEX ] ) )
		return qfalse;

	dlightFP = ARB_BuildDlightFragmentProgram();

	if ( !ARB_CompileProgram( Fragment, dlightFP, programs[ DLIGHT_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Vertex, spriteVP, programs[ SPRITE_VERTEX ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, spriteFP, programs[ SPRITE_FRAGMENT ] ) )
		return qfalse;

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
		ARB_DeletePrograms();
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
