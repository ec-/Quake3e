#pragma once

#include "../renderercommon/vulkan/vulkan.h"
#include "tr_common.h"

#define MAX_SWAPCHAIN_IMAGES 8
#define MIN_SWAPCHAIN_IMAGES_IMM 3
#define MIN_SWAPCHAIN_IMAGES_FIFO   3
#define MIN_SWAPCHAIN_IMAGES_FIFO_0 4
#define MIN_SWAPCHAIN_IMAGES_MAILBOX 3

#define MAX_VK_SAMPLERS 32
#define MAX_VK_PIPELINES ((1024 + 128)*2)

#define VERTEX_BUFFER_SIZE (4 * 1024 * 1024)
#define IMAGE_CHUNK_SIZE (32 * 1024 * 1024)
#define MAX_IMAGE_CHUNKS 56

#define NUM_COMMAND_BUFFERS 2	// number of command buffers / render semaphores / framebuffer sets

#define USE_REVERSED_DEPTH
//#define USE_BUFFER_CLEAR

#define VK_NUM_BLOOM_PASSES 4

#ifndef _DEBUG
#define USE_DEDICATED_ALLOCATION
#endif
//#define MIN_IMAGE_ALIGN (128*1024)
#define MAX_ATTACHMENTS_IN_POOL (8+VK_NUM_BLOOM_PASSES*2) // depth + msaa + msaa-resolve + depth-resolve + screenmap.msaa + screenmap.resolve + screenmap.depth + bloom_extract + blur pairs

#define VK_DESC_STORAGE      0
#define VK_DESC_UNIFORM      1
#define VK_DESC_TEXTURE0     2
#define VK_DESC_TEXTURE1     3
#define VK_DESC_TEXTURE2     4
#define VK_DESC_FOG_COLLAPSE 5
#define VK_DESC_COUNT        6

#define VK_DESC_TEXTURE_BASE VK_DESC_TEXTURE0
#define VK_DESC_FOG_ONLY     VK_DESC_TEXTURE1
#define VK_DESC_FOG_DLIGHT   VK_DESC_TEXTURE1

