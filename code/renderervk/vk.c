#include "tr_local.h"
#include "vk.h"
#if defined (_WIN32) && !defined (NDEBUG)
#include <windows.h> // for win32 debug callback
#endif

static int vkSamples = VK_SAMPLE_COUNT_1_BIT;
static int vkMaxSamples = VK_SAMPLE_COUNT_1_BIT;

//
// Vulkan API functions used by the renderer.
//
PFN_vkGetInstanceProcAddr						qvkGetInstanceProcAddr;

PFN_vkCreateInstance							qvkCreateInstance;
PFN_vkEnumerateInstanceExtensionProperties		qvkEnumerateInstanceExtensionProperties;

PFN_vkCreateDevice								qvkCreateDevice;
PFN_vkDestroyInstance							qvkDestroyInstance;
PFN_vkEnumerateDeviceExtensionProperties		qvkEnumerateDeviceExtensionProperties;
PFN_vkEnumeratePhysicalDevices					qvkEnumeratePhysicalDevices;
PFN_vkGetDeviceProcAddr							qvkGetDeviceProcAddr;
PFN_vkGetPhysicalDeviceFeatures					qvkGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceFormatProperties			qvkGetPhysicalDeviceFormatProperties;
PFN_vkGetPhysicalDeviceMemoryProperties			qvkGetPhysicalDeviceMemoryProperties;
PFN_vkGetPhysicalDeviceProperties				qvkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties	qvkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkDestroySurfaceKHR							qvkDestroySurfaceKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR	qvkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR		qvkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR	qvkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR		qvkGetPhysicalDeviceSurfaceSupportKHR;
#ifndef NDEBUG
PFN_vkCreateDebugReportCallbackEXT				qvkCreateDebugReportCallbackEXT;
PFN_vkDestroyDebugReportCallbackEXT				qvkDestroyDebugReportCallbackEXT;
#endif
PFN_vkAllocateCommandBuffers					qvkAllocateCommandBuffers;
PFN_vkAllocateDescriptorSets					qvkAllocateDescriptorSets;
PFN_vkAllocateMemory							qvkAllocateMemory;
PFN_vkBeginCommandBuffer						qvkBeginCommandBuffer;
PFN_vkBindBufferMemory							qvkBindBufferMemory;
PFN_vkBindImageMemory							qvkBindImageMemory;
PFN_vkCmdBeginRenderPass						qvkCmdBeginRenderPass;
PFN_vkCmdBindDescriptorSets						qvkCmdBindDescriptorSets;
PFN_vkCmdBindIndexBuffer						qvkCmdBindIndexBuffer;
PFN_vkCmdBindPipeline							qvkCmdBindPipeline;
PFN_vkCmdBindVertexBuffers						qvkCmdBindVertexBuffers;
PFN_vkCmdBlitImage								qvkCmdBlitImage;
PFN_vkCmdClearAttachments						qvkCmdClearAttachments;
PFN_vkCmdCopyBuffer								qvkCmdCopyBuffer;
PFN_vkCmdCopyBufferToImage						qvkCmdCopyBufferToImage;
PFN_vkCmdCopyImage								qvkCmdCopyImage;
PFN_vkCmdDraw									qvkCmdDraw;
PFN_vkCmdDrawIndexed							qvkCmdDrawIndexed;
PFN_vkCmdEndRenderPass							qvkCmdEndRenderPass;
PFN_vkCmdNextSubpass							qvkCmdNextSubpass;
PFN_vkCmdPipelineBarrier						qvkCmdPipelineBarrier;
PFN_vkCmdPushConstants							qvkCmdPushConstants;
PFN_vkCmdSetDepthBias							qvkCmdSetDepthBias;
PFN_vkCmdSetScissor								qvkCmdSetScissor;
PFN_vkCmdSetViewport							qvkCmdSetViewport;
PFN_vkCreateBuffer								qvkCreateBuffer;
PFN_vkCreateCommandPool							qvkCreateCommandPool;
PFN_vkCreateDescriptorPool						qvkCreateDescriptorPool;
PFN_vkCreateDescriptorSetLayout					qvkCreateDescriptorSetLayout;
PFN_vkCreateFence								qvkCreateFence;
PFN_vkCreateFramebuffer							qvkCreateFramebuffer;
PFN_vkCreateGraphicsPipelines					qvkCreateGraphicsPipelines;
PFN_vkCreateImage								qvkCreateImage;
PFN_vkCreateImageView							qvkCreateImageView;
PFN_vkCreatePipelineLayout						qvkCreatePipelineLayout;
PFN_vkCreatePipelineCache						qvkCreatePipelineCache;
PFN_vkCreateRenderPass							qvkCreateRenderPass;
PFN_vkCreateSampler								qvkCreateSampler;
PFN_vkCreateSemaphore							qvkCreateSemaphore;
PFN_vkCreateShaderModule						qvkCreateShaderModule;
PFN_vkDestroyBuffer								qvkDestroyBuffer;
PFN_vkDestroyCommandPool						qvkDestroyCommandPool;
PFN_vkDestroyDescriptorPool						qvkDestroyDescriptorPool;
PFN_vkDestroyDescriptorSetLayout				qvkDestroyDescriptorSetLayout;
PFN_vkDestroyDevice								qvkDestroyDevice;
PFN_vkDestroyFence								qvkDestroyFence;
PFN_vkDestroyFramebuffer						qvkDestroyFramebuffer;
PFN_vkDestroyImage								qvkDestroyImage;
PFN_vkDestroyImageView							qvkDestroyImageView;
PFN_vkDestroyPipeline							qvkDestroyPipeline;
PFN_vkDestroyPipelineCache						qvkDestroyPipelineCache;
PFN_vkDestroyPipelineLayout						qvkDestroyPipelineLayout;
PFN_vkDestroyRenderPass							qvkDestroyRenderPass;
PFN_vkDestroySampler							qvkDestroySampler;
PFN_vkDestroySemaphore							qvkDestroySemaphore;
PFN_vkDestroyShaderModule						qvkDestroyShaderModule;
PFN_vkDeviceWaitIdle							qvkDeviceWaitIdle;
PFN_vkEndCommandBuffer							qvkEndCommandBuffer;
PFN_vkFlushMappedMemoryRanges					qvkFlushMappedMemoryRanges;
PFN_vkFreeCommandBuffers						qvkFreeCommandBuffers;
PFN_vkFreeDescriptorSets						qvkFreeDescriptorSets;
PFN_vkFreeMemory								qvkFreeMemory;
PFN_vkGetBufferMemoryRequirements				qvkGetBufferMemoryRequirements;
PFN_vkGetDeviceQueue							qvkGetDeviceQueue;
PFN_vkGetImageMemoryRequirements				qvkGetImageMemoryRequirements;
PFN_vkGetImageSubresourceLayout					qvkGetImageSubresourceLayout;
PFN_vkMapMemory									qvkMapMemory;
PFN_vkQueueSubmit								qvkQueueSubmit;
PFN_vkQueueWaitIdle								qvkQueueWaitIdle;
PFN_vkResetCommandBuffer						qvkResetCommandBuffer;
PFN_vkResetDescriptorPool						qvkResetDescriptorPool;
PFN_vkResetFences								qvkResetFences;
PFN_vkUnmapMemory								qvkUnmapMemory;
PFN_vkUpdateDescriptorSets						qvkUpdateDescriptorSets;
PFN_vkWaitForFences								qvkWaitForFences;
PFN_vkAcquireNextImageKHR						qvkAcquireNextImageKHR;
PFN_vkCreateSwapchainKHR						qvkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR						qvkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR						qvkGetSwapchainImagesKHR;
PFN_vkQueuePresentKHR							qvkQueuePresentKHR;

PFN_vkGetBufferMemoryRequirements2KHR			qvkGetBufferMemoryRequirements2KHR;
PFN_vkGetImageMemoryRequirements2KHR			qvkGetImageMemoryRequirements2KHR;

////////////////////////////////////////////////////////////////////////////

// forward declaration
VkPipeline create_pipeline( const Vk_Pipeline_Def *def, uint32_t renderPassIndex );

static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t memory_type_bits, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	uint32_t i;

	qvkGetPhysicalDeviceMemoryProperties( physical_device, &memory_properties );

	for (i = 0; i < memory_properties.memoryTypeCount; i++) {
		if ((memory_type_bits & (1 << i)) != 0 &&
			(memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	ri.Error(ERR_FATAL, "Vulkan: failed to find matching memory type with requested properties");
	return ~0U;
}


static const char *pmode_to_str( VkPresentModeKHR mode )
{
	static char buf[32];

	switch ( mode ) {
		case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
		case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
		case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
		case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
		default: sprintf( buf, "mode#%x", mode ); return buf;
	};
}


static VkFlags get_composite_alpha( VkCompositeAlphaFlagsKHR flags )
{
	const VkCompositeAlphaFlagBitsKHR compositeFlags[] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
	};
	int i;

	for ( i = 1; i < ARRAY_LEN( compositeFlags ); i++ ) {
		if ( flags & compositeFlags[i] ) {
			return compositeFlags[i];
		}
	}

	return compositeFlags[0];
}


static void vk_create_swapchain( VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format, VkSwapchainKHR *swapchain ) {
	VkImageViewCreateInfo view;
	VkSurfaceCapabilitiesKHR surface_caps;
	VkExtent2D image_extent;
	uint32_t present_mode_count, i;
	VkPresentModeKHR present_mode;
	VkPresentModeKHR *present_modes;
	uint32_t image_count;
	VkSwapchainCreateInfoKHR desc;
	qboolean mailbox_supported = qfalse;
	qboolean immediate_supported = qfalse;
	qboolean fifo_relaxed_supported = qfalse;

	//physical_device = vk.physical_device;
	//device = vk.device;
	//surface_format = vk.surface_format;
	//swapchain = &vk.swapchain;

	VK_CHECK( qvkGetPhysicalDeviceSurfaceCapabilitiesKHR( physical_device, surface, &surface_caps ) );

	image_extent = surface_caps.currentExtent;
	if ( image_extent.width == 0xffffffff && image_extent.height == 0xffffffff ) {
		image_extent.width = MIN(surface_caps.maxImageExtent.width, MAX(surface_caps.minImageExtent.width, 640u));
		image_extent.height = MIN(surface_caps.maxImageExtent.height, MAX(surface_caps.minImageExtent.height, 480u));
	}

	// VK_IMAGE_USAGE_TRANSFER_DST_BIT is required by image clear operations.
	if ((surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
		ri.Error(ERR_FATAL, "create_swapchain: VK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported by the swapchain");

	// VK_IMAGE_USAGE_TRANSFER_SRC_BIT is required in order to take screenshots.
	if ((surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
		ri.Error(ERR_FATAL, "create_swapchain: VK_IMAGE_USAGE_TRANSFER_SRC_BIT is not supported by the swapchain");

	// determine present mode and swapchain image count
	VK_CHECK(qvkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, NULL));
	
	present_modes = (VkPresentModeKHR *) ri.Malloc( present_mode_count * sizeof( VkPresentModeKHR ) );
	VK_CHECK(qvkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes));
	
	ri.Printf( PRINT_ALL, "...presentation modes:" );
	for ( i = 0; i < present_mode_count; i++ ) {
		ri.Printf( PRINT_ALL, " %s", pmode_to_str( present_modes[i] ) );
		if ( present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR )
			mailbox_supported = qtrue;
		else if ( present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR )
			immediate_supported = qtrue;
		else if ( present_modes[i] ==  VK_PRESENT_MODE_FIFO_RELAXED_KHR )
			fifo_relaxed_supported = qtrue;

	}
	ri.Printf( PRINT_ALL, "\n" );

	ri.Free( present_modes );

	if ( ri.Cvar_VariableIntegerValue( "r_swapInterval" ) ) {
		present_mode = VK_PRESENT_MODE_FIFO_KHR;
		image_count = MAX(MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount);
	} else {
		if ( immediate_supported ) {
			present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			image_count = MAX(MIN_SWAPCHAIN_IMAGES_IMM, surface_caps.minImageCount);
		} else if ( mailbox_supported ) {
			present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
			image_count = MAX(MIN_SWAPCHAIN_IMAGES_MAILBOX, surface_caps.minImageCount);
		} else if ( fifo_relaxed_supported ) {
			present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
			image_count = MAX(MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount);
		} else {
			present_mode = VK_PRESENT_MODE_FIFO_KHR;
			image_count = MAX(MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount);
		}
	}

	if ( surface_caps.maxImageCount > 0 ) {
		image_count = MIN( MIN( image_count, surface_caps.maxImageCount ), MAX_SWAPCHAIN_IMAGES );
	}

	ri.Printf( PRINT_ALL, "...selected presentation mode: %s, image count: %i\n", pmode_to_str( present_mode ), image_count );

	// create swap chain
	desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.surface = surface;
	desc.minImageCount = image_count;
	desc.imageFormat = surface_format.format;
	desc.imageColorSpace = surface_format.colorSpace;
	desc.imageExtent = image_extent;
	desc.imageArrayLayers = 1;
	desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;
	desc.preTransform = surface_caps.currentTransform;
	desc.compositeAlpha = get_composite_alpha( surface_caps.supportedCompositeAlpha );
	desc.presentMode = present_mode;
	desc.clipped = VK_TRUE;
	desc.oldSwapchain = VK_NULL_HANDLE;

	VK_CHECK( qvkCreateSwapchainKHR(device, &desc, NULL, swapchain ) );

	VK_CHECK( qvkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchain_image_count, NULL ) );
	vk.swapchain_image_count = MIN( vk.swapchain_image_count, MAX_SWAPCHAIN_IMAGES );
	VK_CHECK( qvkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchain_image_count, vk.swapchain_images ) );

	for ( i = 0; i < vk.swapchain_image_count; i++ ) {

		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.pNext = NULL;
		view.flags = 0;
		view.image = vk.swapchain_images[i];
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = vk.surface_format.format;
		view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.levelCount = 1;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;

		VK_CHECK(qvkCreateImageView( vk.device, &view, NULL, &vk.swapchain_image_views[i] ) );
	}
}


static void create_render_pass( VkDevice device, VkFormat depth_format )
{
	VkAttachmentDescription attachments[3];
	VkAttachmentReference resolveRef0;
	VkAttachmentReference colorRef0;
	VkAttachmentReference depthRef0;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[2];
	VkRenderPassCreateInfo desc;

	if ( r_fbo->integer == 0 )
	{
		// presentation
		attachments[0].flags = 0;
		attachments[0].format = vk.surface_format.format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	// Assuming this will be completely overwritten
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;		// needed for next render pass
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}
	else
	{
		// resolve/color buffer
		attachments[0].flags = 0;
		attachments[0].format = vk.color_format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // Assuming this will be completely overwritten
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;   // needed for next render pass
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	// depth buffer
	attachments[1].flags = 0;
	attachments[1].format = depth_format;
	attachments[1].samples = vkSamples;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Need empty depth buffer before use
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	colorRef0.attachment = 0;
	colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	depthRef0.attachment = 1;
	depthRef0.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef0;
	subpass.pDepthStencilAttachment = &depthRef0;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.pAttachments = attachments;
	desc.pSubpasses = &subpass;

	desc.subpassCount = 1;
	desc.attachmentCount = 2;

	if ( vk.msaaActive )
	{
		attachments[0].format = vk.resolve_format;

		attachments[2].flags = 0;
		attachments[2].format = vk.color_format;
		attachments[2].samples = vkSamples;
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // Intermediate storage (not written)
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		desc.attachmentCount = 3;

		colorRef0.attachment = 2; // msaa image attachment
		colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			
		resolveRef0.attachment = 0; // resolve image attachment
		resolveRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		subpass.pResolveAttachments = &resolveRef0;
	}

	// subpass dependencies

	Com_Memset( &deps, 0, sizeof( deps ) );

	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask =  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; //VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; // What pipeline stage must have completed for the dependency
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;	// What pipeline stage is waiting on the dependency
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;						// What access scopes are influence the dependency
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;			// What access scopes are waiting on the dependency
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT; // Only need the current fragment (or tile) synchronized, not the whole framebuffer

	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;	// Fragment data has been written
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;			// Don't start shading until data is available
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;			// Waiting for color data to be written
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;						// Don't read things from the shader before ready
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;					// Only need the current fragment (or tile) synchronized, not the whole framebuffer

	desc.dependencyCount = 2;
	desc.pDependencies = deps;
	//desc.dependencyCount = 0;
	//desc.pDependencies = NULL;

	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass ) );

	if ( r_fbo->integer == 0 )
		return;

	colorRef0.attachment = 0;
	colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	desc.attachmentCount = 1;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef0;

	// presentation
	attachments[0].flags = 0;
	attachments[0].format = vk.surface_format.format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass_gamma ) );

	// screenmap

	// resolve/color buffer
	attachments[0].flags = 0;
	attachments[0].format = vk.color_format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
#ifdef _DEBUG
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
#else
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // Assuming this will be completely overwritten
#endif
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;   // needed for next render pass
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// depth buffer
	attachments[1].flags = 0;
	attachments[1].format = depth_format;
	attachments[1].samples = vk.screenMapSamples;
	//attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Need empty depth buffer before use
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	colorRef0.attachment = 0;
	colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	depthRef0.attachment = 1;
	depthRef0.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef0;
	subpass.pDepthStencilAttachment = &depthRef0;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.pAttachments = attachments;
	desc.pSubpasses = &subpass;
	desc.subpassCount = 1;
	desc.attachmentCount = 2;
	desc.dependencyCount = 2;
	desc.pDependencies = deps;

	if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT ) {

		attachments[0].format = vk.resolve_format;

		attachments[2].flags = 0;
		attachments[2].format = vk.color_format;
		attachments[2].samples = vk.screenMapSamples;
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		desc.attachmentCount = 3;

		colorRef0.attachment = 2; // msaa image attachment
		colorRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			
		resolveRef0.attachment = 0; // resolve image attachment
		resolveRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		subpass.pResolveAttachments = &resolveRef0;
	}

	VK_CHECK( qvkCreateRenderPass( device, &desc, NULL, &vk.render_pass_screenmap ) );
}


static void record_image_layout_transition(VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags image_aspect_flags, VkAccessFlags src_access_flags, VkImageLayout old_layout, VkAccessFlags dst_access_flags, VkImageLayout new_layout) {
	VkImageMemoryBarrier barrier;

	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.pNext = NULL;
	barrier.srcAccessMask = src_access_flags;
	barrier.dstAccessMask = dst_access_flags;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = image_aspect_flags;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

	qvkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
}


static void allocate_and_bind_image_memory(VkImage image) {
	VkMemoryRequirements memory_requirements;
	VkDeviceSize alignment;
	ImageChunk *chunk;
	int i;

	qvkGetImageMemoryRequirements(vk.device, image, &memory_requirements);

	if (memory_requirements.size > IMAGE_CHUNK_SIZE) {
		ri.Error(ERR_FATAL, "Vulkan: could not allocate memory, image is too large.");
	}

	chunk = NULL;

	// Try to find an existing chunk of sufficient capacity.
	alignment = memory_requirements.alignment;
	for (i = 0; i < vk_world.num_image_chunks; i++) {
		// ensure that memory region has proper alignment
		VkDeviceSize offset = PAD( vk_world.image_chunks[i].used, alignment );

		if ( offset + memory_requirements.size <= IMAGE_CHUNK_SIZE ) {
			chunk = &vk_world.image_chunks[i];
			chunk->used = offset + memory_requirements.size;
			break;
		}
	}

	// Allocate a new chunk in case we couldn't find suitable existing chunk.
	if (chunk == NULL) {
		VkMemoryAllocateInfo alloc_info;
		VkDeviceMemory memory;

		if (vk_world.num_image_chunks >= MAX_IMAGE_CHUNKS) {
			ri.Error(ERR_FATAL, "Vulkan: image chunk limit has been reached" );
		}

		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.pNext = NULL;
		alloc_info.allocationSize = IMAGE_CHUNK_SIZE;
		alloc_info.memoryTypeIndex = find_memory_type(vk.physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &memory));

		chunk = &vk_world.image_chunks[vk_world.num_image_chunks];
		vk_world.num_image_chunks++;
		chunk->memory = memory;
		chunk->used = memory_requirements.size;
	}

	VK_CHECK(qvkBindImageMemory(vk.device, image, chunk->memory, chunk->used - memory_requirements.size));
}


static void ensure_staging_buffer_allocation(VkDeviceSize size) {
	VkBufferCreateInfo buffer_desc;
	VkMemoryRequirements memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	uint32_t memory_type;
	void *data;

	if (vk_world.staging_buffer_size >= size)
		return;

	if (vk_world.staging_buffer != VK_NULL_HANDLE)
		qvkDestroyBuffer(vk.device, vk_world.staging_buffer, NULL);

	if (vk_world.staging_buffer_memory != VK_NULL_HANDLE)
		qvkFreeMemory(vk.device, vk_world.staging_buffer_memory, NULL);

	vk_world.staging_buffer_size = size;

	buffer_desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_desc.pNext = NULL;
	buffer_desc.flags = 0;
	buffer_desc.size = size;
	buffer_desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	buffer_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buffer_desc.queueFamilyIndexCount = 0;
	buffer_desc.pQueueFamilyIndices = NULL;
	VK_CHECK(qvkCreateBuffer(vk.device, &buffer_desc, NULL, &vk_world.staging_buffer));

	qvkGetBufferMemoryRequirements(vk.device, vk_world.staging_buffer, &memory_requirements);

	memory_type = find_memory_type(vk.physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = memory_type;

	VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &vk_world.staging_buffer_memory));
	VK_CHECK(qvkBindBufferMemory(vk.device, vk_world.staging_buffer, vk_world.staging_buffer_memory, 0));

	VK_CHECK(qvkMapMemory(vk.device, vk_world.staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data));
	vk_world.staging_buffer_ptr = (byte*)data;
}


#ifndef NDEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT object_type, uint64_t object, size_t location,
	int32_t message_code, const char* layer_prefix, const char* message, void* user_data) {
#ifdef _WIN32
	MessageBoxA( 0, message, layer_prefix, MB_ICONWARNING );
	OutputDebugString(message);
	OutputDebugString("\n");
	DebugBreak();
#endif
	return VK_FALSE;
}
#endif


static void create_instance( void )
{
#ifndef NDEBUG
	const char* validation_layer_name = "VK_LAYER_LUNARG_standard_validation";
#endif
	VkInstanceCreateInfo desc;
	VkExtensionProperties *extension_properties;
	const char **extension_names, *end;
	char *str;
	uint32_t i, len, count;

	count = 0;
	VK_CHECK(qvkEnumerateInstanceExtensionProperties(NULL, &count, NULL));

	extension_properties = (VkExtensionProperties *)ri.Malloc(sizeof(VkExtensionProperties) * count);
	extension_names = (const char**)ri.Malloc(sizeof(char *) * count);

	VK_CHECK( qvkEnumerateInstanceExtensionProperties( NULL, &count, extension_properties ) );
	for ( i = 0; i < count; i++ ) {
		extension_names[i] = extension_properties[i].extensionName;
	}

	// create instance
	desc.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.pApplicationInfo = NULL;
	desc.enabledLayerCount = 0;
	desc.ppEnabledLayerNames = NULL;
	desc.enabledExtensionCount = count;
	desc.ppEnabledExtensionNames = extension_names;

#ifndef NDEBUG
	desc.enabledLayerCount = 1;
	desc.ppEnabledLayerNames = &validation_layer_name;
#endif

	VK_CHECK( qvkCreateInstance( &desc, NULL, &vk.instance ) );

	// fill glConfig.extensions_string
	str = glConfig.extensions_string; *str = '\0';
	end = &glConfig.extensions_string[ sizeof( glConfig.extensions_string ) - 1];
	for ( i = 0; i < count; i++ ) {
		if ( i != 0 ) {
			if ( str + 1 >= end )
				break;
			str = Q_stradd( str, " " );
		}
		len = (uint32_t)strlen( extension_names[i] );
		if ( str + len >= end )
			break;
		str = Q_stradd( str, extension_names[i] );
	}

	ri.Free( (void*)extension_names );
	ri.Free( extension_properties );
}


