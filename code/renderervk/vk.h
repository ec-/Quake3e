#pragma once

#include "../renderercommon/vulkan/vulkan.h"
#include "tr_common.h"

#define MAX_SWAPCHAIN_IMAGES 4
#define MAX_VK_SAMPLERS 32
#define MAX_VK_PIPELINES (1024 + 128)

#define IMAGE_CHUNK_SIZE (32 * 1024 * 1024)
#define MAX_IMAGE_CHUNKS 48

#define USE_IMAGE_POOL
#define USE_IMAGE_LAYOUT_1
#define MIN_IMAGE_ALIGN (128*1024)

#define VK_CHECK(function_call) { \
	VkResult result = function_call; \
	if (result < 0) \
		ri.Error(ERR_FATAL, "Vulkan: error code %d returned by %s", result, #function_call); \
}

typedef enum {
	TYPE_SIGNLE_TEXTURE,
	TYPE_SIGNLE_TEXTURE_DF,
	TYPE_SIGNLE_TEXTURE_ENVIRO,
	TYPE_SIGNLE_TEXTURE_LIGHTING,
	TYPE_SIGNLE_TEXTURE_LIGHTING1,
	TYPE_MULTI_TEXTURE_MUL,
	TYPE_MULTI_TEXTURE_ADD,
	TYPE_COLOR_WHITE,
	TYPE_COLOR_GREEN,
	TYPE_COLOR_RED,
	TYPE_FOG_ONLY,
	TYPE_DOT,
} Vk_Shader_Type;

// used with cg_shadows == 2
typedef enum {
	SHADOW_DISABLED,
	SHADOW_EDGES,
	SHADOW_FS_QUAD,
} Vk_Shadow_Phase;

typedef enum {
	DEPTH_RANGE_NORMAL, // [0..1]
	DEPTH_RANGE_ZERO, // [0..0]
	DEPTH_RANGE_ONE, // [1..1]
	DEPTH_RANGE_WEAPON // [0..0.3]
}  Vk_Depth_Range;

typedef struct {
	VkSamplerAddressMode address_mode; // clamp/repeat texture addressing mode
	int gl_mag_filter; // GL_XXX mag filter
	int gl_min_filter; // GL_XXX min filter
} Vk_Sampler_Def;

typedef struct {
	Vk_Shader_Type shader_type;
	unsigned int state_bits; // GLS_XXX flags
	cullType_t face_culling;
	qboolean polygon_offset;
	qboolean clipping_plane;
	qboolean mirror;
	qboolean line_primitives;
	Vk_Shadow_Phase shadow_phase;
	int fog_stage; // off, fog-in / fog-out
	int line_width;
	qboolean gamma;
} Vk_Pipeline_Def;

typedef struct VK_Pipeline {
	Vk_Pipeline_Def def;
	VkPipeline handle;
} VK_Pipeline_t;
	
// this structure must be in sync with shader uniforms!
typedef struct vkUniform_s {
	// vertex shader reference
	vec4_t eyePos;
	vec4_t lightPos;
	// vertex - fog parameters
	vec4_t fogDistanceVector;
	vec4_t fogDepthVector;
	vec4_t fogEyeT;
	// fragment shader reference
	vec4_t lightColor; // rgb + 1/(r*r)
	vec4_t fogColor;
	// fragment - linear dynamic light
	vec4_t lightVector;
} vkUniform_t;

#define VERTEX_BUFFER_SIZE (8 * 1024 * 1024)

#define NUM_COMMAND_BUFFERS 2	// number of command buffers / render semaphores / framebuffer sets
#define USE_SINGLE_FBO			// use single framebuffer set for all command buffers

#define MIN_SWAPCHAIN_IMAGES_IMM 2
#define MIN_SWAPCHAIN_IMAGES_FIFO 3
#define MIN_SWAPCHAIN_IMAGES_MAILBOX 2

#define TESS_IDX   (1)
#define TESS_XYZ   (2)
#define TESS_RGBA  (4)
#define TESS_ST0   (8)
#define TESS_ST1   (16)
#define TESS_NNN   (32)
#define TESS_ST0_1 (64)  // use data from TMU1
#define TESS_VPOS  (128) // uniform with eyePos
#define TESS_ENV   (256) // mark shader stage with environment mapping

//
// Initialization.
//

// Initializes VK_Instance structure.
// After calling this function we get fully functional vulkan subsystem.
void vk_initialize( void );

