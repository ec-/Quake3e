#include "tr_local.h"
#include "tr_common.h"

#define COMMON_DEPTH_STENCIL
//#define DEPTH_RENDER_BUFFER
//#define USE_FBO_BLIT

// screenMap texture dimensions
#define SCR_WIDTH 128
#define SCR_HEIGHT 64

#define BLOOM_BASE 5
#define FBO_COUNT (BLOOM_BASE+(MAX_BLUR_PASSES*2))

#if BLOOM_BASE < 2
#error no space for main/postprocess buffers
#endif

static GLuint programs[ PROGRAM_COUNT ];
static GLuint current_vp;
static GLuint current_fp;

static int programAvailable	= 0;
static int programCompiled = 0;
static int programEnabled	= 0;
static int gl_version = 0;

qboolean fboAvailable = qfalse;
qboolean fboEnabled = qfalse;
qboolean fboBloomInited = qfalse;
int      fboReadIndex = 0;
GLint    fboInternalFormat;
GLint    fboTextureFormat;
GLint    fboTextureType;
int      fboBloomPasses;
int      fboBloomBlendBase;
int      fboBloomFilterSize;

qboolean windowAdjusted;
int		windowWidth;
int		windowHeight;
int		blitX0, blitX1;
int		blitY0, blitY1;
int		blitClear;
GLenum	blitFilter;

int		captureWidth;
int		captureHeight;
qboolean superSampled;

typedef struct frameBuffer_s {
	GLuint fbo;
	GLuint color;			// renderbuffer if multisampled
	GLuint depthStencil;	// renderbuffer if multisampled
	GLint  width;
	GLint  height;
	qboolean multiSampled;
} frameBuffer_t;

static GLuint commonDepthStencil;
static frameBuffer_t frameBufferMS;
static frameBuffer_t frameBuffers[ FBO_COUNT ];

static qboolean frameBufferMultiSampling = qfalse;

qboolean blitMSfbo = qfalse;

#ifndef GL_TEXTURE_IMAGE_FORMAT
#define GL_TEXTURE_IMAGE_FORMAT 0x828F
#endif

#ifndef GL_TEXTURE_IMAGE_TYPE
#define GL_TEXTURE_IMAGE_TYPE 0x8290
#endif

extern void RB_SetGL2D( void );

qboolean GL_ProgramAvailable( void )
{
	return (programCompiled != 0);
}


static void ARB_ProgramDisable( void )
{
	if ( current_vp )
		qglDisable( GL_VERTEX_PROGRAM_ARB );
	if ( current_fp )
		qglDisable( GL_FRAGMENT_PROGRAM_ARB );
	current_vp = 0;
	current_fp = 0;
	programEnabled = 0;
}


void GL_ProgramDisable( void )
{
	if ( programEnabled )
	{
		ARB_ProgramDisable();
	}
}


void ARB_ProgramEnableExt( GLuint vertexProgram, GLuint fragmentProgram )
{
	if ( programCompiled )
	{
		if ( current_vp != vertexProgram ) {
			current_vp = vertexProgram;
			if ( current_vp ) {
				qglEnable( GL_VERTEX_PROGRAM_ARB );
				qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, current_vp );
			} else {
				qglDisable( GL_VERTEX_PROGRAM_ARB );
			}
		}

		if ( current_fp != fragmentProgram ) {
			current_fp = fragmentProgram;
			if ( current_fp ) {
				qglEnable( GL_FRAGMENT_PROGRAM_ARB );
				qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, current_fp );
			} else {
				qglDisable( GL_FRAGMENT_PROGRAM_ARB );
			}
		}
		programEnabled = 1;
	}
}


static void ARB_ProgramEnable( programNum vp, programNum fp )
{
	ARB_ProgramEnableExt( programs[ vp ], programs[ fp ] );
}


void GL_ProgramEnable( void )
{
	ARB_ProgramEnable( DUMMY_VERTEX, SPRITE_FRAGMENT );
}


#ifdef USE_PMLIGHT
static void ARB_Lighting( const shaderStage_t* pStage )
{
	const dlight_t* dl;
	byte clipBits[ SHADER_MAX_VERTEXES ];
	unsigned hitIndexes[ SHADER_MAX_INDEXES ];
	int numIndexes;
	int clip;
	int i;
	
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

	R_BindAnimatedImage( &pStage->bundle[ tess.shader->lightingBundle ] );
	
	R_DrawElements( numIndexes, hitIndexes );
}


static void ARB_Lighting_Fast( const shaderStage_t* pStage )
{
	if ( !tess.numIndexes )
		return;

	if ( tess.shader->sort < SS_OPAQUE ) {
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	}

	GL_SelectTexture( 0 );

	R_BindAnimatedImage( &pStage->bundle[ tess.shader->lightingBundle ] );
	
	R_DrawElements( tess.numIndexes, tess.indexes );
}


void ARB_SetupLightParams( void )
{
	programNum vertexProgram;
	programNum fragmentProgram;
	const fogProgramParms_t *fp;
	qboolean fogPass;
	const dlight_t *dl;
	vec3_t lightRGB;
	float radius;

	tess.dlightUpdateParams = qfalse;

	if ( !programCompiled )
		return;

	dl = tess.light;

	if ( !glConfig.deviceSupportsGamma )
		VectorScale( dl->color, 2 * pow( r_intensity->value, r_gamma->value ), lightRGB );
	else
		VectorCopy( dl->color, lightRGB );

	radius = dl->radius * r_dlightScale->value;

	fogPass = ( tess.fogNum && tess.shader->fogPass );
	fp = NULL;

	if ( dl->linear ) {
		vertexProgram = DLIGHT_LINEAR_VERTEX;
		fragmentProgram = DLIGHT_LINEAR_FRAGMENT;
	} else {
		vertexProgram = DLIGHT_VERTEX;
		fragmentProgram = DLIGHT_FRAGMENT;
	}

	if ( fogPass ) {
		fp = RB_CalcFogProgramParms();
		// switch to fog programs
		if ( fp->eyeOutside ) {
			vertexProgram += 2;
		} else {
			vertexProgram += 1;
		}
		++fragmentProgram;
	}

	ARB_ProgramEnable( vertexProgram, fragmentProgram );

	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, lightRGB[0], lightRGB[1], lightRGB[2], 1.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, 1.0f / Square( radius ), 0, 0, 0 );

	if ( dl->linear )
	{
		vec3_t ab;
		VectorSubtract( dl->transformed2, dl->transformed, ab );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 2, dl->transformed[0], dl->transformed[1], dl->transformed[2], 0 );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 3, dl->transformed2[0], dl->transformed2[1], dl->transformed2[2], 0 );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 4, ab[0], ab[1], ab[2], 1.0f / DotProduct( ab, ab ) );
	}
	else
	{
		qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 1, dl->transformed[0], dl->transformed[1], dl->transformed[2], 0 );
	}

	qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 0, backEnd.or.viewOrigin[0], backEnd.or.viewOrigin[1], backEnd.or.viewOrigin[2], 0 );

	if ( fogPass )
	{
		GL_BindTexture( 1, tr.fogImage->texnum );
		//qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 5, fp->fogColor );
		qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 2, fp->fogDistanceVector );
		qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 3, fp->fogDepthVector );
		qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 4, fp->eyeT, 0.0f, 0.0f, 0.0f );
		GL_SelectTexture( 0 );
	}
}


void ARB_LightingPass( void )
{
	const shaderStage_t* pStage;

	if ( !programAvailable )
		return;

	if ( tess.shader->lightingStage < 0 )
		return;

	// we may need to update programs for fog transitions
	if ( tess.dlightUpdateParams )
		ARB_SetupLightParams();

	RB_DeformTessGeometry();

	GL_Cull( tess.shader->cullType );

	// set polygon offset if necessary
	if ( tess.shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	pStage = tess.xstages[ tess.shader->lightingStage ];

	R_ComputeTexCoords( pStage );

	// since this is guaranteed to be a single pass, fill and lock all the arrays
	
	qglDisableClientState( GL_COLOR_ARRAY );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[ tess.shader->lightingBundle ] );

	qglEnableClientState( GL_NORMAL_ARRAY );
	qglNormalPointer( GL_FLOAT, 16, tess.normal );

	qglVertexPointer( 3, GL_FLOAT, 16, tess.xyz );

	//if ( qglLockArraysEXT )
	//		qglLockArraysEXT( 0, tess.numVertexes );

	// CPU may limit performance in following cases
	if ( tess.light->linear || gl_version >= 40 )
		ARB_Lighting_Fast( pStage );
	else
		ARB_Lighting( pStage );

	//if ( qglUnlockArraysEXT )
	//		qglUnlockArraysEXT();

	// reset polygon offset
	if ( tess.shader->polygonOffset ) 
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}

	qglDisableClientState( GL_NORMAL_ARRAY );
}
#endif // USE_PMLIGHT


