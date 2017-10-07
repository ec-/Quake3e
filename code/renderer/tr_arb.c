#include "tr_local.h"
#include "tr_common.h"

#ifdef USE_PMLIGHT

#define MAX_BLUR_PASSES MAX_TEXTURE_UNITS
#define FBO_COUNT (2+(MAX_BLUR_PASSES*2))
#define BLOOM_BASE 2

#define MAX_FILTER_SIZE 16
#define MIN_FILTER_SIZE 1

typedef enum {
	DLIGHT_VERTEX,
	DLIGHT_FRAGMENT,

	DLIGHT_LINEAR_VERTEX,
	DLIGHT_LINEAR_FRAGMENT,

	DUMMY_VERTEX,

	SPRITE_FRAGMENT,
	GAMMA_FRAGMENT,
	BLOOM_EXTRACT_FRAGMENT,
	BLUR_FRAGMENT,
	BLENDX_FRAGMENT,
	BLEND2_FRAGMENT,
	BLEND2_GAMMA_FRAGMENT,

	PROGRAM_COUNT

} programNum;

typedef enum {
	Vertex,
	Fragment
} programType;

cvar_t *r_bloom2_threshold;
cvar_t *r_bloom2_threshold_mode;
cvar_t *r_bloom2_modulate;
cvar_t *r_bloom2_passes;
cvar_t *r_bloom2_blend_base;
cvar_t *r_bloom2_intensity;
cvar_t *r_bloom2_filter_size;

static GLuint programs[ PROGRAM_COUNT ];
static GLuint current_vp;
static GLuint current_fp;

static int programAvail	= 0;
static int programCompiled = 0;
static int programEnabled	= 0;
static int gl_version = 0;

void ( APIENTRY * qglGenProgramsARB )( GLsizei n, GLuint *programs );
void ( APIENTRY * qglDeleteProgramsARB)( GLsizei n, const GLuint *programs );
void ( APIENTRY * qglProgramStringARB )( GLenum target, GLenum format, GLsizei len, const GLvoid *string );
void ( APIENTRY * qglBindProgramARB )( GLenum target, GLuint program );
void ( APIENTRY * qglProgramLocalParameter4fARB )( GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w );

qboolean fboAvailable = qfalse;
qboolean fboEnabled = qfalse;
qboolean fboBloomInited = qfalse;
int      fboReadIndex = 0;
GLuint   fboTextureFormat;
int      fboBloomPasses;
int      fboBloomBlendBase;
int      fboBloomFilterSize;

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

// framebuffer functions
GLboolean (APIENTRY *qglIsRenderbuffer)( GLuint renderbuffer );
void ( APIENTRY *qglBindRenderbuffer )( GLenum target, GLuint renderbuffer );
void ( APIENTRY *qglDeleteFramebuffers )( GLsizei n, const GLuint *framebuffers );
void ( APIENTRY *qglDeleteRenderbuffers )( GLsizei n, const GLuint *renderbuffers );
void ( APIENTRY *qglGenRenderbuffers )( GLsizei n, GLuint *renderbuffers );
void ( APIENTRY *qglRenderbufferStorage )( GLenum target, GLenum internalformat, GLsizei width, GLsizei height );
void ( APIENTRY *qglGetRenderbufferParameteriv )( GLenum target, GLenum pname, GLint *params );
GLboolean ( APIENTRY *qglIsFramebuffer)( GLuint framebuffer );
void ( APIENTRY *qglBindFramebuffer)( GLenum target, GLuint framebuffer );
void ( APIENTRY *qglGenFramebuffers)( GLsizei n, GLuint *framebuffers );
GLenum ( APIENTRY *qglCheckFramebufferStatus )( GLenum target );
void ( APIENTRY *qglFramebufferTexture2D )( GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level );
void ( APIENTRY *qglFramebufferRenderbuffer )( GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer );
void ( APIENTRY *qglGetFramebufferAttachmentParameteriv )( GLenum target, GLenum attachment, GLenum pname, GLint *params );
void ( APIENTRY *qglGenerateMipmap)( GLenum target );
void ( APIENTRY *qglBlitFramebuffer)( GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter );
void ( APIENTRY *qglRenderbufferStorageMultisample )(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);


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