static VkFormat get_depth_format( VkPhysicalDevice physical_device ) {
	VkFormatProperties props;
	VkFormat formats[2];
	int i;

	if (r_stencilbits->integer > 0) {
		formats[0] = glConfig.depthBits == 16 ? VK_FORMAT_D16_UNORM_S8_UINT : VK_FORMAT_D24_UNORM_S8_UINT;
		formats[1] = VK_FORMAT_D32_SFLOAT_S8_UINT;
		glConfig.stencilBits = 8;
	} else {
		formats[0] = glConfig.depthBits == 16 ? VK_FORMAT_D16_UNORM : VK_FORMAT_X8_D24_UNORM_PACK32;
		formats[1] = VK_FORMAT_D32_SFLOAT;
		glConfig.stencilBits = 0;
	}
	for ( i = 0; i < ARRAY_LEN( formats ); i++ ) {
		qvkGetPhysicalDeviceFormatProperties( physical_device, formats[i], &props );
		if ( ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) != 0 ) {
			return formats[i];
		}
	}

	ri.Error( ERR_FATAL, "get_depth_format: failed to find depth attachment format" );
	return VK_FORMAT_UNDEFINED; // never get here
}


#define CASE_STR(x) case (x): return #x

const char *vk_get_format_name( VkFormat format )
{
	static char buf[16];

	switch ( format ) {
		// color formats
		CASE_STR( VK_FORMAT_B8G8R8A8_SRGB );
		CASE_STR( VK_FORMAT_B8G8R8A8_UNORM );
		CASE_STR( VK_FORMAT_R8G8B8A8_UNORM );
		CASE_STR( VK_FORMAT_B4G4R4A4_UNORM_PACK16 );
		CASE_STR( VK_FORMAT_R16G16B16A16_UNORM );
		// depth formats
		CASE_STR( VK_FORMAT_D16_UNORM );
		CASE_STR( VK_FORMAT_D16_UNORM_S8_UINT );
		CASE_STR( VK_FORMAT_X8_D24_UNORM_PACK32 );
		CASE_STR( VK_FORMAT_D24_UNORM_S8_UINT );
		CASE_STR( VK_FORMAT_D32_SFLOAT );
		CASE_STR( VK_FORMAT_D32_SFLOAT_S8_UINT );
		default:
			Com_sprintf( buf, sizeof( buf ), "#%i", format );
			return buf;
	}
}


static void get_surface_formats( void )
{
	vk.depth_format = get_depth_format( vk.physical_device );
	
	// primary buffer
	switch ( r_hdr->integer ) {
		case -1: vk.color_format = VK_FORMAT_B4G4R4A4_UNORM_PACK16; break;
		case 1:	vk.color_format = VK_FORMAT_R16G16B16A16_UNORM; break;
		default: vk.color_format = vk.surface_format.format; break;
	}

	vk.resolve_format = vk.color_format;

	if ( vk.fboActive ) {
		if ( vk.msaaActive ) {
			vk.resolve_format = vk.surface_format.format;
		}
	}
}


static void vk_create_device( void ) {

	VkPhysicalDevice *physical_devices;
	uint32_t device_count;
	int device_index;
	VkResult res;

	res = qvkEnumeratePhysicalDevices( vk.instance, &device_count, NULL );
	if ( device_count == 0 ) {
		ri.Error( ERR_FATAL, "Vulkan: no physical device found" );
	} else if ( res < 0 ) {
		ri.Error( ERR_FATAL, "vkEnumeratePhysicalDevices returned error %i", res );
	}

	physical_devices = (VkPhysicalDevice*)ri.Malloc( device_count * sizeof( VkPhysicalDevice ) );
	VK_CHECK(qvkEnumeratePhysicalDevices( vk.instance, &device_count, physical_devices ) );

	// select physical device
	device_index = r_device->integer;
	if ( device_index > device_count - 1 )
		device_index = device_count - 1;

	vk.physical_device = physical_devices[ device_index ];

	ri.Free( physical_devices );

	ri.Printf( PRINT_ALL, "...selected physical device #%i\n", device_index );

	if ( !ri.VK_CreateSurface( vk.instance, &vk.surface ) ) {
		ri.Error( ERR_FATAL, "Error creating Vulkan surface" );
		return;
	}

	// select surface format
	{
		VkSurfaceFormatKHR *candidates;
		uint32_t format_count;

		VK_CHECK( qvkGetPhysicalDeviceSurfaceFormatsKHR( vk.physical_device, vk.surface, &format_count, NULL ) );
		if ( format_count == 0 )
			ri.Error( ERR_FATAL, "%s: no surface formats found", __func__ );

		candidates = (VkSurfaceFormatKHR*) ri.Malloc( format_count * sizeof(VkSurfaceFormatKHR) );

		VK_CHECK( qvkGetPhysicalDeviceSurfaceFormatsKHR( vk.physical_device, vk.surface, &format_count, candidates ) );
		
		if ( format_count == 1 && candidates[0].format == VK_FORMAT_UNDEFINED ) {
			// special case that means we can choose any format
			vk.surface_format.format = VK_FORMAT_R8G8B8A8_UNORM;
			vk.surface_format.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
		} else {
			uint32_t i;
			vk.surface_format = candidates[0];
			for ( i = 1; i < format_count; i++ ) {
				if ( candidates[i].format == VK_FORMAT_B8G8R8A8_UNORM && candidates[i].colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR ) {
					vk.surface_format = candidates[i];
					break;
				}
				//if ( candidates[i].format == VK_FORMAT_B8G8R8A8_SRGB && candidates[i].colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR ) {
				//	vk.surface_format = candidates[i];
				//	break;
				//}
			}
		}

		ri.Free( candidates );
	}

	get_surface_formats();

	// select queue family
	{
		VkQueueFamilyProperties *queue_families;
		uint32_t queue_family_count;
		uint32_t i;

		qvkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &queue_family_count, NULL);
		queue_families = (VkQueueFamilyProperties*)ri.Malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
		qvkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &queue_family_count, queue_families);

		// select queue family with presentation and graphics support
		vk.queue_family_index = ~0U;
		for (i = 0; i < queue_family_count; i++) {
			VkBool32 presentation_supported;
			VK_CHECK(qvkGetPhysicalDeviceSurfaceSupportKHR(vk.physical_device, i, vk.surface, &presentation_supported));

			if (presentation_supported && (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
				vk.queue_family_index = i;
				break;
			}
		}
		
		ri.Free( queue_families );

		if ( vk.queue_family_index == ~0U )
			ri.Error( ERR_FATAL, "%s: failed to find queue family", __func__ );
	}

	// create VkDevice
	{
		const char *device_extensions[3] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
			VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME
		};
		const float priority = 1.0;
		VkExtensionProperties *extension_properties;
		VkDeviceQueueCreateInfo queue_desc;
		VkPhysicalDeviceFeatures device_features;
		VkPhysicalDeviceFeatures features;
		VkDeviceCreateInfo device_desc;
		qboolean swapchainSupported = qfalse;
		qboolean dedicatedAllocation = qfalse;
		qboolean memoryRequirements2 = qfalse;
		uint32_t i, count = 0;

		VK_CHECK(qvkEnumerateDeviceExtensionProperties(vk.physical_device, NULL, &count, NULL));
		extension_properties = (VkExtensionProperties*)ri.Malloc(count * sizeof(VkExtensionProperties));
		VK_CHECK(qvkEnumerateDeviceExtensionProperties(vk.physical_device, NULL, &count, extension_properties));

		for (i = 0; i < count; i++) {
			if ( strcmp( extension_properties[i].extensionName, device_extensions[0] ) == 0 ) {
				swapchainSupported = qtrue;
			} else if ( strcmp( extension_properties[i].extensionName, device_extensions[1] ) == 0 ) {
				dedicatedAllocation = qtrue;
			} else if ( strcmp( extension_properties[i].extensionName, device_extensions[2] ) == 0 ) {
				memoryRequirements2 = qtrue;
			}
		}

		ri.Free( extension_properties );

		if ( !swapchainSupported )
			ri.Error( ERR_FATAL, "%s: required device extension is not available: %s", __func__, device_extensions[0] );

		if ( !memoryRequirements2 )
			dedicatedAllocation = qfalse;
		else
			vk.dedicatedAllocation = dedicatedAllocation;

		qvkGetPhysicalDeviceFeatures( vk.physical_device, &device_features );
		if ( device_features.shaderClipDistance == VK_FALSE )
			ri.Error( ERR_FATAL, "%s: shaderClipDistance feature is not supported", __func__ );
		if ( device_features.fillModeNonSolid == VK_FALSE )
			ri.Error( ERR_FATAL, "%s: fillModeNonSolid feature is not supported", __func__ );

		queue_desc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_desc.pNext = NULL;
		queue_desc.flags = 0;
		queue_desc.queueFamilyIndex = vk.queue_family_index;
		queue_desc.queueCount = 1;
		queue_desc.pQueuePriorities = &priority;

		Com_Memset( &features, 0, sizeof( features ) );
		features.shaderClipDistance = VK_TRUE;
		features.fillModeNonSolid = VK_TRUE;

		if ( device_features.wideLines ) { // needed for RB_SurfaceAxis
			features.wideLines = VK_TRUE;
			vk.wideLines = qtrue;
		}

		if ( device_features.fragmentStoresAndAtomics ) {
			features.fragmentStoresAndAtomics = VK_TRUE;
			vk.fragmentStores = qtrue;
		}

		if ( r_ext_texture_filter_anisotropic->integer && device_features.samplerAnisotropy ) {
			features.samplerAnisotropy = VK_TRUE;
			vk.samplerAnisotropy = qtrue;
		}

		device_desc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_desc.pNext = NULL;
		device_desc.flags = 0;
		device_desc.queueCreateInfoCount = 1;
		device_desc.pQueueCreateInfos = &queue_desc;
		device_desc.enabledLayerCount = 0;
		device_desc.ppEnabledLayerNames = NULL;
		device_desc.enabledExtensionCount = vk.dedicatedAllocation ? 3 : 1;
		device_desc.ppEnabledExtensionNames = device_extensions;
		device_desc.pEnabledFeatures = &features;

		VK_CHECK( qvkCreateDevice( vk.physical_device, &device_desc, NULL, &vk.device ) );
	}
}


#define INIT_INSTANCE_FUNCTION(func) \
	q##func = /*(PFN_ ## func)*/ ri.VK_GetInstanceProcAddr(vk.instance, #func); \
	if (q##func == NULL) {											\
		ri.Error(ERR_FATAL, "Failed to find entrypoint %s", #func);	\
	}

#define INIT_INSTANCE_FUNCTION_EXT(func) \
	q##func = /*(PFN_ ## func)*/ ri.VK_GetInstanceProcAddr(vk.instance, #func);


#define INIT_DEVICE_FUNCTION(func) \
	q##func = (PFN_ ## func) qvkGetDeviceProcAddr(vk.device, #func);\
	if (q##func == NULL) {											\
		ri.Error(ERR_FATAL, "Failed to find entrypoint %s", #func);	\
	}

#define INIT_DEVICE_FUNCTION_EXT(func) \
	q##func = (PFN_ ## func) qvkGetDeviceProcAddr(vk.device, #func);


static void init_vulkan_library( void )
{
	//
	// Get functions that do not depend on VkInstance (vk.instance == nullptr at this point).
	//
	INIT_INSTANCE_FUNCTION(vkCreateInstance)
	INIT_INSTANCE_FUNCTION(vkEnumerateInstanceExtensionProperties)

	//
	// Get instance level functions.
	//
	create_instance();

	INIT_INSTANCE_FUNCTION(vkCreateDevice)
	INIT_INSTANCE_FUNCTION(vkDestroyInstance)
	INIT_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties)
	INIT_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices)
	INIT_INSTANCE_FUNCTION(vkGetDeviceProcAddr)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceFormatProperties)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceMemoryProperties)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties)
	INIT_INSTANCE_FUNCTION(vkDestroySurfaceKHR)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR)

#ifndef NDEBUG
	INIT_INSTANCE_FUNCTION_EXT(vkCreateDebugReportCallbackEXT)
	INIT_INSTANCE_FUNCTION_EXT(vkDestroyDebugReportCallbackEXT)

	//
	// Create debug callback.
	//
	if ( qvkCreateDebugReportCallbackEXT )
	{
		VkDebugReportCallbackCreateInfoEXT desc;
		desc.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		desc.pNext = NULL;
		desc.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
					 VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
					 VK_DEBUG_REPORT_ERROR_BIT_EXT;
		desc.pfnCallback = &debug_callback;
		desc.pUserData = NULL;

		VK_CHECK(qvkCreateDebugReportCallbackEXT(vk.instance, &desc, NULL, &vk.debug_callback));
	}
#endif

	//
	// Get device level functions.
	//
	vk_create_device();

	INIT_DEVICE_FUNCTION(vkAllocateCommandBuffers)
	INIT_DEVICE_FUNCTION(vkAllocateDescriptorSets)
	INIT_DEVICE_FUNCTION(vkAllocateMemory)
	INIT_DEVICE_FUNCTION(vkBeginCommandBuffer)
	INIT_DEVICE_FUNCTION(vkBindBufferMemory)
	INIT_DEVICE_FUNCTION(vkBindImageMemory)
	INIT_DEVICE_FUNCTION(vkCmdBeginRenderPass)
	INIT_DEVICE_FUNCTION(vkCmdBindDescriptorSets)
	INIT_DEVICE_FUNCTION(vkCmdBindIndexBuffer)
	INIT_DEVICE_FUNCTION(vkCmdBindPipeline)
	INIT_DEVICE_FUNCTION(vkCmdBindVertexBuffers)
	INIT_DEVICE_FUNCTION(vkCmdBlitImage)
	INIT_DEVICE_FUNCTION(vkCmdClearAttachments)
	INIT_DEVICE_FUNCTION(vkCmdCopyBuffer)
	INIT_DEVICE_FUNCTION(vkCmdCopyBufferToImage)
	INIT_DEVICE_FUNCTION(vkCmdCopyImage)
	INIT_DEVICE_FUNCTION(vkCmdDraw)
	INIT_DEVICE_FUNCTION(vkCmdDrawIndexed)
	INIT_DEVICE_FUNCTION(vkCmdEndRenderPass)
	INIT_DEVICE_FUNCTION(vkCmdNextSubpass)
	INIT_DEVICE_FUNCTION(vkCmdPipelineBarrier)
	INIT_DEVICE_FUNCTION(vkCmdPushConstants)
	INIT_DEVICE_FUNCTION(vkCmdSetDepthBias)
	INIT_DEVICE_FUNCTION(vkCmdSetScissor)
	INIT_DEVICE_FUNCTION(vkCmdSetViewport)
	INIT_DEVICE_FUNCTION(vkCreateBuffer)
	INIT_DEVICE_FUNCTION(vkCreateCommandPool)
	INIT_DEVICE_FUNCTION(vkCreateDescriptorPool)
	INIT_DEVICE_FUNCTION(vkCreateDescriptorSetLayout)
	INIT_DEVICE_FUNCTION(vkCreateFence)
	INIT_DEVICE_FUNCTION(vkCreateFramebuffer)
	INIT_DEVICE_FUNCTION(vkCreateGraphicsPipelines)
	INIT_DEVICE_FUNCTION(vkCreateImage)
	INIT_DEVICE_FUNCTION(vkCreateImageView)
	INIT_DEVICE_FUNCTION(vkCreatePipelineCache)
	INIT_DEVICE_FUNCTION(vkCreatePipelineLayout)
	INIT_DEVICE_FUNCTION(vkCreateRenderPass)
	INIT_DEVICE_FUNCTION(vkCreateSampler)
	INIT_DEVICE_FUNCTION(vkCreateSemaphore)
	INIT_DEVICE_FUNCTION(vkCreateShaderModule)
	INIT_DEVICE_FUNCTION(vkDestroyBuffer)
	INIT_DEVICE_FUNCTION(vkDestroyCommandPool)
	INIT_DEVICE_FUNCTION(vkDestroyDescriptorPool)
	INIT_DEVICE_FUNCTION(vkDestroyDescriptorSetLayout)
	INIT_DEVICE_FUNCTION(vkDestroyDevice)
	INIT_DEVICE_FUNCTION(vkDestroyFence)
	INIT_DEVICE_FUNCTION(vkDestroyFramebuffer)
	INIT_DEVICE_FUNCTION(vkDestroyImage)
	INIT_DEVICE_FUNCTION(vkDestroyImageView)
	INIT_DEVICE_FUNCTION(vkDestroyPipeline)
	INIT_DEVICE_FUNCTION(vkDestroyPipelineCache)
	INIT_DEVICE_FUNCTION(vkDestroyPipelineLayout)
	INIT_DEVICE_FUNCTION(vkDestroyRenderPass)
	INIT_DEVICE_FUNCTION(vkDestroySampler)
	INIT_DEVICE_FUNCTION(vkDestroySemaphore)
	INIT_DEVICE_FUNCTION(vkDestroyShaderModule)
	INIT_DEVICE_FUNCTION(vkDeviceWaitIdle)
	INIT_DEVICE_FUNCTION(vkEndCommandBuffer)
	INIT_DEVICE_FUNCTION(vkFlushMappedMemoryRanges)
	INIT_DEVICE_FUNCTION(vkFreeCommandBuffers)
	INIT_DEVICE_FUNCTION(vkFreeDescriptorSets)
	INIT_DEVICE_FUNCTION(vkFreeMemory)
	INIT_DEVICE_FUNCTION(vkGetBufferMemoryRequirements)
	INIT_DEVICE_FUNCTION(vkGetDeviceQueue)
	INIT_DEVICE_FUNCTION(vkGetImageMemoryRequirements)
	INIT_DEVICE_FUNCTION(vkGetImageSubresourceLayout)
	INIT_DEVICE_FUNCTION(vkMapMemory)
	INIT_DEVICE_FUNCTION(vkQueueSubmit)
	INIT_DEVICE_FUNCTION(vkQueueWaitIdle)
	INIT_DEVICE_FUNCTION(vkResetCommandBuffer)
	INIT_DEVICE_FUNCTION(vkResetDescriptorPool)
	INIT_DEVICE_FUNCTION(vkResetFences)
	INIT_DEVICE_FUNCTION(vkUnmapMemory)
	INIT_DEVICE_FUNCTION(vkUpdateDescriptorSets)
	INIT_DEVICE_FUNCTION(vkWaitForFences)
	INIT_DEVICE_FUNCTION(vkAcquireNextImageKHR)
	INIT_DEVICE_FUNCTION(vkCreateSwapchainKHR)
	INIT_DEVICE_FUNCTION(vkDestroySwapchainKHR)
	INIT_DEVICE_FUNCTION(vkGetSwapchainImagesKHR)
	INIT_DEVICE_FUNCTION(vkQueuePresentKHR)

	if ( vk.dedicatedAllocation ) {
		INIT_DEVICE_FUNCTION_EXT(vkGetBufferMemoryRequirements2KHR);
		INIT_DEVICE_FUNCTION_EXT(vkGetImageMemoryRequirements2KHR);
		if ( !qvkGetBufferMemoryRequirements2KHR || !qvkGetImageMemoryRequirements2KHR ) {
			vk.dedicatedAllocation = qfalse;
		}
	}
}

#undef INIT_INSTANCE_FUNCTION
#undef INIT_DEVICE_FUNCTION
#undef INIT_DEVICE_FUNCTION_EXT

static void deinit_vulkan_library( void )
{
	qvkCreateInstance							= NULL;
	qvkEnumerateInstanceExtensionProperties		= NULL;

	qvkCreateDevice								= NULL;
	qvkDestroyInstance							= NULL;
	qvkEnumerateDeviceExtensionProperties		= NULL;
	qvkEnumeratePhysicalDevices					= NULL;
	qvkGetDeviceProcAddr						= NULL;
	qvkGetPhysicalDeviceFeatures				= NULL;
	qvkGetPhysicalDeviceFormatProperties		= NULL;
	qvkGetPhysicalDeviceMemoryProperties		= NULL;
	qvkGetPhysicalDeviceProperties				= NULL;
	qvkGetPhysicalDeviceQueueFamilyProperties	= NULL;
	qvkDestroySurfaceKHR						= NULL;
	qvkGetPhysicalDeviceSurfaceCapabilitiesKHR	= NULL;
	qvkGetPhysicalDeviceSurfaceFormatsKHR		= NULL;
	qvkGetPhysicalDeviceSurfacePresentModesKHR	= NULL;
	qvkGetPhysicalDeviceSurfaceSupportKHR		= NULL;
#ifndef NDEBUG
	qvkCreateDebugReportCallbackEXT				= NULL;
	qvkDestroyDebugReportCallbackEXT			= NULL;
#endif
	qvkAllocateCommandBuffers					= NULL;
	qvkAllocateDescriptorSets					= NULL;
	qvkAllocateMemory							= NULL;
	qvkBeginCommandBuffer						= NULL;
	qvkBindBufferMemory							= NULL;
	qvkBindImageMemory							= NULL;
	qvkCmdBeginRenderPass						= NULL;
	qvkCmdBindDescriptorSets					= NULL;
	qvkCmdBindIndexBuffer						= NULL;
	qvkCmdBindPipeline							= NULL;
	qvkCmdBindVertexBuffers						= NULL;
	qvkCmdBlitImage								= NULL;
	qvkCmdClearAttachments						= NULL;
	qvkCmdCopyBuffer							= NULL;
	qvkCmdCopyBufferToImage						= NULL;
	qvkCmdCopyImage								= NULL;
	qvkCmdDraw									= NULL;
	qvkCmdDrawIndexed							= NULL;
	qvkCmdEndRenderPass							= NULL;
	qvkCmdNextSubpass							= NULL;
	qvkCmdPipelineBarrier						= NULL;
	qvkCmdPushConstants							= NULL;
	qvkCmdSetDepthBias							= NULL;
	qvkCmdSetScissor							= NULL;
	qvkCmdSetViewport							= NULL;
	qvkCreateBuffer								= NULL;
	qvkCreateCommandPool						= NULL;
	qvkCreateDescriptorPool						= NULL;
	qvkCreateDescriptorSetLayout				= NULL;
	qvkCreateFence								= NULL;
	qvkCreateFramebuffer						= NULL;
	qvkCreateGraphicsPipelines					= NULL;
	qvkCreateImage								= NULL;
	qvkCreateImageView							= NULL;
	qvkCreatePipelineCache						= NULL;
	qvkCreatePipelineLayout						= NULL;
	qvkCreateRenderPass							= NULL;
	qvkCreateSampler							= NULL;
	qvkCreateSemaphore							= NULL;
	qvkCreateShaderModule						= NULL;
	qvkDestroyBuffer							= NULL;
	qvkDestroyCommandPool						= NULL;
	qvkDestroyDescriptorPool					= NULL;
	qvkDestroyDescriptorSetLayout				= NULL;
	qvkDestroyDevice							= NULL;
	qvkDestroyFence								= NULL;
	qvkDestroyFramebuffer						= NULL;
	qvkDestroyImage								= NULL;
	qvkDestroyImageView							= NULL;
	qvkDestroyPipeline							= NULL;
	qvkDestroyPipelineCache						= NULL;
	qvkDestroyPipelineLayout					= NULL;
	qvkDestroyRenderPass						= NULL;
	qvkDestroySampler							= NULL;
	qvkDestroySemaphore							= NULL;
	qvkDestroyShaderModule						= NULL;
	qvkDeviceWaitIdle							= NULL;
	qvkEndCommandBuffer							= NULL;
	qvkFlushMappedMemoryRanges					= NULL;
	qvkFreeCommandBuffers						= NULL;
	qvkFreeDescriptorSets						= NULL;
	qvkFreeMemory								= NULL;
	qvkGetBufferMemoryRequirements				= NULL;
	qvkGetDeviceQueue							= NULL;
	qvkGetImageMemoryRequirements				= NULL;
	qvkGetImageSubresourceLayout				= NULL;
	qvkMapMemory								= NULL;
	qvkQueueSubmit								= NULL;
	qvkQueueWaitIdle							= NULL;
	qvkResetCommandBuffer						= NULL;
	qvkResetDescriptorPool						= NULL;
	qvkResetFences								= NULL;
	qvkUnmapMemory								= NULL;
	qvkUpdateDescriptorSets						= NULL;
	qvkWaitForFences							= NULL;
	qvkAcquireNextImageKHR						= NULL;
	qvkCreateSwapchainKHR						= NULL;
	qvkDestroySwapchainKHR						= NULL;
	qvkGetSwapchainImagesKHR					= NULL;
	qvkQueuePresentKHR							= NULL;

	qvkGetBufferMemoryRequirements2KHR			= NULL;
	qvkGetImageMemoryRequirements2KHR			= NULL;
}