typedef enum {
	TYPE_COLOR_BLACK,
	TYPE_COLOR_WHITE,
	TYPE_COLOR_GREEN,
	TYPE_COLOR_RED,
	TYPE_FOG_ONLY,
	TYPE_DOT,

	TYPE_SIGNLE_TEXTURE_LIGHTING,
	TYPE_SIGNLE_TEXTURE_LIGHTING_LINEAR,

	TYPE_SIGNLE_TEXTURE_DF,

	TYPE_GENERIC_BEGIN, // start of non-env/env shader pairs
	TYPE_SIGNLE_TEXTURE = TYPE_GENERIC_BEGIN,
	TYPE_SIGNLE_TEXTURE_ENV,

	TYPE_SIGNLE_TEXTURE_IDENTITY,
	TYPE_SIGNLE_TEXTURE_IDENTITY_ENV,

	TYPE_SIGNLE_TEXTURE_FIXED_COLOR,
	TYPE_SIGNLE_TEXTURE_FIXED_COLOR_ENV,

	TYPE_SIGNLE_TEXTURE_ENT_COLOR,
	TYPE_SIGNLE_TEXTURE_ENT_COLOR_ENV,

	TYPE_MULTI_TEXTURE_ADD2_IDENTITY,
	TYPE_MULTI_TEXTURE_ADD2_IDENTITY_ENV,
	TYPE_MULTI_TEXTURE_MUL2_IDENTITY,
	TYPE_MULTI_TEXTURE_MUL2_IDENTITY_ENV,

	TYPE_MULTI_TEXTURE_ADD2_FIXED_COLOR,
	TYPE_MULTI_TEXTURE_ADD2_FIXED_COLOR_ENV,
	TYPE_MULTI_TEXTURE_MUL2_FIXED_COLOR,
	TYPE_MULTI_TEXTURE_MUL2_FIXED_COLOR_ENV,

	TYPE_MULTI_TEXTURE_MUL2,
	TYPE_MULTI_TEXTURE_MUL2_ENV,
	TYPE_MULTI_TEXTURE_ADD2_1_1,
	TYPE_MULTI_TEXTURE_ADD2_1_1_ENV,
	TYPE_MULTI_TEXTURE_ADD2,
	TYPE_MULTI_TEXTURE_ADD2_ENV,

	TYPE_MULTI_TEXTURE_MUL3,
	TYPE_MULTI_TEXTURE_MUL3_ENV,
	TYPE_MULTI_TEXTURE_ADD3_1_1,
	TYPE_MULTI_TEXTURE_ADD3_1_1_ENV,
	TYPE_MULTI_TEXTURE_ADD3,
	TYPE_MULTI_TEXTURE_ADD3_ENV,

	TYPE_BLEND2_ADD,
	TYPE_BLEND2_ADD_ENV,
	TYPE_BLEND2_MUL,
	TYPE_BLEND2_MUL_ENV,
	TYPE_BLEND2_ALPHA,
	TYPE_BLEND2_ALPHA_ENV,
	TYPE_BLEND2_ONE_MINUS_ALPHA,
	TYPE_BLEND2_ONE_MINUS_ALPHA_ENV,
	TYPE_BLEND2_MIX_ALPHA,
	TYPE_BLEND2_MIX_ALPHA_ENV,

	TYPE_BLEND2_MIX_ONE_MINUS_ALPHA,
	TYPE_BLEND2_MIX_ONE_MINUS_ALPHA_ENV,

	TYPE_BLEND2_DST_COLOR_SRC_ALPHA,
	TYPE_BLEND2_DST_COLOR_SRC_ALPHA_ENV,

	TYPE_BLEND3_ADD,
	TYPE_BLEND3_ADD_ENV,
	TYPE_BLEND3_MUL,
	TYPE_BLEND3_MUL_ENV,
	TYPE_BLEND3_ALPHA,
	TYPE_BLEND3_ALPHA_ENV,
	TYPE_BLEND3_ONE_MINUS_ALPHA,
	TYPE_BLEND3_ONE_MINUS_ALPHA_ENV,
	TYPE_BLEND3_MIX_ALPHA,
	TYPE_BLEND3_MIX_ALPHA_ENV,
	TYPE_BLEND3_MIX_ONE_MINUS_ALPHA,
	TYPE_BLEND3_MIX_ONE_MINUS_ALPHA_ENV,

	TYPE_BLEND3_DST_COLOR_SRC_ALPHA,
	TYPE_BLEND3_DST_COLOR_SRC_ALPHA_ENV,

	TYPE_GENERIC_END = TYPE_BLEND3_MIX_ONE_MINUS_ALPHA_ENV

} Vk_Shader_Type;

// used with cg_shadows == 2
typedef enum {
	SHADOW_DISABLED,
	SHADOW_EDGES,
	SHADOW_FS_QUAD,
} Vk_Shadow_Phase;

typedef enum {
	TRIANGLE_LIST = 0,
	TRIANGLE_STRIP,
	LINE_LIST,
	POINT_LIST
} Vk_Primitive_Topology;

typedef enum {
	DEPTH_RANGE_NORMAL,		// [0..1]
	DEPTH_RANGE_ZERO,		// [0..0]
	DEPTH_RANGE_ONE,		// [1..1]
	DEPTH_RANGE_WEAPON,		// [0..0.3]
	DEPTH_RANGE_COUNT
}  Vk_Depth_Range;

typedef struct {
	VkSamplerAddressMode address_mode; // clamp/repeat texture addressing mode
	int gl_mag_filter;		// GL_XXX mag filter
	int gl_min_filter;		// GL_XXX min filter
	qboolean max_lod_1_0;	// fixed 1.0 lod
	qboolean noAnisotropy;
} Vk_Sampler_Def;

typedef enum {
	RENDER_PASS_MAIN = 0,
	RENDER_PASS_SCREENMAP,
	RENDER_PASS_POST_BLOOM,
	RENDER_PASS_COUNT
} renderPass_t;