static void ARB_ProgramEnable( programNum vp, programNum fp )
{
	if ( programCompiled )
	{
		if ( current_vp != programs[ vp ] ) {
			current_vp = programs[ vp ];
			qglEnable( GL_VERTEX_PROGRAM_ARB );
			qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, current_vp );
		}

		if ( current_fp != programs[ fp ] ) {
			current_fp = programs[ fp ];
			qglEnable( GL_FRAGMENT_PROGRAM_ARB );
			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, current_fp );
		}
		programEnabled = 1;
	}
}


void GL_ProgramEnable( void ) 
{
	ARB_ProgramEnable( DUMMY_VERTEX, SPRITE_FRAGMENT );
}


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

	R_BindAnimatedImage( &pStage->bundle[ 0 ] );
	
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

	R_BindAnimatedImage( &pStage->bundle[ 0 ] );
	
	R_DrawElements( tess.numIndexes, tess.indexes );
}


void ARB_SetupLightParams( void )
{
	const dlight_t *dl;
	vec3_t lightRGB;
	float radius;

	if ( !programCompiled )
		return;

	dl = tess.light;

	if ( dl->linear ) {
		ARB_ProgramEnable( DLIGHT_LINEAR_VERTEX, DLIGHT_LINEAR_FRAGMENT );
	} else {
		ARB_ProgramEnable( DLIGHT_VERTEX, DLIGHT_FRAGMENT );
	}

	if ( !glConfig.deviceSupportsGamma )
		VectorScale( dl->color, 2 * pow( r_intensity->value, r_gamma->value ), lightRGB );
	else
		VectorCopy( dl->color, lightRGB );

	radius = dl->radius * r_dlightScale->value;

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
}


void ARB_LightingPass( void )
{
	const shaderStage_t* pStage;

	if ( !programAvail )
		return;

	if ( tess.shader->lightingStage == -1 )
		return;

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
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );

	qglEnableClientState( GL_NORMAL_ARRAY );
	qglNormalPointer( GL_FLOAT, 16, tess.normal );

	qglVertexPointer( 3, GL_FLOAT, 16, tess.xyz );

	//if ( qglLockArraysEXT )
	//		qglLockArraysEXT( 0, tess.numVertexes );

	// CPU may limit performance in following cases
	if ( tess.light->linear || gl_version >= 4 )
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

extern cvar_t *r_dlightSpecPower;
extern cvar_t *r_dlightSpecColor;

// welding these into the code to avoid having a pk3 dependency in the engine

static const char *dlightVP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"PARAM posEye = program.local[0]; \n"
	"PARAM posLight = program.local[1]; \n"
	"OUTPUT lv = result.texcoord[1]; \n" // 1
	"OUTPUT ev = result.texcoord[2]; \n" // 2
	"OUTPUT n = result.texcoord[3]; \n"  // 3
	"MOV result.texcoord[0], vertex.texcoord; \n"
	"MOV n, vertex.normal; \n"
	"SUB ev, posEye, vertex.position; \n"
	"SUB lv, posLight, vertex.position; \n"
	"END \n" 
};


static const char *dlightVP_linear = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"PARAM posEye = program.local[0]; \n"
	"OUTPUT fp = result.texcoord[1]; \n"
	"OUTPUT ev = result.texcoord[2]; \n"
	"OUTPUT n = result.texcoord[3]; \n"
	"MOV result.texcoord[0], vertex.texcoord; \n"
	"MOV fp, vertex.position; \n"
	"SUB ev, posEye, vertex.position; \n"
	"MOV n, vertex.normal; \n"
	"END \n"
};


static const char *ARB_BuildLinearDlightFragmentProgram( char *program )
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
	"SUB tmp.x, 1.0, tmp.x; \n"
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

	"MAD base, base, bump.w, spec; \n"
	"MUL_SAT result.color, base, light; \n"
	"END \n" );

	return program;
};


