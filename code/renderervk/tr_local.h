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

#ifndef TR_LOCAL_H
#define TR_LOCAL_H

#define USE_VBO				// store static world geometry in VBO
#define USE_FOG_ONLY
#define USE_FOG_COLLAPSE	// not compatible with legacy dlights
#if defined (USE_VBO) && !defined(USE_FOG_ONLY)
#define USE_FOG_ONLY
#endif
#define USE_LEGACY_DLIGHTS	// vq3 dynamic lights
#define USE_PMLIGHT			// promode dynamic lights via \r_dlightMode 1|2
#define MAX_REAL_DLIGHTS	(MAX_DLIGHTS*2)
#define MAX_LITSURFS		(MAX_DRAWSURFS)
#define	MAX_FLARES			256

#define MAX_TEXTURE_SIZE	2048 // must be less or equal to 32768

//#define USE_TESS_NEEDS_NORMAL
//#define USE_TESS_NEEDS_ST2

#include "../qcommon/q_shared.h"
#include "../qcommon/qfiles.h"
#include "../qcommon/qcommon.h"
#include "../renderercommon/tr_public.h"
#include "tr_common.h"
#include "iqm.h"


#ifdef USE_VULKAN
#include "vk.h"
// GL constants substitutions
typedef enum {
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST_MIPMAP_NEAREST,
	GL_LINEAR_MIPMAP_NEAREST,
	GL_NEAREST_MIPMAP_LINEAR,
	GL_LINEAR_MIPMAP_LINEAR,
	GL_MODULATE,
	GL_ADD,
	GL_ADD_NONIDENTITY,

	GL_BLEND_MODULATE,
	GL_BLEND_ADD,
	GL_BLEND_ALPHA,
	GL_BLEND_ONE_MINUS_ALPHA,
	GL_BLEND_MIX_ALPHA, // SRC_ALPHA + ONE_MINUS_SRC_ALPHA
	GL_BLEND_MIX_ONE_MINUS_ALPHA, // ONE_MINUS_SRC_ALPHA + SRC_ALPHA

	GL_BLEND_DST_COLOR_SRC_ALPHA, // GLS_SRCBLEND_DST_COLOR + GLS_DSTBLEND_SRC_ALPHA

	GL_DECAL,
	GL_BACK_LEFT,
	GL_BACK_RIGHT
} glCompat;

#define GL_INDEX_TYPE		uint32_t
#define GLint				int
#define GLuint				unsigned int
#define GLboolean			VkBool32
#else
#define GL_INDEX_TYPE		GL_UNSIGNED_INT
#endif

typedef uint32_t glIndex_t;

#define	REFENTITYNUM_BITS	12	// as we actually using only 1 bit for dlight mask in opengl1 renderer
#define	REFENTITYNUM_MASK	((1<<REFENTITYNUM_BITS) - 1)
// the last N-bit number (2^REFENTITYNUM_BITS - 1) is reserved for the special world refentity,
//  and this is reflected by the value of MAX_REFENTITIES (which therefore is not a power-of-2)
#define	MAX_REFENTITIES		((1<<REFENTITYNUM_BITS) - 1)
#define	REFENTITYNUM_WORLD	((1<<REFENTITYNUM_BITS) - 1)
// 14 bits
// can't be increased without changing bit packing for drawsurfs
// see QSORT_SHADERNUM_SHIFT
#define SHADERNUM_BITS	14
#define MAX_SHADERS		(1<<SHADERNUM_BITS)
#define SHADERNUM_MASK	(MAX_SHADERS-1)

typedef struct dlight_s {
	vec3_t	origin;
	vec3_t	origin2;
	vec3_t	dir;		// origin2 - origin

	vec3_t	color;				// range from 0.0 to 1.0, should be color normalized
	float	radius;

	vec3_t	transformed;		// origin in local coordinate system
	vec3_t	transformed2;		// origin2 in local coordinate system
	int		additive;			// texture detail is lost tho when the lightmap is dark
	qboolean linear;
#ifdef USE_PMLIGHT
	struct litSurf_s	*head;
	struct litSurf_s	*tail;
#endif
} dlight_t;


// a trRefEntity_t has all the information passed in by
// the client game, as well as some locally derived info
typedef struct {
	refEntity_t	e;

	float		axisLength;		// compensate for non-normalized axis
#ifdef USE_LEGACY_DLIGHTS
	int			needDlights;	// 1 for bmodels that touch a dlight
#endif
	qboolean	lightingCalculated;
	vec3_t		lightDir;		// normalized direction towards light
	vec3_t		ambientLight;	// color normalized to 0-255
	int			ambientLightInt;	// 32 bit rgba packed
	vec3_t		directedLight;
#ifdef USE_PMLIGHT
	vec3_t		shadowLightDir;	// normalized direction towards light
#endif
	qboolean	intShaderTime;
} trRefEntity_t;


typedef struct {
	vec3_t		origin;			// in world coordinates
	vec3_t		axis[3];		// orientation in world
	vec3_t		viewOrigin;		// viewParms->or.origin in local coordinates
	float		modelMatrix[16];
} orientationr_t;

//===============================================================================

typedef enum {
	SS_BAD,
	SS_PORTAL,			// mirrors, portals, viewscreens
	SS_ENVIRONMENT,		// sky box
	SS_OPAQUE,			// opaque

	SS_DECAL,			// scorch marks, etc.
	SS_SEE_THROUGH,		// ladders, grates, grills that may have small blended edges
						// in addition to alpha test
	SS_BANNER,

	SS_FOG,

	SS_UNDERWATER,		// for items that should be drawn in front of the water plane

	SS_BLEND0,			// regular transparency and filters
	SS_BLEND1,			// generally only used for additive type effects
	SS_BLEND2,
	SS_BLEND3,

	SS_BLEND6,
	SS_STENCIL_SHADOW,
	SS_ALMOST_NEAREST,	// gun smoke puffs

	SS_NEAREST			// blood blobs
} shaderSort_t;


#define MAX_SHADER_STAGES 8

typedef enum {
	GF_NONE,

	GF_SIN,
	GF_SQUARE,
	GF_TRIANGLE,
	GF_SAWTOOTH, 
	GF_INVERSE_SAWTOOTH, 

	GF_NOISE

} genFunc_t;


typedef enum {
	DEFORM_NONE,
	DEFORM_WAVE,
	DEFORM_NORMALS,
	DEFORM_BULGE,
	DEFORM_MOVE,
	DEFORM_PROJECTION_SHADOW,
	DEFORM_AUTOSPRITE,
	DEFORM_AUTOSPRITE2,
	DEFORM_TEXT0,
	DEFORM_TEXT1,
	DEFORM_TEXT2,
	DEFORM_TEXT3,
	DEFORM_TEXT4,
	DEFORM_TEXT5,
	DEFORM_TEXT6,
	DEFORM_TEXT7
} deform_t;

typedef enum {
	AGEN_IDENTITY,
	AGEN_SKIP,
	AGEN_ENTITY,
	AGEN_ONE_MINUS_ENTITY,
	AGEN_VERTEX,
	AGEN_ONE_MINUS_VERTEX,
	AGEN_LIGHTING_SPECULAR,
	AGEN_WAVEFORM,
	AGEN_PORTAL,
	AGEN_CONST
} alphaGen_t;

typedef enum {
	CGEN_BAD,
	CGEN_IDENTITY_LIGHTING,	// tr.identityLight
	CGEN_IDENTITY,			// always (1,1,1,1)
	CGEN_ENTITY,			// grabbed from entity's modulate field
	CGEN_ONE_MINUS_ENTITY,	// grabbed from 1 - entity.modulate
	CGEN_EXACT_VERTEX,		// tess.vertexColors
	CGEN_VERTEX,			// tess.vertexColors * tr.identityLight
	CGEN_ONE_MINUS_VERTEX,
	CGEN_WAVEFORM,			// programmatically generated
	CGEN_LIGHTING_DIFFUSE,
	CGEN_FOG,				// standard fog
	CGEN_CONST				// fixed color
} colorGen_t;

typedef enum {
	TCGEN_BAD,
	TCGEN_IDENTITY,			// clear to 0,0
	TCGEN_LIGHTMAP,
	TCGEN_TEXTURE,
	TCGEN_ENVIRONMENT_MAPPED,
	TCGEN_ENVIRONMENT_MAPPED_FP, // with correct first-person mapping
	TCGEN_FOG,
	TCGEN_VECTOR			// S and T from world coordinates
} texCoordGen_t;