static VkShaderModule create_shader_module(const uint8_t *bytes, const int count) {
	VkShaderModuleCreateInfo desc;
	VkShaderModule module;

	if ( count % 4 != 0 ) {
		ri.Error( ERR_FATAL, "Vulkan: SPIR-V binary buffer size is not a multiple of 4" );
	}

	desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.codeSize = count;
	desc.pCode = (const uint32_t*)bytes;

	VK_CHECK(qvkCreateShaderModule(vk.device, &desc, NULL, &module));

	return module;
}


static void vk_create_layout_binding( int binding, VkDescriptorType type, VkShaderStageFlags flags, VkDescriptorSetLayout *layout )
{
	VkDescriptorSetLayoutBinding bind;
	VkDescriptorSetLayoutCreateInfo desc;

	bind.binding = binding;
	bind.descriptorType = type;
	bind.descriptorCount = 1;
	bind.stageFlags = flags;
	bind.pImmutableSamplers = NULL;

	desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.bindingCount = 1;
	desc.pBindings = &bind;

	VK_CHECK( qvkCreateDescriptorSetLayout(vk.device, &desc, NULL, layout ) );
}


void vk_update_uniform_descriptor( VkDescriptorSet descriptor, VkBuffer buffer )
{
	VkDescriptorBufferInfo info;
	VkWriteDescriptorSet desc;

	info.buffer = buffer;
	info.offset = 0;
	info.range = sizeof( vkUniform_t );

	desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc.dstSet = descriptor;
	desc.dstBinding = 0;
	desc.dstArrayElement = 0;
	desc.descriptorCount = 1;
	desc.pNext = NULL;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	desc.pImageInfo = NULL;
	desc.pBufferInfo = &info;
	desc.pTexelBufferView = NULL;

	qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
}


void vk_init_buffers( void )
{
	VkDescriptorSetAllocateInfo alloc;
	VkDescriptorBufferInfo info;
	VkWriteDescriptorSet desc;
	int i;

	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.pNext = NULL;
	alloc.descriptorPool = vk.descriptor_pool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &vk.set_layout_storage;

	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.storage.descriptor ) );

	info.buffer = vk.storage.buffer;
	info.offset = 0;
	info.range = sizeof( uint32_t );

	desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc.dstSet = vk.storage.descriptor;
	desc.dstBinding = 0;
	desc.dstArrayElement = 0;
	desc.descriptorCount = 1;
	desc.pNext = NULL;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	desc.pImageInfo = NULL;
	desc.pBufferInfo = &info;
	desc.pTexelBufferView = NULL;

	qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

	// allocated and update descriptor set
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_uniform;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.tess[i].uniform_descriptor ) );

		vk_update_uniform_descriptor( vk.tess[ i ].uniform_descriptor, vk.tess[ i ].vertex_buffer );

		// allocate color attachment descriptor if post-processing enabled
#ifndef USE_SINGLE_FBO
		if ( vk.tess[i].color_image_view )
		{
			alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			alloc.pNext = NULL;
			alloc.descriptorPool = vk.descriptor_pool;
			alloc.descriptorSetCount = 1;
			alloc.pSetLayouts = &vk.set_layout_sampler;
			VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.tess[i].color_descriptor ) );

			VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.tess[i].color_descriptor3 ) ); // screenmap
	
			// update descriptor set
			{
				VkDescriptorImageInfo info;
				VkWriteDescriptorSet desc;
				Vk_Sampler_Def sd;

				Com_Memset( &sd, 0, sizeof( sd ) );
				sd.gl_mag_filter = sd.gl_min_filter = vk.blitFilter;
				sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				sd.max_lod_1_0 = qtrue;
				sd.noAnisotropy = qtrue;

				info.sampler = vk_find_sampler( &sd );
				info.imageView = vk.tess[i].color_image_view;
				info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				desc.dstSet = vk.tess[i].color_descriptor;
				desc.dstBinding = 0;
				desc.dstArrayElement = 0;
				desc.descriptorCount = 1;
				desc.pNext = NULL;
				desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				desc.pImageInfo = &info;
				desc.pBufferInfo = NULL;
				desc.pTexelBufferView = NULL;

				qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

				// screenmap
				sd.gl_mag_filter = sd.gl_min_filter = GL_LINEAR;
				sd.max_lod_1_0 = qfalse;
				sd.noAnisotropy = qfalse;

				info.sampler = vk_find_sampler( &sd );
				info.imageView = vk.tess[i].color_image_view3;

				desc.dstSet = vk.tess[i].color_descriptor3;

				qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
			}
		}
#endif
	}

#ifdef USE_SINGLE_FBO
	if ( vk.color_image_view )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_sampler;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.color_descriptor ) );

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &alloc, &vk.color_descriptor3 ) ); // screenmap
	
		// update descriptor set
		{
			VkDescriptorImageInfo info;
			VkWriteDescriptorSet desc;
			Vk_Sampler_Def sd;

			Com_Memset( &sd, 0, sizeof( sd ) );
			sd.gl_mag_filter = sd.gl_min_filter = vk.blitFilter;
			sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sd.max_lod_1_0 = qtrue;
			sd.noAnisotropy = qtrue;

			info.sampler = vk_find_sampler( &sd );
			info.imageView = vk.color_image_view;
			info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc.dstSet = vk.color_descriptor;
			desc.dstBinding = 0;
			desc.dstArrayElement = 0;
			desc.descriptorCount = 1;
			desc.pNext = NULL;
			desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc.pImageInfo = &info;
			desc.pBufferInfo = NULL;
			desc.pTexelBufferView = NULL;

			qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

			// screenmap
			sd.gl_mag_filter = sd.gl_min_filter = GL_LINEAR;
			sd.max_lod_1_0 = qfalse;
			sd.noAnisotropy = qfalse;

			info.sampler = vk_find_sampler( &sd );
			info.imageView = vk.color_image_view3;

			desc.dstSet = vk.color_descriptor3;

			qvkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
		}
	}
#endif
}


void vk_bind_fog_image( void )
{
	vk_update_descriptor( 3, tr.fogImage->descriptor );
}

static VkCommandBuffer begin_command_buffer( void )
{
	VkCommandBufferBeginInfo begin_info;
	VkCommandBufferAllocateInfo alloc_info;
	VkCommandBuffer command_buffer;

	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.commandPool = vk.command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc_info, &command_buffer ) );

	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;

	VK_CHECK( qvkBeginCommandBuffer( command_buffer, &begin_info ) );

	return command_buffer;
}


void end_command_buffer( VkCommandBuffer command_buffer )
{
	VkSubmitInfo submit_info;
	VkCommandBuffer cmdbuf[1];

	cmdbuf[0] = command_buffer;

	VK_CHECK( qvkEndCommandBuffer( command_buffer ) );

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.waitSemaphoreCount = 0;
	submit_info.pWaitSemaphores = NULL;
	submit_info.pWaitDstStageMask = NULL;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = cmdbuf;
	submit_info.signalSemaphoreCount = 0;
	submit_info.pSignalSemaphores = NULL;

	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit_info, VK_NULL_HANDLE ) );
	VK_CHECK( qvkQueueWaitIdle( vk.queue ) );

	qvkFreeCommandBuffers( vk.device, vk.command_pool, 1, cmdbuf );
}


static void vk_release_geometry_buffers( void )
{
	int i;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkDestroyBuffer( vk.device, vk.tess[i].vertex_buffer, NULL );
		vk.tess[i].vertex_buffer = VK_NULL_HANDLE;
	}

	qvkFreeMemory( vk.device, vk.geometry_buffer_memory, NULL );
	vk.geometry_buffer_memory = VK_NULL_HANDLE;
}


static void vk_create_geometry_buffers( VkDeviceSize size )
{
	VkMemoryRequirements vb_memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	VkBufferCreateInfo desc;
	VkDeviceSize vertex_buffer_offset;
	uint32_t memory_type_bits;
	uint32_t memory_type;
	void *data;
	int i;

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;

	Com_Memset( &vb_memory_requirements, 0, sizeof( vb_memory_requirements ) );

	for ( i = 0 ; i < NUM_COMMAND_BUFFERS; i++ ) {
		desc.size = size;
		desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &vk.tess[i].vertex_buffer ) );

		qvkGetBufferMemoryRequirements( vk.device, vk.tess[i].vertex_buffer, &vb_memory_requirements );
	}

	memory_type_bits = vb_memory_requirements.memoryTypeBits;
	memory_type = find_memory_type( vk.physical_device, memory_type_bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = vb_memory_requirements.size * NUM_COMMAND_BUFFERS;
	alloc_info.memoryTypeIndex = memory_type;

	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &vk.geometry_buffer_memory ) );
	VK_CHECK( qvkMapMemory( vk.device, vk.geometry_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data ) );

	vertex_buffer_offset = 0;

	for ( i = 0 ; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkBindBufferMemory( vk.device, vk.tess[i].vertex_buffer, vk.geometry_buffer_memory, vertex_buffer_offset );
		vk.tess[i].vertex_buffer_ptr = (byte*)data + vertex_buffer_offset;
		vk.tess[i].vertex_buffer_offset = 0;
		vertex_buffer_offset += vb_memory_requirements.size;
	}

	vk.geometry_buffer_size = vb_memory_requirements.size;

	Com_Memset( &vk.stats, 0, sizeof( vk.stats ) );
}


static void vk_create_storage_buffer( uint32_t size )
{
	VkMemoryRequirements memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	VkBufferCreateInfo desc;
	uint32_t memory_type_bits;
	uint32_t memory_type;

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;

	Com_Memset( &memory_requirements, 0, sizeof( memory_requirements ) );

	desc.size = size;
	desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &vk.storage.buffer ) );

	qvkGetBufferMemoryRequirements( vk.device, vk.storage.buffer, &memory_requirements );

	memory_type_bits = memory_requirements.memoryTypeBits;
	memory_type = find_memory_type( vk.physical_device, memory_type_bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = memory_type;

	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &vk.storage.memory ) );
	VK_CHECK( qvkMapMemory( vk.device, vk.storage.memory, 0, VK_WHOLE_SIZE, 0, (void**)&vk.storage.buffer_ptr ) );

	Com_Memset( vk.storage.buffer_ptr, 0, memory_requirements.size ); 

	qvkBindBufferMemory( vk.device, vk.storage.buffer, vk.storage.memory, 0 );
}


#ifdef USE_VBO
void vk_release_vbo( void )
{
	if ( vk.vbo.vertex_buffer )
		qvkDestroyBuffer( vk.device, vk.vbo.vertex_buffer, NULL );
	vk.vbo.vertex_buffer = VK_NULL_HANDLE;

	if ( vk.vbo.buffer_memory )
		qvkFreeMemory( vk.device, vk.vbo.buffer_memory, NULL );
	vk.vbo.buffer_memory = VK_NULL_HANDLE;
}


qboolean vk_alloc_vbo( const byte *vbo_data, int vbo_size )
{
	VkMemoryRequirements vb_mem_reqs;
	VkMemoryAllocateInfo alloc_info;
	VkBufferCreateInfo desc;
	VkDeviceSize vertex_buffer_offset;
	VkDeviceSize allocationSize;
	uint32_t memory_type_bits;
	VkBuffer staging_vertex_buffer;
	VkDeviceMemory staging_buffer_memory;
	VkCommandBuffer command_buffer;
	VkBufferCopy copyRegion[1];
	void *data;

	vk_release_vbo();

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;

	// device-local buffer
	desc.size = vbo_size;
	desc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &vk.vbo.vertex_buffer ) );

	// staging buffer
	desc.size = vbo_size;
	desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &staging_vertex_buffer ) );

	// memory requirements
	qvkGetBufferMemoryRequirements( vk.device, vk.vbo.vertex_buffer, &vb_mem_reqs );
	vertex_buffer_offset = 0;
	allocationSize = vertex_buffer_offset + vb_mem_reqs.size;
	memory_type_bits = vb_mem_reqs.memoryTypeBits;

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = allocationSize;
	alloc_info.memoryTypeIndex = find_memory_type( vk.physical_device, memory_type_bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &vk.vbo.buffer_memory ) );
	qvkBindBufferMemory( vk.device, vk.vbo.vertex_buffer, vk.vbo.buffer_memory, vertex_buffer_offset );

	// staging buffers
	
	// memory requirements
	qvkGetBufferMemoryRequirements( vk.device, staging_vertex_buffer, &vb_mem_reqs );
	vertex_buffer_offset = 0;
	allocationSize = vertex_buffer_offset + vb_mem_reqs.size;
	memory_type_bits = vb_mem_reqs.memoryTypeBits;

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = allocationSize;
	alloc_info.memoryTypeIndex = find_memory_type( vk.physical_device, memory_type_bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &staging_buffer_memory ) );
	qvkBindBufferMemory( vk.device, staging_vertex_buffer, staging_buffer_memory, vertex_buffer_offset );

	VK_CHECK( qvkMapMemory( vk.device, staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data ) );
	memcpy( (byte*)data + vertex_buffer_offset, vbo_data, vbo_size );
	qvkUnmapMemory( vk.device, staging_buffer_memory );

	command_buffer = begin_command_buffer();
	copyRegion[0].srcOffset = 0;
	copyRegion[0].dstOffset = 0;
	copyRegion[0].size = vbo_size;
	qvkCmdCopyBuffer( command_buffer, staging_vertex_buffer, vk.vbo.vertex_buffer, 1, &copyRegion[0] );

	end_command_buffer( command_buffer );

	qvkDestroyBuffer( vk.device, staging_vertex_buffer, NULL );
	qvkFreeMemory( vk.device, staging_buffer_memory, NULL );

	return qtrue;
}
#endif


static void vk_create_shader_modules( void )
{
	extern const unsigned char st_clip_vert_spv[];
	extern const int st_clip_vert_spv_size;
	extern const unsigned char st_clip_fog_vert_spv[];
	extern const int st_clip_fog_vert_spv_size;

	extern const unsigned char st_enviro_vert_spv[];
	extern const int st_enviro_vert_spv_size;
	extern const unsigned char st_enviro_fog_vert_spv[];
	extern const int st_enviro_fog_vert_spv_size;

	extern const unsigned char st_frag_spv[];
	extern const int st_frag_spv_size;
	extern const unsigned char st_df_frag_spv[];
	extern const int st_df_frag_spv_size;
	extern const unsigned char st_fog_frag_spv[];
	extern const int st_fog_frag_spv_size;

	extern const unsigned char color_frag_spv[];
	extern const int color_frag_spv_size;
	extern const unsigned char color_clip_vert_spv[];
	extern const int color_clip_vert_spv_size;

	extern const unsigned char mt_clip_vert_spv[];
	extern const int mt_clip_vert_spv_size;
	extern const unsigned char mt_clip_fog_vert_spv[];
	extern const int mt_clip_fog_vert_spv_size;

	extern const unsigned char mt_mul_frag_spv[];
	extern const int mt_mul_frag_spv_size;
	extern const unsigned char mt_mul_fog_frag_spv[];
	extern const int mt_mul_fog_frag_spv_size;

	extern const unsigned char mt_add_frag_spv[];
	extern const int mt_add_frag_spv_size;
	extern const unsigned char mt_add_fog_frag_spv[];
	extern const int mt_add_fog_frag_spv_size;

	extern const unsigned char fog_vert_spv[];
	extern const int fog_vert_spv_size;
	extern const unsigned char fog_frag_spv[];
	extern const int fog_frag_spv_size;

	extern const unsigned char dot_vert_spv[];
	extern const int dot_vert_spv_size;
	extern const unsigned char dot_frag_spv[];
	extern const int dot_frag_spv_size;

	extern const unsigned char light_clip_vert_spv[];
	extern const int light_clip_vert_spv_size;
	extern const unsigned char light_clip_fog_vert_spv[];
	extern const int light_clip_fog_vert_spv_size;

	extern const unsigned char light_frag_spv[];
	extern const int light_frag_spv_size;
	extern const unsigned char light_fog_frag_spv[];
	extern const int light_fog_frag_spv_size;

	extern const unsigned char light1_frag_spv[];
	extern const int light1_frag_spv_size;
	extern const unsigned char light1_fog_frag_spv[];
	extern const int light1_fog_frag_spv_size;

	extern const unsigned char gamma_frag_spv[];
	extern const int gamma_frag_spv_size;
	extern const unsigned char gamma_vert_spv[];
	extern const int gamma_vert_spv_size;

	vk.modules.st_clip_vs[0] = create_shader_module(st_clip_vert_spv, st_clip_vert_spv_size);
	vk.modules.st_clip_vs[1] = create_shader_module(st_clip_fog_vert_spv, st_clip_fog_vert_spv_size);

	vk.modules.st_enviro_vs[0] = create_shader_module(st_enviro_vert_spv, st_enviro_vert_spv_size);
	vk.modules.st_enviro_vs[1] = create_shader_module(st_enviro_fog_vert_spv, st_enviro_fog_vert_spv_size);

	vk.modules.mt_clip_vs[0] = create_shader_module(mt_clip_vert_spv, mt_clip_vert_spv_size);
	vk.modules.mt_clip_vs[1] = create_shader_module(mt_clip_fog_vert_spv, mt_clip_fog_vert_spv_size);

	vk.modules.st_fs[0] = create_shader_module(st_frag_spv, st_frag_spv_size);
	vk.modules.st_fs[1] = create_shader_module(st_fog_frag_spv, st_fog_frag_spv_size);
	vk.modules.st_df_fs = create_shader_module(st_df_frag_spv, st_df_frag_spv_size);

	vk.modules.color_fs = create_shader_module(color_frag_spv, color_frag_spv_size);
	vk.modules.color_clip_vs = create_shader_module(color_clip_vert_spv, color_clip_vert_spv_size);

	vk.modules.mt_mul_fs[0] = create_shader_module(mt_mul_frag_spv, mt_mul_frag_spv_size);
	vk.modules.mt_mul_fs[1] = create_shader_module(mt_mul_fog_frag_spv, mt_mul_fog_frag_spv_size);

	vk.modules.mt_add_fs[0] = create_shader_module(mt_add_frag_spv, mt_add_frag_spv_size);
	vk.modules.mt_add_fs[1] = create_shader_module(mt_add_fog_frag_spv, mt_add_fog_frag_spv_size);

	vk.modules.fog_vs = create_shader_module(fog_vert_spv, fog_vert_spv_size);
	vk.modules.fog_fs = create_shader_module(fog_frag_spv, fog_frag_spv_size);

	vk.modules.dot_vs = create_shader_module(dot_vert_spv, dot_vert_spv_size);
	vk.modules.dot_fs = create_shader_module(dot_frag_spv, dot_frag_spv_size);

	vk.modules.light.vs_clip[0] = create_shader_module(light_clip_vert_spv, light_clip_vert_spv_size);
	vk.modules.light.vs_clip[1] = create_shader_module(light_clip_fog_vert_spv, light_clip_fog_vert_spv_size);

	vk.modules.light.fs[0] = create_shader_module(light_frag_spv, light_frag_spv_size);
	vk.modules.light.fs[1] = create_shader_module(light_fog_frag_spv, light_fog_frag_spv_size);

	vk.modules.light1.fs[0] = create_shader_module(light1_frag_spv, light1_frag_spv_size);
	vk.modules.light1.fs[1] = create_shader_module(light1_fog_frag_spv, light1_fog_frag_spv_size);

	vk.modules.gamma_fs = create_shader_module(gamma_frag_spv, gamma_frag_spv_size);
	vk.modules.gamma_vs = create_shader_module(gamma_vert_spv, gamma_vert_spv_size);
}