// dynamically apply custom parameters
static const char *ARB_BuildDlightFragmentProgram( char *program )
{
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
	"MUL_SAT result.color, base, light; \n"
	"END \n" 
	);
	
	r_dlightSpecColor->modified = qfalse;
	r_dlightSpecPower->modified = qfalse;

	return program;
}

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

	if ( r_bloom2_threshold_mode->integer == 0 ) {
		// (r|g|b) >= threshold
		s = Q_stradd( s,
			"TEMP minv; \n"
			"SGE minv, base, thres; \n"
			"DP3_SAT minv.w, minv, minv; \n"
			"MUL base.rgb, base, minv.w; \n" );
	} else if ( r_bloom2_threshold_mode->integer == 1 ) {
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
	if ( r_bloom2_modulate->integer ) {
		if ( r_bloom2_modulate->integer == 1 ) {
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


static void ARB_BloomParams( int width, int height, int ksize, qboolean horizontal ) 
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
	qglDeleteProgramsARB( ARRAY_LEN( programs ), programs );
	Com_Memset( programs, 0, sizeof( programs ) );
	programCompiled = 0;
}


static qboolean ARB_CompileProgram( programType ptype, const char *text, GLuint program ) 
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
			ri.Printf( PRINT_ALL, S_COLOR_YELLOW "%s Compile Error(%i,%i): %s", (ptype == Fragment) ? "FP" : "VP", 
				errCode, errorPos, qglGetString( GL_PROGRAM_ERROR_STRING_ARB ) );
			qglBindProgramARB( kind, 0 );
			ARB_DeletePrograms();
			return qfalse;
		}
	}

	return qtrue;
}


qboolean ARB_UpdatePrograms( void ) 
{
	const char *program;
	char buf[4096];

	if ( !qglGenProgramsARB || !programAvail )
		return qfalse;

	if ( programCompiled ) // delete old programs
	{
		ARB_ProgramDisable();
		ARB_DeletePrograms();
	}

	qglGenProgramsARB( ARRAY_LEN( programs ), programs );

	if ( !ARB_CompileProgram( Vertex, dlightVP, programs[ DLIGHT_VERTEX ] ) )
		return qfalse;

	program = ARB_BuildDlightFragmentProgram( buf );

	if ( !ARB_CompileProgram( Fragment, program, programs[ DLIGHT_FRAGMENT ] ) )
		return qfalse;

	program = ARB_BuildLinearDlightFragmentProgram( buf );
	if ( !ARB_CompileProgram( Fragment, program, programs[ DLIGHT_LINEAR_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Vertex, dlightVP_linear, programs[ DLIGHT_LINEAR_VERTEX ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Vertex, dummyVP, programs[ DUMMY_VERTEX ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, spriteFP, programs[ SPRITE_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, va( gammaFP, ARB_BuildGreyscaleProgram( buf ) ), programs[ GAMMA_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, ARB_BuildBloomProgram( buf ), programs[ BLOOM_EXTRACT_FRAGMENT ] ) )
		return qfalse;
	
	// only 1, 2, 3, 6, 8, 10, 12 and 14 produces real visual difference
	fboBloomFilterSize = r_bloom2_filter_size->integer;
	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlurProgram( buf, fboBloomFilterSize ), programs[ BLUR_FRAGMENT ] ) )
		return qfalse;

	fboBloomBlendBase = r_bloom2_blend_base->integer;
	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlendProgram( buf, r_bloom2_passes->integer - fboBloomBlendBase ), programs[ BLENDX_FRAGMENT ] ) )
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
				if ( r_stencilbits->integer == 0 )
					qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );
				else
					qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );
				if ( fb->depthStencil && fb->depthStencil != commonDepthStencil )
				{
					qglDeleteTextures( 1, &fb->depthStencil );
					fb->depthStencil = 0;
				}
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
	if ( commonDepthStencil ) 
	{
		GL_BindTexture( 0, 0 );
		qglDeleteTextures( 1, &commonDepthStencil );
		commonDepthStencil = 0;
	}
}


static GLuint FBO_CreateDepthTexture( GLsizei width, GLsizei height ) 
{
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
}