const char *fogOutVPCode = {
	"PARAM fogDistanceVector = program.local[2]; \n"
	"PARAM fogDepthVector = program.local[3]; \n"
	"PARAM eyeT = program.local[4]; \n"
	"PARAM _01_32 = 0.03125; \n"
	"PARAM _30_32 = 0.93750; \n"
	"TEMP st; \n"
	
	// s = DotProduct( v, fogDistanceVector ) + fogDistanceVector[3];
	"DP3 st.x, fogDistanceVector, vertex.position; \n"	
	"ADD st.x, st.x, fogDistanceVector.w; \n"

	// t = DotProduct( v, fogDepthVector ) + fogDepthVector[3];
	"DP3 st.y, fogDepthVector, vertex.position; \n"	
	"ADD st.y, st.y, fogDepthVector.w; \n"

	// if ( t < 1.0 ) { t = 1.0/32; } else { t = 1.0/32 + 30.0/32 * t / ( t - eyeT ); }
	"SGE st.w, st.y, 1.0; \n"
	"SUB st.z, st.y, eyeT.x; \n"
	"RCP st.z, st.z; \n"
	"MUL st.z, st.z, st.y; \n"
	"MUL st.z, st.z, _30_32; \n"
	"MAD st.y, st.z, st.w, _01_32; \n"
	
	//"MOV st.z, {1.0}; \n"
	"MOV st.w, {1.0}; \n"

	"MOV result.texcoord[4], st; \n"
};


const char *fogInVPCode = {

	"PARAM fogDistanceVector = program.local[2]; \n"
	"PARAM fogDepthVector = program.local[3]; \n"
	"PARAM eyeT = program.local[4]; \n"
	"PARAM _01_32 = 0.03125; \n"
	"PARAM _30_32 = 0.93750; \n"
	"TEMP st; \n"
	
	// s = DotProduct( v, fogDistanceVector ) + fogDistanceVector[3];
	"DP3 st.x, fogDistanceVector, vertex.position; \n"	
	"ADD st.x, st.x, fogDistanceVector.w; \n"

	// t = DotProduct( v, fogDepthVector ) + fogDepthVector[3];
	"DP3 st.y, fogDepthVector, vertex.position; \n"
	"ADD st.y, st.y, fogDepthVector.w; \n"

	//if ( t < 0 ) { t = 1.0/32; } else { t = 31.0/32; }
	"SGE st.w, st.y, 0.0; \n"
	"MAD st.y, st.w, _30_32, _01_32; \n"

	//"MOV st.z, {1.0}; \n"
	"MOV st.w, {1.0}; \n"

	"MOV result.texcoord[4], st; \n"
};


#ifdef USE_PMLIGHT
static const char *dlightVP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"PARAM posEye = program.local[0]; \n"
	"PARAM posLight = program.local[1]; \n"
	"OUTPUT lv = result.texcoord[1]; \n" // 1
	"OUTPUT ev = result.texcoord[2]; \n" // 2
	"OUTPUT n = result.texcoord[3]; \n"  // 3
	"MOV result.texcoord[0], vertex.texcoord; \n" // 0
	"MOV n, vertex.normal; \n"
	"SUB ev, posEye, vertex.position; \n"
	"SUB lv, posLight, vertex.position; \n"
	"%s" // fog shader if needed
	"END \n"
};


static const char *dlightVP_linear = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"PARAM posEye = program.local[0]; \n"
	"OUTPUT fp = result.texcoord[1]; \n"
	"OUTPUT ev = result.texcoord[2]; \n"
	"OUTPUT n = result.texcoord[3]; \n"
	"MOV result.texcoord[0], vertex.texcoord; \n" // 0
	"MOV fp, vertex.position; \n"
	"SUB ev, posEye, vertex.position; \n"
	"MOV n, vertex.normal; \n"
	"%s" // fog shader if needed
	"END \n"
};


static const char *ARB_BuildLinearDlightFP( char *program, qboolean fog )
{
	program[0] = '\0';
	strcat( program,
	
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM lightRGB = program.local[0]; \n"
	"PARAM lightRange2recip = program.local[1]; \n"
	"PARAM lightOrigin = program.local[2]; \n"
	"PARAM lightOrigin2 = program.local[3]; \n"
	"PARAM lightVector = program.local[4]; \n"
	//"PARAM fogColor = program.local[5]; \n" // fog color

	"TEMP base; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"ATTRIB fragmentPos = fragment.texcoord[1]; \n"
	"ATTRIB dnEV = fragment.texcoord[2]; \n"
	"ATTRIB n = fragment.texcoord[3]; \n"

	// project fragment on light vector
	"TEMP dnLV, tmp; \n"
	"SUB tmp, fragmentPos, lightOrigin; \n"
	"DP3 tmp.w, tmp, lightVector; \n"
	"MUL_SAT tmp.x, tmp.w, lightVector.w; \n"
	"MAD dnLV, lightVector, tmp.x, lightOrigin; \n"
	// calculate light vector from projection point
	"SUB dnLV, dnLV, fragmentPos; \n"
		
	// normalize light vector
	"TEMP lv; \n"
	"DP3 tmp.w, dnLV, dnLV; \n"
	"RSQ lv.w, tmp.w; \n"
	"MUL lv.xyz, dnLV, lv.w; \n"

	// calculate light intensity
	"TEMP light; \n"
	"MUL tmp.x, tmp.w, lightRange2recip; \n"
	"SUB tmp.x, {1.0}, tmp.x; \n"
	"MUL light, lightRGB, tmp.x; \n" // light.rgb
	);

	if ( r_dlightSpecColor->value > 0 )
		strcat( program, va( "PARAM specRGB = %1.2f; \n", r_dlightSpecColor->value ) );

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
	"TEMP spec; \n"
	);

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

#if 0
	// add some light leaks from line plane
	"SUB tmp, fragmentPos, lightOrigin; \n"
	"DP3 tmp.w, tmp, tmp;\n"
	"RSQ tmp.w, tmp.w; \n"
	"MUL tmp.xyz, tmp, tmp.w; \n"
	"DP3_SAT tmp.w, n, tmp; \n"
	//"MUL tmp.w, tmp, 0.5; \n"
	"MAX bump.w, bump.w, tmp.w; \n"

	"SUB tmp, fragmentPos, lightOrigin2; \n"
	"DP3 tmp.w, tmp, tmp;\n"
	"RSQ tmp.w, tmp.w; \n"
	"MUL tmp.xyz, tmp, tmp.w; \n"
	"DP3_SAT tmp.w, n, tmp; \n"
	//"MUL tmp.w, tmp, 0.5; \n"
	"MAX bump.w, bump.w, tmp.w; \n"
#endif

	"MAD base, base, bump.w, spec; \n" );

	if ( fog ) {
		strcat( program,
		"TEMP fog; \n"
		"TEX fog, fragment.texcoord[4], texture[1], 2D; \n" // fog texture
		//"MUL fog, fog, fogColor; \n"
		// blend with fog
		//"LRP_SAT base, fog.a, fog, base; \n"
		// modulate by inverted fog alpha
		"SUB fog.a, {1.0}, fog.a; \n"
		"MUL base, base, fog.a; \n" );
	}

	strcat( program,
	"MUL_SAT result.color, base, light; \n"
	"END \n" );

	return program;
};


static const char *ARB_BuildDlightFP( char *program, qboolean fog )
{
	program[0] = '\0';
	strcat( program,
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM lightRGB = program.local[0]; \n"
	"PARAM lightRange2recip = program.local[1]; \n"
	//"PARAM fogColor = program.local[5]; \n" // fogColor
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
	"SUB tmp.x, {1.0}, tmp.x; \n"
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
	"MAD base, base, bump.w, spec; \n" );

	if ( fog ) {
		strcat( program,
		"TEMP fog; \n"
		"TEX fog, fragment.texcoord[4], texture[1], 2D; \n" // fog texture
		//"MUL fog, fog, fogColor; \n"
		// blend with fog
		//"LRP_SAT base, fog.a, fog, base; \n"
		// modulate by inverted fog alpha
		"SUB fog.a, {1.0}, fog.a; \n"
		"MUL base, base, fog.a; \n" );
	}

	strcat( program,
	"MUL_SAT result.color, base, light; \n"
	"END \n" );
	
	r_dlightSpecColor->modified = qfalse;
	r_dlightSpecPower->modified = qfalse;

	return program;
}

#endif // USE_PMLIGHT


static const char *dummyVP = {
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


static char *ARB_BuildGreyscaleProgram( char *buf ) {
	char *s;

	if ( r_greyscale->value == 0 ) {
		*buf = '\0';
		return buf;
	}
	
	s = Q_stradd( buf, "PARAM sRGB = { 0.2126, 0.7152, 0.0722, 1.0 }; \n" );

	if ( r_greyscale->value == 1.0 ) {
		Q_stradd( s, "DP3 base.xyz, base, sRGB; \n"  );
	} else {
		s = Q_stradd( s, "TEMP luma; \n" );
		s = Q_stradd( s, "DP3 luma, base, sRGB; \n" );
		s += sprintf( s, "LRP base.xyz, %1.2f, luma, base; \n", r_greyscale->value );
	}

	return buf;
}


static const char *gammaFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM gamma = program.local[0]; \n"
	"TEMP base; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"POW base.x, base.x, gamma.x; \n"
	"POW base.y, base.y, gamma.y; \n"
	"POW base.z, base.z, gamma.z; \n"
	"MUL base.xyz, base, gamma.w; \n"
	"%s" // for greyscale shader if needed
	"MOV base.w, 1.0; \n"
	"MOV_SAT result.color, base; \n"
	"END \n"
};