static void vk_create_persistent_pipelines( void )
{
	unsigned int state_bits;
	Com_Memset( &vk.pipelines, 0, sizeof( vk.pipelines ) );
	
	vk.pipelines_count = 0;
	vk.pipelines_created_count = 0;
	vk.pipelines_world_base = 0;

	{
		// skybox
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.shader_type = TYPE_SIGNLE_TEXTURE;
			def.state_bits = 0;
			def.face_culling = CT_FRONT_SIDED;
			def.polygon_offset = qfalse;
			def.clipping_plane = qfalse;
			def.mirror = qfalse;
			vk.skybox_pipeline = vk_find_pipeline_ext( 0, &def, qtrue );
		}

		// Q3 stencil shadows
		{
			{
				cullType_t cull_types[2] = { CT_FRONT_SIDED, CT_BACK_SIDED };
				qboolean mirror_flags[2] = { qfalse, qtrue };
				Vk_Pipeline_Def def;
				int i, j;

				Com_Memset(&def, 0, sizeof(def));
				def.polygon_offset = qfalse;
				def.state_bits = 0;
				def.shader_type = TYPE_SIGNLE_TEXTURE;
				def.clipping_plane = qfalse;
				def.shadow_phase = SHADOW_EDGES;

				for (i = 0; i < 2; i++) {
					def.face_culling = cull_types[i];
					for (j = 0; j < 2; j++) {
						def.mirror = mirror_flags[j];
						vk.shadow_volume_pipelines[i][j] = vk_find_pipeline_ext( 0, &def, r_shadows->integer ? qtrue: qfalse );
					}
				}
			}

			{
				Vk_Pipeline_Def def;

				Com_Memset( &def, 0, sizeof( def ) );
				def.face_culling = CT_FRONT_SIDED;
				def.polygon_offset = qfalse;
				def.state_bits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
				def.shader_type = TYPE_SIGNLE_TEXTURE;
				def.clipping_plane = qfalse;
				def.mirror = qfalse;
				def.shadow_phase = SHADOW_FS_QUAD;

				vk.shadow_finish_pipeline = vk_find_pipeline_ext( 0, &def, r_shadows->integer ? qtrue: qfalse );
			}
		}

		// fog and dlights
		{
			Vk_Pipeline_Def def;
			unsigned int fog_state_bits[2] = {
				GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL, // fogPass == FP_EQUAL
				GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA // fogPass == FP_LE
			};
			unsigned int dlight_state_bits[2] = {
				GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL,	// modulated
				GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL			// additive
			};
			qboolean polygon_offset[2] = { qfalse, qtrue };
			qboolean clipping_plane[2] = { qfalse, qtrue };
			int i, j, k, l, m;

			Com_Memset(&def, 0, sizeof(def));
			def.shader_type = TYPE_SIGNLE_TEXTURE;
			def.clipping_plane = qfalse;
			def.mirror = qfalse;

			for (i = 0; i < 2; i++) {
				unsigned fog_state = fog_state_bits[i];
				unsigned dlight_state = dlight_state_bits[i];

				for (j = 0; j < 3; j++) {
					def.face_culling = j; // cullType_t value

					for (k = 0; k < 2; k++) {
						def.polygon_offset = polygon_offset[k];
#ifdef USE_FOG_ONLY
						def.shader_type = TYPE_FOG_ONLY;
#else
						def.shader_type = TYPE_SIGNLE_TEXTURE;
#endif
						def.state_bits = fog_state;
						vk.fog_pipelines[i][j][k] = vk_find_pipeline_ext( 0, &def, qtrue );

						def.shader_type = TYPE_SIGNLE_TEXTURE;
						def.state_bits = dlight_state;
#ifdef USE_PMLIGHT
						vk.dlight_pipelines[i][j][k] = vk_find_pipeline_ext( 0, &def, r_dlightMode->integer == 0 ? qtrue : qfalse );
#else
						vk.dlight_pipelines[i][j][k] = vk_find_pipeline_ext( 0, &def, qtrue );
#endif
					}
				}
			}

#ifdef USE_PMLIGHT
			def.state_bits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL;
			//def.shader_type = TYPE_SIGNLE_TEXTURE_LIGHTING;
			for ( i = 0; i < 2; i++ ) { // clipping plane off/on
				def.clipping_plane = clipping_plane[i];
				for (j = 0; j < 3; j++) { // cullType
					def.face_culling = j;
					for ( k = 0; k < 2; k++ ) { // polygonOffset
						def.polygon_offset = polygon_offset[k];
						for ( l = 0; l < 2; l++ ) {
							def.fog_stage = l; // fogStage
							for ( m = 0; m < 2; m++ ) {
								def.abs_light = m;
								def.shader_type = TYPE_SIGNLE_TEXTURE_LIGHTING;
								vk.dlight_pipelines_x[i][j][k][l][m] = vk_find_pipeline_ext( 0, &def, qfalse );
								def.shader_type = TYPE_SIGNLE_TEXTURE_LIGHTING1;
								vk.dlight1_pipelines_x[i][j][k][l][m] = vk_find_pipeline_ext( 0, &def, qfalse );
							}
						}
					}
				}
			}
#endif // USE_PMLIGHT
		}

		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
			def.face_culling = CT_TWO_SIDED;

			vk.surface_beam_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}

		{
			Vk_Pipeline_Def def;

			Com_Memset( &def, 0, sizeof( def ) );
			def.state_bits = GLS_DEFAULT;
			def.face_culling = CT_TWO_SIDED;
			def.line_primitives = qtrue;
			if ( vk.wideLines )
				def.line_width = 3;

			vk.surface_axis_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}

		{
			Vk_Pipeline_Def def;

			Com_Memset( &def, 0, sizeof( def ) );
			//def.state_bits = GLS_DEFAULT;
			def.face_culling = CT_TWO_SIDED;
			def.shader_type = TYPE_DOT;
			vk.dot_pipeline = vk_find_pipeline_ext( 0, &def, qtrue );
		}


		// debug pipelines
		state_bits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE;
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = state_bits;
			def.shader_type = TYPE_COLOR_WHITE;
			vk.tris_debug_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = state_bits;
			def.shader_type = TYPE_COLOR_WHITE;
			def.face_culling = CT_BACK_SIDED;

			vk.tris_mirror_debug_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = state_bits;
			def.shader_type = TYPE_COLOR_GREEN;
			vk.tris_debug_green_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = state_bits;
			def.shader_type = TYPE_COLOR_GREEN;
			def.face_culling = CT_BACK_SIDED;
			
			vk.tris_mirror_debug_green_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = state_bits;
			def.shader_type = TYPE_COLOR_RED;
			vk.tris_debug_red_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = state_bits;
			def.shader_type = TYPE_COLOR_RED;
			def.face_culling = CT_BACK_SIDED;
			vk.tris_mirror_debug_red_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}

		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = GLS_DEPTHMASK_TRUE;
			def.line_primitives = qtrue;

			vk.normals_debug_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;

			vk.surface_debug_pipeline_solid = vk_find_pipeline_ext( 0, &def, qfalse );
		}
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
			def.line_primitives = qtrue;
			vk.surface_debug_pipeline_outline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
		{
			Vk_Pipeline_Def def;

			Com_Memset(&def, 0, sizeof(def));
			def.state_bits = GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;

			vk.images_debug_pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}
	}
}


typedef struct vk_attach_desc_s  {
	VkImage descriptor;
	VkImageView *image_view;
	VkMemoryRequirements reqs;
	uint32_t memoryTypeIndex;
	VkDeviceSize  memory_offset;
	// for layout transition:
	VkImageAspectFlags aspect_flags;
	VkAccessFlags access_flags;
	VkImageLayout image_layout;
	VkFormat image_format;
} vk_attach_desc_t;

#define MAX_ATTACHMENTS 6 // depth + msaa + msaa-resolve + screenmap.msaa + screenmap.resolve + screenmap.depth

static vk_attach_desc_t attachments[ NUM_COMMAND_BUFFERS * MAX_ATTACHMENTS ];
static uint32_t num_attachments;

static void vk_clear_attachment_pool( void )
{
	num_attachments = 0;
}


static void vk_alloc_attachment_memory( void )
{
#ifdef USE_IMAGE_POOL
	VkImageViewCreateInfo view_desc;
	VkMemoryAllocateInfo alloc_info;
	VkCommandBuffer command_buffer;
	VkDeviceSize offset;
	uint32_t memoryTypeBits;
	uint32_t memoryTypeIndex;
	uint32_t i;

	memoryTypeBits = ~0U;
	offset = 0;

	for ( i = 0; i < num_attachments; i++ ) {
#ifdef MIN_IMAGE_ALIGN
		VkDeviceSize alignment = MAX( attachments[ i ].reqs.alignment, MIN_IMAGE_ALIGN );
#else
		VkDeviceSize alignment = attachments[ i ].reqs.alignment;
#endif
		memoryTypeBits &= attachments[ i ].reqs.memoryTypeBits;
		offset = PAD( offset, alignment );
		attachments[ i ].memory_offset = offset;
		offset += attachments[ i ].reqs.size;
#ifdef _DEBUG
		ri.Printf( PRINT_ALL, S_COLOR_CYAN "[%i] type %i, size %i, align %i\n", i,
			attachments[ i ].reqs.memoryTypeBits,
			(int)attachments[ i ].reqs.size,
			(int)attachments[ i ].reqs.alignment );
#endif
	}

	memoryTypeIndex = find_memory_type( vk.physical_device, memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

#ifdef _DEBUG
	ri.Printf( PRINT_ALL, "memory type bits: %04x\n", memoryTypeBits );
	ri.Printf( PRINT_ALL, "memory type index: %04x\n", memoryTypeIndex );
	ri.Printf( PRINT_ALL, "total size: %i\n", (int)offset );
#endif

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = offset;
	alloc_info.memoryTypeIndex = memoryTypeIndex;

	// allocate and bind memory
	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, &vk.image_memory ) );
	for ( i = 0; i < num_attachments; i++ ) {
		
		VK_CHECK( qvkBindImageMemory( vk.device, attachments[i].descriptor, vk.image_memory, attachments[i].memory_offset ) );

		// create color image view
		view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_desc.pNext = NULL;
		view_desc.flags = 0;
		view_desc.image = attachments[ i ].descriptor;
		view_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_desc.format = attachments[ i ].image_format;
		view_desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_desc.subresourceRange.aspectMask = attachments[ i ].aspect_flags;
		view_desc.subresourceRange.baseMipLevel = 0;
		view_desc.subresourceRange.levelCount = 1;
		view_desc.subresourceRange.baseArrayLayer = 0;
		view_desc.subresourceRange.layerCount = 1;

		VK_CHECK( qvkCreateImageView( vk.device, &view_desc, NULL, attachments[ i ].image_view ) );
	}
	// perform layout transition
	command_buffer = begin_command_buffer();
	for ( i = 0; i < num_attachments; i++ ) {
		record_image_layout_transition( command_buffer,
			attachments[i].descriptor,
			attachments[i].aspect_flags,
			0, // src_access_flags
			VK_IMAGE_LAYOUT_UNDEFINED, // old_layout
			attachments[i].access_flags, 
			attachments[i].image_layout
		);
	}
	end_command_buffer( command_buffer );
#endif
}


static void vk_add_attachment_desc( VkImage desc, VkImageView *image_view, VkMemoryRequirements *reqs, VkFormat image_format, VkImageAspectFlags aspect_flags, VkAccessFlags access_flags, VkImageLayout image_layout )
{
	if ( num_attachments >= ARRAY_LEN( attachments ) ) {
		ri.Error( ERR_FATAL, "Attachments array ovrerlow" );
	} else {
		attachments[ num_attachments ].descriptor = desc;
		attachments[ num_attachments ].image_view = image_view;
		attachments[ num_attachments ].reqs = *reqs;
		attachments[ num_attachments ].aspect_flags = aspect_flags;
		attachments[ num_attachments ].access_flags = access_flags;
		attachments[ num_attachments ].image_layout = image_layout;
		attachments[ num_attachments ].image_format = image_format;
		attachments[ num_attachments ].memory_offset = 0;
		num_attachments++;
	}
}


static void vk_get_image_memory_erquirements( VkImage image, VkMemoryRequirements *memory_requirements )
{
	if ( vk.dedicatedAllocation ) {
		VkMemoryRequirements2KHR memory_requirements2;
		VkImageMemoryRequirementsInfo2KHR image_requirements2;
		VkMemoryDedicatedRequirementsKHR mem_req2;

		Com_Memset( &mem_req2, 0, sizeof( mem_req2 ) );
		mem_req2.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR;

		image_requirements2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR;
		image_requirements2.image = image;
		image_requirements2.pNext = NULL;

		Com_Memset( &memory_requirements2, 0, sizeof( memory_requirements2 ) );
		memory_requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;
		memory_requirements2.pNext = &mem_req2;

		qvkGetImageMemoryRequirements2KHR( vk.device, &image_requirements2, &memory_requirements2 );

		*memory_requirements = memory_requirements2.memoryRequirements;
	} else {
		qvkGetImageMemoryRequirements( vk.device, image, memory_requirements );
	}
}


static void create_color_attachment( uint32_t width, uint32_t height, VkSampleCountFlagBits samples, VkFormat format,
	VkImageUsageFlags usage, VkImage *image, VkImageView *image_view, VkDeviceMemory *image_memory, qboolean multisample )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;

#ifndef USE_IMAGE_POOL
	VkMemoryDedicatedAllocateInfoKHR alloc_info2;
	VkMemoryAllocateInfo alloc_info;
	VkImageViewCreateInfo view_desc;
	VkCommandBuffer command_buffer;
#endif

	// create color image
	create_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create_desc.pNext = NULL;
	create_desc.flags = 0;
	create_desc.imageType = VK_IMAGE_TYPE_2D;
	create_desc.format = format;
	create_desc.extent.width = width;
	create_desc.extent.height = height;
	create_desc.extent.depth = 1;
	create_desc.mipLevels = 1;
	create_desc.arrayLayers = 1;
	create_desc.samples = samples;
	create_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_desc.usage = usage;
	create_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_desc.queueFamilyIndexCount = 0;
	create_desc.pQueueFamilyIndices = NULL;
	create_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &create_desc, NULL, image ) );

	vk_get_image_memory_erquirements( *image, &memory_requirements );

#ifdef USE_IMAGE_POOL
	if ( !multisample )
		vk_add_attachment_desc( *image, image_view, &memory_requirements, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	else
		vk_add_attachment_desc( *image, image_view, &memory_requirements, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
#else

	// allocate color image memory
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type( vk.physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	if ( vk.dedicatedAllocation ) {
		Com_Memset( &alloc_info2, 0, sizeof( alloc_info2 ) );
		alloc_info2.sType =  VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
		alloc_info2.image = *image;
		alloc_info.pNext = &alloc_info2;
	}

	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, image_memory ) );
	VK_CHECK( qvkBindImageMemory( vk.device, *image, *image_memory, 0 ) );

	// create color image view
	view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_desc.pNext = NULL;
	view_desc.flags = 0;
	view_desc.image = *image;
	view_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_desc.format = format;
	view_desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_desc.subresourceRange.baseMipLevel = 0;
	view_desc.subresourceRange.levelCount = 1;
	view_desc.subresourceRange.baseArrayLayer = 0;
	view_desc.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &view_desc, NULL, image_view ) );

	// change layout to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	command_buffer = begin_command_buffer();
	record_image_layout_transition( command_buffer, *image, VK_IMAGE_ASPECT_COLOR_BIT,
		0, // src_access_flags
		VK_IMAGE_LAYOUT_UNDEFINED, // old_layout
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	);
	end_command_buffer( command_buffer );
#endif
}


static void create_depth_attachment( uint32_t width, uint32_t height, VkSampleCountFlagBits samples, VkImage *image, VkImageView *image_view, VkDeviceMemory *image_memory )
{
	VkImageCreateInfo create_desc;
	VkMemoryRequirements memory_requirements;
	VkImageAspectFlags image_aspect_flags;
#ifndef USE_IMAGE_POOL
	VkMemoryDedicatedAllocateInfoKHR alloc_info2;
	VkMemoryAllocateInfo alloc_info;
	VkImageViewCreateInfo view_desc;
	VkCommandBuffer command_buffer;
#endif

	// create depth image
	create_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create_desc.pNext = NULL;
	create_desc.flags = 0;
	create_desc.imageType = VK_IMAGE_TYPE_2D;
	create_desc.format = vk.depth_format;
	create_desc.extent.width = width;
	create_desc.extent.height = height;
	create_desc.extent.depth = 1;
	create_desc.mipLevels = 1;
	create_desc.arrayLayers = 1;
	create_desc.samples = samples;
	create_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	create_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_desc.queueFamilyIndexCount = 0;
	create_desc.pQueueFamilyIndices = NULL;
	create_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	image_aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;
	if ( r_stencilbits->integer )
		image_aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;

	VK_CHECK( qvkCreateImage( vk.device, &create_desc, NULL, image ) );

	vk_get_image_memory_erquirements( *image, &memory_requirements );

#ifdef USE_IMAGE_POOL
	vk_add_attachment_desc( *image, image_view, &memory_requirements, vk.depth_format, image_aspect_flags, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
#else
	// allocate depth image memory
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type( vk.physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	if ( vk.dedicatedAllocation ) {
		Com_Memset( &alloc_info2, 0, sizeof( alloc_info2 ) );
		alloc_info2.sType =  VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
		alloc_info2.image = *image;
		alloc_info.pNext = &alloc_info2;
	}

	VK_CHECK( qvkAllocateMemory( vk.device, &alloc_info, NULL, image_memory ) );
	VK_CHECK( qvkBindImageMemory( vk.device, *image, *image_memory, 0 ) );

	// create depth image view
	view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_desc.pNext = NULL;
	view_desc.flags = 0;
	view_desc.image = *image;
	view_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_desc.format = vk.depth_format;
	view_desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if ( r_stencilbits->integer )
		view_desc.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	view_desc.subresourceRange.baseMipLevel = 0;
	view_desc.subresourceRange.levelCount = 1;
	view_desc.subresourceRange.baseArrayLayer = 0;
	view_desc.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &view_desc, NULL, image_view ) );

	// change layout to VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	command_buffer = begin_command_buffer();
	record_image_layout_transition( command_buffer, *image, image_aspect_flags,
		0,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	);
	end_command_buffer( command_buffer );
#endif
}


static void vk_create_framebuffers( void )
{
	VkImageView attachments[3];
	VkFramebufferCreateInfo desc;
#ifdef USE_SINGLE_FBO
	uint32_t n;
#else
	uint32_t i, n;
#endif

	desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.pAttachments = attachments;
	desc.layers = 1;

#ifndef USE_SINGLE_FBO
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
#endif
	for ( n = 0; n < vk.swapchain_image_count; n++ )
	{
		desc.renderPass = vk.render_pass;
		desc.attachmentCount = 2;
		if ( r_fbo->integer == 0 )
		{
			desc.width = vk.windowWidth;
			desc.height = vk.windowHeight;
			attachments[0] = vk.swapchain_image_views[n];
#ifdef USE_SINGLE_FBO
			attachments[1] = vk.depth_image_view;
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers[n] ) );
#else
			attachments[1] = vk.tess[i].depth_image_view;
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.tess[i].framebuffers[n] ) );
#endif
		}
		else
		{
			desc.width = glConfig.vidWidth;
			desc.height = glConfig.vidHeight;
#ifdef USE_SINGLE_FBO
			attachments[0] = vk.color_image_view;
			attachments[1] = vk.depth_image_view;
#else
			attachments[0] = vk.tess[i].color_image_view;
			attachments[1] = vk.tess[i].depth_image_view;
#endif
			if ( vk.msaaActive )
			{
				desc.attachmentCount = 3;
#ifdef USE_SINGLE_FBO
				attachments[2] = vk.msaa_image_view;
#else
				attachments[2] = vk.tess[i].msaa_image_view;
#endif
			}
#ifdef USE_SINGLE_FBO
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers[n] ) );
#else
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.tess[i].framebuffers[n] ) );
#endif
			desc.renderPass = vk.render_pass_gamma;
			desc.attachmentCount = 1;
			desc.width = vk.windowWidth;
			desc.height = vk.windowHeight;
			attachments[0] = vk.swapchain_image_views[n];
#ifdef USE_SINGLE_FBO
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers2[n] ) );

			// screenmap
			desc.renderPass = vk.render_pass_screenmap;
			desc.attachmentCount = 2;
			desc.width = vk.screenMapWidth;
			desc.height = vk.screenMapHeight;
			attachments[0] = vk.color_image_view3;
			attachments[1] = vk.depth_image_view3;
			if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT )
			{
				desc.attachmentCount = 3;
				attachments[2] = vk.color_image_view3_msaa;
			}
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.framebuffers3[n] ) );
#else
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.tess[i].framebuffers2[n] ) );

			// screenmap
			desc.renderPass = vk.render_pass_screenmap;
			desc.attachmentCount = 2;
			desc.width = vk.screenMapWidth;
			desc.height = vk.screenMapHeight;
			attachments[0] = vk.tess[i].color_image_view3;
			attachments[1] = vk.tess[i].depth_image_view3;
			if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT )
			{
				desc.attachmentCount = 3;
				attachments[2] = vk.tess[i].color_image_view3_msaa;
			}
			VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, NULL, &vk.tess[i].framebuffers3[n] ) );
#endif
		}
	}
}


static void vk_destroy_framebuffers( void ) {
#ifdef USE_SINGLE_FBO
	uint32_t n;
	for ( n = 0; n < vk.swapchain_image_count; n++ ) {
		if ( vk.framebuffers[n] != VK_NULL_HANDLE ) {
			qvkDestroyFramebuffer( vk.device, vk.framebuffers[n], NULL );
			vk.framebuffers[n] = VK_NULL_HANDLE;
		}
		if ( vk.framebuffers2[n] != VK_NULL_HANDLE ) {
			qvkDestroyFramebuffer( vk.device, vk.framebuffers2[n], NULL );
			vk.framebuffers2[n] = VK_NULL_HANDLE;
		}
		if ( vk.framebuffers3[n] != VK_NULL_HANDLE ) {
			qvkDestroyFramebuffer( vk.device, vk.framebuffers3[n], NULL );
			vk.framebuffers3[n] = VK_NULL_HANDLE;
		}
	}
#else
	uint32_t i, n;
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		for ( n = 0; n < vk.swapchain_image_count; n++ ) {
			if ( vk.tess[i].framebuffers[n] != VK_NULL_HANDLE ) {
				qvkDestroyFramebuffer( vk.device, vk.tess[i].framebuffers[n], NULL );
				vk.tess[i].framebuffers[n] = VK_NULL_HANDLE;
			}
			if ( vk.tess[i].framebuffers2[n] != VK_NULL_HANDLE ) {
				qvkDestroyFramebuffer( vk.device, vk.tess[i].framebuffers2[n], NULL );
				vk.tess[i].framebuffers2[n] = VK_NULL_HANDLE;
			}
			if ( vk.tess[i].framebuffers3[n] != VK_NULL_HANDLE ) {
				qvkDestroyFramebuffer( vk.device, vk.tess[i].framebuffers3[n], NULL );
				vk.tess[i].framebuffers3[n] = VK_NULL_HANDLE;
			}
		}
	}
#endif
}


static void vk_destroy_swapchain( void ) {
	uint32_t i;

	for ( i = 0; i < vk.swapchain_image_count; i++ ) {
		if ( vk.swapchain_image_views[i] != VK_NULL_HANDLE ) {
			qvkDestroyImageView( vk.device, vk.swapchain_image_views[i], NULL );
			vk.swapchain_image_views[i] = VK_NULL_HANDLE;
		}
	}

	qvkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );
}


void vk_restart_swapchain( const char *funcname )
{
	ri.Printf( PRINT_WARNING, "%s(): restarting swapchain...\n", funcname );
	vk_wait_idle();
	vk_destroy_framebuffers();
	vk_destroy_swapchain();
	vk_create_swapchain( vk.physical_device, vk.device, vk.surface, vk.surface_format, &vk.swapchain );
	vk_create_framebuffers();
}


static void vk_set_render_scale( void )
{
	int scaleMode;

	if ( r_fbo->integer == 0 )
		return;

	if ( vk.windowWidth != glConfig.vidWidth || vk.windowHeight != glConfig.vidHeight )
	{
		if ( r_renderScale->integer > 0 )
		{
			scaleMode = r_renderScale->integer - 1;
			if ( scaleMode & 1 )
			{
				// preserve aspect ratio (black bars on sides)
				float windowAspect = (float) vk.windowWidth / (float) vk.windowHeight;
				float renderAspect = (float) glConfig.vidWidth / (float) glConfig.vidHeight;
				if ( windowAspect >= renderAspect ) 
				{
					float scale = (float) vk.windowHeight / ( float ) glConfig.vidHeight;
					int bias = ( vk.windowWidth - scale * (float) glConfig.vidWidth ) / 2;
					vk.blitX0 += bias;
				}
				else
				{
					float scale = (float) vk.windowWidth / ( float ) glConfig.vidWidth;
					int bias = ( vk.windowHeight - scale * (float) glConfig.vidHeight ) / 2;
					vk.blitY0 += bias;
				}
			}
			// linear filtering
			if ( scaleMode & 2 )
				vk.blitFilter = GL_LINEAR;
			else
				vk.blitFilter = GL_NEAREST;
		}

		vk.windowAdjusted = qtrue;
	}
}