static qboolean FBO_Create( frameBuffer_t *fb, GLsizei width, GLsizei height, qboolean depthStencil )
{
	int fboStatus;

	fb->multiSampled = qfalse;

	if ( depthStencil )
	{
		if ( !commonDepthStencil ) 
		{
			commonDepthStencil = FBO_CreateDepthTexture( width, height );
		}
		fb->depthStencil = commonDepthStencil;
	}

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
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB10_A2, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL );
	else
		qglTexImage2D( GL_TEXTURE_2D, 0, fboTextureFormat, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL );
	
	qglGenFramebuffers( 1, &fb->fbo );
	FBO_Bind( GL_FRAMEBUFFER, fb->fbo );
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb->color, 0 );

	if ( depthStencil ) 
	{
		if ( r_stencilbits->integer == 0 )
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb->depthStencil, 0 );
		else
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fb->depthStencil, 0 );
	}

	fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ri.Printf( PRINT_ALL, "Failed to create FBO (status %d, error %d)\n", fboStatus, (int)qglGetError() );
		FBO_Clean( fb );
		return qfalse;
	}

	fb->width = width;
	fb->height = height;

	FBO_Bind( GL_FRAMEBUFFER, 0 );

	return qtrue;
}


static qboolean FBO_CreateMS( frameBuffer_t *fb )
{
	GLsizei nSamples = r_ext_multisample->integer;
	int fboStatus;
	
	fb->multiSampled = qtrue;

	if ( nSamples <= 0 )
	{
		return qfalse;
	}
	nSamples = (nSamples + 1) & ~1;

	qglGenFramebuffers( 1, &fb->fbo );
	FBO_Bind( GL_FRAMEBUFFER, fb->fbo );

	qglGenRenderbuffers( 1, &fb->color );
	qglBindRenderbuffer( GL_RENDERBUFFER, fb->color );
	while ( nSamples > 0 ) {
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, fboTextureFormat, glConfig.vidWidth, glConfig.vidHeight );
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
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, GL_DEPTH_COMPONENT32, glConfig.vidWidth, glConfig.vidHeight );
	else
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, GL_DEPTH24_STENCIL8, glConfig.vidWidth, glConfig.vidHeight );

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
		Com_Printf( "Failed to create MS FBO (status 0x%x, error %d)\n", fboStatus, (int)qglGetError() );
		FBO_Clean( fb );
		return qfalse;
	}

	FBO_Bind( GL_FRAMEBUFFER, 0 );

	return qtrue;
}


static qboolean FBO_CreateBloom( int width, int height ) 
{
	int i;

	fboBloomPasses = 0;

	if ( glConfig.numTextureUnits < r_bloom2_passes->integer ) 
	{
		ri.Printf( PRINT_WARNING, "...not enough texture units (%i) for %i-pass bloom\n", 
			glConfig.numTextureUnits, r_bloom2_passes->integer );
		return qfalse;
	}

	for ( i = 0; i < r_bloom2_passes->integer; i++ ) 
	{
		// we may need depth/stencil buffers for first bloom buffer in \r_bloom 2 mode
		if ( !FBO_Create( &frameBuffers[ i*2 + BLOOM_BASE + 0 ], width, height, i == 0 ? qtrue : qfalse ) ||
			 !FBO_Create( &frameBuffers[ i*2 + BLOOM_BASE + 1 ], width, height, qfalse ) ) {
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
	if ( fboAvailable && programAvail ) 
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
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;

	const frameBuffer_t *src = &frameBuffers[ index ];
	FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, 0 );
	//qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	qglDrawBuffer( GL_BACK );

	qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	fboReadIndex = index;
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


static void FBO_Blur( const frameBuffer_t *fb1, const frameBuffer_t *fb2,  const frameBuffer_t *fb3, const int w, const int h ) 
{
	qglViewport( 0, 0, fb1->width, fb1->height );

	// apply horizontal blur - render from FBO1 to FBO2
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb2->fbo );
	GL_BindTexture( 0, fb1->color );
	ARB_ProgramEnable( DUMMY_VERTEX, BLUR_FRAGMENT );
	ARB_BloomParams( fb1->width, fb1->height, fboBloomFilterSize, qtrue );
	RenderQuad( w, h );

	// apply vectical blur - render from FBO2 to FBO3
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb3->fbo );
	GL_BindTexture( 0, fb2->color );
	ARB_BloomParams( fb1->width, fb1->height, fboBloomFilterSize, qfalse );
	RenderQuad( w, h );
}