static char *ARB_BuildBloomProgram( char *buf ) {
	qboolean intensityCalculated;
	char *s = buf;

	intensityCalculated = qfalse;
	s = Q_stradd( s,
		"!!ARBfp1.0 \n"
		"OPTION ARB_precision_hint_fastest; \n"
		"PARAM thres = program.local[0]; \n"
		"TEMP base; \n"
		"TEX base, fragment.texcoord[0], texture[0], 2D; \n" );

	if ( r_bloom_threshold_mode->integer == 0 ) {
		// (r|g|b) >= threshold
		s = Q_stradd( s,
			"TEMP minv; \n"
			"SGE minv, base, thres; \n"
			"DP3_SAT minv.w, minv, minv; \n"
			"MUL base.rgb, base, minv.w; \n" );
	} else if ( r_bloom_threshold_mode->integer == 1 ) {
		// (r+g+b)/3 >= threshold
		s = Q_stradd( s,
			"PARAM scale = { 0.3333, 0.3334, 0.3333, 1.0 }; \n"
			"TEMP avg; \n"
			"DP3_SAT avg, base, scale; \n"
			"SGE avg.w, avg.x, thres.x; \n"
			"MUL base.rgb, base, avg.w; \n" );
	} else {
		// luma(r,g,b) >= threshold
		s = Q_stradd( s,
			"PARAM luma = { 0.2126, 0.7152, 0.0722, 1.0 }; \n"
			"TEMP intensity; \n"
			"DP3_SAT intensity, base, luma; \n"
			"SGE intensity.w, intensity.x, thres.x; \n"
			"MUL base.rgb, base, intensity.w; \n" );
		intensityCalculated = qtrue;
	}

	// modulation
	if ( r_bloom_modulate->integer ) {
		if ( r_bloom_modulate->integer == 1 ) {
			// by itself
			s = Q_stradd( s, "MUL base, base, base; \n" );
		} else {
			// by intensity
			if ( !intensityCalculated ) {
				s = Q_stradd( s,
					"PARAM luma = { 0.2126, 0.7152, 0.0722, 1.0 }; \n"
					"TEMP intensity; \n"
					"DP3_SAT intensity, base, luma; \n" );
			}
			s = Q_stradd( s, "MUL base, base, intensity; \n" );
		}
	}

	s = Q_stradd( s,
		"MOV base.w, 1.0; \n"
		"MOV result.color, base; \n"
		"END \n" );

	return buf;
}


// Gaussian blur shader
static char *ARB_BuildBlurProgram( char *buf, int taps ) {
	int i;
	char *s = buf;

	*s = '\0';

	s = Q_stradd( s,
		"!!ARBfp1.0 \n"
		"OPTION ARB_precision_hint_fastest; \n"
		"ATTRIB tc = fragment.texcoord[0]; \n" );

	for ( i = 0; i < taps; i++ ) {
		s = Q_stradd( s, va( "PARAM p%i = program.local[%i]; \n", i, i ) ); // tex_offset_x, tex_offset_y, 0.0, weight
	}

	s = Q_stradd( s, "TEMP cc; \n"
		"MOV cc, {0.0, 0.0, 0.0, 1.0};\n" ); // initialize final color

	for ( i = 0; i < taps; i++ ) {
		s = Q_stradd( s, va( "TEMP c%i, tc%i; \n", i, i ) );
	}

	for ( i = 0; i < taps; i++ ) {
		s = Q_stradd( s, va( "ADD tc%i.xy, tc, p%i; \n", i, i ) );
	}

	for ( i = 0; i < taps; i++ ) {
		s = Q_stradd( s, va( "TEX c%i, tc%i, texture[0], 2D; \n", i, i ) );
		s = Q_stradd( s, va( "MAD cc, c%i, p%i.w, cc; \n", i, i ) ); // cc = cc + cN + pN.w
	}

	s = Q_stradd( s,
		"MOV cc.a, 1.0; \n"
		"MOV_SAT result.color, cc; \n"
		"END \n" );

	return buf;
}


static char *ARB_BuildBlendProgram( char *buf, int count ) {
	int i;
	char *s = buf;

	*s = '\0';
	s = Q_stradd( s, 
		"!!ARBfp1.0 \n"
		"OPTION ARB_precision_hint_fastest; \n"
		"ATTRIB tc = fragment.texcoord[0]; \n"
		"TEMP cx, cc;\n"
		"MOV cc, {0.0, 0.0, 0.0, 1.0}; \n" );

	for ( i = 0; i < count; i++ ) {
		s = Q_stradd( s, va( "TEX cx, fragment.texcoord[0], texture[%i], 2D; \n"
			"ADD cc, cx, cc; \n", i ) );
	}

	s = Q_stradd( s,
		"MOV cc.a, 1.0; \n"
		"MOV_SAT result.color, cc; \n"
		"END \n" );

	return buf;
}


// blend 2 texture together
static const char *blend2FP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM factor = program.local[1]; \n"
	"TEMP base; \n"
	"TEMP post; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"TEX post, fragment.texcoord[0], texture[1], 2D; \n"
	"MAD base, post, factor.x, base; \n"
	//"ADD base, base, post; \n"
	"MOV base.w, 1.0; \n"
	"MOV_SAT result.color, base; \n"
	"END \n"
};

// combined blend + gamma correction pass
static const char *blend2gammaFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM gamma = program.local[0]; \n"
	"PARAM factor = program.local[1]; \n"
	"TEMP base; \n"
	"TEMP post; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"TEX post, fragment.texcoord[0], texture[1], 2D; \n"
	//"ADD base, base, post; \n"
	"MAD base, post, factor.x, base; \n"
	"POW base.x, base.x, gamma.x; \n"
	"POW base.y, base.y, gamma.y; \n"
	"POW base.z, base.z, gamma.z; \n"
	"MUL base.xyz, base, gamma.w; \n"
	"%s" // for greyscale shader if needed
	"MOV base.w, 1.0; \n"
	"MOV_SAT result.color, base; \n"
	"END \n" 
};


static void RenderQuad( int w, int h )
{
	qglBegin( GL_QUADS );
		qglTexCoord2f( 0.0f, 0.0f );
		qglVertex2f( 0.0f, h );
		qglTexCoord2f( 0.0f, 1.0f );
		qglVertex2f( 0.0f, 0.0f );
		qglTexCoord2f( 1.0f, 1.0f );
		qglVertex2f( w, 0.0f );
		qglTexCoord2f( 1.0f, 0.0f );
		qglVertex2f( w, h );
	qglEnd();
}