typedef enum {
	ACFF_NONE,
	ACFF_MODULATE_RGB,
	ACFF_MODULATE_RGBA,
	ACFF_MODULATE_ALPHA
} acff_t;

typedef struct {
	float base;
	float amplitude;
	float phase;
	float frequency;

	genFunc_t	func;
} waveForm_t;

#define TR_MAX_TEXMODS 4

typedef enum {
	TMOD_NONE,
	TMOD_TRANSFORM,
	TMOD_TURBULENT,
	TMOD_SCROLL,
	TMOD_SCALE,
	TMOD_STRETCH,
	TMOD_ROTATE,
	TMOD_ROTATE2,
	TMOD_ENTITY_TRANSLATE,
	TMOD_OFFSET,
	TMOD_SCALE_OFFSET,
	TMOD_OFFSET_SCALE,
} texMod_t;

#define	MAX_SHADER_DEFORMS	3
typedef struct {
	deform_t	deformation;			// vertex coordinate modification type

	vec3_t		moveVector;
	waveForm_t	deformationWave;
	float		deformationSpread;

	float		bulgeWidth;
	float		bulgeHeight;
	float		bulgeSpeed;
} deformStage_t;


typedef struct {
	texMod_t		type;

	union {

		// used for TMOD_TURBULENT and TMOD_STRETCH
		waveForm_t		wave;

		// used for TMOD_TRANSFORM
		struct {
			float		matrix[2][2];	// s' = s * m[0][0] + t * m[1][0] + trans[0]
			float		translate[2];	// t' = s * m[0][1] + t * m[0][1] + trans[1]
		};

		// used for TMOD_SCALE, TMOD_OFFSET, TMOD_SCALE_OFFSET
		struct {
			float		scale[2];		// s' = s * scale[0] + offset[0]
			float		offset[2];		// t' = t * scale[1] + offset[1]
		};

		// used for TMOD_SCROLL
		float			scroll[2];		// s' = s + scroll[0] * time
										// t' = t + scroll[1] * time
		// used for TMOD_ROTATE
		// + = clockwise
		// - = counterclockwise
		float			rotateSpeed;

	};

} texModInfo_t;


#define MAX_IMAGE_ANIMATIONS		24
#define MAX_IMAGE_ANIMATIONS_VQ3	24

#define LIGHTMAP_INDEX_NONE			0
#define LIGHTMAP_INDEX_SHADER		1
#define LIGHTMAP_INDEX_OFFSET		2

typedef struct {
	image_t			*image[MAX_IMAGE_ANIMATIONS];
	int				numImageAnimations;
	double			imageAnimationSpeed;	// -EC- set to double

	texCoordGen_t	tcGen;
	vec3_t			tcGenVectors[2];

	int				numTexMods;
	texModInfo_t	*texMods;

	waveForm_t		rgbWave;
	colorGen_t		rgbGen;

	waveForm_t		alphaWave;
	alphaGen_t		alphaGen;

	color4ub_t		constantColor;			// for CGEN_CONST and AGEN_CONST

	acff_t			adjustColorsForFog;

	int				videoMapHandle;
	int				lightmap;				// LIGHTMAP_INDEX_NONE, LIGHTMAP_INDEX_SHADER, LIGHTMAP_INDEX_OFFSET
	qboolean		isVideoMap;
	qboolean		isScreenMap;
} textureBundle_t;

#ifdef USE_VULKAN
#define NUM_TEXTURE_BUNDLES 3
#else
#define NUM_TEXTURE_BUNDLES 2
#endif

typedef struct {
	qboolean		active;
	
	textureBundle_t	bundle[NUM_TEXTURE_BUNDLES];

	unsigned		stateBits;					// GLS_xxxx mask
	GLint			mtEnv;						// 0, GL_MODULATE, GL_ADD, GL_DECAL
	GLint			mtEnv3;						// 0, GL_MODULATE, GL_ADD, GL_DECAL

	qboolean		isDetail;
	qboolean		depthFragment;

#ifdef USE_VULKAN
	uint32_t		tessFlags;
	uint32_t		numTexBundles;

	uint32_t		vk_pipeline[2]; // normal,fogged
	uint32_t		vk_mirror_pipeline[2];

	uint32_t		vk_pipeline_df; // depthFragment
	uint32_t		vk_mirror_pipeline_df;
#endif

#ifdef USE_VBO
	uint32_t		rgb_offset[NUM_TEXTURE_BUNDLES]; // within current shader
	uint32_t		tex_offset[NUM_TEXTURE_BUNDLES]; // within current shader
#endif

} shaderStage_t;

struct shaderCommands_s;

typedef enum {
	FP_NONE,		// surface is translucent and will just be adjusted properly
	FP_EQUAL,		// surface is opaque but possibly alpha tested
	FP_LE			// surface is translucent, but still needs a fog pass (fog surface)
} fogPass_t;

typedef struct {
	float		cloudHeight;
	image_t		*outerbox[6], *innerbox[6];
} skyParms_t;

typedef struct {
	vec3_t	color;
	float	depthForOpaque;
} fogParms_t;

typedef struct shader_s {
	char		name[MAX_QPATH];		// game path, including extension
	int			lightmapSearchIndex;	// for a shader to match, both name and lightmapIndex must match
	int			lightmapIndex;			// for rendering

	int			index;					// this shader == tr.shaders[index]
	int			sortedIndex;			// this shader == tr.sortedShaders[sortedIndex]

	float		sort;					// lower numbered shaders draw before higher numbered

	qboolean	defaultShader;			// we want to return index 0 if the shader failed to
										// load for some reason, but R_FindShader should
										// still keep a name allocated for it, so if
										// something calls RE_RegisterShader again with
										// the same name, we don't try looking for it again

	qboolean	explicitlyDefined;		// found in a .shader file

	int			surfaceFlags;			// if explicitlyDefined, this will have SURF_* flags
	int			contentFlags;

	qboolean	entityMergable;			// merge across entites optimizable (smoke, blood)

	qboolean	isSky;
	skyParms_t	sky;
	fogParms_t	fogParms;

	float		portalRange;			// distance to fog out at
	float		portalRangeR;

	qboolean	multitextureEnv;		// if shader has multitexture stage(s)

	cullType_t	cullType;				// CT_FRONT_SIDED, CT_BACK_SIDED, or CT_TWO_SIDED
	qboolean	polygonOffset;			// set for decals and other items that must be offset 
	
	unsigned	noMipMaps:1;			// for console fonts, 2D elements, etc.
	unsigned	noPicMip:1;				// for images that must always be full resolution
	unsigned	noLightScale:1;
	unsigned	noVLcollapse:1;			// ignore vertexlight mode

	fogPass_t	fogPass;				// draw a blended pass, possibly with depth test equals

	qboolean	needsNormal;			// not all shaders will need all data to be gathered
	//qboolean	needsST1;
	qboolean	needsST2;
	//qboolean	needsColor;

	int			numDeforms;
	deformStage_t	deforms[MAX_SHADER_DEFORMS];


	int			numUnfoggedPasses;
	shaderStage_t	*stages[MAX_SHADER_STAGES];

#ifdef USE_PMLIGHT
	int			lightingStage;
	int			lightingBundle;
#endif
	qboolean	fogCollapse;
	int			tessFlags;

#ifdef USE_VBO
	// VBO structures
	qboolean	isStaticShader;
	int			svarsSize;
	int			iboOffset;
	int			vboOffset;
	int			normalOffset;
	int			numIndexes;
	int			numVertexes;
	int			curVertexes;
	int			curIndexes;
#endif

	int			hasScreenMap;

	void	(*optimalStageIteratorFunc)( void );

	double	clampTime;						// time this shader is clamped to - set to double for frameloss fix -EC-
	double	timeOffset;						// current time offset for this shader - set to double for frameloss fix -EC-

	struct shader_s *remappedShader;		// current shader this one is remapped too

	struct	shader_s	*next;
} shader_t;