// Called after initialization or renderer restart
void vk_init_buffers( void );

// Shutdown vulkan subsystem by releasing resources acquired by Vk_Instance.
void vk_shutdown( void );

// Releases vulkan resources allocated during program execution.
// This effectively puts vulkan subsystem into initial state (the state we have after vk_initialize call).
void vk_release_resources( void );

void vk_wait_idle( void );

//
// Resources allocation.
//
void vk_create_image( int width, int height, VkFormat format, int mip_levels, VkSamplerAddressMode address_mode, image_t *image );
void vk_upload_image_data( VkImage image, int x, int y, int width, int height, qboolean mipmap, const uint8_t* pixels, int bytes_per_pixel );
void vk_update_descriptor_set( VkDescriptorSet desc, VkImageView image_view, qboolean mipmap, VkSamplerAddressMode address_mode );
VkSampler vk_find_sampler( const Vk_Sampler_Def *def );
	
uint32_t vk_find_pipeline_ext( uint32_t base, const Vk_Pipeline_Def *def, qboolean use );
void vk_get_pipeline_def( uint32_t pipeline, Vk_Pipeline_Def *def );

void vk_create_gamma_pipeline( void );
void vk_restart_swapchain( const char *funcname );

//
// Rendering setup.
//
void vk_clear_attachments(qboolean clear_depth, qboolean clear_stencil, qboolean clear_color, const vec4_t color);
void vk_bind_geometry( void );
void vk_begin_frame( void );
void vk_end_frame( void );

void vk_bind_geometry_ext(int flags);
void vk_draw_geometry( uint32_t pipeline, int32_t set_count, Vk_Depth_Range depth_range, qboolean indexed);

void vk_draw_light( uint32_t pipeline, Vk_Depth_Range depth_range, uint32_t uniform_offset, int fog);

void vk_read_pixels( byte* buffer, uint32_t width, uint32_t height ); // screenshots

qboolean vk_alloc_vbo( const byte *vbo_data, int vbo_size );
void vk_bind_fog_image( void );
void vk_update_mvp( const float *m );

uint32_t vk_tess_index( uint32_t numIndexes, const void *src );

const char *vk_get_format_name( VkFormat format );

void VBO_PrepareQueues( void );
void VBO_RenderIBOItems( void );
void VBO_ClearQueue( void );

typedef struct vk_tess_s {
	VkCommandBuffer command_buffer;

	VkSemaphore rendering_finished;
	VkFence rendering_finished_fence;

	VkBuffer vertex_buffer;
	byte *vertex_buffer_ptr; // pointer to mapped vertex buffer
	VkDeviceSize vertex_buffer_offset;

	VkDescriptorSet uniform_descriptor;
	uint32_t		uniform_read_offset;
	VkDeviceSize	buf_offset[5];
	VkDeviceSize	vbo_offset[5];

	qboolean		fog_bound;

#ifndef USE_SINGLE_FBO
	VkDescriptorSet color_descriptor;

	VkImage color_image;
//#ifndef USE_IMAGE_POOL
	VkDeviceMemory color_image_memory;
//#endif
	VkImageView color_image_view;

	VkImage depth_image;
//#ifndef USE_IMAGE_POOL
	VkDeviceMemory depth_image_memory;
//#endif
	VkImageView depth_image_view;

	VkImage msaa_image;
//#ifndef USE_IMAGE_POOL
	VkDeviceMemory msaa_image_memory;
//#endif
	VkImageView msaa_image_view;

	VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];
#endif

} vk_tess_t;