static void ARB_BlurParams( int width, int height, int ksize, qboolean horizontal )
{
	static float weight[ MAX_FILTER_SIZE ];
	static int old_ksize = -1;

	static const float x_k[ MAX_FILTER_SIZE+1 ][ MAX_FILTER_SIZE + 1 ] = {
		// [1/weight], coeff.1, coeff.2, [...]
		{ 0 },
		{ 1.0/1, 1 },
		{ 1.0/2, 1, 1 },
	//	{ 1/4,   1, 2, 1 },
		{ 1.0/16,  5, 6, 5 },
		{ 1.0/8,   1, 3, 3, 1 },
		{ 1.0/16,  1, 4, 6, 4, 1 },
		{ 1.0/32,  1, 5, 10, 10, 5, 1 },
		{ 1.0/64,  1, 6, 15, 20, 15, 6, 1 },
		{ 1.0/128, 1, 7, 21, 35, 35, 21, 7, 1 },
		{ 1.0/256, 1, 8, 28, 56, 70, 56, 28, 8, 1 },
		{ 1.0/512, 1, 9, 36, 84, 126, 126, 84, 36, 9, 1 },
		{ 1.0/1024, 1, 10, 45, 120, 210, 252, 210, 120, 45, 10, 1 },
		{ 1.0/2048, 1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1 },
		{ 1.0/4096, 1, 12, 66, 220, 495, 792, 924, 792, 495, 220, 66, 12, 1 },
		{ 1.0/8192, 1, 13, 78, 286, 715, 1287, 1716, 1716, 1287, 715, 286, 78, 13, 1 },
		{ 1.0/16384, 1, 14, 91, 364, 1001, 2002, 3003, 3432, 3003, 2002, 1001, 364, 91, 14, 1 },
		{ 1.0/32768, 1, 15, 105, 455, 1365, 3003, 5005, 6435, 6435, 5005, 3003, 1365, 455, 105, 15, 1 },
		{ 1.0/65536, 1, 16, 120, 560, 1820, 4368, 8008, 11440, 12870, 11440, 8008, 4368, 1820, 560, 120, 16, 1 },
		{ 1.0/131072, 1, 17, 136, 680, 2380, 6188, 12376, 19448, 24310, 24310, 19448, 12376, 6188, 2380, 680, 136, 17, 1 },
		{ 1.0/262144, 1, 18, 153, 816, 3060, 8568, 18564, 31824, 43758, 48620, 43758, 31824, 18564, 8568, 3060, 816, 153, 18, 1 },
		{ 1.0/524288, 1, 19, 171, 969, 3876, 11628, 27132, 50388, 75582, 92378, 92378, 75582, 50388, 27132, 11628, 3876, 969, 171, 19, 1 },

	};

	static const float x_o[ MAX_FILTER_SIZE+1 ][ MAX_FILTER_SIZE ] = {
		{ 0 },
		{ 0.0 },
		{ -0.5, 0.5 },
	//	{ -1.0, 0.0, 1.0 },
		{ -1.2f, 0.0, 1.2f },
		{ -1.5, -0.5, 0.5, 1.5 },
		{ -2.0, -1.0, 0.0, 1.0, 2.0 },
		{ -2.5, -1.5, -0.5, 0.5, 1.5, 2.5 },
		{ -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0 },
		{ -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5 },
		{ -4.0, -3.0, -2.0, -1.0, 0.0, 1.0,	2.0, 3.0, 4.0 },
		{ -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5 },
		{ -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0 },
		{ -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5 },
		{ -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 },
		{ -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5 },
		{ -7.0, -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 },
		{ -7.5, -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5 },
		{ -8.0, -7.0, -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 },
		{ -8.5, -7.5, -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5 },
		{ -9.0, -8.0, -7.0, -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0 },
		{ -9.5, -8.5, -7.5, -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5 },
	};

	const float *coeffs = x_k[ ksize ] + 1;
	const float *off = x_o[ ksize ];

	int i;
	float rsum;
	float texel_size_x;
	float texel_size_y;
	float offset[ MAX_FILTER_SIZE ][ 2 ]; // xy

	// texel size
	texel_size_x = 1.0 / (float) width;
	texel_size_y = 1.0 / (float) height;
	rsum = x_k[ ksize ][ 0 ];

	if ( old_ksize != ksize ) {
		old_ksize = ksize;
		for ( i = 0; i < ksize; i++ ) {
			weight[i] = coeffs[i] * rsum;
		}
	}

	// calculate texture offsets for lookup
	for ( i = 0; i < ksize; i++ ) {
		offset[i][0] = texel_size_x * off[i];
		offset[i][1] = texel_size_y * off[i];
	}

	if ( horizontal ) {
		// horizontal pass
		for (  i = 0; i < ksize; i++ )
			qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, i, offset[i][0], 0.0, 0.0, weight[i] );
	} else {
		// vertical pass
		for (  i = 0; i < ksize; i++ )
			qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, i, 0.0, offset[i][1], 0.0, weight[i] );
	}
}


static void ARB_DeletePrograms( void )
{
	qglDeleteProgramsARB( ARRAY_LEN( programs ) - PROGRAM_BASE, programs + PROGRAM_BASE );
	Com_Memset( programs, 0, sizeof( programs ) );
	programCompiled = 0;
}


qboolean ARB_CompileProgram( programType ptype, const char *text, GLuint program )
{
	GLint errorPos;
	unsigned int errCode;
	int kind;

	if ( ptype == Fragment )
		kind = GL_FRAGMENT_PROGRAM_ARB;
	else
		kind = GL_VERTEX_PROGRAM_ARB;

	qglBindProgramARB( kind, program );
	qglProgramStringARB( kind, GL_PROGRAM_FORMAT_ASCII_ARB, strlen( text ), text );
	qglGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &errorPos );
	if ( (errCode = qglGetError()) != GL_NO_ERROR || errorPos != -1 ) 
	{
		// we may receive error with active FBO but compiled programs will continue to work properly
		if ( (errCode == GL_INVALID_OPERATION && !fboAvailable) || errorPos != -1 ) 
		{
			ri.Printf( PRINT_ALL, S_COLOR_YELLOW "%s Compile Error(%i,%i): %s\n" S_COLOR_CYAN "%s\n", (ptype == Fragment) ? "FP" : "VP", 
				errCode, errorPos, qglGetString( GL_PROGRAM_ERROR_STRING_ARB ), text );
			qglBindProgramARB( kind, 0 );
			ARB_DeletePrograms();
			return qfalse;
		}
	}

	return qtrue;
}


qboolean ARB_UpdatePrograms( void )
{
#ifdef USE_PMLIGHT
	const char *program;
#endif
	char buf[4096];

	if ( !qglGenProgramsARB || !programAvailable )
		return qfalse;

	if ( programCompiled ) // delete old programs
	{
		ARB_ProgramDisable();
		ARB_DeletePrograms();
	}

	qglGenProgramsARB( ARRAY_LEN( programs ) - PROGRAM_BASE, programs + PROGRAM_BASE );

#ifdef USE_PMLIGHT
	if ( !ARB_CompileProgram( Vertex, va( dlightVP, "" ), programs[ DLIGHT_VERTEX ] ) )
		return qfalse;
	if ( !ARB_CompileProgram( Vertex, va( dlightVP, fogInVPCode ), programs[ DLIGHT_VERTEX_FOG_IN ] ) )
		return qfalse;
	if ( !ARB_CompileProgram( Vertex, va( dlightVP, fogOutVPCode ), programs[ DLIGHT_VERTEX_FOG_OUT ] ) )
		return qfalse;

	program = ARB_BuildDlightFP( buf, qfalse );
	if ( !ARB_CompileProgram( Fragment, program, programs[ DLIGHT_FRAGMENT ] ) )
		return qfalse;
	program = ARB_BuildDlightFP( buf, qtrue );
	if ( !ARB_CompileProgram( Fragment, program, programs[ DLIGHT_FRAGMENT_FOG ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Vertex, va( dlightVP_linear, "" ), programs[ DLIGHT_LINEAR_VERTEX ] ) )
		return qfalse;
	if ( !ARB_CompileProgram( Vertex, va( dlightVP_linear, fogInVPCode ), programs[ DLIGHT_LINEAR_VERTEX_FOG_IN ] ) )
		return qfalse;
	if ( !ARB_CompileProgram( Vertex, va( dlightVP_linear, fogOutVPCode ), programs[ DLIGHT_LINEAR_VERTEX_FOG_OUT ] ) )
		return qfalse;

	program = ARB_BuildLinearDlightFP( buf, qfalse );
	if ( !ARB_CompileProgram( Fragment, program, programs[ DLIGHT_LINEAR_FRAGMENT ] ) )
		return qfalse;
	program = ARB_BuildLinearDlightFP( buf, qtrue );
	if ( !ARB_CompileProgram( Fragment, program, programs[ DLIGHT_LINEAR_FRAGMENT_FOG ] ) )
		return qfalse;
#endif // USE_PMLIGHT

	if ( !ARB_CompileProgram( Vertex, dummyVP, programs[ DUMMY_VERTEX ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, spriteFP, programs[ SPRITE_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, va( gammaFP, ARB_BuildGreyscaleProgram( buf ) ), programs[ GAMMA_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, ARB_BuildBloomProgram( buf ), programs[ BLOOM_EXTRACT_FRAGMENT ] ) )
		return qfalse;
	
	// only 1, 2, 3, 6, 8, 10, 12, 14, 16, 18 and 20 produces real visual difference
	fboBloomFilterSize = r_bloom_filter_size->integer;
	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlurProgram( buf, fboBloomFilterSize ), programs[ BLUR_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlurProgram( buf, 6 ), programs[ BLUR2_FRAGMENT ] ) )
		return qfalse;

	fboBloomBlendBase = r_bloom_blend_base->integer;
	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlendProgram( buf, r_bloom_passes->integer - fboBloomBlendBase ), programs[ BLENDX_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, blend2FP, programs[ BLEND2_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, va( blend2gammaFP, ARB_BuildGreyscaleProgram( buf ) ), programs[ BLEND2_GAMMA_FRAGMENT ] ) )
		return qfalse;

	programCompiled = 1;

	return qtrue;
}

static void FBO_Bind( GLuint target, GLuint buffer );

void FBO_Clean( frameBuffer_t *fb )
{
	if ( fb->fbo )
	{
		FBO_Bind( GL_FRAMEBUFFER, fb->fbo );
		if ( fb->multiSampled )
		{
			qglBindRenderbuffer( GL_RENDERBUFFER, 0 );
			if ( fb->color )
			{
				qglDeleteRenderbuffers( 1, &fb->color );
				fb->color = 0;
			}
			if ( fb->depthStencil )
			{
				qglDeleteRenderbuffers( 1, &fb->depthStencil );
				fb->depthStencil = 0;
			}
		}
		else
		{
			GL_BindTexture( 0, 0 );
			if ( fb->color )
			{
				qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
				qglDeleteTextures( 1, &fb->color );
				fb->color = 0;
			}
			if ( fb->depthStencil )
			{
#ifdef DEPTH_RENDER_BUFFER
				if ( fb->depthStencil && fb->depthStencil != commonDepthStencil )
				{
					qglDeleteRenderbuffers( 1, &fb->depthStencil );
					fb->depthStencil = 0;
				}
#else
				if ( r_stencilbits->integer == 0 )
					qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );
				else
					qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );

				if ( fb->depthStencil && fb->depthStencil != commonDepthStencil )
				{
					qglDeleteTextures( 1, &fb->depthStencil );
					fb->depthStencil = 0;
				}
#endif
			}
		}
		FBO_Bind( GL_FRAMEBUFFER, 0 );
		qglDeleteFramebuffers( 1, &fb->fbo );
		fb->fbo = 0;
	}
	Com_Memset( fb, 0, sizeof( *fb ) );
}


static void FBO_CleanBloom( void )
{
	int i;
	for ( i = 0; i < MAX_BLUR_PASSES; i++ )
	{
		FBO_Clean( &frameBuffers[ i * 2 + BLOOM_BASE + 0 ] );
		FBO_Clean( &frameBuffers[ i * 2 + BLOOM_BASE + 1 ] );
	}
}


static void FBO_CleanDepth( void )
{
#ifdef COMMON_DEPTH_STENCIL
	if ( commonDepthStencil )
	{
#ifdef DEPTH_RENDER_BUFFER
		qglDeleteRenderbuffers( 1, &commonDepthStencil );
#else
		GL_BindTexture( 0, 0 );
		qglDeleteTextures( 1, &commonDepthStencil );
#endif
		commonDepthStencil = 0;
	}
#endif
}


static GLuint FBO_CreateDepthTextureOrBuffer( GLsizei width, GLsizei height )
{
#ifdef DEPTH_RENDER_BUFFER
	GLuint buffer;
	qglGenRenderbuffers( 1, &buffer );
	qglBindRenderbuffer( GL_RENDERBUFFER, buffer );
	if ( r_stencilbits->integer == 0 )
		qglRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT32, width, height );
	else
		qglRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height );
	return buffer;