// trRefdef_t holds everything that comes in refdef_t,
// as well as the locally generated scene information
typedef struct {
	int			x, y, width, height;
	float		fov_x, fov_y;
	vec3_t		vieworg;
	vec3_t		viewaxis[3];		// transformation matrix

	stereoFrame_t	stereoFrame;

	int			time;				// time in milliseconds for shader effects and other time dependent rendering issues
	int			rdflags;			// RDF_NOWORLDMODEL, etc

	// 1 bits will prevent the associated area from rendering at all
	byte		areamask[MAX_MAP_AREA_BYTES];
	qboolean	areamaskModified;	// qtrue if areamask changed since last scene

	double		floatTime;			// tr.refdef.time / 1000.0 -EC- set to double

	// text messages for deform text shaders
	char		text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];

	int			num_entities;
	trRefEntity_t	*entities;

	unsigned int num_dlights;
	struct dlight_s	*dlights;

	int			numPolys;
	struct srfPoly_s	*polys;

	int			numDrawSurfs;
	struct drawSurf_s	*drawSurfs;
#ifdef USE_PMLIGHT
	int			numLitSurfs;
	struct litSurf_s	*litSurfs;
#endif
#ifdef USE_VULKAN
	qboolean	switchRenderPass;
	qboolean	needScreenMap;
#endif
} trRefdef_t;


typedef struct image_s {
	char		*imgName;			// image path, including extension
	char		*imgName2;			// image path with real file extension
	struct image_s *next;			// for hash search
	int			width, height;		// source image
	int			uploadWidth;		// after power of two and picmip but not including clamp to MAX_TEXTURE_SIZE
	int			uploadHeight;
	imgFlags_t	flags;
	int			frameUsed;			// for texture usage in frame statistics

#ifdef USE_VULKAN
	int			internalFormat;

	VkSamplerAddressMode wrapClampMode;
	VkImage		handle;
	VkImageView	view;
	// Descriptor set that contains single descriptor used to access the given image.
	// It is updated only once during image initialization.
	VkDescriptorSet descriptor;
#else
	GLuint		texnum;				// gl texture binding
	GLint		internalFormat;
	int			TMU;				// only needed for voodoo2
#endif

} image_t;


//=================================================================================

// max surfaces per-skin
// This is an arbitrary limit. Vanilla Q3 only supported 32 surfaces in skins but failed to
// enforce the maximum limit when reading skin files. It was possile to use more than 32
// surfaces which accessed out of bounds memory past end of skin->surfaces hunk block.
#define MAX_SKIN_SURFACES	256

// skins allow models to be retextured without modifying the model file
typedef struct {
	char		name[MAX_QPATH];
	shader_t	*shader;
} skinSurface_t;

typedef struct skin_s {
	char		name[MAX_QPATH];		// game path, including extension
	int			numSurfaces;
	skinSurface_t	*surfaces;			// dynamically allocated array of surfaces
} skin_t;


typedef struct {
	int			originalBrushNumber;
	vec3_t		bounds[2];

	color4ub_t	colorInt;				// in packed byte format
	vec4_t		color;
	float		tcScale;				// texture coordinate vector scales
	fogParms_t	parms;

	// for clipping distance in fog when outside
	qboolean	hasSurface;
	float		surface[4];
} fog_t;

typedef struct {
	float		eyeT;
	qboolean	eyeOutside;
	vec4_t		fogDistanceVector;
	vec4_t		fogDepthVector;
	const float *fogColor; // vec4_t
} fogProgramParms_t;

typedef enum {
	PV_NONE = 0,
	PV_PORTAL, // this view is through a portal
	PV_MIRROR, // portal + inverted face culling
	PV_COUNT
} portalView_t;

typedef struct {
	orientationr_t	or;
	orientationr_t	world;
	vec3_t		pvsOrigin;			// may be different than or.origin for portals
	portalView_t portalView;
	int			frameSceneNum;		// copied from tr.frameSceneNum
	int			frameCount;			// copied from tr.frameCount
	cplane_t	portalPlane;		// clip anything behind this if mirroring
	int			viewportX, viewportY, viewportWidth, viewportHeight;
	int			scissorX, scissorY, scissorWidth, scissorHeight;
	float		fovX, fovY;
	float		projectionMatrix[16];
	cplane_t	frustum[5];
	vec3_t		visBounds[2];
	float		zFar;
	stereoFrame_t	stereoFrame;
#ifdef USE_PMLIGHT
	// each view will have its own dlight set
	unsigned int num_dlights;
	struct dlight_s	*dlights;
#endif
} viewParms_t;

/*
==============================================================================

SURFACES

==============================================================================
*/

// any changes in surfaceType must be mirrored in rb_surfaceTable[]
typedef enum {
	SF_BAD,
	SF_SKIP,				// ignore
	SF_FACE,
	SF_GRID,
	SF_TRIANGLES,
	SF_POLY,
	SF_MD3,
	SF_MDR,
	SF_IQM,
	SF_FLARE,
	SF_ENTITY,				// beams, rails, lightning, etc that can be determined by entity

	SF_NUM_SURFACE_TYPES,
	SF_MAX = 0x7fffffff			// ensures that sizeof( surfaceType_t ) == sizeof( int )
} surfaceType_t;

typedef struct drawSurf_s {
	unsigned int		sort;			// bit combination for fast compares
	surfaceType_t		*surface;		// any of surface*_t
} drawSurf_t;

#ifdef USE_PMLIGHT
typedef struct litSurf_s {
	unsigned int		sort;			// bit combination for fast compares
	surfaceType_t		*surface;		// any of surface*_t
	struct litSurf_s	*next;
} litSurf_t;
#endif

#define	MAX_FACE_POINTS		64

#define	MAX_PATCH_SIZE		32			// max dimensions of a patch mesh in map file
#define	MAX_GRID_SIZE		65			// max dimensions of a grid mesh in memory

// when cgame directly specifies a polygon, it becomes a srfPoly_t
// as soon as it is called
typedef struct srfPoly_s {
	surfaceType_t	surfaceType;
	qhandle_t		hShader;
	int				fogIndex;
	int				numVerts;
	polyVert_t		*verts;
} srfPoly_t;


typedef struct srfFlare_s {
	surfaceType_t	surfaceType;
	vec3_t			origin;
	vec3_t			normal;
	vec3_t			color;
} srfFlare_t;

typedef struct srfGridMesh_s {
	surfaceType_t	surfaceType;

	// dynamic lighting information
	int				dlightBits;

	// culling information
	vec3_t			meshBounds[2];
	vec3_t			localOrigin;
	float			meshRadius;

	// lod information, which may be different
	// than the culling information to allow for
	// groups of curves that LOD as a unit
	vec3_t			lodOrigin;
	float			lodRadius;
	int				lodFixed;
	int				lodStitched;
#ifdef USE_VBO
	int				vboItemIndex;
	int				vboExpectIndices;
	int				vboExpectVertices;
#endif
	// vertexes
	int				width, height;
	float			*widthLodError;
	float			*heightLodError;
	drawVert_t		verts[1];		// variable sized
} srfGridMesh_t;


#define	VERTEXSIZE	8
typedef struct {
	surfaceType_t	surfaceType;
	cplane_t	plane;

	// dynamic lighting information
#ifdef USE_LEGACY_DLIGHTS
	int			dlightBits;
#endif
#ifdef USE_VBO
	int			vboItemIndex;
#endif
	float		*normals;

	// triangle definitions (no normals at points)
	int			numPoints;
	int			numIndices;
	int			ofsIndices;
	float		points[1][VERTEXSIZE];	// variable sized
										// there is a variable length list of indices here also
} srfSurfaceFace_t;


// misc_models in maps are turned into direct geometry by q3map
typedef struct {
	surfaceType_t	surfaceType;

	// dynamic lighting information
#ifdef USE_LEGACY_DLIGHTS
	int				dlightBits;
#endif
#ifdef USE_VBO
	int				vboItemIndex;
#endif

	// culling information (FIXME: use this!)
	vec3_t			bounds[2];
	vec3_t			localOrigin;
	float			radius;

	// triangle definitions
	int				numIndexes;
	int				*indexes;

	int				numVerts;
	drawVert_t		*verts;
} srfTriangles_t;

typedef struct {
	vec3_t translate;
	quat_t rotate;
	vec3_t scale;
} iqmTransform_t;