qboolean FBO_Bloom( const int w, const int h, const float gamma, const float obScale, qboolean finalStage ) 
{
	frameBuffer_t *src, *dst;
	int finalBloomFBO;
	int i;

	if ( backEnd.doneBloom2fbo || !backEnd.doneSurfaces )
	{
		return qfalse;
	}

	backEnd.doneBloom2fbo = qtrue;

	if( !fboBloomInited )  
	{
		if ( (fboBloomInited = FBO_CreateBloom( w, h ) ) == qfalse ) 
		{
			ri.Printf( PRINT_WARNING, "...error creating framebuffers for bloom\n" );
			ri.Cvar_Set( "r_bloom", "0" );
			FBO_CleanBloom();
			return qfalse;
		}
		else 
		{
			ri.Printf( PRINT_ALL, "...bloom framebuffers created\n" );
			if ( fboBloomPasses != r_bloom2_passes->integer ) 
			{
				ri.Printf( PRINT_ALL, "...blend shader updated\n" );
				ARB_UpdatePrograms();
			}
		}
	}

	if ( frameBufferMultiSampling && blitMSfbo ) 
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
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, r_bloom2_threshold->value, r_bloom2_threshold->value,
		r_bloom2_threshold->value, 1.0 );
	RenderQuad( w, h );

	// downscale and blur
	for ( i = 1; i < fboBloomPasses; i++ ) {
		src = &frameBuffers[ i*2 ];
		dst = &frameBuffers[ i*2 + 2 ];
		// copy image to next level
		FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
		FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
		qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );
		FBO_Blur( dst, dst+1, dst, w, h );
	}

	// restore viewport
	qglViewport( 0, 0, w, h );

#if 0 // debug 
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, frameBuffers[0].fbo );
	//qglBindFramebuffer( GL_READ_FRAMEBUFFER, frameBuffers[2].fbo );
	//qglBlitFramebuffer( 0, 0, w/1, h/1, 0, 0, w/2, h/2, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	//qglBindFramebuffer( GL_READ_FRAMEBUFFER, frameBuffers[4].fbo );
	//qglBlitFramebuffer( 0, 0, w/2, h/2, 0, 0, w/2, h/2, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	//qglBindFramebuffer( GL_READ_FRAMEBUFFER, frameBuffers[6].fbo );
	//qglBlitFramebuffer( 0, 0, w/4, h/4, 0, 0, w/2, h/2, GL_COLOR_BUFFER_BIT, GL_LINEAR );
#else

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
	GL_BindTexture( 0, frameBuffers[0].color ); // original image
	if ( finalStage ) {
		// blend & apply gamma in one pass
		ARB_ProgramEnable( DUMMY_VERTEX, BLEND2_GAMMA_FRAGMENT );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, gamma, gamma, gamma, obScale );	
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, r_bloom2_intensity->value, 0, 0, 0 );
	} else {
		// just blend
		ARB_ProgramEnable( DUMMY_VERTEX, BLEND2_FRAGMENT );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, r_bloom2_intensity->value, 0, 0, 0 );
	}
	RenderQuad( w, h );
	ARB_ProgramDisable();

	if ( finalStage ) {
		if ( backEnd.screenshotMask ) {
			FBO_BlitToBackBuffer( BLOOM_BASE ); // so any further qglReadPixels() will read from BLOOM_BASE
			fboReadIndex = 0;
		} else {
			//	already in back buffer
			fboReadIndex = 0;
		}
	} else {
		// we need depth/stencil buffers there
		fboReadIndex = BLOOM_BASE;
	}
#endif
	return qtrue;
}