typedef struct {
	Vk_Shader_Type shader_type;
	unsigned int state_bits; // GLS_XXX flags
	cullType_t face_culling;
	qboolean polygon_offset;
	qboolean mirror;
	Vk_Shadow_Phase shadow_phase;
	Vk_Primitive_Topology primitives;
	int line_width;
	int fog_stage; // off, fog-in / fog-out
	int abs_light;
	int allow_discard;
	int acff; // none, rgb, rgba, alpha
	struct {
		byte rgb;
		byte alpha;
	} color;
} Vk_Pipeline_Def;

typedef struct VK_Pipeline {
	Vk_Pipeline_Def def;
	VkPipeline handle[ RENDER_PASS_COUNT ];
} VK_Pipeline_t;

// this structure must be in sync with shader uniforms!
typedef struct vkUniform_s {
	// light/env parameters:
	vec4_t eyePos;				// vertex
	union {
		struct {
			vec4_t pos;			// vertex: light origin
			vec4_t color;		// fragment: rgb + 1/(r*r)
			vec4_t vector;		// fragment: linear dynamic light
		} light;
		struct {
			vec4_t color[3];	// ent.color[3]
		} ent;
	};
	// fog parameters:
	vec4_t fogDistanceVector;	// vertex
	vec4_t fogDepthVector;		// vertex
	vec4_t fogEyeT;				// vertex
	vec4_t fogColor;			// fragment
} vkUniform_t;

#define TESS_XYZ   (1)
#define TESS_RGBA0 (2)
#define TESS_RGBA1 (4)
#define TESS_RGBA2 (8)
#define TESS_ST0   (16)
#define TESS_ST1   (32)
#define TESS_ST2   (64)
#define TESS_NNN   (128)
#define TESS_VPOS  (256)  // uniform with eyePos
#define TESS_ENV   (512)  // mark shader stage with environment mapping
#define TESS_ENT0  (1024) // uniform with ent.color[0]
#define TESS_ENT1  (2048) // uniform with ent.color[1]
#define TESS_ENT2  (4096) // uniform with ent.color[2]
//
// Initialization.
//

// Initializes VK_Instance structure.
// After calling this function we get fully functional vulkan subsystem.
void vk_initialize( void );

// Called after initialization or renderer restart
void vk_init_descriptors( void );

// Shutdown vulkan subsystem by releasing resources acquired by Vk_Instance.
void vk_shutdown( refShutdownCode_t code );

// Releases vulkan resources allocated during program execution.
// This effectively puts vulkan subsystem into initial state (the state we have after vk_initialize call).
void vk_release_resources( void );

void vk_wait_idle( void );

//
// Resources allocation.
//
void vk_create_image( image_t *image, int width, int height, int mip_levels );
void vk_upload_image_data( image_t *image, int x, int y, int width, int height, int miplevels, byte *pixels, int size, qboolean update );
void vk_update_descriptor_set( image_t *image, qboolean mipmap );
void vk_destroy_image_resources( VkImage *image, VkImageView *imageView );

uint32_t vk_find_pipeline_ext( uint32_t base, const Vk_Pipeline_Def *def, qboolean use );
void vk_get_pipeline_def( uint32_t pipeline, Vk_Pipeline_Def *def );

void vk_create_post_process_pipeline( int program_index, uint32_t width, uint32_t height );
void vk_create_pipelines( void );

//
// Rendering setup.
//

void vk_clear_color( const vec4_t color );
void vk_clear_depth( qboolean clear_stencil );
void vk_begin_frame( void );
void vk_end_frame( void );
void vk_present_frame( void );

void vk_end_render_pass( void );
void vk_begin_main_render_pass( void );

void vk_bind_pipeline( uint32_t pipeline );
void vk_bind_index( void );
void vk_bind_index_ext( const int numIndexes, const uint32_t*indexes );
void vk_bind_geometry( uint32_t flags );
void vk_bind_lighting( int stage, int bundle );
void vk_draw_geometry( Vk_Depth_Range depth_range, qboolean indexed );

void vk_read_pixels( byte* buffer, uint32_t width, uint32_t height ); // screenshots
qboolean vk_bloom( void );

qboolean vk_alloc_vbo( const byte *vbo_data, int vbo_size );
void vk_update_mvp( const float *m );