// Vk_Instance contains engine-specific vulkan resources that persist entire renderer lifetime.
// This structure is initialized/deinitialized by vk_initialize/vk_shutdown functions correspondingly.
typedef struct {
	VkInstance instance;
	VkPhysicalDevice physical_device;
	VkSurfaceKHR surface;
	VkSurfaceFormatKHR surface_format;

	uint32_t queue_family_index;
	VkDevice device;
	VkQueue queue;

	VkSwapchainKHR swapchain;
	uint32_t swapchain_image_count;
	VkImage swapchain_images[MAX_SWAPCHAIN_IMAGES];
	VkImageView swapchain_image_views[MAX_SWAPCHAIN_IMAGES];
	uint32_t swapchain_image_index;

	VkCommandPool command_pool;
	VkSemaphore image_acquired;

#ifdef USE_IMAGE_POOL
	VkDeviceMemory image_memory;
#endif

	VkRenderPass render_pass;

	VkDescriptorPool descriptor_pool;
	VkDescriptorSetLayout set_layout_sampler;	// combined image sampler
	VkDescriptorSetLayout set_layout_uniform;	// dynamic uniform buffer
	VkDescriptorSetLayout set_layout_storage;	// color input attachment
	VkDescriptorSetLayout set_layout_input;		// color input attachment

	VkPipelineLayout pipeline_layout;			// default shaders
	//VkPipelineLayout pipeline_layout_storage;	// flare test shader layout
	VkPipelineLayout pipeline_layout_gamma;		// gamma post-processing

#ifdef USE_SINGLE_FBO
	VkDescriptorSet color_descriptor;

	VkImage color_image;
//#ifndef USE_IMAGE_POOL
	VkDeviceMemory color_image_memory;
//#endif
	VkImageView color_image_view;

	VkImage depth_image;
//#ifndef USE_IMAGE_POOL
	VkDeviceMemory depth_image_memory;
//#endif
	VkImageView depth_image_view;

	VkImage msaa_image;
//#ifndef USE_IMAGE_POOL
	VkDeviceMemory msaa_image_memory;
//#endif
	VkImageView msaa_image_view;

	VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];
#endif

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
		VkShaderModule st_clip_vs[2];
		VkShaderModule st_enviro_vs[2];
		VkShaderModule mt_clip_vs[2];

		VkShaderModule st_fs[2];
		VkShaderModule st_df_fs;
		VkShaderModule mt_mul_fs[2];
		VkShaderModule mt_add_fs[2];

		VkShaderModule color_fs;
		VkShaderModule color_clip_vs;

		VkShaderModule gamma_fs;
		VkShaderModule gamma_vs;

		VkShaderModule fog_fs;
		VkShaderModule fog_vs;

		VkShaderModule dot_fs;
		VkShaderModule dot_vs;

		struct {
			VkShaderModule vs_clip[2];
			VkShaderModule fs[2];
		} light;

		struct {
			VkShaderModule fs[2];
		} light1;

	} modules;

	VK_Pipeline_t pipelines[ MAX_VK_PIPELINES ];
	uint32_t pipelines_count;
	uint32_t pipelines_created_count;
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
	uint32_t dlight_pipelines[2][3][2];

	// clippingPlane[2], cullType[3], polygonOffset[2], fogStage[3]
#ifdef USE_PMLIGHT
	uint32_t dlight_pipelines_x[2][3][2][2];
	uint32_t dlight1_pipelines_x[2][3][2][2];
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

#ifndef NDEBUG
	VkDebugReportCallbackEXT debug_callback;
#endif
	uint32_t frame_count;
	qboolean active;
	qboolean wideLines;
	qboolean samplerAnisotropy;
	qboolean fragmentStores;
	float maxAnisotropy;
	float maxLodBias;

	VkFormat color_format;
	VkFormat resolve_format;
	VkFormat depth_format;

	qboolean fboActive;
	qboolean msaaActive;

	qboolean offscreenRender;

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
	VkDescriptorSet current_descriptor_sets[ MAX_TEXTURE_UNITS ];

	// This flag is used to decide whether framebuffer's depth attachment should be cleared
	// with vmCmdClearAttachment (dirty_depth_attachment == true), or it have just been
	// cleared by render pass instance clear op (dirty_depth_attachment == false).
	qboolean dirty_depth_attachment;

	float modelview_transform[16];
} Vk_World;

extern Vk_Instance	vk;				// shouldn't be cleared during ref re-init
extern Vk_World		vk_world;		// this data is cleared during ref re-init

// Most of the renderer's code uses Vulkan API via function provides in this file but 
// there are few places outside of vk.cpp where we use Vulkan commands directly.

extern PFN_vkGetPhysicalDeviceProperties qvkGetPhysicalDeviceProperties;
extern PFN_vkDestroyImage qvkDestroyImage;
extern PFN_vkDestroyImageView qvkDestroyImageView;
extern PFN_vkCmdBindIndexBuffer qvkCmdBindIndexBuffer;
extern PFN_vkCmdDrawIndexed qvkCmdDrawIndexed;
extern PFN_vkCmdBindDescriptorSets qvkCmdBindDescriptorSets;