#else
	GLuint tex;
	qglGenTextures( 1, &tex );
	GL_BindTexture( 0, tex );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	if ( r_stencilbits->integer == 0 )
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL );
	else
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL );
	return tex;
#endif
}


static const char *glDefToStr( GLint define )
{
	#define CASE_STR(x) case (x): return #x
	static int index;
	static char buf[8][32];
	char *s;

	switch ( define )
	{
		// texture formats
		CASE_STR(GL_BGR);
		CASE_STR(GL_BGRA);
		CASE_STR(GL_RGB);
		CASE_STR(GL_RGBA);
		CASE_STR(GL_RGBA4);
		CASE_STR(GL_RGBA8);
		CASE_STR(GL_RGBA12);
		CASE_STR(GL_RGBA16);
		CASE_STR(GL_RGB10_A2);
		CASE_STR(GL_R11F_G11F_B10F);
		// data types
		CASE_STR(GL_BYTE);
		CASE_STR(GL_UNSIGNED_BYTE);
		CASE_STR(GL_SHORT);
		CASE_STR(GL_UNSIGNED_SHORT);
		CASE_STR(GL_INT);
		CASE_STR(GL_UNSIGNED_INT);
		CASE_STR(GL_FLOAT);
		CASE_STR(GL_DOUBLE);
		CASE_STR(GL_UNSIGNED_SHORT_4_4_4_4);
		CASE_STR(GL_UNSIGNED_INT_8_8_8_8);
		CASE_STR(GL_UNSIGNED_INT_10_10_10_2);
		CASE_STR(GL_UNSIGNED_SHORT_4_4_4_4_REV);
		CASE_STR(GL_UNSIGNED_INT_8_8_8_8_REV);
		CASE_STR(GL_UNSIGNED_INT_2_10_10_10_REV);
		CASE_STR(GL_UNSIGNED_NORMALIZED);
		// error codes
		CASE_STR(GL_NO_ERROR);
		CASE_STR(GL_INVALID_ENUM);
		CASE_STR(GL_INVALID_VALUE);
		CASE_STR(GL_INVALID_OPERATION);
		CASE_STR(GL_STACK_OVERFLOW);
		CASE_STR(GL_STACK_UNDERFLOW);
		CASE_STR(GL_OUT_OF_MEMORY);
		// fbo error codes
		CASE_STR(GL_FRAMEBUFFER_COMPLETE);
		CASE_STR(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
		CASE_STR(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
		CASE_STR(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER);
		CASE_STR(GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER);
		CASE_STR(GL_FRAMEBUFFER_UNSUPPORTED);
	}
	s = buf[ index ]; // to handle multiple invocations as function parameters
	sprintf( s, "0x%04x", define );
	index = ( index + 1 ) & 7;
	return s;
}


static void getPreferredFormatAndType( GLint format, GLint *pFormat, GLint *pType )
{
	GLint preferredFormat;
	GLint preferredType;

	if ( qglGetInternalformativ && gl_version >= 43 ) {
		qglGetInternalformativ( GL_TEXTURE_2D, /*GL_RGBA8*/ format, GL_TEXTURE_IMAGE_FORMAT, 1, &preferredFormat );
		if ( qglGetError() != GL_NO_ERROR ) {
			goto __fallback;
		}
		qglGetInternalformativ( GL_TEXTURE_2D, /*GL_RGBA8*/ format, GL_TEXTURE_IMAGE_TYPE, 1, &preferredType );
		if ( qglGetError() != GL_NO_ERROR ) {
			goto __fallback;
		}
		if ( preferredFormat == 0 ) // nVidia ION drivers can do that
			preferredFormat = GL_RGBA;
		if ( preferredType == GL_UNSIGNED_NORMALIZED ) { // Intel HD 530 drivers can do that as well
			if ( format == GL_RGBA12 || format == GL_RGBA16 )
				preferredType = GL_UNSIGNED_SHORT;
			else
				preferredType = GL_UNSIGNED_BYTE;
		}
	} else {
__fallback:
		if ( format == GL_RGBA12 || format == GL_RGBA16 ) {
			preferredFormat = GL_RGBA;
			preferredType = GL_UNSIGNED_SHORT;
		} else {
			preferredFormat = GL_RGBA;
			preferredType = GL_UNSIGNED_BYTE;
		}
	}

	*pFormat = preferredFormat;
	*pType = preferredType;
}


static qboolean FBO_Create( frameBuffer_t *fb, GLsizei width, GLsizei height, qboolean depthStencil, GLint *outFormat, GLint *outType )
{
	int fboStatus;
	GLint internalFormat;
	GLint textureFormat;
	GLint textureType;

	fb->multiSampled = qfalse;
	fb->depthStencil = 0;

	// color texture
	qglGenTextures( 1, &fb->color );
	GL_BindTexture( 0, fb->color );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	// always use GL_RGB10_A2 for bloom textures which is fast as usual GL_RGBA8
	// (GL_R11F_G11F_B10F is a bit slower at least on AMD GPUs)
	// but can provide better precision for blurring, also we barely need more than 10 bits for that,
	// texture formats that doesn't fit into 32bits are just performance-killers for bloom
	if ( fb - frameBuffers >= BLOOM_BASE )
		internalFormat = GL_RGB10_A2;
	else
		internalFormat = fboInternalFormat;

	getPreferredFormatAndType( internalFormat, &textureFormat, &textureType );

	qglTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, textureFormat, textureType, NULL );
	// TODO: handle GL_INVALID_OPERATION in case of unsupported internalFormat/textureFormat
	
	if ( outFormat )
		*outFormat = textureFormat;
	if ( outType )
		*outType = textureType;

	qglGenFramebuffers( 1, &fb->fbo );
	FBO_Bind( GL_FRAMEBUFFER, fb->fbo );
	
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb->color, 0 );

	if ( depthStencil )
	{
#ifdef COMMON_DEPTH_STENCIL
		if ( !commonDepthStencil )
			commonDepthStencil = FBO_CreateDepthTextureOrBuffer( width, height );

		fb->depthStencil = commonDepthStencil;
#else
		fb->depthStencil = FBO_CreateDepthTextureOrBuffer( width, height );
#endif
#ifdef DEPTH_RENDER_BUFFER
		if ( r_stencilbits->integer == 0 )
			qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb->depthStencil );
		else
			qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb->depthStencil );
#else
		if ( r_stencilbits->integer == 0 )
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb->depthStencil, 0 );
		else
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fb->depthStencil, 0 );
#endif
	}

	GL_BindTexture( 0, 0 );

	fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ri.Printf( PRINT_ALL, "Failed to create %s (%s:%s) FBO (status %s, error %s)\n",
			glDefToStr( internalFormat ), glDefToStr( textureFormat ), glDefToStr( textureType ),
			glDefToStr( fboStatus ), glDefToStr( (int)qglGetError() ) );
		FBO_Clean( fb );
		return qfalse;
	}

	fb->width = width;
	fb->height = height;

	qglClearColor( 0.0, 0.0, 0.0, 1.0 );
	if ( depthStencil )
		qglClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );
	else
		qglClear( GL_COLOR_BUFFER_BIT );

	FBO_Bind( GL_FRAMEBUFFER, 0 );

	return qtrue;
}