void FBO_PostProcess( void )
{
	const float obScale = 1 << tr.overbrightBits;
	const float gamma = 1.0f / r_gamma->value;
	const float w = glConfig.vidWidth;
	const float h = glConfig.vidHeight;
	int bloom;

	if ( !fboAvailable )
		return;

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

	if ( frameBufferMultiSampling && blitMSfbo )
		FBO_BlitMS( qfalse );
	blitMSfbo = qfalse;
	
	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_Cull( CT_FRONT_SIDED );

	bloom = ri.Cvar_VariableIntegerValue( "r_bloom" );

	if ( bloom > 1 && programCompiled )
	{
		if ( FBO_Bloom( w, h, gamma, obScale, qtrue ) )
		{
			return;
		}
	}

	// check if we can perform final draw directly into back buffer
	if ( backEnd.screenshotMask == 0 ) {
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

	FBO_BlitToBackBuffer( 1 );
}


static const void *fp;
#define GPA(fn) fp = qwglGetProcAddress( #fn ); if ( !fp ) { Com_Printf( "GPA failed on '%s'\n", #fn ); goto __fail; } else { memcpy( &q##fn, &fp, sizeof( fp ) ); }

static void QGL_InitShaders( void ) 
{
	float version;
	programAvail = 0;

	r_bloom2_threshold = ri.Cvar_Get( "r_bloom2_threshold", "0.6", CVAR_ARCHIVE_ND );
	ri.Cvar_SetGroup( r_bloom2_threshold, CVG_RENDERER );
	r_bloom2_threshold_mode = ri.Cvar_Get( "r_bloom2_threshold_mode", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetGroup( r_bloom2_threshold_mode, CVG_RENDERER );
	r_bloom2_intensity = ri.Cvar_Get( "r_bloom2_intensity", "0.5", CVAR_ARCHIVE_ND );
	r_bloom2_passes = ri.Cvar_Get( "r_bloom2_passes", "5", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_bloom2_passes, "2", XSTRING( MAX_BLUR_PASSES ), CV_INTEGER );
	r_bloom2_blend_base = ri.Cvar_Get( "r_bloom2_blend_base", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_SetGroup( r_bloom2_blend_base, CVG_RENDERER );
	ri.Cvar_CheckRange( r_bloom2_blend_base, "0", va("%i", r_bloom2_passes->integer-1), CV_INTEGER );
	r_bloom2_modulate = ri.Cvar_Get( "r_bloom2_modulate", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetGroup( r_bloom2_modulate, CVG_RENDERER );
	r_bloom2_filter_size = ri.Cvar_Get( "r_bloom2_filter_size", "3", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_bloom2_filter_size, XSTRING( MIN_FILTER_SIZE ), XSTRING( MAX_FILTER_SIZE ), CV_INTEGER );
	ri.Cvar_SetGroup( r_bloom2_filter_size, CVG_RENDERER );

	if ( !r_allowExtensions->integer )
		return;

	version = atof( (const char *)qglGetString( GL_VERSION ) );

	gl_version = (int)version;

	if ( version < 1.4 ) {
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "...OpenGL 1.4 is not available\n" );
		return;
	}

	if ( !GLimp_HaveExtension( "GL_ARB_vertex_program" ) ) {
		return;
	}

	if ( !GLimp_HaveExtension( "GL_ARB_fragment_program" ) ) {
		return;
	}

	GPA( glGenProgramsARB );
	GPA( glBindProgramARB );
	GPA( glProgramStringARB );
	GPA( glDeleteProgramsARB );
	GPA( glProgramLocalParameter4fARB );
	programAvail = 1;

	ri.Printf( PRINT_ALL, "...using ARB shaders\n" );

__fail:
	return;
}


static void QGL_InitFBO( void ) 
{
	fboAvailable = qfalse;

	if ( !r_allowExtensions->integer )
		return;

	if ( !programAvail )
		return;

	if ( !GLimp_HaveExtension( "GL_EXT_framebuffer_object" ) )
		return;

	if ( !GLimp_HaveExtension( "GL_EXT_framebuffer_blit" ) )
		return;

	if ( !GLimp_HaveExtension( "GL_EXT_framebuffer_multisample" ) )
		return;

	GPA( glBindRenderbuffer );
	GPA( glBlitFramebuffer );
	GPA( glDeleteRenderbuffers );
	GPA( glGenRenderbuffers );
	GPA( glGetRenderbufferParameteriv );
	GPA( glIsFramebuffer );
	GPA( glBindFramebuffer );
	GPA( glDeleteFramebuffers );
	GPA( glCheckFramebufferStatus );
	GPA( glFramebufferTexture2D );
	GPA( glFramebufferRenderbuffer );
	GPA( glGenerateMipmap );
	GPA( glGenFramebuffers );
	GPA( glGetFramebufferAttachmentParameteriv );
	GPA( glIsRenderbuffer );
	GPA( glRenderbufferStorage );
	if ( r_ext_multisample->integer ) 
	{
		GPA( glRenderbufferStorageMultisample );
	}
	fboAvailable = qtrue;
__fail:
	return;
}


void QGL_EarlyInitARB( void ) 
{
	QGL_InitShaders();
	QGL_InitFBO();
}


void QGL_DoneARB( void );
void QGL_InitARB( void )
{
	if ( ARB_UpdatePrograms() )
	{
		if ( r_fbo->integer && fboAvailable ) 
		{
			int w, h, hdr;
			qboolean depthStencil;
			qboolean result = qfalse;
			frameBufferMultiSampling = qfalse;
			
			w = glConfig.vidWidth;
			h = glConfig.vidHeight;

			hdr = ri.Cvar_VariableIntegerValue( "r_hdr" );
			switch ( hdr ) {
				case -2: fboTextureFormat = GL_RGBA4; break;
				case -1: fboTextureFormat = GL_RGB5_A1; break;
				case 0: fboTextureFormat = GL_RGBA8; break;
				case 1: fboTextureFormat = GL_RGB10_A2; break;
				case 2: fboTextureFormat = GL_R11F_G11F_B10F; break;
				default: fboTextureFormat = GL_RGBA12; break;
			}

			if ( FBO_CreateMS( &frameBufferMS ) ) 
			{
				frameBufferMultiSampling = qtrue;
				if ( r_flares->integer )
					depthStencil = qtrue;
				else
					depthStencil = qfalse;

				result = FBO_Create( &frameBuffers[ 0 ], w, h, depthStencil ) && FBO_Create( &frameBuffers[ 1 ], w, h, depthStencil );
				frameBufferMultiSampling = result;
			}
			else 
			{
				result = FBO_Create( &frameBuffers[ 0 ], w, h, qtrue ) && FBO_Create( &frameBuffers[ 1 ], w, h, qtrue );
			}

			if ( result ) 
			{
				FBO_BindMain();
				qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
			}
			else 
			{
				FBO_Clean( &frameBufferMS );
				FBO_Clean( &frameBuffers[ 0 ] );
				FBO_Clean( &frameBuffers[ 1 ] );
				FBO_CleanBloom();
				FBO_CleanDepth();

				fboAvailable = qfalse;
				fboBloomInited = qfalse;
			}
		}
		else 
		{
			fboAvailable = qfalse;
		}


		if ( fboAvailable ) 
		{
			ri.Printf( PRINT_ALL, "...using FBO\n" );
		}
	}
	else
	{
		QGL_DoneARB();
	}
}


void QGL_DoneARB( void )
{
	if ( programCompiled )
	{
		ARB_ProgramDisable();
		ARB_DeletePrograms();
	}

	FBO_Clean( &frameBufferMS );
	FBO_Clean( &frameBuffers[ 0 ] );
	FBO_Clean( &frameBuffers[ 1 ] );
	FBO_CleanBloom();
	FBO_CleanDepth();

	fboAvailable = qfalse;
	fboBloomInited = qfalse;

	programAvail = 0;

	qglGenProgramsARB		= NULL;
	qglDeleteProgramsARB	= NULL;
	qglProgramStringARB		= NULL;
	qglBindProgramARB		= NULL;
	qglProgramLocalParameter4fARB = NULL;
}

#endif // USE_PMLIGHT