// inter-quake-model
typedef struct {
	int		num_vertexes;
	int		num_triangles;
	int		num_frames;
	int		num_surfaces;
	int		num_joints;
	int		num_poses;
	struct srfIQModel_s	*surfaces;

	int		*triangles;

	// vertex arrays
	float		*positions;
	float		*texcoords;
	float		*normals;
	float		*tangents;
	byte		*colors;
	int		*influences; // [num_vertexes] indexes into influenceBlendVertexes

	// unique list of vertex blend indexes/weights for faster CPU vertex skinning
	byte		*influenceBlendIndexes; // [num_influences]
	union {
		float	*f;
		byte	*b;
	} influenceBlendWeights; // [num_influences]

	// depending upon the exporter, blend indices and weights might be int/float
	// as opposed to the recommended byte/byte, for example Noesis exports
	// int/float whereas the official IQM tool exports byte/byte
	int		blendWeightsType; // IQM_UBYTE or IQM_FLOAT

	char		*jointNames;
	int		*jointParents;
	float		*bindJoints; // [num_joints * 12]
	float		*invBindJoints; // [num_joints * 12]
	iqmTransform_t	*poses; // [num_frames * num_poses]
	float		*bounds;
} iqmData_t;

// inter-quake-model surface
typedef struct srfIQModel_s {
	surfaceType_t	surfaceType;
	char		name[MAX_QPATH];
	shader_t	*shader;
	iqmData_t	*data;
	int		first_vertex, num_vertexes;
	int		first_triangle, num_triangles;
	int		first_influence, num_influences;
} srfIQModel_t;


extern	void (*rb_surfaceTable[SF_NUM_SURFACE_TYPES])(void *);

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/


//
// in memory representation
//

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2

typedef struct msurface_s {
	int					viewCount;		// if == tr.viewCount, already added
	struct shader_s		*shader;
	int					fogIndex;
#ifdef USE_PMLIGHT
	int					vcVisible;		// if == tr.viewCount, is actually VISIBLE in this frame, i.e. passed facecull and has been added to the drawsurf list
	int					lightCount;		// if == tr.lightCount, already added to the litsurf list for the current light
#endif // USE_PMLIGHT
	surfaceType_t		*data;			// any of srf*_t
} msurface_t;


typedef struct mnode_s {
	// common with leaf and node
	int			contents;		// -1 for nodes, to differentiate from leafs
	int			visframe;		// node needs to be traversed if current
	vec3_t		mins, maxs;		// for bounding box culling
	struct mnode_s	*parent;

	// node specific
	cplane_t	*plane;
	struct mnode_s	*children[2];	

	// leaf specific
	int			cluster;
	int			area;

	msurface_t	**firstmarksurface;
	int			nummarksurfaces;
} mnode_t;

typedef struct {
	vec3_t		bounds[2];		// for culling
	msurface_t	*firstSurface;
	int			numSurfaces;
} bmodel_t;

typedef struct {
	char		name[MAX_QPATH];		// ie: maps/tim_dm2.bsp
	char		baseName[MAX_QPATH];	// ie: tim_dm2

	int			dataSize;

	int			numShaders;
	dshader_t	*shaders;

	bmodel_t	*bmodels;

	int			numplanes;
	cplane_t	*planes;

	int			numnodes;		// includes leafs
	int			numDecisionNodes;
	mnode_t		*nodes;

	int			numsurfaces;
	msurface_t	*surfaces;

	int			nummarksurfaces;
	msurface_t	**marksurfaces;

	int			numfogs;
	fog_t		*fogs;

	vec3_t		lightGridOrigin;
	vec3_t		lightGridSize;
	vec3_t		lightGridInverseSize;
	int			lightGridBounds[3];
	byte		*lightGridData;


	int			numClusters;
	int			clusterBytes;
	const byte	*vis;			// may be passed in by CM_LoadMap to save space

	byte		*novis;			// clusterBytes of 0xff

	char		*entityString;
	const char	*entityParsePoint;
} world_t;

//======================================================================

typedef enum {
	MOD_BAD,
	MOD_BRUSH,
	MOD_MESH,
	MOD_MDR,
	MOD_IQM
} modtype_t;

typedef struct model_s {
	char		name[MAX_QPATH];
	modtype_t	type;
	int			index;		// model = tr.models[model->index]

	int			dataSize;	// just for listing purposes
	bmodel_t	*bmodel;		// only if type == MOD_BRUSH
	md3Header_t	*md3[MD3_MAX_LODS];	// only if type == MOD_MESH
	void	*modelData;			// only if type == (MOD_MDR | MOD_IQM)

	int			 numLods;
} model_t;

#define	MAX_MOD_KNOWN	1024

void		R_ModelInit (void);
model_t		*R_GetModelByHandle( qhandle_t hModel );
int			R_LerpTag( orientation_t *tag, qhandle_t handle, int startFrame, int endFrame, 
					 float frac, const char *tagName );
void		R_ModelBounds( qhandle_t handle, vec3_t mins, vec3_t maxs );
const char	*R_GetModelNameByHandle(qhandle_t index); // xq3e hack
void 		R_UpdateShaderColorByHandle(qhandle_t hShader, const vec3_t color); // xq3e hack

void		R_Modellist_f (void);

//====================================================

#define	MAX_DRAWIMAGES			2048
#define	MAX_SKINS				1024


#define	MAX_DRAWSURFS			0x20000
#define	DRAWSURF_MASK			(MAX_DRAWSURFS-1)

/*

the drawsurf sort data is packed into a single 32 bit value so it can be
compared quickly during the qsorting process

the bits are allocated as follows:

0 - 1	: dlightmap index
//2		: used to be clipped flag REMOVED - 03.21.00 rad
2 - 6	: fog index
11 - 20	: entity index
21 - 31	: sorted shader index

	TTimo - 1.32
0-1   : dlightmap index
2-6   : fog index
7-16  : entity index
17-30 : sorted shader index
*/
#define	DLIGHT_BITS 1 // qboolean in opengl1 renderer
#define	DLIGHT_MASK ((1<<DLIGHT_BITS)-1)
#define	FOGNUM_BITS 5
#define	FOGNUM_MASK ((1<<FOGNUM_BITS)-1)

#define	QSORT_FOGNUM_SHIFT	DLIGHT_BITS
#define	QSORT_REFENTITYNUM_SHIFT (QSORT_FOGNUM_SHIFT + FOGNUM_BITS)
#define	QSORT_SHADERNUM_SHIFT	(QSORT_REFENTITYNUM_SHIFT+REFENTITYNUM_BITS)
#if (QSORT_SHADERNUM_SHIFT+SHADERNUM_BITS) > 32
	#error "Need to update sorting, too many bits."
#endif
#define QSORT_REFENTITYNUM_MASK (REFENTITYNUM_MASK << QSORT_REFENTITYNUM_SHIFT)

extern	int			gl_filter_min, gl_filter_max;

/*
** performanceCounters_t
*/
typedef struct {
	int		c_sphere_cull_patch_in, c_sphere_cull_patch_clip, c_sphere_cull_patch_out;
	int		c_box_cull_patch_in, c_box_cull_patch_clip, c_box_cull_patch_out;
	int		c_sphere_cull_md3_in, c_sphere_cull_md3_clip, c_sphere_cull_md3_out;
	int		c_box_cull_md3_in, c_box_cull_md3_clip, c_box_cull_md3_out;

	int		c_leafs;
	int		c_dlightSurfaces;
	int		c_dlightSurfacesCulled;
#ifdef USE_PMLIGHT
	int		c_light_cull_out;
	int		c_light_cull_in;
	int		c_lit_leafs;
	int		c_lit_surfs;
	int		c_lit_culls;
	int		c_lit_masks;
#endif
} frontEndCounters_t;

#define	FOG_TABLE_SIZE		256
#define FUNCTABLE_SIZE		1024
#define FUNCTABLE_SIZE2		10
#define FUNCTABLE_MASK		(FUNCTABLE_SIZE-1)

// the renderer front end should never modify glstate_t
typedef struct {
	GLuint		currenttextures[ MAX_TEXTURE_UNITS ];
	int			currenttmu;
	qboolean	finishCalled;
	GLint		texEnv[2];
	cullType_t	faceCulling;
	unsigned	glStateBits;
	unsigned	glClientStateBits[ MAX_TEXTURE_UNITS ];
	int			currentArray;
} glstate_t;

typedef struct glstatic_s {
	// unmodified width/height according to actual \r_mode*
	int windowWidth;
	int windowHeight;
	int captureWidth;
	int captureHeight;
	int initTime;
	qboolean deviceSupportsGamma;
} glstatic_t;