void vk_initialize( void )
{
	char buf[64], driver_version[64];
	const char *vendor_name;
	const char *device_type;
	VkPhysicalDeviceProperties props;
	uint32_t major;
	uint32_t minor;
	uint32_t patch;
	uint32_t maxSize;
	uint32_t i;

	init_vulkan_library();

	qvkGetDeviceQueue( vk.device, vk.queue_family_index, 0, &vk.queue );

	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );

	vk.cmd = vk.tess + 0;
	vk.uniform_alignment = props.limits.minUniformBufferOffsetAlignment;
	vk.uniform_item_size = PAD( sizeof( vkUniform_t ), vk.uniform_alignment );

	// for flare visibility tests
	vk.storage_alignment = MAX( props.limits.minStorageBufferOffsetAlignment, sizeof( uint32_t ) );

	vk.maxAnisotropy = props.limits.maxSamplerAnisotropy;
	vk.maxLodBias = props.limits.maxSamplerLodBias;

	// framebuffer parameters
	vk.windowAdjusted = qfalse;
	vk.windowWidth = glConfig.vidWidth;
	vk.windowHeight = glConfig.vidHeight;

	vk.blitFilter = GL_NEAREST;
	vk.windowAdjusted = qfalse;
	vk.blitX0 = vk.blitY0 = 0;

	if ( r_fbo->integer ) {
		vk.fboActive = qtrue;
		if ( r_ext_multisample->integer ) {
			vk.msaaActive = qtrue;
		}

		// TODO: set renderScale there!
		if ( r_renderScale->integer ) {
			glConfig.vidWidth = r_renderWidth->integer;
			glConfig.vidHeight = r_renderHeight->integer;
		}

		ri.CL_SetScaling( 1.0, glConfig.vidWidth, glConfig.vidHeight );

		vk_set_render_scale();
	}

	vkMaxSamples = MIN( props.limits.sampledImageColorSampleCounts, props.limits.sampledImageDepthSampleCounts );

	if ( /*vk.fboActive &&*/ vk.msaaActive ) {
		VkSampleCountFlags mask = vkMaxSamples;
		vkSamples = MAX( log2pad( r_ext_multisample->integer, 1 ), VK_SAMPLE_COUNT_2_BIT );
		while ( vkSamples > mask )
				vkSamples >>= 1;
		ri.Printf( PRINT_ALL, "...using %ix MSAA\n", vkSamples );
	} else {
		vkSamples = VK_SAMPLE_COUNT_1_BIT;
	}

	// fill glConfig information

	// maxTextureSize must not exceed IMAGE_CHUNK_SIZE
	maxSize = sqrtf( IMAGE_CHUNK_SIZE / 4 );
	// round down to next power of 2
	glConfig.maxTextureSize = MIN( props.limits.maxImageDimension2D, log2pad( maxSize, 0 ) );

	if ( props.limits.maxPerStageDescriptorSamplers != 0xFFFFFFFF )
		glConfig.numTextureUnits = props.limits.maxPerStageDescriptorSamplers;
	else
		glConfig.numTextureUnits = props.limits.maxBoundDescriptorSets;
	if ( glConfig.numTextureUnits > MAX_TEXTURE_UNITS )
		glConfig.numTextureUnits = MAX_TEXTURE_UNITS;

	glConfig.textureEnvAddAvailable = qtrue;
	glConfig.textureCompression = TC_NONE;

	major = VK_VERSION_MAJOR(props.apiVersion);
	minor = VK_VERSION_MINOR(props.apiVersion);
	patch = VK_VERSION_PATCH(props.apiVersion);

	// decode driver version
	switch ( props.vendorID ) {
		case 0x10DE: // NVidia
			Com_sprintf( driver_version, sizeof( driver_version ), "%i.%i.%i.%i",
				(props.driverVersion >> 22) & 0x3FF,
				(props.driverVersion >> 14) & 0x0FF,
				(props.driverVersion >> 6) & 0x0FF,
				(props.driverVersion >> 0) & 0x03F );
			break;
#ifdef _WIN32
		case 0x8086: // Intel
			Com_sprintf( driver_version, sizeof( driver_version ), "%i.%i",
				(props.driverVersion >> 14),
				(props.driverVersion >> 0) & 0x3FFF );
			break;
#endif
		default:
			Com_sprintf( driver_version, sizeof( driver_version ), "%i.%i.%i",
				(props.driverVersion >> 22),
				(props.driverVersion >> 12) & 0x3FF,
				(props.driverVersion >> 0) & 0xFFF );
	}
	
	Com_sprintf( glConfig.version_string, sizeof( glConfig.version_string ), "API: %i.%i.%i, Driver: %s",
		major, minor, patch, driver_version );

	vk.offscreenRender = qtrue;

	if ( props.vendorID == 0x1002 ) {
		vendor_name = "Advanced Micro Devices, Inc.";
	} else if ( props.vendorID == 0x10DE ) {
#ifdef _WIN32
		// https://github.com/SaschaWillems/Vulkan/issues/493
		// we can't render to offscreen presentation surfaces on nvidia
		vk.offscreenRender = qfalse;
#endif
		vendor_name = "NVIDIA";
	} else if ( props.vendorID == 0x8086 ) {
		vendor_name = "Intel Corporation";
	} else {
		Com_sprintf( buf, sizeof( buf ), "VendorID: %04x", props.vendorID );
		vendor_name = buf;
	}

	switch ( props.deviceType ) {
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: device_type = "Integrated"; break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: device_type = "Discrete"; break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: device_type = "Virtual"; break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU: device_type = "CPU"; break;
		default: device_type = "OTHER"; break;
	}
	
	Q_strncpyz( glConfig.vendor_string, vendor_name, sizeof( glConfig.vendor_string ) );
	Com_sprintf( glConfig.renderer_string, sizeof(	glConfig.renderer_string ),
		"%s %s, 0x%04x", device_type, props.deviceName, props.deviceID );

	//
	// Swapchain.
	//
	vk_create_swapchain( vk.physical_device, vk.device, vk.surface, vk.surface_format, &vk.swapchain );

	//
	// Sync primitives.
	//
	{
		VkSemaphoreCreateInfo desc;
		VkFenceCreateInfo fence_desc;

		desc.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;

		// swapchain image acquired
		VK_CHECK(qvkCreateSemaphore(vk.device, &desc, NULL, &vk.image_acquired));

		// all commands submitted
		for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
		{
			desc.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			desc.pNext = NULL;
			desc.flags = 0;

			VK_CHECK( qvkCreateSemaphore( vk.device, &desc, NULL, &vk.tess[i].rendering_finished ) );

			fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fence_desc.pNext = NULL;
			fence_desc.flags = VK_FENCE_CREATE_SIGNALED_BIT; // so it can be used to start rendering

			VK_CHECK( qvkCreateFence( vk.device, &fence_desc, NULL, &vk.tess[i].rendering_finished_fence ) );
			vk.tess[i].waitForFence = qtrue;
		}
	}

	//
	// Command pool.
	//
	{
		VkCommandPoolCreateInfo desc;

		desc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		desc.queueFamilyIndex = vk.queue_family_index;

		VK_CHECK(qvkCreateCommandPool(vk.device, &desc, NULL, &vk.command_pool));
	}

	//
	// Command buffers and color attachments.
	//
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		VkCommandBufferAllocateInfo alloc_info;

		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.pNext = NULL;
		alloc_info.commandPool = vk.command_pool;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = 1;

		VK_CHECK( qvkAllocateCommandBuffers( vk.device, &alloc_info, &vk.tess[i].command_buffer ) );
	}

	vk_clear_attachment_pool();

	vk.screenMapWidth = (float) glConfig.vidWidth / 16.0;
	if ( vk.screenMapWidth < 4 )
		vk.screenMapWidth = 4;

	vk.screenMapHeight = (float) glConfig.vidHeight / 16.0;
	if ( vk.screenMapHeight < 4 )
		vk.screenMapHeight = 4;

	vk.screenMapSamples = MIN( vkMaxSamples, VK_SAMPLE_COUNT_4_BIT );

#ifdef USE_IMAGE_LAYOUT_1
	// It looks like resulting performance depends from order you're creating/allocating
	// memory for attachments in vulkan i.e. similar images grouped together will provide best results
	// so [resolve0][resolve1][msaa0][msaa1][depth0][depth1] is most optimal
	// while cases like [resolve0][depth0][color0][...] is the worst

	// TODO: preallocate first image chunk in attachment' memory pool?
#ifdef USE_SINGLE_FBO
	if ( vk.fboActive ) {
		// post-processing/msaa-resolve
		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, VK_SAMPLE_COUNT_1_BIT, vk.resolve_format,
			usage, &vk.color_image, &vk.color_image_view, &vk.color_image_memory, qfalse );

		// screenmap
		usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT ) {
			create_color_attachment( vk.screenMapWidth, vk.screenMapHeight, vk.screenMapSamples, vk.color_format,
				usage, &vk.color_image3_msaa, &vk.color_image_view3_msaa, &vk.color_image_memory3_msaa, qtrue );
		}

		create_color_attachment( vk.screenMapWidth, vk.screenMapHeight, VK_SAMPLE_COUNT_1_BIT, vk.resolve_format,
			usage, &vk.color_image3, &vk.color_image_view3, &vk.color_image_memory3, qfalse );

		// screenmap
		create_depth_attachment( vk.screenMapWidth, vk.screenMapHeight, vk.screenMapSamples, &vk.depth_image3, &vk.depth_image_view3, &vk.depth_image_memory3 );

		if ( vk.msaaActive ) {
			create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, vk.color_format, 
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &vk.msaa_image, &vk.msaa_image_view, &vk.msaa_image_memory, qtrue );
		}
	}

	create_depth_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, &vk.depth_image, &vk.depth_image_view, &vk.depth_image_memory );

#else // !USE_SINGLE_FBO

	if ( vk.fboActive ) {
		// screenmap
		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		for ( i	= 0; i < NUM_COMMAND_BUFFERS; i++ ) {
			create_color_attachment( vk.screenMapWidth, vk.screenMapHeight, VK_SAMPLE_COUNT_1_BIT, vk.resolve_format,
				usage, &vk.tess[i].color_image3, &vk.tess[i].color_image_view3, &vk.tess[i].color_image_memory3, qfalse );
		}

		if ( vk.screenMapSamples > VK_SAMPLE_COUNT_1_BIT ) {
			for ( i	= 0; i < NUM_COMMAND_BUFFERS; i++ ) {
				create_color_attachment( vk.screenMapWidth, vk.screenMapHeight, vk.screenMapSamples, vk.color_format,
					usage, &vk.tess[i].color_image3_msaa, &vk.tess[i].color_image_view3_msaa, &vk.tess[i].color_image_memory3_msaa, qtrue );
			}
		}

		for ( i	= 0; i < NUM_COMMAND_BUFFERS; i++ ) {
			create_depth_attachment( vk.screenMapWidth, vk.screenMapHeight, vk.screenMapSamples, &vk.tess[i].depth_image3, &vk.tess[i].depth_image_view3, &vk.tess[i].depth_image_memory3 );
		}

		for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
			// post-processing/msaa-resolve
			create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, VK_SAMPLE_COUNT_1_BIT, vk.resolve_format,
				usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				&vk.tess[i].color_image, &vk.tess[i].color_image_view, &vk.tess[i].color_image_memory, qfalse );
		}

		if ( vk.msaaActive ) {
			for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
				create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, vk.color_format, 
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					&vk.tess[i].msaa_image, &vk.tess[i].msaa_image_view, &vk.tess[i].msaa_image_memory, qtrue );
			}
		}
	}

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		create_depth_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, &vk.tess[i].depth_image, &vk.tess[i].depth_image_view, &vk.tess[i].depth_image_memory );
	}

#endif // !USE_SINGLE_FBO
#else // !USE_IMAGE_LAYOUT_1
#ifdef USE_SINGLE_FBO
	if ( vk.fboActive ) {
		// post-processing/msaa-resolve
		create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, VK_SAMPLE_COUNT_1_BIT, vk.resolve_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
			&vk.color_image, &vk.color_image_view, &vk.color_image_memory, qfalse );
	}
	if ( /* vk.fboActive && */ vk.msaaActive ) {
		// msaa
		create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, vk.color_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&vk.msaa_image, &vk.msaa_image_view, &vk.msaa_image_memory, qtrue );
	}
	// depth
	create_depth_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, &vk.depth_image, &vk.depth_image_view, &vk.depth_image_memory );
#else // !USE_SINGLE_FBO
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		if ( vk.fboActive ) {
			// post-processing/msaa-resolve
			create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, VK_SAMPLE_COUNT_1_BIT, vk.resolve_format,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
				&vk.tess[i].color_image, &vk.tess[i].color_image_view, &vk.tess[i].color_image_memory );
		}
		if ( /* vk.fboActive && */ vk.msaaActive ) {
			// msaa
			create_color_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, vk.color_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				&vk.tess[i].msaa_image,	&vk.tess[i].msaa_image_view, &vk.tess[i].msaa_image_memory );
		}
		// depth
		create_depth_attachment( glConfig.vidWidth, glConfig.vidHeight, vkSamples, &vk.tess[i].depth_image, &vk.tess[i].depth_image_view, &vk.tess[i].depth_image_memory );
	}