static qboolean FBO_CreateMS( frameBuffer_t *fb, int width, int height )
{
	GLsizei nSamples = r_ext_multisample->integer;
	int fboStatus;
	
	fb->multiSampled = qtrue;

	if ( nSamples <= 0 || !qglRenderbufferStorageMultisample )
	{
		return qfalse;
	}
	nSamples = PAD( nSamples, 2 );

	qglGenFramebuffers( 1, &fb->fbo );
	FBO_Bind( GL_FRAMEBUFFER, fb->fbo );

	qglGenRenderbuffers( 1, &fb->color );
	qglBindRenderbuffer( GL_RENDERBUFFER, fb->color );
	while ( nSamples > 0 ) {
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, fboInternalFormat, width, height );
		if ( (int)qglGetError() == GL_INVALID_VALUE/* != GL_NO_ERROR */ ) {
			ri.Printf( PRINT_ALL, "...%ix MSAA is not available\n", nSamples );
			nSamples -= 2;
		} else {
			ri.Printf( PRINT_ALL, "...using %ix MSAA\n", nSamples );
			break;
		}
	}

	if ( nSamples <= 0 )
	{
		FBO_Clean( fb );
		return qfalse;
	}
	qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb->color );

	qglGenRenderbuffers( 1, &fb->depthStencil );
	qglBindRenderbuffer( GL_RENDERBUFFER, fb->depthStencil );
	if ( r_stencilbits->integer == 0 )
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, GL_DEPTH_COMPONENT32, width, height );
	else
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, GL_DEPTH24_STENCIL8, width, height );

	if ( (int)qglGetError() != GL_NO_ERROR )
	{
		FBO_Clean( fb );
		return qfalse;
	}

	if ( r_stencilbits->integer == 0 )
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb->depthStencil );
	else
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb->depthStencil );

	fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ri.Printf( PRINT_WARNING, "Failed to create MS FBO (status %s, error %s)\n", glDefToStr( fboStatus ), glDefToStr( (int)qglGetError() ) );
		FBO_Clean( fb );
		return qfalse;
	}

	fb->width = width;
	fb->height = height;

	qglClearColor( 0.0, 0.0, 0.0, 1.0 );
	qglClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );

	FBO_Bind( GL_FRAMEBUFFER, 0 );

	return qtrue;
}


static qboolean FBO_CreateBloom( void )
{
	int width = glConfig.vidWidth;
	int height = glConfig.vidHeight;
	int i;

	fboBloomPasses = 0;

	if ( glConfig.numTextureUnits < r_bloom_passes->integer )
	{
		ri.Printf( PRINT_WARNING, "...not enough texture units (%i) for %i-pass bloom\n",
			glConfig.numTextureUnits, r_bloom_passes->integer );
		return qfalse;
	}

	for ( i = 0; i < r_bloom_passes->integer; i++ )
	{
		// we may need depth/stencil buffers for first bloom buffer in \r_bloom 2 mode
		if ( !FBO_Create( &frameBuffers[ i*2 + BLOOM_BASE + 0 ], width, height, i == 0 ? qtrue : qfalse, NULL, NULL ) ||
			!FBO_Create( &frameBuffers[ i*2 + BLOOM_BASE + 1 ], width, height, qfalse, NULL, NULL ) ) {
			return qfalse;
		}
		width = width / 2;
		height = height / 2;
		fboBloomPasses++;
		if ( width < 2 || height < 2 )
			break;
	}

	ri.Printf( PRINT_ALL, "...%i bloom passes\n", fboBloomPasses );

	return qtrue;
}


GLuint FBO_ScreenTexture( void )
{
	return frameBuffers[ 2 ].color;
}


static void FBO_Bind( GLuint target, GLuint buffer )
{
#if 1
	static GLuint draw_buffer = (GLuint)-1;
	static GLuint read_buffer = (GLuint)-1;
	if ( target == GL_FRAMEBUFFER ) {
		if ( draw_buffer != buffer || read_buffer != buffer )
			qglBindFramebuffer( GL_FRAMEBUFFER, buffer );
		draw_buffer = buffer;
		read_buffer = buffer;
	} else {
		if ( target == GL_READ_FRAMEBUFFER ) {
			if ( read_buffer != buffer )
				qglBindFramebuffer( GL_READ_FRAMEBUFFER, buffer );
			read_buffer = buffer;
		} else {
			if ( draw_buffer != buffer )
				qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, buffer );
			draw_buffer = buffer;
		}
	}
#else
	qglBindFramebuffer( target, buffer );
#endif
}


void FBO_BindMain( void )
{
	if ( fboEnabled )
	{
		const frameBuffer_t *fb;
		if ( frameBufferMultiSampling )
		{
			blitMSfbo = qtrue;
			fb = &frameBufferMS;
		}
		else
		{
			blitMSfbo = qfalse;
			fb = &frameBuffers[ 0 ];
		}
		FBO_Bind( GL_FRAMEBUFFER, fb->fbo );
		fboReadIndex = 0;
	}
}


static void FBO_BlitToBackBuffer( int index )
{
	const frameBuffer_t *src = &frameBuffers[ index ];

	FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, 0 );
	//qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	qglDrawBuffer( GL_BACK );

	if ( windowAdjusted )
	{
		if ( blitClear > 0 )
		{
			blitClear--;
			qglClearColor( 0.0, 0.0, 0.0, 1.0 );
			qglClear( GL_COLOR_BUFFER_BIT );
		}
		qglViewport( blitX0, blitY0, blitX1 - blitX0, blitY1 - blitY0 );
		qglScissor( blitX0, blitY0, blitX1 - blitX0, blitY1 - blitY0 );
	}

	qglBlitFramebuffer( 0, 0, src->width, src->height, blitX0, blitY0, blitX1, blitY1, GL_COLOR_BUFFER_BIT, blitFilter );
	fboReadIndex = index;
}


void FBO_BlitSS( void )
{
	const frameBuffer_t *src = &frameBuffers[ fboReadIndex ];
	const frameBuffer_t *dst = &frameBuffers[ 4 ];

	FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
	
	qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );

	FBO_Bind( GL_READ_FRAMEBUFFER, dst->fbo );
}


void FBO_BlitMS( qboolean depthOnly )
{
	//if ( blitMSfbo )
	//{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;

	const frameBuffer_t *r = &frameBufferMS;
	const frameBuffer_t *d = &frameBuffers[ 0 ];

	fboReadIndex = 0;

	FBO_Bind( GL_READ_FRAMEBUFFER, r->fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, d->fbo );

	if ( depthOnly )
	{
		qglBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST );
		FBO_Bind( GL_READ_FRAMEBUFFER, d->fbo );
		return;
	}

	qglBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	// bind all further reads to main buffer
	FBO_Bind( GL_READ_FRAMEBUFFER, d->fbo );
}


static void FBO_Blur( const frameBuffer_t *fb1, const frameBuffer_t *fb2,  const frameBuffer_t *fb3 )
{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;

	qglViewport( 0, 0, fb1->width, fb1->height );

	// apply horizontal blur - render from FBO1 to FBO2
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb2->fbo );
	GL_BindTexture( 0, fb1->color );
	ARB_ProgramEnable( DUMMY_VERTEX, BLUR_FRAGMENT );
	ARB_BlurParams( fb1->width, fb1->height, fboBloomFilterSize, qtrue );
	RenderQuad( w, h );

	// apply vectical blur - render from FBO2 to FBO3
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb3->fbo );
	GL_BindTexture( 0, fb2->color );
	ARB_BlurParams( fb1->width, fb1->height, fboBloomFilterSize, qfalse );
	RenderQuad( w, h );
}


static void FBO_Blur2( const frameBuffer_t *fb1, const frameBuffer_t *fb2,  const frameBuffer_t *fb3 )
{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;

	qglViewport( 0, 0, fb1->width, fb1->height );

	// apply horizontal blur - render from FBO1 to FBO2
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb2->fbo );
	GL_BindTexture( 0, fb1->color );
	ARB_ProgramEnable( DUMMY_VERTEX, BLUR2_FRAGMENT );
	ARB_BlurParams( fb1->width, fb1->height, 6, qtrue );
	RenderQuad( w, h );

	// apply vectical blur - render from FBO2 to FBO3
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb3->fbo );
	GL_BindTexture( 0, fb2->color );
	ARB_BlurParams( fb1->width, fb1->height, 6, qfalse );
	RenderQuad( w, h );
}