typedef struct {
	int		c_surfaces, c_shaders, c_vertexes, c_indexes, c_totalIndexes;
	float	c_overDraw;
	
	int		c_dlightVertexes;
	int		c_dlightIndexes;

	int		c_flareAdds;
	int		c_flareTests;
	int		c_flareRenders;

	int		msec;			// total msec for backend run
#ifdef USE_PMLIGHT
	int		c_lit_batches;
	int		c_lit_vertices;
	int		c_lit_indices;
	int		c_lit_indices_latecull_in;
	int		c_lit_indices_latecull_out;
	int		c_lit_vertices_lateculltest;
#endif
} backEndCounters_t;

typedef struct videoFrameCommand_s {
	int					commandId;
	int					width;
	int					height;
	byte				*captureBuffer;
	byte				*encodeBuffer;
	qboolean			motionJpeg;
} videoFrameCommand_t;

enum {
	SCREENSHOT_TGA = 1<<0,
	SCREENSHOT_JPG = 1<<1,
	SCREENSHOT_BMP = 1<<2,
	SCREENSHOT_BMP_CLIPBOARD = 1<<3,
	SCREENSHOT_AVI = 1<<4 // take video frame
};

// all state modified by the back end is separated
// from the front end state
typedef struct {
	trRefdef_t	refdef;
	viewParms_t	viewParms;
	orientationr_t	or;
	backEndCounters_t	pc;
	qboolean	isHyperspace;
	const trRefEntity_t *currentEntity;
	qboolean	skyRenderedThisView;	// flag for drawing sun

	qboolean	projection2D;	// if qtrue, drawstretchpic doesn't need to change modes
	color4ub_t	color2D;
	qboolean	doneSurfaces;   // done any 3d surfaces already
	trRefEntity_t	entity2D;	// currentEntity will point at this when doing 2D rendering

	int		screenshotMask;		// tga | jpg | bmp
	char	screenshotTGA[ MAX_OSPATH ];
	char	screenshotJPG[ MAX_OSPATH ];
	char	screenshotBMP[ MAX_OSPATH ];
	qboolean screenShotTGAsilent;
	qboolean screenShotJPGsilent;
	qboolean screenShotBMPsilent;
	videoFrameCommand_t	vcmd;	// avi capture
	
	qboolean throttle;
	qboolean drawConsole;
	qboolean doneShadows;

	qboolean screenMapDone;
	qboolean doneBloom;

} backEndState_t;

typedef struct drawSurfsCommand_s drawSurfsCommand_t;

/*
** trGlobals_t 
**
** Most renderer globals are defined here.
** backend functions should never modify any of these fields,
** but may read fields that aren't dynamically modified
** by the frontend.
*/
typedef struct {
	qboolean				registered;		// cleared at shutdown, set at beginRegistration
	qboolean				inited;			// cleared at shutdown, set at InitOpenGL

	int						visCount;		// incremented every time a new vis cluster is entered
	int						frameCount;		// incremented every frame
	int						sceneCount;		// incremented every scene
	int						viewCount;		// incremented every view (twice a scene if portaled)
											// and every R_MarkFragments call
#ifdef USE_PMLIGHT
	int						lightCount;		// incremented for each dlight in the view
#endif

	int						frameSceneNum;	// zeroed at RE_BeginFrame

	qboolean				worldMapLoaded;
	world_t					*world;

	const byte				*externalVisData;	// from RE_SetWorldVisData, shared with CM_Load

	image_t					*defaultImage;
	image_t					*scratchImage[ MAX_VIDEO_HANDLES ];
	image_t					*fogImage;
	image_t					*dlightImage;	// inverse-quare highlight for projective adding
	image_t					*flareImage;
	image_t					*blackImage;
	image_t					*whiteImage;			// full of 0xff
	image_t					*identityLightImage;	// full of tr.identityLightByte

	shader_t				*defaultShader;
	shader_t				*whiteShader;
	shader_t				*cinematicShader;
	shader_t				*shadowShader;
	shader_t				*projectionShadowShader;

	shader_t				*flareShader;
	shader_t				*sunShader;

	int						numLightmaps;
	image_t					**lightmaps;

	qboolean				mergeLightmaps;
	float					lightmapOffset[2];	// current shader lightmap offset
	float					lightmapScale[2];	// for lightmap atlases
	int						lightmapMod;		// for lightmap atlases

	trRefEntity_t			*currentEntity;
	trRefEntity_t			worldEntity;		// point currentEntity at this when rendering world
	int						currentEntityNum;
	int						shiftedEntityNum;	// currentEntityNum << QSORT_REFENTITYNUM_SHIFT
	model_t					*currentModel;

	viewParms_t				viewParms;

	float					identityLight;		// 1.0 / ( 1 << overbrightBits )
	int						identityLightByte;	// identityLight * 255
	int						overbrightBits;		// r_overbrightBits->integer, but set to 0 if no hw gamma

	orientationr_t			or;					// for current entity

	trRefdef_t				refdef;

	int						viewCluster;
#ifdef USE_PMLIGHT
	dlight_t				*light;				// current light during R_RecursiveLightNode
#endif
	vec3_t					sunLight;			// from the sky shader for this level
	vec3_t					sunDirection;

	frontEndCounters_t		pc;
	int						frontEndMsec;		// not in pc due to clearing issue

	//
	// put large tables at the end, so most elements will be
	// within the +/32K indexed range on risc processors
	//
	model_t					*models[MAX_MOD_KNOWN];
	int						numModels;

	int						numImages;
	image_t					*images[MAX_DRAWIMAGES];

	// shader indexes from other modules will be looked up in tr.shaders[]
	// shader indexes from drawsurfs will be looked up in sortedShaders[]
	// lower indexed sortedShaders must be rendered first (opaque surfaces before translucent)
	int						numShaders;
	shader_t				*shaders[MAX_SHADERS];
	shader_t				*sortedShaders[MAX_SHADERS];

	int						numSkins;
	skin_t					*skins[MAX_SKINS];

	float					sinTable[FUNCTABLE_SIZE];
	float					squareTable[FUNCTABLE_SIZE];
	float					triangleTable[FUNCTABLE_SIZE];
	float					sawToothTable[FUNCTABLE_SIZE];
	float					inverseSawToothTable[FUNCTABLE_SIZE];
	float					fogTable[FOG_TABLE_SIZE];

	qboolean				mapLoading;

	int						needScreenMap;
#ifdef USE_VULKAN
	drawSurfsCommand_t		*drawSurfCmd;
	int						numDrawSurfCmds;
	int						lastRenderCommand;
	int						numFogs; // read before parsing shaders
#endif

	qboolean				vertexLightingAllowed;
} trGlobals_t;


extern backEndState_t	backEnd;
extern trGlobals_t	tr;

extern int	gl_clamp_mode;

extern glstate_t	glState;		// outside of TR since it shouldn't be cleared during ref re-init

extern glstatic_t gls;

extern void myGlMultMatrix(const float *a, const float *b, float *out);

#ifdef USE_VULKAN
extern Vk_Instance	vk;				// shouldn't be cleared during ref re-init
extern Vk_World		vk_world;		// this data is cleared during ref re-init
#endif

//
// cvars
//
extern cvar_t	*r_flareSize;
extern cvar_t	*r_flareFade;
extern cvar_t	*r_flareCoeff;			// coefficient for the flare intensity falloff function. 

extern cvar_t	*r_railWidth;
extern cvar_t	*r_railCoreWidth;
extern cvar_t	*r_railSegmentLength;

extern cvar_t	*r_znear;				// near Z clip plane
extern cvar_t	*r_zproj;				// z distance of projection plane
extern cvar_t	*r_stereoSeparation;			// separation of cameras for stereo rendering

extern cvar_t	*r_lodbias;				// push/pull LOD transitions
extern cvar_t	*r_lodscale;