uint32_t vk_tess_index( uint32_t numIndexes, const void *src );
void vk_bind_index_buffer( VkBuffer buffer, uint32_t offset );
void vk_draw_indexed( uint32_t indexCount, uint32_t firstIndex );

void vk_reset_descriptor( int index );
void vk_update_descriptor( int index, VkDescriptorSet descriptor );
void vk_update_descriptor_offset( int index, uint32_t offset );

void vk_update_post_process_pipelines( void );

const char *vk_format_string( VkFormat format );

void VBO_PrepareQueues( void );
void VBO_RenderIBOItems( void );
void VBO_ClearQueue( void );

typedef struct vk_tess_s {
	VkCommandBuffer command_buffer;

	VkSemaphore image_acquired;
	VkSemaphore rendering_finished;
	VkFence rendering_finished_fence;
	qboolean waitForFence;

	VkBuffer vertex_buffer;
	byte *vertex_buffer_ptr; // pointer to mapped vertex buffer
	VkDeviceSize vertex_buffer_offset;

	VkDescriptorSet uniform_descriptor;
	uint32_t		uniform_read_offset;
	VkDeviceSize	buf_offset[8];
	VkDeviceSize	vbo_offset[8];

	VkBuffer		curr_index_buffer;
	uint32_t		curr_index_offset;

	struct {
		uint32_t		start, end;
		VkDescriptorSet	current[6]; // 0:storage, 1:uniform, 2:color0, 3:color1, 4:color2, 5:fog
		uint32_t		offset[2]; // 0 (uniform) and 5 (storage)
	} descriptor_set;

	Vk_Depth_Range		depth_range;
	VkPipeline			last_pipeline;

	uint32_t num_indexes; // value from most recent vk_bind_index() call

	VkRect2D scissor_rect;
} vk_tess_t;