#endif // !USE_SINGLE_FBO
#endif // !USE_IMAGE_LAYOUT_1

	vk_alloc_attachment_memory();

	//
	// Renderpass.
	//
	create_render_pass( vk.device, vk.depth_format );

	//
	// Framebuffers for each swapchain image.
	//
	vk_create_framebuffers();

	//
	// Descriptor pool.
	//
	{
		VkDescriptorPoolSize pool_size[3];
		VkDescriptorPoolCreateInfo desc;
		uint32_t i, maxSets;

		pool_size[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_size[0].descriptorCount = MAX_DRAWIMAGES + NUM_COMMAND_BUFFERS * 2;

		pool_size[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		pool_size[1].descriptorCount = NUM_COMMAND_BUFFERS;

		//pool_size[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		//pool_size[2].descriptorCount = NUM_COMMAND_BUFFERS;

		pool_size[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
		pool_size[2].descriptorCount = 1;

		for ( i = 0, maxSets = 0; i < ARRAY_LEN( pool_size ); i++ ) {
			maxSets += pool_size[i].descriptorCount;
		}

		desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.maxSets = maxSets;
		desc.poolSizeCount = ARRAY_LEN( pool_size );
		desc.pPoolSizes = pool_size;

		VK_CHECK( qvkCreateDescriptorPool( vk.device, &desc, NULL, &vk.descriptor_pool ) );
	}

	//
	// Descriptor set layout.
	//
	vk_create_layout_binding( 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, &vk.set_layout_sampler );
	vk_create_layout_binding( 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, &vk.set_layout_uniform );
	vk_create_layout_binding( 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, VK_SHADER_STAGE_FRAGMENT_BIT, &vk.set_layout_storage );
	//vk_create_layout_binding( 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT, &vk.set_layout_input );

	//
	// Pipeline layouts.
	//
	{
		VkDescriptorSetLayout set_layouts[5];
		VkPipelineLayoutCreateInfo desc;
		VkPushConstantRange push_range;

		push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		push_range.offset = 0;
		push_range.size = 128; // 32 floats

		// standard pipelines

		set_layouts[0] = vk.set_layout_uniform; // fog/dlight parameters
		set_layouts[1] = vk.set_layout_sampler; // diffuse
		set_layouts[2] = vk.set_layout_sampler; // lightmap
		set_layouts[3] = vk.set_layout_sampler; // fog 
		set_layouts[4] = vk.set_layout_storage; // storage

		desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.setLayoutCount = 5;
		desc.pSetLayouts = set_layouts;
		desc.pushConstantRangeCount = 1;
		desc.pPushConstantRanges = &push_range;

		VK_CHECK(qvkCreatePipelineLayout(vk.device, &desc, NULL, &vk.pipeline_layout));

		// flare test pipeline
#if 0
		set_layouts[0] = vk.set_layout_storage; // dynamic storage buffer
		
		desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.setLayoutCount = 1;
		desc.pSetLayouts = set_layouts;
		desc.pushConstantRangeCount = 1;
		desc.pPushConstantRanges = &push_range;

		VK_CHECK(qvkCreatePipelineLayout(vk.device, &desc, NULL, &vk.pipeline_layout_storage));
#endif

		// post-processing pipeline
		set_layouts[0] = vk.set_layout_sampler; // sampler

		desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.setLayoutCount = 1;
		desc.pSetLayouts = set_layouts;
		desc.pushConstantRangeCount = 0;
		desc.pPushConstantRanges = NULL;

		VK_CHECK( qvkCreatePipelineLayout( vk.device, &desc, NULL, &vk.pipeline_layout_gamma ) );

	}

	vk.geometry_buffer_size_new = VERTEX_BUFFER_SIZE;
	vk_create_geometry_buffers( vk.geometry_buffer_size_new );
	vk.geometry_buffer_size_new = 0;

	vk_create_storage_buffer( MAX_FLARES * vk.storage_alignment );

	vk_create_shader_modules();

	{
		VkPipelineCacheCreateInfo ci;
		Com_Memset( &ci, 0, sizeof( ci ) );
		ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK( qvkCreatePipelineCache( vk.device, &ci, NULL, &vk.pipelineCache ) );
	}

	vk.renderPassIndex = RENDER_PASS_MAIN; // default render pass

	vk_create_persistent_pipelines();

	vk.pipelines_world_base = vk.pipelines_count;

	vk.active = qtrue;
}


void vk_shutdown( void )
{
	uint32_t i, j;

	if ( !qvkDestroyImage ) // not fully initialized
		return;

	vk_destroy_framebuffers();

#ifdef USE_SINGLE_FBO

	if ( vk.color_image ) {
		qvkDestroyImage( vk.device, vk.color_image, NULL );
#ifndef USE_IMAGE_POOL
		qvkFreeMemory( vk.device, vk.color_image_memory, NULL );
#endif
		qvkDestroyImageView( vk.device, vk.color_image_view, NULL );
	}

	if ( vk.color_image3 ) {
		qvkDestroyImage( vk.device, vk.color_image3, NULL );
#ifndef USE_IMAGE_POOL
		qvkFreeMemory( vk.device, vk.color_image_memory3, NULL );
#endif
		qvkDestroyImageView( vk.device, vk.color_image_view3, NULL );
	}

	if ( vk.color_image3_msaa ) {
		qvkDestroyImage( vk.device, vk.color_image3_msaa, NULL );
#ifndef USE_IMAGE_POOL
		qvkFreeMemory( vk.device, vk.color_image_memory3_msaa, NULL );
#endif
		qvkDestroyImageView( vk.device, vk.color_image_view3_msaa, NULL );
	}

	if ( vk.msaa_image ) {
		qvkDestroyImage( vk.device, vk.msaa_image, NULL );
#ifndef USE_IMAGE_POOL
		qvkFreeMemory( vk.device, vk.msaa_image_memory, NULL );
#endif
		qvkDestroyImageView( vk.device, vk.msaa_image_view, NULL );
	}

	qvkDestroyImage( vk.device, vk.depth_image, NULL );
#ifndef USE_IMAGE_POOL
	qvkFreeMemory( vk.device, vk.depth_image_memory, NULL );
#endif
	qvkDestroyImageView( vk.device, vk.depth_image_view, NULL );

	if ( vk.depth_image3 ) {
		qvkDestroyImage( vk.device, vk.depth_image3, NULL );
#ifndef USE_IMAGE_POOL
		qvkFreeMemory( vk.device, vk.depth_image_memory3, NULL );
#endif
		qvkDestroyImageView( vk.device, vk.depth_image_view3, NULL );
	}

#else // USE_SINGLE_FBO
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {

		if ( vk.tess[i].color_image ) {
			qvkDestroyImage( vk.device, vk.tess[i].color_image, NULL );
#ifndef USE_IMAGE_POOL
			qvkFreeMemory( vk.device, vk.tess[i].color_image_memory, NULL );
#endif
			qvkDestroyImageView( vk.device, vk.tess[i].color_image_view, NULL );
		}

		if ( vk.tess[i].color_image3 ) {
			qvkDestroyImage( vk.device, vk.tess[i].color_image3, NULL );
#ifndef USE_IMAGE_POOL
			qvkFreeMemory( vk.device, vk.tess[i].color_image_memory3, NULL );
#endif
			qvkDestroyImageView( vk.device, vk.tess[i].color_image_view3, NULL );
		}

		if ( vk.tess[i].color_image3_msaa ) {
			qvkDestroyImage( vk.device, vk.tess[i].color_image3_msaa, NULL );
#ifndef USE_IMAGE_POOL
			qvkFreeMemory( vk.device, vk.tess[i].color_image3_memory_msaa, NULL );
#endif
			qvkDestroyImageView( vk.device, vk.tess[i].color_image_view3_msaa, NULL );
		}

		if ( vk.tess[i].msaa_image ) {
			qvkDestroyImage( vk.device, vk.tess[i].msaa_image, NULL );
#ifndef USE_IMAGE_POOL
			qvkFreeMemory( vk.device, vk.tess[i].msaa_image_memory, NULL );
#endif
			qvkDestroyImageView( vk.device, vk.tess[i].msaa_image_view, NULL );
		}

		qvkDestroyImage( vk.device, vk.tess[i].depth_image, NULL );
#ifndef USE_IMAGE_POOL
		qvkFreeMemory( vk.device, vk.tess[i].depth_image_memory, NULL );
#endif
		qvkDestroyImageView( vk.device, vk.tess[i].depth_image_view, NULL );

		qvkDestroyImage( vk.device, vk.tess[i].depth_image3, NULL );
#ifndef USE_IMAGE_POOL
		qvkFreeMemory( vk.device, vk.tess[i].depth_image_memory3, NULL );
#endif
		qvkDestroyImageView( vk.device, vk.tess[i].depth_image_view3, NULL );
	}
#endif

#ifdef USE_IMAGE_POOL
	qvkFreeMemory( vk.device, vk.image_memory, NULL );
#endif

	if ( vk.render_pass != VK_NULL_HANDLE )
		qvkDestroyRenderPass( vk.device, vk.render_pass, NULL );

	if ( vk.render_pass_screenmap != VK_NULL_HANDLE )
		qvkDestroyRenderPass( vk.device, vk.render_pass_screenmap, NULL );

	if ( vk.render_pass_gamma != VK_NULL_HANDLE )
		qvkDestroyRenderPass( vk.device, vk.render_pass_gamma, NULL );

	qvkDestroyCommandPool( vk.device, vk.command_pool, NULL );

	//for ( i = 0; i < vk.swapchain_image_count; i++ ) {
	//	qvkDestroyImageView( vk.device, vk.swapchain_image_views[i], NULL );
	//}

	for ( i = 0; i < vk.pipelines_count; i++ ) {
		for ( j = 0; j < RENDER_PASS_COUNT; j++ ) {
			if ( vk.pipelines[i].handle[j] != VK_NULL_HANDLE ) {
				qvkDestroyPipeline( vk.device, vk.pipelines[i].handle[j], NULL );
				vk.pipelines[i].handle[j] = VK_NULL_HANDLE;
			}
		}
		Com_Memset( &vk.pipelines[i], 0, sizeof( vk.pipelines[0] ) );
	}
	vk.pipelines_count = 0;

	if ( vk.gamma_pipeline ) {
		qvkDestroyPipeline( vk.device, vk.gamma_pipeline, NULL );
	}

	if ( vk.pipelineCache != VK_NULL_HANDLE ) {
		qvkDestroyPipelineCache( vk.device, vk.pipelineCache, NULL );
		vk.pipelineCache = VK_NULL_HANDLE;
	}

	qvkDestroyDescriptorPool(vk.device, vk.descriptor_pool, NULL);

	qvkDestroyDescriptorSetLayout(vk.device, vk.set_layout_sampler, NULL);
	qvkDestroyDescriptorSetLayout(vk.device, vk.set_layout_uniform, NULL);
	qvkDestroyDescriptorSetLayout(vk.device, vk.set_layout_storage, NULL);
	
	qvkDestroyPipelineLayout(vk.device, vk.pipeline_layout, NULL);
	//qvkDestroyPipelineLayout(vk.device, vk.pipeline_layout_storage, NULL);
	qvkDestroyPipelineLayout(vk.device, vk.pipeline_layout_gamma, NULL);

#ifdef USE_VBO	
	vk_release_vbo();
#endif

	vk_release_geometry_buffers();

	qvkDestroyBuffer( vk.device, vk.storage.buffer, NULL );
	qvkFreeMemory( vk.device, vk.storage.memory, NULL );

	qvkDestroySemaphore( vk.device, vk.image_acquired, NULL );

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		qvkDestroySemaphore( vk.device, vk.tess[i].rendering_finished, NULL );
		qvkDestroyFence( vk.device, vk.tess[i].rendering_finished_fence, NULL );
	}
	
	qvkDestroyShaderModule(vk.device, vk.modules.st_clip_vs[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.st_clip_vs[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.st_enviro_vs[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.st_enviro_vs[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.st_fs[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.st_fs[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.st_df_fs, NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.color_fs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.color_clip_vs, NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.mt_clip_vs[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.mt_clip_vs[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.mt_mul_fs[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.mt_mul_fs[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.mt_add_fs[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.mt_add_fs[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.fog_vs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.fog_fs, NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.dot_vs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.dot_fs, NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.light.vs_clip[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.light.vs_clip[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.light.fs[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.light.fs[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.light1.fs[0], NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.light1.fs[1], NULL);

	qvkDestroyShaderModule(vk.device, vk.modules.gamma_vs, NULL);
	qvkDestroyShaderModule(vk.device, vk.modules.gamma_fs, NULL);

	//qvkDestroySwapchainKHR(vk.device, vk.swapchain, NULL);
	vk_destroy_swapchain();

	qvkDestroyDevice( vk.device, NULL );
	qvkDestroySurfaceKHR( vk.instance, vk.surface, NULL );

#ifndef NDEBUG
	if ( qvkDestroyDebugReportCallbackEXT && vk.debug_callback )
		qvkDestroyDebugReportCallbackEXT( vk.instance, vk.debug_callback, NULL );
#endif

	qvkDestroyInstance(vk.instance, NULL);

	Com_Memset(&vk, 0, sizeof(vk));

	deinit_vulkan_library();
}


void vk_wait_idle( void )
{
	VK_CHECK( qvkDeviceWaitIdle( vk.device ) );
}


void vk_release_resources( void ) {
	int i, j;

	vk_wait_idle();

	for (i = 0; i < vk_world.num_image_chunks; i++)
		qvkFreeMemory(vk.device, vk_world.image_chunks[i].memory, NULL);

	if (vk_world.staging_buffer != VK_NULL_HANDLE)
		qvkDestroyBuffer(vk.device, vk_world.staging_buffer, NULL);

	if (vk_world.staging_buffer_memory != VK_NULL_HANDLE)
		qvkFreeMemory(vk.device, vk_world.staging_buffer_memory, NULL);

	for (i = 0; i < vk_world.num_samplers; i++)
		qvkDestroySampler(vk.device, vk_world.samplers[i], NULL);

	for ( i = vk.pipelines_world_base; i < vk.pipelines_count; i++ ) {
		for ( j = 0; j < RENDER_PASS_COUNT; j++ ) {
			if ( vk.pipelines[i].handle[j] != VK_NULL_HANDLE ) {
				qvkDestroyPipeline( vk.device, vk.pipelines[i].handle[j], NULL );
				vk.pipelines[i].handle[j] = VK_NULL_HANDLE;
			}
		}
		Com_Memset( &vk.pipelines[i], 0, sizeof( vk.pipelines[0] ) );
	}
	vk.pipelines_count = vk.pipelines_world_base;

	VK_CHECK( qvkResetDescriptorPool( vk.device, vk.descriptor_pool, 0 ) );

	Com_Memset( &vk_world, 0, sizeof( vk_world ) );

	// Reset geometry buffers offsets
	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vk.tess[i].uniform_read_offset = 0;
		vk.tess[i].vertex_buffer_offset = 0;
	}

	Com_Memset( vk.cmd->buf_offset, 0, sizeof( vk.cmd->buf_offset ) );
	Com_Memset( vk.cmd->vbo_offset, 0, sizeof( vk.cmd->vbo_offset ) );

	Com_Memset( &vk.stats, 0, sizeof( vk.stats ) );
}


static void record_buffer_memory_barrier(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize size,
		VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
		VkAccessFlags src_access, VkAccessFlags dst_access) {

	VkBufferMemoryBarrier barrier;
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.pNext = NULL;
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.offset = 0;
	barrier.size = size;

	qvkCmdPipelineBarrier( cb, src_stages, dst_stages, 0, 0, NULL, 1, &barrier, 0, NULL );
}


void vk_create_image(int width, int height, VkFormat format, int mip_levels, VkSamplerAddressMode address_mode, image_t *image) {
	// create image
	{
		VkImageCreateInfo desc;

		desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.format = format;
		desc.extent.width = width;
		desc.extent.height = height;
		desc.extent.depth = 1;
		desc.mipLevels = mip_levels;
		desc.arrayLayers = 1;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		desc.queueFamilyIndexCount = 0;
		desc.pQueueFamilyIndices = NULL;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VK_CHECK( qvkCreateImage( vk.device, &desc, NULL, &image->handle ) );
		allocate_and_bind_image_memory( image->handle );
	}

	// create image view
	{
		VkImageViewCreateInfo desc;
		
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.image = image->handle;
		desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		desc.format = format;
		desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.subresourceRange.baseMipLevel = 0;
		desc.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		desc.subresourceRange.baseArrayLayer = 0;
		desc.subresourceRange.layerCount = 1;

		VK_CHECK( qvkCreateImageView( vk.device, &desc, NULL, &image->view ) );
	}

	// create associated descriptor set
	if ( image->descriptor == VK_NULL_HANDLE )
	{
		VkDescriptorSetAllocateInfo desc;
		
		desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		desc.pNext = NULL;
		desc.descriptorPool = vk.descriptor_pool;
		desc.descriptorSetCount = 1;
		desc.pSetLayouts = &vk.set_layout_sampler;

		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &desc, &image->descriptor ) );
	}

	vk_update_descriptor_set( image->descriptor, image->view, mip_levels > 1 ? qtrue : qfalse, address_mode );
}


void vk_upload_image_data(VkImage image, int x, int y, int width, int height, qboolean mipmap, const uint8_t *pixels, int bytes_per_pixel) {

	VkCommandBuffer command_buffer;
	VkBufferImageCopy regions[16];
	VkBufferImageCopy region;

	int num_regions = 0;
	int buffer_size = 0;

	while (qtrue) {
		Com_Memset(&region, 0, sizeof(region));
		region.bufferOffset = buffer_size;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = num_regions;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset.x = x;
		region.imageOffset.y = y;
		region.imageOffset.z = 0;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;

		regions[num_regions] = region;
		num_regions++;

		buffer_size += width * height * bytes_per_pixel;

		if ( !mipmap || (width == 1 && height == 1) || num_regions >= ARRAY_LEN( regions ) )
			break;

		x >>= 1;
		y >>= 1;

		width >>= 1;
		if (width < 1) width = 1;

		height >>= 1;
		if (height < 1) height = 1;
	}

	ensure_staging_buffer_allocation(buffer_size);
	Com_Memcpy( vk_world.staging_buffer_ptr, pixels, buffer_size );

	command_buffer = begin_command_buffer();

	record_buffer_memory_barrier( command_buffer, vk_world.staging_buffer, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT );
	record_image_layout_transition( command_buffer, image, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	qvkCmdCopyBufferToImage( command_buffer, vk_world.staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_regions, regions );
	record_image_layout_transition( command_buffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

	end_command_buffer( command_buffer );
}


void vk_update_descriptor_set(VkDescriptorSet set, VkImageView image_view, qboolean mipmap, VkSamplerAddressMode address_mode) {
	Vk_Sampler_Def sampler_def;
	VkDescriptorImageInfo image_info;
	VkWriteDescriptorSet descriptor_write;

	Com_Memset(&sampler_def, 0, sizeof(sampler_def));
	sampler_def.address_mode = address_mode;

	if (mipmap) {
		sampler_def.gl_mag_filter = gl_filter_max;
		sampler_def.gl_min_filter = gl_filter_min;
	} else {
		sampler_def.gl_mag_filter = GL_LINEAR;
		sampler_def.gl_min_filter = GL_LINEAR;
	}

	image_info.sampler = vk_find_sampler(&sampler_def);
	image_info.imageView = image_view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptor_write.dstSet = set;
	descriptor_write.dstBinding = 0;
	descriptor_write.dstArrayElement = 0;
	descriptor_write.descriptorCount = 1;
	descriptor_write.pNext = NULL;
	descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptor_write.pImageInfo = &image_info;
	descriptor_write.pBufferInfo = NULL;
	descriptor_write.pTexelBufferView = NULL;

	qvkUpdateDescriptorSets(vk.device, 1, &descriptor_write, 0, NULL);
}


static void set_shader_stage_desc(VkPipelineShaderStageCreateInfo *desc, VkShaderStageFlagBits stage, VkShaderModule shader_module, const char *entry) {
	desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	desc->pNext = NULL;
	desc->flags = 0;
	desc->stage = stage;
	desc->module = shader_module;
	desc->pName = entry;
	desc->pSpecializationInfo = NULL;
}


void vk_create_gamma_pipeline( void ) {
	VkPipelineShaderStageCreateInfo shader_stages[2];
	VkPipelineVertexInputStateCreateInfo vertex_input_state;
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
	VkPipelineRasterizationStateCreateInfo rasterization_state;
	VkPipelineViewportStateCreateInfo viewport_state;
	VkPipelineMultisampleStateCreateInfo multisample_state;
	VkPipelineColorBlendStateCreateInfo blend_state;
	VkPipelineColorBlendAttachmentState attachment_blend_state;
	VkGraphicsPipelineCreateInfo create_info;
	VkViewport viewport;
	VkRect2D scissor;
	float frag_spec_data[3]; // gamma,overbright,greyscale
	VkSpecializationMapEntry spec_entries[3];
	VkSpecializationInfo frag_spec_info;

	if ( vk.gamma_pipeline ) {
		vk_wait_idle();
		qvkDestroyPipeline( vk.device, vk.gamma_pipeline, NULL );
		vk.gamma_pipeline = VK_NULL_HANDLE;
	}

	vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state.pNext = NULL;
	vertex_input_state.flags = 0;
	vertex_input_state.vertexBindingDescriptionCount = 0;
	vertex_input_state.pVertexBindingDescriptions = NULL;
	vertex_input_state.vertexAttributeDescriptionCount = 0;
	vertex_input_state.pVertexBindingDescriptions = NULL;

	// shaders
	set_shader_stage_desc( shader_stages+0, VK_SHADER_STAGE_VERTEX_BIT, vk.modules.gamma_vs, "main" );
	set_shader_stage_desc( shader_stages+1, VK_SHADER_STAGE_FRAGMENT_BIT, vk.modules.gamma_fs, "main" );

	frag_spec_data[0] = 1.0 / (r_gamma->value);
	frag_spec_data[1] = (float)(1 << tr.overbrightBits);
	frag_spec_data[2] = r_greyscale->value;

	spec_entries[0].constantID = 0;
	spec_entries[0].offset = 0 * sizeof( float );
	spec_entries[0].size = sizeof( float );
	
	spec_entries[1].constantID = 1;
	spec_entries[1].offset = 1 * sizeof( float );
	spec_entries[1].size = sizeof( float );

	spec_entries[2].constantID = 2;
	spec_entries[2].offset = 2 * sizeof( float );
	spec_entries[2].size = sizeof( float );

	frag_spec_info.mapEntryCount = 3;
	frag_spec_info.pMapEntries = spec_entries;
	frag_spec_info.dataSize = 3 * sizeof( float );
	frag_spec_info.pData = &frag_spec_data[0];

	shader_stages[1].pSpecializationInfo = &frag_spec_info;

	//
	// Primitive assembly.
	//
	input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly_state.pNext = NULL;
	input_assembly_state.flags = 0;
	input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	input_assembly_state.primitiveRestartEnable = VK_FALSE;

	//
	// Viewport.
	//
	viewport.x = 0.0 + vk.blitX0;
	viewport.y = 0.0 + vk.blitY0;
	viewport.width = vk.windowWidth - vk.blitX0 * 2;
	viewport.height = vk.windowHeight - vk.blitY0 * 2;
	viewport.minDepth = 0.0;
	viewport.maxDepth = 1.0;

	scissor.offset.x = viewport.x;
	scissor.offset.y = viewport.y;
	scissor.extent.width = viewport.width;
	scissor.extent.height = viewport.height;

	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.pNext = NULL;
	viewport_state.flags = 0;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	//
	// Rasterization.
	//
	rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state.pNext = NULL;
	rasterization_state.flags = 0;
	rasterization_state.depthClampEnable = VK_FALSE;
	rasterization_state.rasterizerDiscardEnable = VK_FALSE;
	rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
	//rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT; // VK_CULL_MODE_NONE;
	rasterization_state.cullMode = VK_CULL_MODE_NONE;
	rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE; // Q3 defaults to clockwise vertex order
	rasterization_state.depthBiasEnable = VK_FALSE;
	rasterization_state.depthBiasConstantFactor = 0.0f;
	rasterization_state.depthBiasClamp = 0.0f;
	rasterization_state.depthBiasSlopeFactor = 0.0f;
	rasterization_state.lineWidth = 1.0f;

	multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state.pNext = NULL;
	multisample_state.flags = 0;
	multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisample_state.sampleShadingEnable = VK_FALSE;
	multisample_state.minSampleShading = 1.0f;
	multisample_state.pSampleMask = NULL;
	multisample_state.alphaToCoverageEnable = VK_FALSE;
	multisample_state.alphaToOneEnable = VK_FALSE;

	//depth_stencil_state.minDepthBounds = 0.0;
	//depth_stencil_state.maxDepthBounds = 1.0;

	Com_Memset(&attachment_blend_state, 0, sizeof(attachment_blend_state));
	attachment_blend_state.blendEnable = VK_FALSE;
	attachment_blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend_state.pNext = NULL;
	blend_state.flags = 0;
	blend_state.logicOpEnable = VK_FALSE;
	blend_state.logicOp = VK_LOGIC_OP_COPY;
	blend_state.attachmentCount = 1;
	blend_state.pAttachments = &attachment_blend_state;
	blend_state.blendConstants[0] = 0.0f;
	blend_state.blendConstants[1] = 0.0f;
	blend_state.blendConstants[2] = 0.0f;
	blend_state.blendConstants[3] = 0.0f;

	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.pNext = NULL;
	create_info.flags = 0;
	create_info.stageCount = 2;
	create_info.pStages = shader_stages;
	create_info.pVertexInputState = &vertex_input_state;
	create_info.pInputAssemblyState = &input_assembly_state;
	create_info.pTessellationState = NULL;
	create_info.pViewportState = &viewport_state;
	create_info.pRasterizationState = &rasterization_state;
	create_info.pMultisampleState = &multisample_state;
	create_info.pDepthStencilState = NULL;
	create_info.pColorBlendState = &blend_state;
	create_info.pDynamicState = NULL;
	create_info.layout = vk.pipeline_layout_gamma;
	create_info.renderPass = vk.render_pass_gamma;
	create_info.subpass = 0;
	create_info.basePipelineHandle = VK_NULL_HANDLE;
	create_info.basePipelineIndex = -1;

	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &create_info, NULL, &vk.gamma_pipeline ) );
}


static VkVertexInputBindingDescription bindings[5];
static VkVertexInputAttributeDescription attribs[5];
static uint32_t num_binds;
static uint32_t num_attrs;

static void push_bind( uint32_t binding, uint32_t stride )
{
	bindings[ num_binds ].binding = binding;
	bindings[ num_binds ].stride = stride;
	bindings[ num_binds ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	num_binds++;
}

static void push_attr( uint32_t location, uint32_t binding, VkFormat format )
{
	attribs[ num_attrs ].location = location;
	attribs[ num_attrs ].binding = binding;
	attribs[ num_attrs ].format = format;
	attribs[ num_attrs ].offset = 0;
	num_attrs++;
}


VkPipeline create_pipeline( const Vk_Pipeline_Def *def, uint32_t renderPassIndex ) {
	VkShaderModule *vs_module = NULL;
	VkShaderModule *fs_module = NULL;
	int32_t vert_spec_data[1]; // clippping
	floatint_t frag_spec_data[6]; // alpha-test-func, alpha-test-value, depth-fragment, alpha-to-coverage, color_mode, abs_light
	VkSpecializationMapEntry spec_entries[7];
	VkSpecializationInfo vert_spec_info;
	VkSpecializationInfo frag_spec_info;
	VkPipelineVertexInputStateCreateInfo vertex_input_state;
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
	VkPipelineRasterizationStateCreateInfo rasterization_state;
	VkPipelineViewportStateCreateInfo viewport_state;
	VkPipelineMultisampleStateCreateInfo multisample_state;
	VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
	VkPipelineColorBlendStateCreateInfo blend_state;
	VkPipelineColorBlendAttachmentState attachment_blend_state;
	VkPipelineDynamicStateCreateInfo dynamic_state;
	VkDynamicState dynamic_state_array[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkGraphicsPipelineCreateInfo create_info;
	VkPipeline pipeline;
	VkPipelineShaderStageCreateInfo shader_stages[2];
	VkBool32 alphaToCoverage = VK_FALSE;
	unsigned int atest_bits;
	unsigned int state_bits = def->state_bits;

	switch ( def->shader_type ) {
		case TYPE_SIGNLE_TEXTURE:
			vs_module = &vk.modules.st_clip_vs[0];
			fs_module = &vk.modules.st_fs[0];
			break;
		case TYPE_SIGNLE_TEXTURE_DF:
			state_bits |= GLS_DEPTHMASK_TRUE;
			vs_module = &vk.modules.st_clip_vs[0];
			fs_module = &vk.modules.st_df_fs;
			break;
		case TYPE_SIGNLE_TEXTURE_ENVIRO:
			vs_module = &vk.modules.st_enviro_vs[0];
			fs_module = &vk.modules.st_fs[0];
			break;
		case TYPE_SIGNLE_TEXTURE_LIGHTING:
			vs_module = &vk.modules.light.vs_clip[0];
			fs_module = &vk.modules.light.fs[0];
			break;
		case TYPE_SIGNLE_TEXTURE_LIGHTING1:
			vs_module = &vk.modules.light.vs_clip[0];
			fs_module = &vk.modules.light1.fs[0];
			break;
		case TYPE_MULTI_TEXTURE_MUL:
		case TYPE_MULTI_TEXTURE_ADD:
			vs_module = &vk.modules.mt_clip_vs[0];
			fs_module = (def->shader_type == TYPE_MULTI_TEXTURE_MUL) ? &vk.modules.mt_mul_fs[0] : &vk.modules.mt_add_fs[0];
			break;
		case TYPE_COLOR_WHITE:
		case TYPE_COLOR_GREEN:
		case TYPE_COLOR_RED:
			vs_module = &vk.modules.color_clip_vs;
			fs_module = &vk.modules.color_fs;
			break;
		case TYPE_FOG_ONLY:
			vs_module = &vk.modules.fog_vs;
			fs_module = &vk.modules.fog_fs;
			break;
		case TYPE_DOT:
			vs_module = &vk.modules.dot_vs;
			fs_module = &vk.modules.dot_fs;
			break;
		default:
			ri.Error(ERR_DROP, "create_pipeline: unknown shader type %i\n", def->shader_type);
			return 0;
	}

	if ( def->fog_stage ) {
		switch ( def->shader_type ) {
			case TYPE_FOG_ONLY:
			case TYPE_DOT:
			case TYPE_SIGNLE_TEXTURE_DF:
			case TYPE_COLOR_WHITE:
			case TYPE_COLOR_GREEN:
			case TYPE_COLOR_RED:
				break;
			default:
				// switch to fogged modules
				vs_module++;
				fs_module++;
				break;
		}
	}

	set_shader_stage_desc(shader_stages+0, VK_SHADER_STAGE_VERTEX_BIT, *vs_module, "main");
	set_shader_stage_desc(shader_stages+1, VK_SHADER_STAGE_FRAGMENT_BIT, *fs_module, "main");

	Com_Memset( vert_spec_data, 0, sizeof( vert_spec_data ) );
	Com_Memset( frag_spec_data, 0, sizeof( frag_spec_data ) );
	
	vert_spec_data[0] = def->clipping_plane ? 1 : 0;

	// fragment shader specialization data
	atest_bits = state_bits & GLS_ATEST_BITS;
	switch ( atest_bits ) {
		case GLS_ATEST_GT_0:
			frag_spec_data[0].i = 1; // not equal
			frag_spec_data[1].f = 0.0f;
			break;
		case GLS_ATEST_LT_80:
			frag_spec_data[0].i = 2; // less than
			frag_spec_data[1].f = 0.5f;
			break;
		case GLS_ATEST_GE_80:
			frag_spec_data[0].i = 3; // greater or equal
			frag_spec_data[1].f = 0.5f;
			break;
		default:
			frag_spec_data[0].i = 0;
			frag_spec_data[1].f = 0.0f;
			break;
	};

	frag_spec_data[2].f = 0.85f;

	if ( r_ext_alpha_to_coverage->integer && vkSamples != VK_SAMPLE_COUNT_1_BIT && frag_spec_data[0].i ) {
		frag_spec_data[3].i = 1;
		alphaToCoverage = VK_TRUE;
	}

	switch ( def->shader_type ) {
		default: frag_spec_data[4].i = 0; break;
		case TYPE_COLOR_GREEN: frag_spec_data[4].i = 1; break;
		case TYPE_COLOR_RED:   frag_spec_data[4].i = 2; break;
	}

	switch ( def->shader_type ) {
		case TYPE_SIGNLE_TEXTURE_LIGHTING:
		case TYPE_SIGNLE_TEXTURE_LIGHTING1:
			frag_spec_data[5].i = def->abs_light ? 1 : 0;
		default:
			break;
	}

	//
	// vertex module specialization data
	//

	spec_entries[0].constantID = 0; // clip_plane
	spec_entries[0].offset = 0 * sizeof( int32_t );
	spec_entries[0].size = sizeof( int32_t );

	vert_spec_info.mapEntryCount = 1;
	vert_spec_info.pMapEntries = spec_entries + 0;
	vert_spec_info.dataSize = 1 * sizeof( int32_t );
	vert_spec_info.pData = &vert_spec_data[0];
	shader_stages[0].pSpecializationInfo = &vert_spec_info;

	//
	// fragment module specialization data
	//

	spec_entries[1].constantID = 0;  // alpha-test-function
	spec_entries[1].offset = 0 * sizeof( int32_t );
	spec_entries[1].size = sizeof( int32_t );

	spec_entries[2].constantID = 1; // alpha-test-value
	spec_entries[2].offset = 1 * sizeof( int32_t );
	spec_entries[2].size = sizeof( float );

	spec_entries[3].constantID = 2; // depth-fragment
	spec_entries[3].offset = 2 * sizeof( int32_t );
	spec_entries[3].size = sizeof( float );

	spec_entries[4].constantID = 3; // alpha-to-coverage
	spec_entries[4].offset = 3 * sizeof( int32_t );
	spec_entries[4].size = sizeof( int32_t );

	spec_entries[5].constantID = 4; // color_mode
	spec_entries[5].offset = 4 * sizeof( int32_t );
	spec_entries[5].size = sizeof( int32_t );

	spec_entries[6].constantID = 5; // abs_light
	spec_entries[6].offset = 5 * sizeof( int32_t );
	spec_entries[6].size = sizeof( int32_t );

	frag_spec_info.mapEntryCount = 6;
	frag_spec_info.pMapEntries = spec_entries + 1;
	frag_spec_info.dataSize = sizeof( int32_t ) * 6;
	frag_spec_info.pData = &frag_spec_data[0];
	shader_stages[1].pSpecializationInfo = &frag_spec_info;

	//
	// Vertex input
	//
	num_binds = num_attrs = 0;
	switch ( def->shader_type ) {
		case TYPE_FOG_ONLY:
		case TYPE_DOT:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
				break;
		case TYPE_SIGNLE_TEXTURE:
		case TYPE_SIGNLE_TEXTURE_DF:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
				break;
		case TYPE_SIGNLE_TEXTURE_ENVIRO:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			push_bind( 4, sizeof( vec4_t ) );					// normals
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 4, 4, VK_FORMAT_R32G32B32A32_SFLOAT );
				break;
		case TYPE_SIGNLE_TEXTURE_LIGHTING:
		case TYPE_SIGNLE_TEXTURE_LIGHTING1:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 4, sizeof( vec4_t ) );					// normals array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 4, 4, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;
		case TYPE_COLOR_WHITE:
		case TYPE_COLOR_GREEN:
		case TYPE_COLOR_RED:
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			break;
		default: // multitexture variations
			push_bind( 0, sizeof( vec4_t ) );					// xyz array
			push_bind( 1, sizeof( color4ub_t ) );				// color array
			push_bind( 2, sizeof( vec2_t ) );					// st0 array
			push_bind( 3, sizeof( vec2_t ) );					// st1 array
			push_attr( 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT );
			push_attr( 1, 1, VK_FORMAT_R8G8B8A8_UNORM );
			push_attr( 2, 2, VK_FORMAT_R32G32_SFLOAT );
			push_attr( 3, 3, VK_FORMAT_R32G32_SFLOAT );
			break;
	}
	vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state.pNext = NULL;
	vertex_input_state.flags = 0;
	vertex_input_state.pVertexBindingDescriptions = bindings;
	vertex_input_state.pVertexAttributeDescriptions = attribs;
	vertex_input_state.vertexBindingDescriptionCount = num_binds;
	vertex_input_state.vertexAttributeDescriptionCount = num_attrs;

	//
	// Primitive assembly.
	//
	input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly_state.pNext = NULL;
	input_assembly_state.flags = 0;
	if ( def->shader_type == TYPE_DOT )
		input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	else
		input_assembly_state.topology = def->line_primitives ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly_state.primitiveRestartEnable = VK_FALSE;

	//
	// Viewport.
	//
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.pNext = NULL;
	viewport_state.flags = 0;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = NULL; // dynamic viewport state
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = NULL; // dynamic scissor state

	//
	// Rasterization.
	//
	rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state.pNext = NULL;
	rasterization_state.flags = 0;
	rasterization_state.depthClampEnable = VK_FALSE;
	rasterization_state.rasterizerDiscardEnable = VK_FALSE;
	if ( def->shader_type == TYPE_DOT ) {
		rasterization_state.polygonMode = VK_POLYGON_MODE_POINT;
	} else {
		rasterization_state.polygonMode = (state_bits & GLS_POLYMODE_LINE) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		rasterization_state.rasterizerDiscardEnable = VK_FALSE;
	}

	switch ( def->face_culling ) {
		case CT_TWO_SIDED:
			rasterization_state.cullMode = VK_CULL_MODE_NONE;
			break;
		case CT_FRONT_SIDED:
			rasterization_state.cullMode = (def->mirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
			break;
		case CT_BACK_SIDED:
			rasterization_state.cullMode = (def->mirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT);
			break;
		default:
			ri.Error( ERR_DROP, "create_pipeline: invalid face culling mode %i\n", def->face_culling );
			break;
	}

	rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE; // Q3 defaults to clockwise vertex order

	 // depth bias state
	if ( def->polygon_offset ) {
		rasterization_state.depthBiasEnable = VK_TRUE;
		rasterization_state.depthBiasConstantFactor = r_offsetUnits->value;
		rasterization_state.depthBiasClamp = 0.0f;
		rasterization_state.depthBiasSlopeFactor = r_offsetFactor->value;
	} else {
		rasterization_state.depthBiasEnable = VK_FALSE;
		rasterization_state.depthBiasConstantFactor = 0.0f;
		rasterization_state.depthBiasClamp = 0.0f;
		rasterization_state.depthBiasSlopeFactor = 0.0f;
	}

	if ( def->line_width )
		rasterization_state.lineWidth = (float)def->line_width;
	else
		rasterization_state.lineWidth = 1.0f;

	multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state.pNext = NULL;
	multisample_state.flags = 0;

	multisample_state.rasterizationSamples = (vk.renderPassIndex == RENDER_PASS_SCREENMAP) ? vk.screenMapSamples : vkSamples;

	multisample_state.sampleShadingEnable = VK_FALSE;
	multisample_state.minSampleShading = 1.0f;
	multisample_state.pSampleMask = NULL;
	multisample_state.alphaToCoverageEnable = alphaToCoverage;
	multisample_state.alphaToOneEnable = VK_FALSE;

	Com_Memset( &depth_stencil_state, 0, sizeof( depth_stencil_state ) );

	depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil_state.pNext = NULL;
	depth_stencil_state.flags = 0;
	depth_stencil_state.depthTestEnable = (state_bits & GLS_DEPTHTEST_DISABLE) ? VK_FALSE : VK_TRUE;
	depth_stencil_state.depthWriteEnable = (state_bits & GLS_DEPTHMASK_TRUE) ? VK_TRUE : VK_FALSE;
	depth_stencil_state.depthCompareOp = (state_bits & GLS_DEPTHFUNC_EQUAL) ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
	depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
	depth_stencil_state.stencilTestEnable = (def->shadow_phase != SHADOW_DISABLED) ? VK_TRUE : VK_FALSE;

	if (def->shadow_phase == SHADOW_EDGES) {
		depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.passOp = (def->face_culling == CT_FRONT_SIDED) ? VK_STENCIL_OP_INCREMENT_AND_CLAMP : VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.compareOp = VK_COMPARE_OP_ALWAYS;
		depth_stencil_state.front.compareMask = 255;
		depth_stencil_state.front.writeMask = 255;
		depth_stencil_state.front.reference = 0;

		depth_stencil_state.back = depth_stencil_state.front;

	}  else if (def->shadow_phase == SHADOW_FS_QUAD) {
		depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.passOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_KEEP;
		depth_stencil_state.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
		depth_stencil_state.front.compareMask = 255;
		depth_stencil_state.front.writeMask = 255;
		depth_stencil_state.front.reference = 0;

		depth_stencil_state.back = depth_stencil_state.front;
	}

	depth_stencil_state.minDepthBounds = 0.0f;
	depth_stencil_state.maxDepthBounds = 1.0f;

	Com_Memset(&attachment_blend_state, 0, sizeof(attachment_blend_state));
	attachment_blend_state.blendEnable = (state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) ? VK_TRUE : VK_FALSE;

	if (def->shadow_phase == SHADOW_EDGES || def->shader_type == TYPE_SIGNLE_TEXTURE_DF)
		attachment_blend_state.colorWriteMask = 0;
	else
		attachment_blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	
	if (attachment_blend_state.blendEnable) {
		switch (state_bits & GLS_SRCBLEND_BITS) {
			case GLS_SRCBLEND_ZERO:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				break;
			case GLS_SRCBLEND_ONE:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ALPHA_SATURATE:
				attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
				break;
			default:
				ri.Error( ERR_DROP, "create_pipeline: invalid src blend state bits\n" );
				break;
		}
		switch (state_bits & GLS_DSTBLEND_BITS) {
			case GLS_DSTBLEND_ZERO:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				break;
			case GLS_DSTBLEND_ONE:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				break;
			default:
				ri.Error( ERR_DROP, "create_pipeline: invalid dst blend state bits\n" );
				break;
		}

		attachment_blend_state.srcAlphaBlendFactor = attachment_blend_state.srcColorBlendFactor;
		attachment_blend_state.dstAlphaBlendFactor = attachment_blend_state.dstColorBlendFactor;
		attachment_blend_state.colorBlendOp = VK_BLEND_OP_ADD;
		attachment_blend_state.alphaBlendOp = VK_BLEND_OP_ADD;
	}

	blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend_state.pNext = NULL;
	blend_state.flags = 0;
	blend_state.logicOpEnable = VK_FALSE;
	blend_state.logicOp = VK_LOGIC_OP_COPY;
	blend_state.attachmentCount = 1;
	blend_state.pAttachments = &attachment_blend_state;
	blend_state.blendConstants[0] = 0.0f;
	blend_state.blendConstants[1] = 0.0f;
	blend_state.blendConstants[2] = 0.0f;
	blend_state.blendConstants[3] = 0.0f;

	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state.pNext = NULL;
	dynamic_state.flags = 0;
	dynamic_state.dynamicStateCount = ARRAY_LEN( dynamic_state_array );
	dynamic_state.pDynamicStates = dynamic_state_array;

	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.pNext = NULL;
	create_info.flags = 0;
	create_info.stageCount = ARRAY_LEN(shader_stages);
	create_info.pStages = shader_stages;
	create_info.pVertexInputState = &vertex_input_state;
	create_info.pInputAssemblyState = &input_assembly_state;
	create_info.pTessellationState = NULL;
	create_info.pViewportState = &viewport_state;
	create_info.pRasterizationState = &rasterization_state;
	create_info.pMultisampleState = &multisample_state;
	create_info.pDepthStencilState = &depth_stencil_state;
	create_info.pColorBlendState = &blend_state;
	create_info.pDynamicState = &dynamic_state;

	//if ( def->shader_type == TYPE_DOT )
	//	create_info.layout = vk.pipeline_layout_storage;
	//else
		create_info.layout = vk.pipeline_layout;

	if ( renderPassIndex == RENDER_PASS_SCREENMAP )
		create_info.renderPass = vk.render_pass_screenmap;
	else
		create_info.renderPass = vk.render_pass;

	create_info.subpass = 0;
	create_info.basePipelineHandle = VK_NULL_HANDLE;
	create_info.basePipelineIndex = -1;

	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &create_info, NULL, &pipeline ) );

	vk.pipeline_create_count++;
		
	return pipeline;
}


uint32_t vk_alloc_pipeline( const Vk_Pipeline_Def *def ) {
	VK_Pipeline_t *pipeline;
	if ( vk.pipelines_count >= MAX_VK_PIPELINES ) {
		ri.Error( ERR_DROP, "alloc_pipeline: MAX_VK_PIPELINES reached" );
		return 0;
	} else {
		int j;
		pipeline = &vk.pipelines[ vk.pipelines_count ];
		pipeline->def = *def;
		for ( j = 0; j < RENDER_PASS_COUNT; j++ ) {
			pipeline->handle[j] = VK_NULL_HANDLE;
		}
		return vk.pipelines_count++;
	}
}


VkPipeline vk_gen_pipeline( uint32_t index ) {
	if ( index < vk.pipelines_count ) {
		VK_Pipeline_t *pipeline = vk.pipelines + index;
		if ( pipeline->handle[ vk.renderPassIndex ] == VK_NULL_HANDLE )
			pipeline->handle[ vk.renderPassIndex ] = create_pipeline( &pipeline->def, vk.renderPassIndex );
		return pipeline->handle[ vk.renderPassIndex ];
	} else {
		return VK_NULL_HANDLE;
	}
}


VkSampler vk_find_sampler( const Vk_Sampler_Def *def ) {
	VkSamplerAddressMode address_mode;
	VkSamplerCreateInfo desc;
	VkSampler sampler;
	VkFilter mag_filter;
	VkFilter min_filter;
	VkSamplerMipmapMode mipmap_mode;
	qboolean max_lod_0_25 = qfalse; // used to emulate OpenGL's GL_LINEAR/GL_NEAREST minification filter
	int i;

	// Look for sampler among existing samplers.
	for (i = 0; i < vk_world.num_samplers; i++) {
		const Vk_Sampler_Def *cur_def = &vk_world.sampler_defs[i];
		if ( memcmp( cur_def, def, sizeof( *def ) ) == 0 )
		{
			return vk_world.samplers[i];
		}
	}

	// Create new sampler.
	if ( vk_world.num_samplers >= MAX_VK_SAMPLERS ) {
		ri.Error( ERR_DROP, "vk_find_sampler: MAX_VK_SAMPLERS hit\n" );
	}

	address_mode = def->address_mode;

	if (def->gl_mag_filter == GL_NEAREST) {
		mag_filter = VK_FILTER_NEAREST;
	} else if (def->gl_mag_filter == GL_LINEAR) {
		mag_filter = VK_FILTER_LINEAR;
	} else {
		ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_mag_filter");
		return VK_NULL_HANDLE;
	}

	if (def->gl_min_filter == GL_NEAREST) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		max_lod_0_25 = qtrue;
	} else if (def->gl_min_filter == GL_LINEAR) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		max_lod_0_25 = qtrue;
	} else if (def->gl_min_filter == GL_NEAREST_MIPMAP_NEAREST) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if (def->gl_min_filter == GL_LINEAR_MIPMAP_NEAREST) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	} else if (def->gl_min_filter == GL_NEAREST_MIPMAP_LINEAR) {
		min_filter = VK_FILTER_NEAREST;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	} else if (def->gl_min_filter == GL_LINEAR_MIPMAP_LINEAR) {
		min_filter = VK_FILTER_LINEAR;
		mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	} else {
		ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_min_filter");
		return VK_NULL_HANDLE;
	}

	desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.magFilter = mag_filter;
	desc.minFilter = min_filter;
	desc.mipmapMode = mipmap_mode;
	desc.addressModeU = address_mode;
	desc.addressModeV = address_mode;
	desc.addressModeW = address_mode;
	desc.mipLodBias = 0.0f;

	if ( def->noAnisotropy ) {
		desc.anisotropyEnable = VK_FALSE;
		desc.maxAnisotropy = 1.0f;
	} else {
		desc.anisotropyEnable = (r_ext_texture_filter_anisotropic->integer && vk.samplerAnisotropy) ? VK_TRUE : VK_FALSE;
		if ( desc.anisotropyEnable ) {
			desc.maxAnisotropy = MIN( r_ext_max_anisotropy->integer, vk.maxAnisotropy );
		}
	}

	desc.compareEnable = VK_FALSE;
	desc.compareOp = VK_COMPARE_OP_ALWAYS;
	desc.minLod = 0.0f;
	desc.maxLod = (def->max_lod_1_0) ? 1.0f : (max_lod_0_25 ? 0.25f : vk.maxLodBias);
	desc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	desc.unnormalizedCoordinates = VK_FALSE;

	VK_CHECK( qvkCreateSampler( vk.device, &desc, NULL, &sampler ) );

	vk_world.sampler_defs[vk_world.num_samplers] = *def;
	vk_world.samplers[vk_world.num_samplers] = sampler;
	vk_world.num_samplers++;

	return sampler;
}


uint32_t vk_find_pipeline_ext( uint32_t base, const Vk_Pipeline_Def *def, qboolean use ) {
	const Vk_Pipeline_Def *cur_def;
	uint32_t index;

	for ( index = base; index < vk.pipelines_count; index++ ) {
		cur_def = &vk.pipelines[ index ].def;
		if ( memcmp( cur_def, def, sizeof( *def ) ) == 0 ) {
			goto found;
		}
	}

	index = vk_alloc_pipeline( def );
found:

	if ( use )
		vk_gen_pipeline( index );

	return index;
}


void vk_get_pipeline_def( uint32_t pipeline, Vk_Pipeline_Def *def ) {
	if ( pipeline >= vk.pipelines_count ) {
		Com_Memset( def, 0, sizeof( *def ) );
	} else {
		Com_Memcpy( def, &vk.pipelines[ pipeline ].def, sizeof( *def ) );
	}
}


static void get_viewport_rect(VkRect2D *r)
{
	if ( backEnd.projection2D )
	{
		r->offset.x = 0;
		r->offset.y = 0;
		r->extent.width = vk.renderWidth;
		r->extent.height = vk.renderHeight;
	}
	else
	{
		r->offset.x = backEnd.viewParms.viewportX * vk.renderScaleX;
		r->offset.y = vk.renderHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight) * vk.renderScaleY;
		r->extent.width = (float)backEnd.viewParms.viewportWidth * vk.renderScaleX;
		r->extent.height = (float)backEnd.viewParms.viewportHeight * vk.renderScaleY;
	}
}

static void get_viewport(VkViewport *viewport, Vk_Depth_Range depth_range) {
	VkRect2D r;
		
	get_viewport_rect( &r );

	viewport->x = (float)r.offset.x;
	viewport->y = (float)r.offset.y;
	viewport->width = (float)r.extent.width;
	viewport->height = (float)r.extent.height;

	switch ( depth_range ) {
		default:
		//case DEPTH_RANGE_NORMAL:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_ZERO:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 0.0f;
			break;
		case DEPTH_RANGE_ONE:
			viewport->minDepth = 1.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_WEAPON:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 0.3f;
			break;
	}
}

static void get_scissor_rect(VkRect2D *r) {

	if ( backEnd.viewParms.portalView != PV_NONE )
	{
		r->offset.x = backEnd.viewParms.scissorX;
		r->offset.y = glConfig.vidHeight - backEnd.viewParms.scissorY - backEnd.viewParms.scissorHeight;
		r->extent.width = backEnd.viewParms.scissorWidth;
		r->extent.height = backEnd.viewParms.scissorHeight;
	}
	else
	{
		get_viewport_rect(r);

		if (r->offset.x < 0)
			r->offset.x = 0;
		if (r->offset.y < 0)
			r->offset.y = 0;

		if (r->offset.x + r->extent.width > glConfig.vidWidth)
			r->extent.width = glConfig.vidWidth - r->offset.x;
		if (r->offset.y + r->extent.height > glConfig.vidHeight)
			r->extent.height = glConfig.vidHeight - r->offset.y;
	}
}


static void get_mvp_transform( float *mvp )
{
	if ( backEnd.projection2D )
	{
		float mvp0 = 2.0f / glConfig.vidWidth;
		float mvp5 = 2.0f / glConfig.vidHeight;

		mvp[0]  =  mvp0; mvp[1]  =  0.0f; mvp[2]  = 0.0f; mvp[3]  = 0.0f;
		mvp[4]  =  0.0f; mvp[5]  =  mvp5; mvp[6]  = 0.0f; mvp[7]  = 0.0f;
		mvp[8]  =  0.0f; mvp[9]  =  0.0f; mvp[10] = 1.0f; mvp[11] = 0.0f;
		mvp[12] = -1.0f; mvp[13] = -1.0f; mvp[14] = 0.0f; mvp[15] = 1.0f;
	}
	else
	{
		const float *p = backEnd.viewParms.projectionMatrix;
#if 0
		myGlMultMatrix( vk_world.modelview_transform, p, mvp );
#else
		// update q3's proj matrix (opengl) to vulkan conventions: z - [0, 1] instead of [-1, 1] and invert y direction
		float zNear	= r_znear->value;
		float zFar = backEnd.viewParms.zFar;
		float P10 = -zFar / (zFar - zNear);
		float P14 = -zFar*zNear / (zFar - zNear);
		float P5 = -p[5];
		float proj[16];

		Com_Memcpy( proj, p, 64 );
		proj[5] = P5;
		proj[10] = P10;
		proj[14] = P14;

		myGlMultMatrix(vk_world.modelview_transform, proj, mvp);
#endif
	}
}


void vk_clear_color( const vec4_t color ) {
	
	VkClearAttachment attachment;
	VkClearRect clear_rect[2];
	uint32_t rect_count;

	if ( !vk.active )
		return;

	attachment.colorAttachment = 0;
	attachment.clearValue.color.float32[0] = color[0];
	attachment.clearValue.color.float32[1] = color[1];
	attachment.clearValue.color.float32[2] = color[2];
	attachment.clearValue.color.float32[3] = color[3];
	attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	get_scissor_rect( &clear_rect[0].rect );
	clear_rect[0].baseArrayLayer = 0;
	clear_rect[0].layerCount = 1;
	rect_count = 1;
#ifdef DEBUG
	// Split viewport rectangle into two non-overlapping rectangles.
	// It's a HACK to prevent Vulkan validation layer's performance warning:
	//		"vkCmdClearAttachments() issued on command buffer object XXX prior to any Draw Cmds.
	//		 It is recommended you use RenderPass LOAD_OP_CLEAR on Attachments prior to any Draw."
	// 
	// NOTE: we don't use LOAD_OP_CLEAR for color attachment when we begin renderpass
	// since at that point we don't know whether we need collor buffer clear (usually we don't).
	{
		uint32_t h = clear_rect[0].rect.extent.height / 2;
		clear_rect[0].rect.extent.height = h;
		clear_rect[1] = clear_rect[0];
		clear_rect[1].rect.offset.y = h;
		rect_count = 2;
	}
#endif
	qvkCmdClearAttachments( vk.cmd->command_buffer, 1, &attachment, rect_count, clear_rect );
}


void vk_clear_depth( qboolean clear_stencil ) {
	
	VkClearAttachment attachment;
	VkClearRect clear_rect[1];

	if ( !vk.active )
		return;

	if ( !vk_world.dirty_depth_attachment )
		return;

	attachment.colorAttachment = 0;
	attachment.clearValue.depthStencil.depth = 1.0f;
	attachment.clearValue.depthStencil.stencil = 0;
	if ( clear_stencil ) {
		attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	} else {
		attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	get_scissor_rect( &clear_rect[0].rect );
	clear_rect[0].baseArrayLayer = 0;
	clear_rect[0].layerCount = 1;

	qvkCmdClearAttachments( vk.cmd->command_buffer, 1, &attachment, 1, clear_rect );
}


void vk_update_mvp( const float *m ) {
	float push_constants[16 + 12 + 4]; // mvp transform + eye transform + clipping plane in eye space
	int push_constants_size = 64;
	int i;

	//
	// Specify push constants.
	//
	if ( m )
		Com_Memcpy( push_constants, m, push_constants_size );
	else
		get_mvp_transform( push_constants );

	if ( backEnd.viewParms.portalView != PV_NONE ) {
		// Eye space transform.
		// NOTE: backEnd.or.modelMatrix incorporates s_flipMatrix, so it should be taken into account 
		// when computing clipping plane too.
		float* eye_xform = push_constants + 16;
		float world_plane[4];
		float eye_plane[4];
		for (i = 0; i < 12; i++) {
			eye_xform[i] = backEnd.or.modelMatrix[(i%4)*4 + i/4 ];
		}

		// Clipping plane in eye coordinates.
		world_plane[0] = backEnd.viewParms.portalPlane.normal[0];
		world_plane[1] = backEnd.viewParms.portalPlane.normal[1];
		world_plane[2] = backEnd.viewParms.portalPlane.normal[2];
		world_plane[3] = backEnd.viewParms.portalPlane.dist;

		eye_plane[0] = DotProduct (backEnd.viewParms.or.axis[0], world_plane);
		eye_plane[1] = DotProduct (backEnd.viewParms.or.axis[1], world_plane);
		eye_plane[2] = DotProduct (backEnd.viewParms.or.axis[2], world_plane);
		eye_plane[3] = DotProduct (world_plane, backEnd.viewParms.or.origin) - world_plane[3];

		// Apply s_flipMatrix to be in the same coordinate system as eye_xfrom.
		push_constants[28] = -eye_plane[1];
		push_constants[29] =  eye_plane[2];
		push_constants[30] = -eye_plane[0];
		push_constants[31] =  eye_plane[3];

		push_constants_size += 64;
	}

	qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, push_constants_size, push_constants );

	vk.stats.push_size += push_constants_size;
}


//static VkDeviceSize shade_offs[5];
static VkBuffer shade_bufs[5];
static int bind_base;
static int bind_count;

static void vk_bind_index( int index )
{
	if ( bind_base == -1 ) {
		bind_base = index;
		bind_count = 1;
	} else {
		bind_count = index - bind_base + 1;
	}
}


static void vk_bind_attr( int index, unsigned int item_size, const void *src ) {
	const uint32_t offset = PAD( vk.cmd->vertex_buffer_offset, 32 );
	const uint32_t size = tess.numVertexes * item_size;

	if ( offset + size > vk.geometry_buffer_size ) {
		// schedule geometry buffer resize
		vk.geometry_buffer_size_new = log2pad( offset + size, 1 );
	} else {
		vk.cmd->buf_offset[ index ] = offset;
		Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, src, size );
		vk.cmd->vertex_buffer_offset = (VkDeviceSize)offset + size;
	}

	vk_bind_index( index );
}


uint32_t vk_tess_index( uint32_t numIndexes, const void *src ) {
	const uint32_t offset = vk.cmd->vertex_buffer_offset;
	const uint32_t size = numIndexes * sizeof( tess.indexes[0] );

	if ( offset + size > vk.geometry_buffer_size ) {
		// schedule geometry buffer resize
		vk.geometry_buffer_size_new = log2pad( offset + size, 1 );
	} else {
		Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, src, size );
		vk.cmd->vertex_buffer_offset = (VkDeviceSize)offset + size;
	}

	return offset;
}


void vk_bind_index_buffer( VkBuffer buffer, uint32_t offset )
{
	if ( vk.cmd->curr_index_buffer != buffer || vk.cmd->curr_index_offset != offset )
		qvkCmdBindIndexBuffer( vk.cmd->command_buffer, buffer, offset, VK_INDEX_TYPE_UINT32 );

	vk.cmd->curr_index_buffer = buffer;
	vk.cmd->curr_index_offset = offset;
}


void vk_bind_geometry_ext( int flags )
{
	//unsigned int size;
	bind_base = -1;
	bind_count = 0;

	if ( !flags )
		return;

#ifdef USE_VBO
	if ( tess.vboIndex ) {
		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = vk.vbo.vertex_buffer;

		//if ( flags & TESS_IDX ) {  // index
			//qvkCmdBindIndexBuffer( vk.cmd->command_buffer, vk.vbo.index_buffer, tess.shader->iboOffset, VK_INDEX_TYPE_UINT32 );
		//}

		if ( flags & TESS_XYZ ) {  // 0
			vk.cmd->vbo_offset[0] = tess.shader->vboOffset + 0; 
			vk_bind_index( 0 );
		}

		if ( flags & TESS_RGBA ) { // 1
			vk.cmd->vbo_offset[1] = tess.shader->stages[ tess.vboStage ]->color_offset;
			vk_bind_index( 1 );
		}

		if ( flags & TESS_ST0 ) {  // 2
			vk.cmd->vbo_offset[2] = tess.shader->stages[ tess.vboStage ]->tex_offset[0];
			vk_bind_index( 2 );
		}

		if ( flags & TESS_ST1 ) {  // 3
			vk.cmd->vbo_offset[3] = tess.shader->stages[ tess.vboStage ]->tex_offset[1];
			vk_bind_index( 3 );
		}

		if ( flags & TESS_NNN ) {
			vk.cmd->vbo_offset[4] = tess.shader->normalOffset;
			vk_bind_index( 4 );
		}

		qvkCmdBindVertexBuffers( vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->vbo_offset + bind_base );

	} else
#endif // USE_VBO
	{
		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = vk.cmd->vertex_buffer;

		if ( flags & TESS_IDX ) {
			uint32_t offset = vk_tess_index( tess.numIndexes, tess.indexes );
			vk_bind_index_buffer( vk.cmd->vertex_buffer, offset );
		}

		if ( flags & TESS_XYZ ) {
			vk_bind_attr(0, sizeof(tess.xyz[0]), &tess.xyz[0]);
		}

		if ( flags & TESS_RGBA ) {
			vk_bind_attr(1, sizeof(tess.svars.colors[0]), tess.svars.colors);
		}

		if ( flags & TESS_ST0 ) {
			vk_bind_attr(2, sizeof(tess.svars.texcoords[0][0]), tess.svars.texcoordPtr[0]);
		}

		if ( flags & TESS_ST1 ) {
			vk_bind_attr(3, sizeof(tess.svars.texcoords[1][0]), tess.svars.texcoordPtr[1]);
		}

		if ( flags & TESS_NNN ) {
			vk_bind_attr(4, sizeof(tess.normal[0]), tess.normal);
		}

		qvkCmdBindVertexBuffers( vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->buf_offset + bind_base );
	}
}


void vk_reset_descriptor( int index )
{
	vk.cmd->descriptor_set.current[ index ] = VK_NULL_HANDLE;
}


void vk_update_descriptor( int index, VkDescriptorSet descriptor )
{
	if ( vk.cmd->descriptor_set.current[ index ] != descriptor ) {
		vk.cmd->descriptor_set.start = ( index < vk.cmd->descriptor_set.start ) ? index : vk.cmd->descriptor_set.start;
		vk.cmd->descriptor_set.end = ( index > vk.cmd->descriptor_set.end ) ? index : vk.cmd->descriptor_set.end;
	}
	vk.cmd->descriptor_set.current[ index ] = descriptor;
}


void vk_update_descriptor_offset( int index, uint32_t offset )
{
	vk.cmd->descriptor_set.offset[ index ] = offset;
}


void vk_bind_descriptor_sets( void )
{
	uint32_t offsets[2], offset_count;
	int start, end, count;

	start = vk.cmd->descriptor_set.start;
	if ( start == ~0U )
		return;

	end = vk.cmd->descriptor_set.end;

	offset_count = 0;
	if ( start == 0 ) { // uniform offset
		offsets[ offset_count++ ] = vk.cmd->descriptor_set.offset[ 0 ];
	}
	if ( end >= 4 ) { // storage offset
		offsets[ offset_count++ ] = vk.cmd->descriptor_set.offset[ 1 ];
	}

	count = end - start + 1;

	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, start, count, vk.cmd->descriptor_set.current + start, offset_count, offsets );

	vk.cmd->descriptor_set.end = 0;
	vk.cmd->descriptor_set.start = ~0U;
}


void vk_draw_geometry( uint32_t pipeline, Vk_Depth_Range depth_range, qboolean indexed ) {
	VkPipeline vkpipe;
	VkRect2D scissor_rect;
	VkViewport viewport;

	if ( vk.geometry_buffer_size_new ) {
		// geometry buffer overflow happened this frame
		return;
	}

	vk_bind_descriptor_sets();

	// bind pipeline
	vkpipe = vk_gen_pipeline( pipeline );
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkpipe );

	// configure pipeline's dynamic state
	if ( vk.cmd->depth_range != depth_range ) {
		vk.cmd->depth_range = depth_range;

		get_scissor_rect( &scissor_rect );
		qvkCmdSetScissor( vk.cmd->command_buffer, 0, 1, &scissor_rect );

		get_viewport( &viewport, depth_range );
		qvkCmdSetViewport( vk.cmd->command_buffer, 0, 1, &viewport );
	}

	// issue draw call(s)
#ifdef USE_VBO
	if ( tess.vboIndex )
		VBO_RenderIBOItems();
	else
#endif
	if ( indexed ) {
		qvkCmdDrawIndexed( vk.cmd->command_buffer, tess.numIndexes, 1, 0, 0, 0 );
	} else {
		qvkCmdDraw( vk.cmd->command_buffer, tess.numVertexes, 1, 0, 0 );
	}

	vk_world.dirty_depth_attachment = qtrue;
}


static void vk_begin_render_pass( VkRenderPass renderPass, VkFramebuffer frameBuffer, qboolean clearValues, uint32_t width, uint32_t height )
{
	VkRenderPassBeginInfo render_pass_begin_info;
	VkClearValue clear_values[3];

	// Begin render pass.

	render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin_info.pNext = NULL;
	render_pass_begin_info.renderPass = renderPass;
	render_pass_begin_info.framebuffer = frameBuffer;
	render_pass_begin_info.renderArea.offset.x = 0;
	render_pass_begin_info.renderArea.offset.y = 0;
	render_pass_begin_info.renderArea.extent.width = width; // TODO: renderWidth?
	render_pass_begin_info.renderArea.extent.height = height; // TODO: renderHeight?

	if ( clearValues ) {
		/// ignore clear_values[0] which corresponds to color attachment
		Com_Memset( clear_values, 0, sizeof( clear_values ) );
		clear_values[0].depthStencil.depth = 1.0;
		clear_values[1].depthStencil.depth = 1.0;
		render_pass_begin_info.clearValueCount = ARRAY_LEN( clear_values );
		render_pass_begin_info.pClearValues = clear_values;
	} else {
		render_pass_begin_info.clearValueCount = 0;
		render_pass_begin_info.pClearValues = NULL;
	}

	qvkCmdBeginRenderPass( vk.cmd->command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE );
}


void vk_begin_main_render_pass( void )
{
#ifdef USE_SINGLE_FBO
	VkFramebuffer frameBuffer = vk.framebuffers[ vk.swapchain_image_index ];
#else
	VkFramebuffer frameBuffer = vk.cmd->framebuffers[ vk.swapchain_image_index ];
#endif

	vk.renderPassIndex = RENDER_PASS_MAIN;

	vk.renderWidth = glConfig.vidWidth;
	vk.renderHeight = glConfig.vidHeight;

	vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;

	vk_begin_render_pass( vk.render_pass, frameBuffer, qtrue, vk.renderWidth, vk.renderHeight );
}


void vk_begin_screenmap_render_pass( void )
{
#ifdef USE_SINGLE_FBO
	VkFramebuffer frameBuffer = vk.framebuffers3[ vk.swapchain_image_index ];

	record_image_layout_transition( vk.cmd->command_buffer, vk.color_image3, VK_IMAGE_ASPECT_COLOR_BIT,
		0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
#else
	VkFramebuffer frameBuffer = vk.cmd->framebuffers3[ vk.swapchain_image_index ];

	record_image_layout_transition( vk.cmd->command_buffer, vk.cmd->color_image3, VK_IMAGE_ASPECT_COLOR_BIT,
		0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
#endif

	vk.renderPassIndex = RENDER_PASS_SCREENMAP;

	vk.renderWidth = vk.screenMapWidth;
	vk.renderHeight = vk.screenMapHeight;

	vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;

	vk_begin_render_pass( vk.render_pass_screenmap, frameBuffer, qtrue, vk.renderWidth, vk.renderHeight );
}


void vk_end_render_pass( void )
{
	qvkCmdEndRenderPass( vk.cmd->command_buffer );

//	vk.renderPassIndex = RENDER_PASS_MAIN;
}

static qboolean vk_find_drawsurfs( void )
{
	const void *curCmd = &backEndData->commands.cmds;
	const drawBufferCommand_t *db_cmd;

	for ( ;; ) {
		curCmd = PADP( curCmd, sizeof(void *) );
		switch ( *(const int *)curCmd ) {
			case RC_DRAW_BUFFER:
				db_cmd = (const drawBufferCommand_t *)curCmd;
				curCmd = (const void *)(db_cmd + 1);
				break;
			case RC_DRAW_SURFS:
				return qtrue;
			default:
				return qfalse;
		}
	}
}


void vk_begin_frame( void )
{
	VkCommandBufferBeginInfo begin_info;
	//VkFramebuffer frameBuffer;
	VkResult res;

	if ( vk.frame_count++ ) // might happen during stereo rendering
		return;

	if ( vk.cmd->waitForFence ) {
		if ( !ri.CL_IsMinimized() ) {
			res = qvkAcquireNextImageKHR( vk.device, vk.swapchain, 1e10, vk.image_acquired, VK_NULL_HANDLE, &vk.swapchain_image_index );
			// when running via RDP: "Application has already acquired the maximum number of images (0x2)"
			// probably caused by "device lost" errors
			if ( res < 0 ) {
				if ( res == VK_ERROR_OUT_OF_DATE_KHR ) {
					// swapchain re-creation needed
					vk_restart_swapchain( __func__ );
				} else {
					ri.Error( ERR_FATAL, "vkAcquireNextImageKHR returned error code %x", res );
				}
			}
		} else {
			vk.swapchain_image_index++;
			vk.swapchain_image_index %= vk.swapchain_image_count;
		}

		// TODO: do not switch with r_swapInterval?
		vk.cmd = &vk.tess[ vk.cmd_index++ ];
		vk.cmd_index %= NUM_COMMAND_BUFFERS;
		
		vk.cmd->waitForFence = qfalse;
		VK_CHECK( qvkWaitForFences( vk.device, 1, &vk.cmd->rendering_finished_fence, VK_FALSE, 1e12 ) );
	} else {
		// current command buffer has been reset due to geometry buffer overflow/update
		// so we will reuse it with current swapchain image as well
	}

	VK_CHECK( qvkResetFences( vk.device, 1, &vk.cmd->rendering_finished_fence ) );

	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;

	VK_CHECK( qvkBeginCommandBuffer( vk.cmd->command_buffer, &begin_info ) );

	// Ensure visibility of geometry buffers writes.
	//record_buffer_memory_barrier( vk.cmd->command_buffer, vk.cmd->vertex_buffer, vk.cmd->vertex_buffer_offset, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT );

#ifdef USE_SINGLE_FBO
	//frameBuffer = vk.framebuffers[ vk.swapchain_image_index ];
	if ( vk.fboActive ) {
		record_image_layout_transition( vk.cmd->command_buffer,
			vk.color_image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, //VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	}
#else
	//frameBuffer = vk.cmd->framebuffers[ vk.swapchain_image_index ];
	if ( vk.fboActive ) {
		record_image_layout_transition( vk.cmd->command_buffer,
			vk.cmd->color_image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, //VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	}
#endif

	if ( vk.cmd->vertex_buffer_offset > vk.stats.vertex_buffer_max ) {
		vk.stats.vertex_buffer_max = vk.cmd->vertex_buffer_offset;
	}

	if ( vk.stats.push_size > vk.stats.push_size_max ) {
		vk.stats.push_size_max = vk.stats.push_size;
	}

	vk_world.dirty_depth_attachment = qfalse;

	backEnd.screenMapDone = qfalse;

	if ( tr.needScreenMap && vk_find_drawsurfs() ) {
		vk_begin_screenmap_render_pass();
	} else {
		vk_begin_main_render_pass();
	}

	// dynamic vertex buffer layout
	vk.cmd->uniform_read_offset = 0;
	vk.cmd->vertex_buffer_offset = 0;
	Com_Memset( vk.cmd->buf_offset, 0, sizeof( vk.cmd->buf_offset ) );
	Com_Memset( vk.cmd->vbo_offset, 0, sizeof( vk.cmd->vbo_offset ) );
	vk.cmd->curr_index_buffer = VK_NULL_HANDLE;
	vk.cmd->curr_index_offset = 0;

	Com_Memset( &vk.cmd->descriptor_set, 0, sizeof( vk.cmd->descriptor_set ) );
	vk.cmd->descriptor_set.start = ~0U;
	vk.cmd->descriptor_set.end = 0;

	vk_update_descriptor( 2, tr.whiteImage->descriptor );

	// other stats
	vk.stats.push_size = 0;
}


static void vk_resize_geometry_buffer( void )
{
	int i;

	vk_end_render_pass();
	
	VK_CHECK( qvkEndCommandBuffer( vk.cmd->command_buffer ) );

	qvkResetCommandBuffer( vk.cmd->command_buffer, 0 );

	vk_wait_idle();

	vk_release_geometry_buffers();

	vk_create_geometry_buffers( vk.geometry_buffer_size_new );
	vk.geometry_buffer_size_new = 0;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
		vk_update_uniform_descriptor( vk.tess[ i ].uniform_descriptor, vk.tess[ i ].vertex_buffer );

	ri.Printf( PRINT_DEVELOPER, "...geometry buffer resized to %iK\n", (int)( vk.geometry_buffer_size / 1024 ) );
}


void vk_end_frame( void )
{
	VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkPresentInfoKHR present_info;
	VkSubmitInfo submit_info;
	VkResult res;

	if ( vk.frame_count == 0 )
		return;

	vk.frame_count = 0;

	if ( vk.geometry_buffer_size_new )
	{
		vk_resize_geometry_buffer();
		return;
	}

	if ( vk.fboActive )
	{
		if ( !ri.CL_IsMinimized() )
		{
			if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP )
			{
				// just to make proper layout transition
				vk_end_render_pass();
				vk_begin_main_render_pass();
			}

			vk_end_render_pass();

			vk.renderWidth = vk.windowWidth;
			vk.renderHeight = vk.windowHeight;

			vk.renderScaleX = 1.0;
			vk.renderScaleY = 1.0;

#ifdef USE_SINGLE_FBO
			vk_begin_render_pass( vk.render_pass_gamma, vk.framebuffers2[ vk.swapchain_image_index ], qfalse, vk.renderWidth, vk.renderHeight );
			qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gamma_pipeline );
			qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_gamma, 0, 1, &vk.color_descriptor, 0, NULL );
#else
			vk_begin_render_pass( vk.render_pass_gamma, vk.cmd->framebuffers2[ vk.swapchain_image_index ], qfalse, vk.renderWidth, vk.renderHeight );
			qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.gamma_pipeline );
			qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_gamma, 0, 1, &vk.cmd->color_descriptor, 0, NULL );
#endif
			qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
		}
	}

	vk_end_render_pass();

	VK_CHECK( qvkEndCommandBuffer( vk.cmd->command_buffer ) );

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &vk.cmd->command_buffer;
	if ( !ri.CL_IsMinimized() ) {
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &vk.image_acquired;
		submit_info.pWaitDstStageMask = &wait_dst_stage_mask;
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &vk.cmd->rendering_finished;
	} else {
		submit_info.waitSemaphoreCount = 0;
		submit_info.pWaitSemaphores = NULL;
		submit_info.pWaitDstStageMask = NULL;
		submit_info.signalSemaphoreCount = 0;
		submit_info.pSignalSemaphores = NULL;
	}

	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &submit_info, vk.cmd->rendering_finished_fence ) );
	vk.cmd->waitForFence = qtrue;

	// presentation may take undefined time to complete, we can't measure it in a reliable way
	backEnd.pc.msec = ri.Milliseconds() - backEnd.pc.msec;

	if ( ri.CL_IsMinimized() )
		return;

	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.pNext = NULL;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &vk.cmd->rendering_finished;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &vk.swapchain;
	present_info.pImageIndices = &vk.swapchain_image_index;
	present_info.pResults = NULL;

	res = qvkQueuePresentKHR( vk.queue, &present_info );
	if ( res < 0 ) {
		if ( res == VK_ERROR_DEVICE_LOST ) {
			 // we can ignore that
			ri.Printf( PRINT_DEVELOPER, "vkQueuePresentKHR: device lost\n" );
		} else if ( res == VK_ERROR_OUT_OF_DATE_KHR ) {
			// swapchain re-creation needed
			vk_restart_swapchain( __func__ );
		} else {
			// or we don't
			ri.Error( ERR_FATAL, "vkQueuePresentKHR: error %i", res );
		}
	}
}


void vk_read_pixels( byte *buffer, uint32_t width, uint32_t height )
{
	VkFormatProperties formatProps;
	VkCommandBuffer command_buffer;
	VkDeviceMemory memory;
	VkMemoryRequirements memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	VkImageCreateInfo desc;
	VkImage srcImage;
	VkImageLayout srcImageLayout;
	VkImage dstImage;
	qboolean blit_enabled;
	byte *buffer_ptr;
	byte *data;
	int i;

	//vk_wait_idle();
	VK_CHECK( qvkWaitForFences( vk.device, 1, &vk.cmd->rendering_finished_fence, VK_FALSE, 1e12 ) );

	if ( vk.fboActive ) {
		
		srcImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#ifdef USE_SINGLE_FBO
		srcImage = vk.color_image;
#else
		srcImage = vk.cmd->color_image;
#endif
	} else {

		srcImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		srcImage = vk.swapchain_images[ vk.swapchain_image_index ];
	}

	Com_Memset( &desc, 0, sizeof( desc ) );

	// Create image in host visible memory to serve as a destination for framebuffer pixels.
	desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.imageType = VK_IMAGE_TYPE_2D;
	desc.format = VK_FORMAT_R8G8B8A8_UNORM;
	desc.extent.width = width;
	desc.extent.height = height;
	desc.extent.depth = 1;
	desc.mipLevels = 1;
	desc.arrayLayers = 1;
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.tiling = VK_IMAGE_TILING_LINEAR;
	desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;
	desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VK_CHECK( qvkCreateImage( vk.device, &desc, NULL, &dstImage ) );

	qvkGetImageMemoryRequirements( vk.device, dstImage, &memory_requirements );

	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type(vk.physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &memory));
	VK_CHECK(qvkBindImageMemory(vk.device, dstImage, memory, 0));

	command_buffer = begin_command_buffer();

	record_image_layout_transition( command_buffer, srcImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_MEMORY_READ_BIT, srcImageLayout, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	record_image_layout_transition( command_buffer, dstImage, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ); 

	//end_command_buffer( command_buffer );

	// Check if we can use vkCmdBlitImage for the given source and destination image formats.
	blit_enabled = qtrue;

	qvkGetPhysicalDeviceFormatProperties(vk.physical_device, vk.surface_format.format, &formatProps);
	if ((formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0)
		blit_enabled = qfalse;

	qvkGetPhysicalDeviceFormatProperties(vk.physical_device, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
	if ((formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0)
		blit_enabled = qfalse;

	//command_buffer = begin_command_buffer();

	if ( blit_enabled ) {
		VkImageBlit region;

		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcOffsets[0].x = 0;
		region.srcOffsets[0].y = 0;
		region.srcOffsets[0].z = 0;
		region.srcOffsets[1].x = width;
		region.srcOffsets[1].y = height;
		region.srcOffsets[1].z = 1;
		region.dstSubresource = region.srcSubresource;
		region.dstOffsets[0] = region.srcOffsets[0];
		region.dstOffsets[1] = region.srcOffsets[1];

		qvkCmdBlitImage( command_buffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST );

	} else {
		VkImageCopy region;

		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcOffset.x = 0;
		region.srcOffset.y = 0;
		region.srcOffset.z = 0;
		region.dstSubresource = region.srcSubresource;
		region.dstOffset = region.srcOffset;
		region.extent.width = width;
		region.extent.height = height;
		region.extent.depth = 1;

		qvkCmdCopyImage( command_buffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	}

	end_command_buffer( command_buffer );

	// Copy data from destination image to memory buffer.
	subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresource.mipLevel = 0;
	subresource.arrayLayer = 0;

	qvkGetImageSubresourceLayout( vk.device, dstImage, &subresource, &layout );

	VK_CHECK(qvkMapMemory(vk.device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&data));
	data += layout.offset;

	buffer_ptr = buffer + width * (height - 1) * 4;
	for (i = 0; i < height; i++) {
		Com_Memcpy(buffer_ptr, data, width * 4);
		buffer_ptr -= width * 4;
		data += layout.rowPitch;
	}

	if (!blit_enabled) {
		VkFormat fmt = vk.surface_format.format;
		int swizzle_components = (fmt == VK_FORMAT_B8G8R8A8_SRGB || fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SNORM);
		if (swizzle_components) {
			buffer_ptr = buffer;
			for (i = 0; i < width * height; i++) {
				byte tmp = buffer_ptr[0];
				buffer_ptr[0] = buffer_ptr[2];
				buffer_ptr[2] = tmp;
				buffer_ptr += 4;
			}
		}
	}

	// strip alpha component
	for ( i = 0; i < width * height; i++ ) {
		Com_Memcpy( buffer + i*3, buffer + i*4, 3 );
	}

	qvkDestroyImage( vk.device, dstImage, NULL );
	qvkFreeMemory( vk.device, memory, NULL );

	// restore previous layout
	command_buffer = begin_command_buffer();
	record_image_layout_transition( command_buffer, srcImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_READ_BIT, srcImageLayout );
	end_command_buffer( command_buffer );
}