extern cvar_t	*r_fastsky;				// controls whether sky should be cleared or drawn
extern cvar_t	*r_neatsky;				// nomip and nopicmip for skyboxes, cnq3 like look
extern cvar_t	*r_drawSun;				// controls drawing of sun quad
extern cvar_t	*r_dynamiclight;		// dynamic lights enabled/disabled
extern cvar_t	*r_mergeLightmaps;
#ifdef USE_PMLIGHT
extern cvar_t	*r_dlightMode;			// 0 - vq3, 1 - pmlight
//extern cvar_t	*r_dlightSpecPower;		// 1 - 32
//extern cvar_t	*r_dlightSpecColor;		// -1.0 - 1.0
extern cvar_t	*r_dlightScale;			// 0.1 - 1.0
extern cvar_t	*r_dlightIntensity;		// 0.1 - 1.0
#endif
extern cvar_t	*r_dlightSaturation;	// 0.0 - 1.0
#ifdef USE_VULKAN
extern cvar_t	*r_device;
#ifdef USE_VBO
extern cvar_t	*r_vbo;
#endif
extern cvar_t	*r_fbo;
extern cvar_t	*r_hdr;
extern cvar_t	*r_bloom;
extern cvar_t	*r_bloom_threshold;
extern cvar_t	*r_bloom_intensity;
extern cvar_t	*r_bloom_threshold_mode;
extern cvar_t	*r_bloom_modulate;
extern cvar_t	*r_ext_multisample;
extern cvar_t	*r_ext_supersample;
//extern cvar_t	*r_ext_alpha_to_coverage;
extern cvar_t	*r_renderWidth;
extern cvar_t	*r_renderHeight;
extern cvar_t	*r_renderScale;
#endif

extern cvar_t	*r_dlightBacks;			// dlight non-facing surfaces for continuity

extern	cvar_t	*r_norefresh;			// bypasses the ref rendering
extern	cvar_t	*r_drawentities;		// disable/enable entity rendering
extern	cvar_t	*r_drawworld;			// disable/enable world rendering
extern	cvar_t	*r_speeds;				// various levels of information display
extern  cvar_t	*r_detailTextures;		// enables/disables detail texturing stages
extern	cvar_t	*r_novis;				// disable/enable usage of PVS
extern	cvar_t	*r_nocull;
extern	cvar_t	*r_facePlaneCull;		// enables culling of planar surfaces with back side test
extern	cvar_t	*r_nocurves;
extern	cvar_t	*r_showcluster;

extern cvar_t	*r_gamma;

extern	cvar_t	*r_nobind;						// turns off binding to appropriate textures
extern	cvar_t	*r_singleShader;				// make most world faces use default shader
extern	cvar_t	*r_roundImagesDown;
extern	cvar_t	*r_colorMipLevels;				// development aid to see texture mip usage
extern	cvar_t	*r_picmip;						// controls picmip values
extern	cvar_t	*r_nomip;						// apply picmip only on worldspawn textures
extern	cvar_t	*r_finish;
extern	cvar_t	*r_textureMode;
extern	cvar_t	*r_offsetFactor;
extern	cvar_t	*r_offsetUnits;

extern	cvar_t	*r_fullbright;					// avoid lightmap pass
extern	cvar_t	*r_lightmap;					// render lightmaps only
extern	cvar_t	*r_vertexLight;					// vertex lighting mode for better performance

extern	cvar_t	*r_showtris;					// enables wireframe rendering of the world
extern	cvar_t	*r_showsky;						// forces sky in front of all surfaces
extern	cvar_t	*r_shownormals;					// draws wireframe normals
extern	cvar_t	*r_clear;						// force screen clear every frame

extern	cvar_t	*r_shadows;						// controls shadows: 0 = none, 1 = blur, 2 = stencil, 3 = black planar projection
extern	cvar_t	*r_flares;						// light flares

extern	cvar_t	*r_intensity;

extern	cvar_t	*r_lockpvs;
extern	cvar_t	*r_noportals;
extern	cvar_t	*r_portalOnly;

extern	cvar_t	*r_subdivisions;
extern	cvar_t	*r_lodCurveError;
extern	cvar_t	*r_skipBackEnd;

extern	cvar_t	*r_greyscale;
extern	cvar_t	*r_dither;
extern	cvar_t	*r_presentBits;

extern	cvar_t	*r_ignoreGLErrors;

extern	cvar_t	*r_overBrightBits;
extern	cvar_t	*r_mapOverBrightBits;
extern	cvar_t	*r_mapGreyScale;

extern	cvar_t	*r_debugSurface;
extern	cvar_t	*r_simpleMipMaps;

extern	cvar_t	*r_showImages;
extern	cvar_t	*r_defaultImage;
extern	cvar_t	*r_debugSort;

extern	cvar_t	*r_printShaders;

extern cvar_t	*r_marksOnTriangleMeshes;

//====================================================================

void R_SwapBuffers( int );

void R_RenderView( const viewParms_t *parms );

void R_AddMD3Surfaces( trRefEntity_t *e );
void R_AddNullModelSurfaces( trRefEntity_t *e );
void R_AddBeamSurfaces( trRefEntity_t *e );
void R_AddRailSurfaces( trRefEntity_t *e, qboolean isUnderwater );
void R_AddLightningBoltSurfaces( trRefEntity_t *e );

void R_AddPolygonSurfaces( void );

void R_DecomposeSort( unsigned sort, int *entityNum, shader_t **shader, 
					 int *fogNum, int *dlightMap );

void R_AddDrawSurf( surfaceType_t *surface, shader_t *shader, int fogIndex, int dlightMap );
#ifdef USE_PMLIGHT
void R_DecomposeLitSort( unsigned sort, int *entityNum, shader_t **shader, int *fogNum );
void R_AddLitSurf( surfaceType_t *surface, shader_t *shader, int fogIndex );
#endif

#define	CULL_IN		0		// completely unclipped
#define	CULL_CLIP	1		// clipped by one or more planes
#define	CULL_OUT	2		// completely outside the clipping planes

void R_LocalPointToWorld( const vec3_t local, vec3_t world );
int R_CullLocalBox( const vec3_t bounds[2] );
int R_CullPointAndRadius( const vec3_t origin, float radius );
int R_CullLocalPointAndRadius( const vec3_t origin, float radius );
int R_CullDlight( const dlight_t *dl );

void R_SetupProjection( viewParms_t *dest, float zProj, qboolean computeFrustum );
void R_RotateForEntity( const trRefEntity_t *ent, const viewParms_t *viewParms, orientationr_t *or );

/*
** GL wrapper/helper functions
*/
const float *GL_Ortho( const float left, const float right, const float bottom, const float top, const float znear, const float zfar );
void	GL_Bind( image_t *image );
void	GL_SelectTexture( int unit );
void	GL_TextureMode( const char *string );
void	GL_CheckErrors( void );
void	GL_State( unsigned stateVector );
void	GL_ClientState( int unit, unsigned stateVector );
#ifndef USE_VULKAN
void	GL_TexEnv( GLint env );
void	GL_Cull( cullType_t cullType );
#endif

#define GLS_SRCBLEND_ZERO						0x00000001
#define GLS_SRCBLEND_ONE						0x00000002
#define GLS_SRCBLEND_DST_COLOR					0x00000003
#define GLS_SRCBLEND_ONE_MINUS_DST_COLOR		0x00000004
#define GLS_SRCBLEND_SRC_ALPHA					0x00000005
#define GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA		0x00000006
#define GLS_SRCBLEND_DST_ALPHA					0x00000007
#define GLS_SRCBLEND_ONE_MINUS_DST_ALPHA		0x00000008
#define GLS_SRCBLEND_ALPHA_SATURATE				0x00000009
#define GLS_SRCBLEND_BITS						0x0000000f

#define GLS_DSTBLEND_ZERO						0x00000010
#define GLS_DSTBLEND_ONE						0x00000020
#define GLS_DSTBLEND_SRC_COLOR					0x00000030
#define GLS_DSTBLEND_ONE_MINUS_SRC_COLOR		0x00000040
#define GLS_DSTBLEND_SRC_ALPHA					0x00000050
#define GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA		0x00000060
#define GLS_DSTBLEND_DST_ALPHA					0x00000070
#define GLS_DSTBLEND_ONE_MINUS_DST_ALPHA		0x00000080
#define GLS_DSTBLEND_BITS						0x000000f0

#define GLS_BLEND_BITS							(GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)

#define GLS_DEPTHMASK_TRUE						0x00000100

#define GLS_POLYMODE_LINE						0x00000200

#define GLS_DEPTHTEST_DISABLE					0x00000400
#define GLS_DEPTHFUNC_EQUAL						0x00000800

#define GLS_ATEST_GT_0							0x00001000
#define GLS_ATEST_LT_80							0x00002000
#define GLS_ATEST_GE_80							0x00003000
#define GLS_ATEST_BITS							0x00003000

#define GLS_DEFAULT								GLS_DEPTHMASK_TRUE