// Vk_Instance contains engine-specific vulkan resources that persist entire renderer lifetime.
// This structure is initialized/deinitialized by vk_initialize/vk_shutdown functions correspondingly.
typedef struct {
	VkPhysicalDevice physical_device;
	VkSurfaceFormatKHR base_format;
	VkSurfaceFormatKHR present_format;

	uint32_t queue_family_index;
	VkDevice device;
	VkQueue queue;

	VkSwapchainKHR swapchain;
	uint32_t swapchain_image_count;
	VkImage swapchain_images[MAX_SWAPCHAIN_IMAGES];
	VkImageView swapchain_image_views[MAX_SWAPCHAIN_IMAGES];
	uint32_t swapchain_image_index;

	VkCommandPool command_pool;

	VkDeviceMemory image_memory[ MAX_ATTACHMENTS_IN_POOL ];
	uint32_t image_memory_count;

	struct {
		VkRenderPass main;
		VkRenderPass screenmap;
		VkRenderPass gamma;
		VkRenderPass capture;
		VkRenderPass bloom_extract;
		VkRenderPass blur[VK_NUM_BLOOM_PASSES*2]; // horizontal-vertical pairs
		VkRenderPass post_bloom;
	} render_pass;

	VkDescriptorPool descriptor_pool;
	VkDescriptorSetLayout set_layout_sampler;	// combined image sampler
	VkDescriptorSetLayout set_layout_uniform;	// dynamic uniform buffer
	VkDescriptorSetLayout set_layout_storage;	// feedback buffer

	VkPipelineLayout pipeline_layout;			// default shaders
	//VkPipelineLayout pipeline_layout_storage;	// flare test shader layout
	VkPipelineLayout pipeline_layout_post_process;	// post-processing
	VkPipelineLayout pipeline_layout_blend;		// post-processing

	VkDescriptorSet color_descriptor;

	VkImage color_image;
	VkImageView color_image_view;

	VkImage bloom_image[1+VK_NUM_BLOOM_PASSES*2];
	VkImageView bloom_image_view[1+VK_NUM_BLOOM_PASSES*2];

	VkDescriptorSet bloom_image_descriptor[1+VK_NUM_BLOOM_PASSES*2];

	VkImage depth_image;
	VkImageView depth_image_view;

	VkImage msaa_image;
	VkImageView msaa_image_view;

	// screenMap
	struct {
		VkDescriptorSet color_descriptor;
		VkImage color_image;
		VkImageView color_image_view;

		VkImage color_image_msaa;
		VkImageView color_image_view_msaa;

		VkImage depth_image;
		VkImageView depth_image_view;

	} screenMap;

	struct {
		VkImage image;
		VkImageView image_view;
	} capture;

	struct {
		VkFramebuffer blur[VK_NUM_BLOOM_PASSES*2];
		VkFramebuffer bloom_extract;
		VkFramebuffer main[MAX_SWAPCHAIN_IMAGES];
		VkFramebuffer gamma[MAX_SWAPCHAIN_IMAGES];
		VkFramebuffer screenmap;
		VkFramebuffer capture;
	} framebuffers;

	vk_tess_t tess[ NUM_COMMAND_BUFFERS ], *cmd;
	int cmd_index;

	struct {
		VkBuffer		buffer;
		byte			*buffer_ptr;
		VkDeviceMemory	memory;
		VkDescriptorSet	descriptor;
	} storage;

	uint32_t uniform_item_size;
	uint32_t uniform_alignment;
	uint32_t storage_alignment;

	struct {
		VkBuffer vertex_buffer;
		VkDeviceMemory	buffer_memory;
	} vbo;

	// host visible memory that holds vertex, index and uniform data
	VkDeviceMemory geometry_buffer_memory;
	VkDeviceSize geometry_buffer_size;
	VkDeviceSize geometry_buffer_size_new;

	// statistics
	struct {
		VkDeviceSize vertex_buffer_max;
		uint32_t push_size;
		uint32_t push_size_max;
	} stats;

	//
	// Shader modules.
	//
	struct {
		struct {
			VkShaderModule gen[3][2][2][2]; // tx[0,1,2], cl[0,1] env0[0,1] fog[0,1]
			VkShaderModule ident1[2][2][2]; // tx[0,1], env0[0,1] fog[0,1]
			VkShaderModule fixed[2][2][2];  // tx[0,1], env0[0,1] fog[0,1]
			VkShaderModule light[2];        // fog[0,1]
		} vert;
		struct {
			VkShaderModule gen0_df;
			VkShaderModule gen[3][2][2]; // tx[0,1,2] cl[0,1] fog[0,1]
			VkShaderModule ident1[2][2]; // tx[0,1], fog[0,1]
			VkShaderModule fixed[2][2];  // tx[0,1], fog[0,1]
			VkShaderModule ent[1][2];    // tx[0], fog[0,1]
			VkShaderModule light[2][2];  // linear[0,1] fog[0,1]
		} frag;

		VkShaderModule color_fs;
		VkShaderModule color_vs;

		VkShaderModule bloom_fs;
		VkShaderModule blur_fs;
		VkShaderModule blend_fs;

		VkShaderModule gamma_fs;
		VkShaderModule gamma_vs;

		VkShaderModule fog_fs;
		VkShaderModule fog_vs;

		VkShaderModule dot_fs;
		VkShaderModule dot_vs;
	} modules;

	VkPipelineCache pipelineCache;

	VK_Pipeline_t pipelines[ MAX_VK_PIPELINES ];
	uint32_t pipelines_count;
	uint32_t pipelines_world_base;

	// pipeline statistics
	int32_t pipeline_create_count;

	//
	// Standard pipelines.
	//
	uint32_t skybox_pipeline;

	// dim 0: 0 - front side, 1 - back size
	// dim 1: 0 - normal view, 1 - mirror view
	uint32_t shadow_volume_pipelines[2][2];
	uint32_t shadow_finish_pipeline;

	// dim 0 is based on fogPass_t: 0 - corresponds to FP_EQUAL, 1 - corresponds to FP_LE.
	// dim 1 is directly a cullType_t enum value.
	// dim 2 is a polygon offset value (0 - off, 1 - on).
	uint32_t fog_pipelines[2][3][2];

	// dim 0 is based on dlight additive flag: 0 - not additive, 1 - additive
	// dim 1 is directly a cullType_t enum value.
	// dim 2 is a polygon offset value (0 - off, 1 - on).
#ifdef USE_LEGACY_DLIGHTS
	uint32_t dlight_pipelines[2][3][2];
#endif

	// cullType[3], polygonOffset[2], fogStage[2], absLight[2]
#ifdef USE_PMLIGHT
	uint32_t dlight_pipelines_x[3][2][2][2];
	uint32_t dlight1_pipelines_x[3][2][2][2];
#endif

	// debug visualization pipelines
	uint32_t tris_debug_pipeline;
	uint32_t tris_mirror_debug_pipeline;
	uint32_t tris_debug_green_pipeline;
	uint32_t tris_mirror_debug_green_pipeline;
	uint32_t tris_debug_red_pipeline;
	uint32_t tris_mirror_debug_red_pipeline;

	uint32_t normals_debug_pipeline;
	uint32_t surface_debug_pipeline_solid;
	uint32_t surface_debug_pipeline_outline;
	uint32_t images_debug_pipeline;
	uint32_t surface_beam_pipeline;
	uint32_t surface_axis_pipeline;
	uint32_t dot_pipeline;

	VkPipeline gamma_pipeline;
	VkPipeline capture_pipeline;
	VkPipeline bloom_extract_pipeline;
	VkPipeline blur_pipeline[VK_NUM_BLOOM_PASSES*2]; // horizontal & vertical pairs
	VkPipeline bloom_blend_pipeline;

	uint32_t frame_count;
	qboolean active;
	qboolean wideLines;
	qboolean samplerAnisotropy;
	qboolean fragmentStores;
	qboolean dedicatedAllocation;
	qboolean debugMarkers;

	float maxAnisotropy;
	float maxLod;

	VkFormat color_format;
	VkFormat capture_format;
	VkFormat depth_format;
	VkFormat bloom_format;

	VkImageLayout initSwapchainLayout;

	qboolean fastSky;		// requires VK_IMAGE_USAGE_TRANSFER_DST_BIT
	qboolean fboActive;
	qboolean blitEnabled;
	qboolean msaaActive;

	qboolean offscreenRender;

	qboolean windowAdjusted;
	int		blitX0;
	int		blitY0;
	int		blitFilter;

	uint32_t renderWidth;
	uint32_t renderHeight;

	float renderScaleX;
	float renderScaleY;

	renderPass_t renderPassIndex;

	uint32_t screenMapWidth;
	uint32_t screenMapHeight;
	uint32_t screenMapSamples;

	uint32_t image_chunk_size;

	uint32_t maxBoundDescriptorSets;

	VkFence aux_fence;

} Vk_Instance;