void FBO_CopyScreen( void )
{
	const frameBuffer_t *dst;
	const frameBuffer_t *src;
	int yCrop;

	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );

	// resolve multisample buffer first
	if ( blitMSfbo )
	{
		src = &frameBufferMS;
		dst = &frameBuffers[ 0 ];
		FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
		FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
		qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	}

	src = &frameBuffers[ 0 ];
	dst = &frameBuffers[ 2 ];
	FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );

	yCrop = backEnd.viewParms.viewportHeight / 4;

	qglBlitFramebuffer( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY + yCrop,
		backEnd.viewParms.viewportWidth + backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportHeight + backEnd.viewParms.viewportY,
		0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );

	//if ( !backEnd.projection2D )
	{
		qglMatrixMode( GL_PROJECTION );
		qglLoadIdentity();
		qglOrtho( 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1 );
		qglMatrixMode( GL_MODELVIEW );
		qglLoadIdentity();
		GL_Cull( CT_TWO_SIDED );
		qglDisable( GL_CLIP_PLANE0 );
	}

	qglColor4f( 1, 1, 1, 1 );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	FBO_Blur2( dst, dst+1, dst );
	ARB_ProgramDisable();

	//restore viewport and scissor
	qglViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight ); 
	qglScissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
		backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight ); 

	FBO_BindMain();
}


static void R_Bloom_Quad_Lens( float offset )
{
	const int width = glConfig.vidWidth;
	const int height = glConfig.vidHeight;

	qglBegin( GL_QUADS );
	qglTexCoord2f( 0.0f, 1.0f );
	qglVertex2f( width + offset, height + offset );

	qglTexCoord2f( 0.0f, 0.0f );
	qglVertex2f( width + offset, -offset );

	qglTexCoord2f( 1.0f, 0.0f );
	qglVertex2f( -offset, -offset );

	qglTexCoord2f( 1.0f, 1.0f );
	qglVertex2f( -offset, height + offset );
	qglEnd();
}


static void R_Bloom_LensEffect( float alpha )
{
	// lens rainbow colors
	static const GLfloat lc[][3] = {
		{ 0.78f, 0.23f, 0.34f },
		{ 0.78f, 0.39f, 0.21f },
		{ 0.78f, 0.59f, 0.21f },
		{ 0.71f, 0.75f, 0.21f },
		{ 0.52f, 0.78f, 0.21f },
		{ 0.32f, 0.78f, 0.21f },
		{ 0.21f, 0.78f, 0.28f },
		{ 0.21f, 0.78f, 0.47f },
		{ 0.21f, 0.77f, 0.66f },
		{ 0.21f, 0.67f, 0.78f },
		{ 0.21f, 0.47f, 0.78f },
		{ 0.21f, 0.28f, 0.78f },
		{ 0.35f, 0.21f, 0.78f },
		{ 0.53f, 0.21f, 0.78f },
		{ 0.72f, 0.21f, 0.75f },
		{ 0.78f, 0.21f, 0.59f },
	};
	int i;
	
	alpha /= (float)ARRAY_LEN( lc );
	for ( i = 0; i < ARRAY_LEN( lc ); i++ ) {
		qglColor4f( lc[i][0], lc[i][1], lc[i][2], alpha );
		R_Bloom_Quad_Lens( (i+1)*144 );
	}
}


qboolean FBO_Bloom( const float gamma, const float obScale, qboolean finalStage )
{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;

	frameBuffer_t *src, *dst;
	int finalBloomFBO;
	int i;

	if ( backEnd.doneBloom || !backEnd.doneSurfaces )
	{
		return qfalse;
	}

	backEnd.doneBloom = qtrue;

	if ( !fboBloomInited )
	{
		if ( (fboBloomInited = FBO_CreateBloom() ) == qfalse )
		{
			ri.Printf( PRINT_WARNING, "...error creating framebuffers for bloom\n" );
			ri.Cvar_Set( "r_bloom", "0" );
			FBO_CleanBloom();
			return qfalse;
		}
		else
		{
			ri.Printf( PRINT_ALL, "...bloom framebuffers created\n" );
		}
	}

	if ( blitMSfbo )
	{
		FBO_BlitMS( qfalse );
		blitMSfbo = qfalse;
	}
	
	// extract intensity from main FBO to BLOOM_BASE
	src = &frameBuffers[ 0 ];
	dst = &frameBuffers[ BLOOM_BASE ];
	FBO_Bind( GL_FRAMEBUFFER, dst->fbo );
	GL_BindTexture( 0, src->color );
	qglViewport( 0, 0, dst->width, dst->height );
	ARB_ProgramEnable( DUMMY_VERTEX, BLOOM_EXTRACT_FRAGMENT );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, r_bloom_threshold->value, r_bloom_threshold->value,
		r_bloom_threshold->value, 1.0 );
	RenderQuad( w, h );

	// downscale and blur
	src = frameBuffers + BLOOM_BASE;
	for ( i = 1; i < fboBloomPasses; i++, src+=2 ) {
		dst = src + 2;
		// copy image to next level
#ifdef USE_FBO_BLIT
		FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
		FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
		qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );
#else
		ARB_ProgramDisable();
		FBO_Bind( GL_FRAMEBUFFER, dst->fbo );
		GL_BindTexture( 0, src->color );
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
		qglViewport( 0, 0, dst->width, dst->height );
		RenderQuad( w, h );
#endif
		FBO_Blur( dst, dst+1, dst );
	}

	// restore viewport
	qglViewport( 0, 0, w, h );

	// blend all bloom buffers to BLOOM_BASE+1 texture
	finalBloomFBO = BLOOM_BASE+1;
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ finalBloomFBO ].fbo );
	ARB_ProgramEnable( DUMMY_VERTEX, BLENDX_FRAGMENT );
	// setup all texture units
	for ( i = 0; i < fboBloomPasses - fboBloomBlendBase; i++ ) {
		GL_BindTexture( i, frameBuffers[ (i+fboBloomBlendBase)*2 + BLOOM_BASE ].color );
	}
	RenderQuad( w, h );

	if ( r_bloom_reflection->value )
	{
		ARB_ProgramDisable();

		// copy final bloom image to some downscaled buffer
		src = &frameBuffers[ finalBloomFBO ];
		dst = &frameBuffers[ BLOOM_BASE + 2 + 2 ]; // 4x downscale
		FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
		FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
		qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );
		
		// set render target to paired destination buffer and draw reflections
		FBO_Bind( GL_DRAW_FRAMEBUFFER, (dst+1)->fbo );
		GL_BindTexture( 0, dst->color );
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE );
		qglViewport( 0, 0, dst->width, dst->height );
		R_Bloom_LensEffect( fabs( r_bloom_reflection->value ) );
		
		// restore color and blend mode
		qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
		
		// blur lens effect in paired buffer
		FBO_Blur( dst+1, dst, dst+1 );
		ARB_ProgramDisable();

		// add lens effect to final bloom buffer
		FBO_Bind( GL_FRAMEBUFFER, src->fbo );
		if ( r_bloom_reflection->value > 0 ) {
			GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
		} else {
			// negative reflection values will replace bloom texture with just lens effect
		}
		qglViewport( 0, 0, w, h );
		GL_BindTexture( 0, (dst+1)->color );
		RenderQuad( w, h );

		// restore blend mode
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	}

	if ( windowAdjusted ) {
		finalStage = qfalse; // can't blit directly into back buffer in this case
	}

	// if we don't need to read pixels later - blend directly to back buffer
	if ( finalStage ) {
		if ( backEnd.screenshotMask ) {
			FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ BLOOM_BASE ].fbo );
		} else {
			FBO_Bind( GL_FRAMEBUFFER, 0 );
		}
	} else {
		FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ BLOOM_BASE ].fbo );
	}

	GL_BindTexture( 1, frameBuffers[ finalBloomFBO ].color ); // final bloom texture
	GL_BindTexture( 0, frameBuffers[ 0 ].color ); // original image
	if ( finalStage ) {
		// blend & apply gamma in one pass
		ARB_ProgramEnable( DUMMY_VERTEX, BLEND2_GAMMA_FRAGMENT );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, gamma, gamma, gamma, obScale );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, r_bloom_intensity->value, 0, 0, 0 );
	} else {
		// just blend
		ARB_ProgramEnable( DUMMY_VERTEX, BLEND2_FRAGMENT );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, r_bloom_intensity->value, 0, 0, 0 );
	}
	RenderQuad( w, h );
	ARB_ProgramDisable();

	if ( finalStage ) {
		if ( backEnd.screenshotMask ) {
			FBO_BlitToBackBuffer( BLOOM_BASE ); // so any further qglReadPixels() will read from BLOOM_BASE
			 // fboReadIndex = 0;
		} else {
			//	already in back buffer
			fboReadIndex = 0;
		}
	} else {
		// we need depth/stencil buffers there
		fboReadIndex = BLOOM_BASE;
	}

	return finalStage;
}


void R_BloomScreen( void )
{
	if ( r_bloom->integer == 1 && fboEnabled )
	{
		if ( !backEnd.doneBloom && backEnd.doneSurfaces )
		{
			if ( !backEnd.projection2D )
				RB_SetGL2D();
			qglColor4f( 1, 1, 1, 1 );
			FBO_Bloom( 0, 0, qfalse );
		}
	}
}