// vertex array states

#define CLS_NONE								0x00000000
#define CLS_COLOR_ARRAY							0x00000001
#define CLS_TEXCOORD_ARRAY						0x00000002
#define CLS_NORMAL_ARRAY						0x00000004

void		RE_StretchRaw( int x, int y, int w, int h, int cols, int rows, byte *data, int client, qboolean dirty );
void		RE_UploadCinematic( int w, int h, int cols, int rows, byte *data, int client, qboolean dirty );

void		RE_BeginFrame( stereoFrame_t stereoFrame );
void		RE_BeginRegistration( glconfig_t *glconfig );
void		RE_LoadWorldMap( const char *mapname );
void		RE_SetWorldVisData( const byte *vis );
qhandle_t	RE_RegisterModel( const char *name );
qhandle_t	RE_RegisterSkin( const char *name );

qboolean	RE_GetEntityToken( char *buffer, int size );

model_t		*R_AllocModel( void );

void		R_Init( void );

void		R_SetColorMappings( void );
void		R_GammaCorrect( byte *buffer, int bufSize );
void		R_ColorShiftLightingBytes( const byte in[4], byte out[4], qboolean hasAlpha );

void	R_ImageList_f( void );
void	R_SkinList_f( void );

void	R_InitFogTable( void );
float	R_FogFactor( float s, float t );
void	R_InitImages( void );
void	R_DeleteTextures( void );
int		R_SumOfUsedImages( void );
void	R_InitSkins( void );
skin_t	*R_GetSkinByHandle( qhandle_t hSkin );

int R_ComputeLOD( trRefEntity_t *ent );

const void *RB_TakeVideoFrameCmd( const void *data );

//
// tr_shader.c
//
shader_t	*R_FindShader( const char *name, int lightmapIndex, qboolean mipRawImage );
shader_t	*R_GetShaderByHandle( qhandle_t hShader );
shader_t	*R_GetShaderByState( int index, long *cycleTime );
shader_t	*R_FindShaderByName( const char *name );
void		R_InitShaders( void );
void		R_ShaderList_f( void );
void		RE_RemapShader(const char *oldShader, const char *newShader, const char *timeOffset);


//
// tr_surface.c
//
void		RB_SurfaceGridEstimate( srfGridMesh_t *cv, int *numVertexes, int *numIndexes ); 

/*
====================================================================

TESSELATOR/SHADER DECLARATIONS

====================================================================
*/

typedef struct stageVars
{
	color4ub_t	colors[NUM_TEXTURE_BUNDLES][SHADER_MAX_VERTEXES]; // we need at least 2xSHADER_MAX_VERTEXES for shadows and normals
	vec2_t		texcoords[NUM_TEXTURE_BUNDLES][SHADER_MAX_VERTEXES];
	vec2_t		*texcoordPtr[NUM_TEXTURE_BUNDLES];
} stageVars_t;

typedef struct shaderCommands_s 
{
#pragma pack(push,16)
	glIndex_t	indexes[SHADER_MAX_INDEXES] QALIGN(16);
	vec4_t		xyz[SHADER_MAX_VERTEXES*2] QALIGN(16); // 2x needed for shadows
	vec4_t		normal[SHADER_MAX_VERTEXES] QALIGN(16);
	vec2_t		texCoords[2][SHADER_MAX_VERTEXES] QALIGN(16);
	vec2_t		texCoords00[SHADER_MAX_VERTEXES] QALIGN(16);
	color4ub_t	vertexColors[SHADER_MAX_VERTEXES] QALIGN(16);
#ifdef USE_LEGACY_DLIGHTS
	int			vertexDlightBits[SHADER_MAX_VERTEXES] QALIGN(16);
#endif
	stageVars_t	svars QALIGN(16);

	color4ub_t	constantColor255[SHADER_MAX_VERTEXES] QALIGN(16);
#pragma pack(pop)

#ifdef USE_VBO
	surfaceType_t	surfType;
	int			vboIndex;
	int			vboStage;
	qboolean	allowVBO;
#endif

	shader_t	*shader;
	double		shaderTime;	// -EC- set to double for frameloss fix
	int			fogNum;
#ifdef USE_LEGACY_DLIGHTS
	int			dlightBits;	// or together of all vertexDlightBits
#endif
	int			numIndexes;
	int			numVertexes;

#ifdef USE_PMLIGHT
	const dlight_t* light;
	qboolean	dlightPass;
	qboolean	dlightUpdateParams;
#endif

#ifdef USE_VULKAN
	Vk_Depth_Range depthRange;
#endif

	// info extracted from current shader
#ifdef USE_TESS_NEEDS_NORMAL
	int			needsNormal;
#endif
#ifdef USE_TESS_NEEDS_ST2
	int			needsST2;
#endif

	int			numPasses;
	shaderStage_t **xstages;

} shaderCommands_t;

extern	shaderCommands_t	tess;

void RB_BeginSurface( shader_t *shader, int fogNum );
void RB_EndSurface( void );
void RB_CheckOverflow( int verts, int indexes );
#define RB_CHECKOVERFLOW(v,i) RB_CheckOverflow(v,i)

void RB_StageIteratorGeneric( void );
void RB_StageIteratorSky( void );

void RB_AddQuadStamp( const vec3_t origin, const vec3_t left, const vec3_t up, color4ub_t color );
void RB_AddQuadStampExt( const vec3_t origin, const vec3_t left, const vec3_t up, color4ub_t color, float s1, float t1, float s2, float t2 );
void RB_AddQuadStamp2( float x, float y, float w, float h, float s1, float t1, float s2, float t2, color4ub_t color );

void RB_ShowImages( void );


/*
============================================================

WORLD MAP

============================================================
*/

void R_AddBrushModelSurfaces( trRefEntity_t *e );
void R_AddWorldSurfaces( void );
qboolean R_inPVS( const vec3_t p1, const vec3_t p2 );


/*
============================================================

FLARES

============================================================
*/

void R_ClearFlares( void );

void RB_AddFlare( void *surface, int fogNum, vec3_t point, vec3_t color, vec3_t normal );
void RB_AddDlightFlares( void );
void RB_RenderFlares( void );

/*
============================================================

LIGHTS

============================================================
*/
void R_DlightBmodel( bmodel_t *bmodel );
void R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent );
void R_TransformDlights( int count, dlight_t *dl, orientationr_t *or );
int R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir );

#ifdef USE_PMLIGHT
void VK_LightingPass( void );
qboolean R_LightCullBounds( const dlight_t* dl, const vec3_t mins, const vec3_t maxs );
#endif // USE_PMLIGHT

void R_DrawElements( int numIndexes, const glIndex_t *indexes );
void R_ComputeColors( const int bundle, color4ub_t *dest, const shaderStage_t *pStage );
void R_ComputeTexCoords( const int b, const textureBundle_t *bundle );

/*
============================================================

SHADOWS

============================================================
*/

void RB_ShadowTessEnd( void );
void RB_ShadowFinish( void );
void RB_ProjectionShadowDeform( void );

/*
============================================================

SKIES

============================================================
*/

void R_InitSkyTexCoords( float cloudLayerHeight );
void R_DrawSkyBox( const shaderCommands_t *shader );
void RB_DrawSun( float scale, shader_t *shader );

/*
============================================================

CURVE TESSELATION

============================================================
*/

#define PATCH_STITCHING

srfGridMesh_t *R_SubdividePatchToGrid( int width, int height,
								drawVert_t points[MAX_PATCH_SIZE*MAX_PATCH_SIZE] );
srfGridMesh_t *R_GridInsertColumn( srfGridMesh_t *grid, int column, int row, vec3_t point, float loderror );
srfGridMesh_t *R_GridInsertRow( srfGridMesh_t *grid, int row, int column, vec3_t point, float loderror );
void R_FreeSurfaceGridMesh( srfGridMesh_t *grid );

/*
============================================================

MARKERS, POLYGON PROJECTION ON WORLD POLYGONS

============================================================
*/

int R_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
				   int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer );


/*
============================================================

SCENE GENERATION

============================================================
*/

void R_InitNextFrame( void );

void RE_ClearScene( void );
void RE_AddRefEntityToScene( const refEntity_t *ent, qboolean intShaderTime );
void RE_AddPolyToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts, int num );
void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b );
void RE_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b );
void RE_AddLinearLightToScene( const vec3_t start, const vec3_t end, float intensity, float r, float g, float b );