typedef struct {
	VkDeviceMemory memory;
	VkDeviceSize used;
} ImageChunk;

// Vk_World contains vulkan resources/state requested by the game code.
// It is reinitialized on a map change.
typedef struct {
	//
	// Resources.
	//
	int num_samplers;
	Vk_Sampler_Def sampler_defs[MAX_VK_SAMPLERS];
	VkSampler samplers[MAX_VK_SAMPLERS];

	//
	// Memory allocations.
	//
	int num_image_chunks;
	ImageChunk image_chunks[MAX_IMAGE_CHUNKS];

	// Host visible memory used to copy image data to device local memory.
	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;
	VkDeviceSize staging_buffer_size;
	byte *staging_buffer_ptr; // pointer to mapped staging buffer

	//
	// State.
	//

	// Descriptor sets corresponding to bound texture images.
	//VkDescriptorSet current_descriptor_sets[ MAX_TEXTURE_UNITS ];

	// This flag is used to decide whether framebuffer's depth attachment should be cleared
	// with vmCmdClearAttachment (dirty_depth_attachment != 0), or it have just been
	// cleared by render pass instance clear op (dirty_depth_attachment == 0).
	int dirty_depth_attachment;

	float modelview_transform[16];
} Vk_World;

extern Vk_Instance	vk;				// shouldn't be cleared during ref re-init
extern Vk_World		vk_world;		// this data is cleared during ref re-init