void FBO_PostProcess( void )
{
	const float obScale = 1 << tr.overbrightBits;
	const float gamma = 1.0f / r_gamma->value;
	const float w = glConfig.vidWidth;
	const float h = glConfig.vidHeight;
	qboolean minimized;

	ARB_ProgramDisable();

	if ( !backEnd.projection2D )
	{
		qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		qglMatrixMode( GL_PROJECTION );
		qglLoadIdentity();
		qglOrtho( 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1 );
		qglMatrixMode( GL_MODELVIEW );
		qglLoadIdentity();
		backEnd.projection2D = qtrue;
	}

	if ( blitMSfbo )
	{
		FBO_BlitMS( qfalse );
		blitMSfbo = qfalse;
	}

	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_Cull( CT_TWO_SIDED );
	if ( r_anaglyphMode->integer )
		qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	minimized = ri.CL_IsMinimized();

	if ( r_bloom->integer && programCompiled ) {
		if ( FBO_Bloom( gamma, obScale, !minimized ) ) {
			return;
		}
	}

	// check if we can perform final draw directly into back buffer
	if ( backEnd.screenshotMask == 0 && !windowAdjusted && !minimized ) {
		FBO_Bind( GL_FRAMEBUFFER, 0 );
		GL_BindTexture( 0, frameBuffers[ fboReadIndex ].color );
		ARB_ProgramEnable( DUMMY_VERTEX, GAMMA_FRAGMENT );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, gamma, gamma, gamma, obScale );
		RenderQuad( w, h );
		ARB_ProgramDisable();
		return;
	}

	// apply gamma shader
	FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ 1 ].fbo ); // destination - secondary buffer
	GL_BindTexture( 0, frameBuffers[ fboReadIndex ].color );  // source - main color buffer
	ARB_ProgramEnable( DUMMY_VERTEX, GAMMA_FRAGMENT );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, gamma, gamma, gamma, obScale );
	RenderQuad( w, h );
	ARB_ProgramDisable();

	if ( !minimized ) {
		FBO_BlitToBackBuffer( 1 );
	}
}


static void QGL_InitPrograms( void )
{
	float version;
	programAvailable = 0;

	if ( !qglGenProgramsARB )
		return;

	version = atof( (const char *)qglGetString( GL_VERSION ) );

	gl_version = (int)(version * 10.001);

	programAvailable = 1;
}


static void QGL_EarlyInitFBO( void )
{
	int scaleMode;
	fboAvailable = qfalse;

	windowAdjusted = qfalse;
	windowWidth = glConfig.vidWidth;
	windowHeight = glConfig.vidHeight;

	blitX0 = blitY0 = 0;
	blitX1 = windowWidth;
	blitY1 = windowHeight;

	superSampled = qfalse;

	if ( !programAvailable || !qglGenFramebuffers || !qglBlitFramebuffer )
		return;

	if ( !r_fbo->integer )
	{
		if ( r_renderScale->integer )
		{
			Com_Printf( "...ignoring r_renderScale due to disabled FBO\n" );
		}
		return;
	}

	if ( r_renderScale->integer )
	{
		glConfig.vidWidth = r_renderWidth->integer;
		glConfig.vidHeight = r_renderHeight->integer;
	}

	captureWidth = glConfig.vidWidth;
	captureHeight = glConfig.vidHeight;

	if ( r_ext_supersample->integer )
	{
		glConfig.vidWidth *= 2;
		glConfig.vidHeight *= 2;
		superSampled = qtrue;
		ri.CL_SetScaling( 2.0, captureWidth, captureHeight );
		blitFilter = GL_LINEAR; // default value for (r_renderScale==0) case
	}

	if ( windowWidth != glConfig.vidWidth || windowHeight != glConfig.vidHeight )
	{
		if ( r_renderScale->integer > 0 )
		{
			scaleMode = r_renderScale->integer - 1;
			if ( scaleMode & 1 )
			{
				// preserve aspect ratio (black bars on sides)
				float windowAspect = (float) windowWidth / (float) windowHeight;
				float renderAspect = (float) glConfig.vidWidth / (float) glConfig.vidHeight;
				if ( windowAspect >= renderAspect ) 
				{
					float scale = (float) windowHeight / ( float ) glConfig.vidHeight;
					int bias = ( windowWidth - scale * (float) glConfig.vidWidth ) / 2;
					blitX0 += bias;
					blitX1 -= bias;
				}
				else
				{
					float scale = (float) windowWidth / ( float ) glConfig.vidWidth;
					int bias = ( windowHeight - scale * (float) glConfig.vidHeight ) / 2;
					blitY0 += bias;
					blitY1 -= bias;
				}
			}
			// linear filtering
			if ( scaleMode & 2 )
				blitFilter = GL_LINEAR;
			else
				blitFilter = GL_NEAREST;
		}

		windowAdjusted = qtrue;
	}
	else
	{
		blitFilter = GL_NEAREST;

		windowAdjusted = qfalse;
	}

	fboAvailable = qtrue;
}


void QGL_DoneFBO( void )
{
	if ( fboAvailable )
	{
		FBO_Bind(GL_FRAMEBUFFER, 0);
		FBO_Clean(&frameBufferMS);
		FBO_Clean(&frameBuffers[0]);
		FBO_Clean(&frameBuffers[1]);
		FBO_Clean(&frameBuffers[2]);
		FBO_Clean(&frameBuffers[3]);
		FBO_Clean(&frameBuffers[4]);
		FBO_CleanBloom();
		FBO_CleanDepth();
		fboEnabled = qfalse;
		fboBloomInited = qfalse;
	}
}


void QGL_InitFBO( void )
{
	int w, h;
	qboolean depthStencil;
	qboolean result = qfalse;

	QGL_DoneFBO();

	w = glConfig.vidWidth;
	h = glConfig.vidHeight;
	
	fboEnabled = qfalse;
	frameBufferMultiSampling = qfalse;

	if ( r_fbo->integer && ( !programAvailable || !fboAvailable ) )
		ri.Printf( PRINT_WARNING, "...FBO is not available\n" );

	if ( !r_fbo->integer || !programAvailable || !fboAvailable )
		return;

	qglGetError(); // reset error code

	if ( windowAdjusted )
		blitClear = 2; // front & back buffers
	else
		blitClear = 0;

	switch ( r_hdr->integer )
	{
		case -1: fboInternalFormat = GL_RGBA4; break;
		case 0: fboInternalFormat = GL_RGBA8; break;
		default: fboInternalFormat = GL_RGBA16; break;
	}

	if ( FBO_CreateMS( &frameBufferMS, w, h ) )
	{
		frameBufferMultiSampling = qtrue;
		if ( r_flares->integer )
			depthStencil = qtrue;
		else
			depthStencil = qfalse;
		result = FBO_Create( &frameBuffers[ 0 ], w, h, depthStencil, &fboTextureFormat, &fboTextureType )
			&& FBO_Create( &frameBuffers[ 1 ], w, h, depthStencil, NULL, NULL )
			&& FBO_Create( &frameBuffers[ 2 ], SCR_WIDTH, SCR_HEIGHT, qfalse, NULL, NULL )
			&& FBO_Create( &frameBuffers[ 3 ], SCR_WIDTH, SCR_HEIGHT, qfalse, NULL, NULL );
		frameBufferMultiSampling = result;
	}
	else
	{
		result = FBO_Create( &frameBuffers[ 0 ], w, h, qtrue, &fboTextureFormat, &fboTextureType )
			&& FBO_Create( &frameBuffers[ 1 ], w, h, qtrue, NULL, NULL )
			&& FBO_Create( &frameBuffers[ 2 ], SCR_WIDTH, SCR_HEIGHT, qfalse, NULL, NULL )
			&& FBO_Create( &frameBuffers[ 3 ], SCR_WIDTH, SCR_HEIGHT, qfalse, NULL, NULL );
	}

	if ( result && superSampled )
	{
		result &= FBO_Create( &frameBuffers[ 4 ], captureWidth, captureHeight, qfalse, NULL, NULL );
	}

	if ( result )
	{
		fboEnabled = qtrue;
		FBO_BindMain();
		ri.Printf( PRINT_ALL, "...using %s (%s:%s) FBO\n", glDefToStr( fboInternalFormat ),
			glDefToStr( fboTextureFormat ), glDefToStr( fboTextureType ) );
	}
	else
	{
		QGL_DoneFBO();
	}
}


void QGL_InitARB( void )
{
	QGL_InitPrograms();
	ARB_UpdatePrograms();
	QGL_EarlyInitFBO();
	QGL_InitFBO();
	ri.Cvar_ResetGroup( CVG_RENDERER, qtrue );
}


void QGL_DoneARB( void )
{
	QGL_DoneFBO();

	if ( programCompiled )
	{
		ARB_ProgramDisable();
		ARB_DeletePrograms();
	}

	programAvailable = 0;
}