void RE_RenderScene( const refdef_t *fd );

/*
=============================================================

UNCOMPRESSING BONES

=============================================================
*/

#define MC_BITS_X (16)
#define MC_BITS_Y (16)
#define MC_BITS_Z (16)
#define MC_BITS_VECT (16)

#define MC_SCALE_X (1.0f/64)
#define MC_SCALE_Y (1.0f/64)
#define MC_SCALE_Z (1.0f/64)

void MC_UnCompress(float mat[3][4],const unsigned char * comp);

/*
=============================================================

ANIMATED MODELS

=============================================================
*/

void R_MDRAddAnimSurfaces( trRefEntity_t *ent );
void RB_MDRSurfaceAnim( mdrSurface_t *surface );
qboolean R_LoadIQM (model_t *mod, void *buffer, int filesize, const char *name );
void R_AddIQMSurfaces( trRefEntity_t *ent );
void RB_IQMSurfaceAnim( const surfaceType_t *surface );
int R_IQMLerpTag( orientation_t *tag, iqmData_t *data,
                  int startFrame, int endFrame,
                  float frac, const char *tagName );

/*
=============================================================
=============================================================
*/
void	R_TransformModelToClip( const vec3_t src, const float *modelMatrix, const float *projectionMatrix,
							vec4_t eye, vec4_t dst );
void	R_TransformClipToWindow( const vec4_t clip, const viewParms_t *view, vec4_t normalized, vec4_t window );

void	RB_DeformTessGeometry( void );

void	RB_CalcEnvironmentTexCoords( float *dstTexCoords );
void	RB_CalcEnvironmentTexCoordsFP( float *dstTexCoords, int screenMap );
void	RB_CalcFogTexCoords( float *dstTexCoords );
const fogProgramParms_t *RB_CalcFogProgramParms( void );
void	RB_CalcScrollTexCoords( const float scroll[2], float *srcTexCoords, float *dstTexCoords );
void	RB_CalcRotateTexCoords( float rotSpeed, float *srcTexCoords, float *dstTexCoords );
void	RB_CalcRotateTexCoords2( float rotSpeed, float *srcTexCoords, float *dstTexCoords );
void	RB_CalcScaleTexCoords( const float scale[2], float *srcTexCoords, float *dstTexCoords );
void	RB_CalcTurbulentTexCoords( const waveForm_t *wf, float *srcTexCoords, float *dstTexCoords );
void	RB_CalcTransformTexCoords( const texModInfo_t *tmi, float *srcTexCoords, float *dstTexCoords );
void	RB_CalcModulateColorsByFog( unsigned char *dstColors );
void	RB_CalcModulateAlphasByFog( unsigned char *dstColors );
void	RB_CalcModulateRGBAsByFog( unsigned char *dstColors );
void	RB_CalcWaveAlpha( const waveForm_t *wf, unsigned char *dstColors );
void	RB_CalcWaveColor( const waveForm_t *wf, unsigned char *dstColors );
void	RB_CalcAlphaFromEntity( unsigned char *dstColors );
void	RB_CalcAlphaFromOneMinusEntity( unsigned char *dstColors );
void	RB_CalcStretchTexCoords( const waveForm_t *wf, float *srcTexCoords, float *dstTexCoords );
void	RB_CalcColorFromEntity( unsigned char *dstColors );
void	RB_CalcColorFromOneMinusEntity( unsigned char *dstColors );
void	RB_CalcSpecularAlpha( unsigned char *alphas );
void	RB_CalcDiffuseColor( unsigned char *colors );

/*
=============================================================

RENDERER BACK END FUNCTIONS

=============================================================
*/

void RB_ExecuteRenderCommands( const void *data );

/*
=============================================================

RENDERER BACK END COMMAND QUEUE

=============================================================
*/

#define	MAX_RENDER_COMMANDS	0x80000

typedef struct {
	byte	cmds[MAX_RENDER_COMMANDS];
	int		used;
} renderCommandList_t;

typedef struct {
	int		commandId;
	float	color[4];
} setColorCommand_t;

typedef struct {
	int		commandId;
	int		buffer;
} drawBufferCommand_t;

typedef struct {
	int		commandId;
	image_t	*image;
	int		width;
	int		height;
	void	*data;
} subImageCommand_t;

typedef struct {
	int		commandId;
} swapBuffersCommand_t;

typedef struct {
	int		commandId;
} finishBloomCommand_t;

typedef struct {
	int		commandId;
	shader_t	*shader;
	float	x, y;
	float	w, h;
	float	s1, t1;
	float	s2, t2;
} stretchPicCommand_t;

typedef struct drawSurfsCommand_s {
	int		commandId;
	trRefdef_t	refdef;
	viewParms_t	viewParms;
	drawSurf_t *drawSurfs;
	int		numDrawSurfs;
} drawSurfsCommand_t;

typedef struct
{
	int commandId;

	GLboolean rgba[4];
} colorMaskCommand_t;

typedef struct
{
	int commandId;
} clearDepthCommand_t;

typedef struct
{
	int commandId;
} clearColorCommand_t;

typedef enum {
	RC_END_OF_LIST,
	RC_SET_COLOR,
	RC_STRETCH_PIC,
	RC_DRAW_SURFS,
	RC_DRAW_BUFFER,
	RC_SWAP_BUFFERS,
	RC_FINISHBLOOM,
	RC_COLORMASK,
	RC_CLEARDEPTH,
	RC_CLEARCOLOR
} renderCommand_t;


// these are sort of arbitrary limits.
// the limits apply to the sum of all scenes in a frame --
// the main view, all the 3D icons, etc
#define	MAX_POLYS		8192
#define	MAX_POLYVERTS	32768

// all of the information needed by the back end must be
// contained in a backEndData_t
typedef struct {
	drawSurf_t	drawSurfs[MAX_DRAWSURFS];
#ifdef USE_PMLIGHT
	litSurf_t	litSurfs[MAX_LITSURFS];
	dlight_t	dlights[MAX_REAL_DLIGHTS];
#else
	dlight_t	dlights[MAX_DLIGHTS];
#endif

	trRefEntity_t	entities[MAX_REFENTITIES];
	srfPoly_t	*polys;//[MAX_POLYS];
	polyVert_t	*polyVerts;//[MAX_POLYVERTS];
	renderCommandList_t	commands;
} backEndData_t;

extern	int		max_polys;
extern	int		max_polyverts;

extern	backEndData_t	*backEndData;

void RB_ExecuteRenderCommands( const void *data );
void RB_TakeScreenshot( int x, int y, int width, int height, const char *fileName );
void RB_TakeScreenshotJPEG( int x, int y, int width, int height, const char *fileName );
void RB_TakeScreenshotBMP( int x, int y, int width, int height, const char *fileName, int clipboard );

void R_AddDrawSurfCmd( drawSurf_t *drawSurfs, int numDrawSurfs );

void RE_SetColor( const float *rgba );
void RE_StretchPic ( float x, float y, float w, float h, 
					  float s1, float t1, float s2, float t2, qhandle_t hShader );
void RE_BeginFrame( stereoFrame_t stereoFrame );
void RE_EndFrame( int *frontEndMsec, int *backEndMsec );
void RE_TakeVideoFrame( int width, int height,
		byte *captureBuffer, byte *encodeBuffer, qboolean motionJpeg );

void RE_FinishBloom( void );
void RE_ThrottleBackend( void );
qboolean RE_CanMinimize( void );
const glconfig_t *RE_GetConfig( void );
void RE_VertexLighting( qboolean allowed );

#ifndef USE_VULKAN
#define GLE( ret, name, ... ) extern ret ( APIENTRY * q##name )( __VA_ARGS__ );
	QGL_Core_PROCS;
	QGL_Ext_PROCS;
#undef GLE
#endif

#ifdef USE_VBO
// VBO functions
extern void R_BuildWorldVBO( msurface_t *surf, int surfCount );

extern void VBO_PushData( int itemIndex, shaderCommands_t *input );
extern void VBO_UnBind( void );

extern void VBO_Cleanup( void );
extern void VBO_QueueItem( int itemIndex );
extern void VBO_ClearQueue( void );
extern void VBO_Flush( void );
#endif

int R_GetLightmapCoords( const int lightmapIndex, float *x, float *y );

#endif //TR_LOCAL_H
