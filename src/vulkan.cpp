/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
 */

#include <stdexcept>
#include <algorithm>
#include <array>
#include <vector>
#include <map>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include "vulkan.hpp"
#include "common/mesh.hpp"
#include "common/image.hpp"
#include "common/utils.hpp"

#include <GLFW/glfw3.h>

#define VKSUCCESS(x) ((x) == VK_SUCCESS)
#define VKFAILED(x)  ((x) != VK_SUCCESS)

namespace Vulkan {

struct TransformUniforms
{
	glm::mat4 viewProjectionMatrix;
	glm::mat4 skyProjectionMatrix;
	glm::mat4 sceneRotationMatrix;
};

struct ShadingUniforms
{
	struct {
		glm::vec4 direction;
		glm::vec4 radiance;
	} lights[SceneSettings::NumLights];
	glm::vec4 eyePosition;
};

struct SpecularFilterPushConstants
{
	uint32_t level;
	float roughness;
};

GLFWwindow* Renderer::initialize(int width, int height, int maxSamples)
{
	if(VKFAILED(volkInitialize())) {
		throw std::runtime_error("Vulkan loader has not been found");
	}

	// Create instance
	{
		std::vector<const char*> instanceLayers;
		std::vector<const char*> instanceExtensions;

		uint32_t glfwNumRequiredExtensions;
		const char** glfwRequiredExtensions = glfwGetRequiredInstanceExtensions(&glfwNumRequiredExtensions);
		if(glfwNumRequiredExtensions > 0) {
			instanceExtensions = std::vector<const char*>{glfwRequiredExtensions, glfwRequiredExtensions + glfwNumRequiredExtensions};
		}

#if _DEBUG
		instanceLayers.push_back("VK_LAYER_LUNARG_standard_validation");
		instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

		VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

		VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceCreateInfo.pApplicationInfo = &appInfo;

		if(!instanceLayers.empty()) {
			instanceCreateInfo.enabledLayerCount = (uint32_t)instanceLayers.size();
			instanceCreateInfo.ppEnabledLayerNames = &instanceLayers[0];
		}
		if(!instanceExtensions.empty()) {
			instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
			instanceCreateInfo.ppEnabledExtensionNames = &instanceExtensions[0];
		}
		if(VKFAILED(vkCreateInstance(&instanceCreateInfo, nullptr, &m_instance))) {
			throw std::runtime_error("Failed to create Vulkan instance");
		}
		volkLoadInstance(m_instance);
	}

#if _DEBUG
	// Initialize debug callback
	{
		VkDebugReportCallbackCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
		createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		createInfo.pfnCallback = Renderer::logMessage;
		if(VKFAILED(vkCreateDebugReportCallbackEXT(m_instance, &createInfo, nullptr, &m_logCallback))) {
			throw std::runtime_error("Failed to install debug report callback");
		}
	}
#endif

	// Create window & WSI surface
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(width, height, "Physically Based Rendering (Vulkan)", nullptr, nullptr);
	if(!window) {
		throw std::runtime_error("Failed to create window");
	}
	if(VKFAILED(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface))) {
		throw std::runtime_error("Failed to create window surface");
	}

	// Find suitable physical device
	const std::vector<const char*> requiredDeviceExtensions = {
		"VK_KHR_swapchain"
	};
	VkPhysicalDeviceFeatures requiredDeviceFeatures = {};
	requiredDeviceFeatures.shaderStorageImageExtendedFormats = VK_TRUE;
	requiredDeviceFeatures.samplerAnisotropy = VK_TRUE;

	m_phyDevice = choosePhyDevice(m_surface, requiredDeviceFeatures, requiredDeviceExtensions);
	queryPhyDeviceSurfaceCapabilities(m_phyDevice, m_surface);

	// Create logical device
	{
		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueCreateInfo.queueFamilyIndex = m_phyDevice.queueFamilyIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		
		VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.pEnabledFeatures = &requiredDeviceFeatures;
		createInfo.enabledExtensionCount = (uint32_t)requiredDeviceExtensions.size();
		createInfo.ppEnabledExtensionNames = &requiredDeviceExtensions[0];
		if(VKFAILED(vkCreateDevice(m_phyDevice.handle, &createInfo, nullptr, &m_device))) {
			throw std::runtime_error("Failed to create Vulkan logical device");
		}

		volkLoadDevice(m_device);
		vkGetDeviceQueue(m_device, m_phyDevice.queueFamilyIndex, 0, &m_queue);
	}

	// Create swap chain
	{
		uint32_t selectedMinImageCount = 2;
		selectedMinImageCount = glm::clamp(selectedMinImageCount, m_phyDevice.surfaceCaps.minImageCount, m_phyDevice.surfaceCaps.maxImageCount);

		VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
		if(std::find(m_phyDevice.presentModes.begin(), m_phyDevice.presentModes.end(), selectedPresentMode) == m_phyDevice.presentModes.end()) {
			selectedPresentMode = m_phyDevice.presentModes[0];
		}

		VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		createInfo.surface = m_surface;
		createInfo.minImageCount = selectedMinImageCount;
		createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
		createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		createInfo.imageExtent = m_phyDevice.surfaceCaps.currentExtent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform = m_phyDevice.surfaceCaps.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = selectedPresentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;
		if(VKFAILED(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain))) {
			throw std::runtime_error("Failed to create swap chain");
		}

		if(VKSUCCESS(vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_numFrames, nullptr)) && m_numFrames > 0) {
			m_swapchainImages.resize(m_numFrames);
			if(VKFAILED(vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_numFrames, &m_swapchainImages[0]))) {
				m_numFrames = 0;
			}
		}
		if(m_numFrames == 0) {
			throw std::runtime_error("Failed to retrieve swapchain image handles");
		}
	}

	// Create swapchain image views
	m_swapchainViews.resize(m_numFrames);
	for(uint32_t i=0; i<m_numFrames; ++i) {

		VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewCreateInfo.image = m_swapchainImages[i];
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
		viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.layerCount = 1;

		if(VKFAILED(vkCreateImageView(m_device, &viewCreateInfo, nullptr, &m_swapchainViews[i]))) {
			throw std::runtime_error("Failed to create swapchain image view");
		}
	}

	// Create render targets
	{
		const VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

		const uint32_t maxColorSamples = queryRenderTargetFormatMaxSamples(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		const uint32_t maxDepthSamples = queryRenderTargetFormatMaxSamples(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

		m_renderSamples = std::min({uint32_t(maxSamples), maxColorSamples, maxDepthSamples});
		assert(m_renderSamples >= 1);

		m_renderTargets.resize(m_numFrames);
		m_resolveRenderTargets.resize(m_numFrames);
		for(uint32_t i=0; i<m_numFrames; ++i) {
			m_renderTargets[i] = createRenderTarget(width, height, m_renderSamples, colorFormat, depthFormat);
			if(m_renderSamples > 1) {
				m_resolveRenderTargets[i] = createRenderTarget(width, height, 1, colorFormat, VK_FORMAT_UNDEFINED);
			}
		}
	}

	// Create command pool & allocate command buffers
	m_commandBuffers.resize(m_numFrames);
	{
		VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		createInfo.queueFamilyIndex = m_phyDevice.queueFamilyIndex;
		if(VKFAILED(vkCreateCommandPool(m_device, &createInfo, nullptr, &m_commandPool))) {
			throw std::runtime_error("Failed to create command pool");
		}

		VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocateInfo.commandPool = m_commandPool;
		allocateInfo.commandBufferCount = m_numFrames;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		if(VKFAILED(vkAllocateCommandBuffers(m_device, &allocateInfo, &m_commandBuffers[0]))) {
			throw std::runtime_error("Failed to allocate command buffer");
		}
	}

	// Create fences
	m_submitFences.resize(m_numFrames);
	{
		VkFenceCreateInfo createInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		if(VKFAILED(vkCreateFence(m_device, &createInfo, nullptr, &m_presentationFence))) {
			throw std::runtime_error("Failed to create presentation fence");
		}
		for(auto& fence : m_submitFences) {
			if(VKFAILED(vkCreateFence(m_device, &createInfo, nullptr, &fence))) {
				throw std::runtime_error("Failed to create queue submission fence");
			}
		}
	}

	// Acquire initial swapchain image
	{
		if(VKFAILED(vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, VK_NULL_HANDLE, m_presentationFence, &m_frameIndex))) {
			throw std::runtime_error("Failed to acquire initial swapchain image for rendering");
		}
		vkWaitForFences(m_device, 1, &m_presentationFence, VK_TRUE, UINT64_MAX);
		vkResetFences(m_device, 1, &m_presentationFence);
	}

	// Create descriptor pool
	{
		const std::array<VkDescriptorPoolSize, 3> poolSizes = {{
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 16 },
		}};

		VkDescriptorPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		createInfo.maxSets = 16;
		createInfo.poolSizeCount = (uint32_t)poolSizes.size();
		createInfo.pPoolSizes = poolSizes.data();
		if(VKFAILED(vkCreateDescriptorPool(m_device, &createInfo, nullptr, &m_descriptorPool))) {
			throw std::runtime_error("Failed to create descriptor pool");
		}
	}

	m_frameRect  = { 0, 0, (uint32_t)width, (uint32_t)height };
	m_frameCount = 0;

	std::printf("Vulkan 1.0 Renderer [%s]\n", m_phyDevice.properties.deviceName);
	return window;
}
	
void Renderer::shutdown()
{
	vkDeviceWaitIdle(m_device);
	
	destroyTexture(m_envTexture);
	destroyTexture(m_irmapTexture);
	destroyTexture(m_spBRDF_LUT);

	destroyMeshBuffer(m_skybox);

	destroyMeshBuffer(m_pbrModel);
	destroyTexture(m_albedoTexture);
	destroyTexture(m_normalTexture);
	destroyTexture(m_metalnessTexture);
	destroyTexture(m_roughnessTexture);

	destroyUniformBuffer(m_uniformBuffer);

	vkDestroySampler(m_device, m_defaultSampler, nullptr);
	vkDestroySampler(m_device, m_spBRDFSampler, nullptr);
	
	vkDestroyPipelineLayout(m_device, m_pbrPipelineLayout, nullptr);
	vkDestroyPipeline(m_device, m_pbrPipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_skyboxPipelineLayout, nullptr);
	vkDestroyPipeline(m_device, m_skyboxPipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_tonemapPipelineLayout, nullptr);
	vkDestroyPipeline(m_device, m_tonemapPipeline, nullptr);

	vkDestroyRenderPass(m_device, m_renderPass, nullptr);

	for(uint32_t i=0; i<m_numFrames; ++i) {
		destroyRenderTarget(m_renderTargets[i]);
		if(m_renderSamples > 1) {
			destroyRenderTarget(m_resolveRenderTargets[i]);
		}

		vkDestroyFramebuffer(m_device, m_framebuffers[i], nullptr);
		vkDestroyImageView(m_device, m_swapchainViews[i], nullptr);
		vkDestroyFence(m_device, m_submitFences[i], nullptr);
	}

	vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
	vkDestroyCommandPool(m_device, m_commandPool, nullptr);
	vkDestroyFence(m_device, m_presentationFence, nullptr);
	vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

	vkDestroyDevice(m_device, nullptr);

#if _DEBUG
	vkDestroyDebugReportCallbackEXT(m_instance, m_logCallback, nullptr);
#endif
	vkDestroyInstance(m_instance, nullptr);
}
	
void Renderer::setup()
{
	// Parameters
	static constexpr uint32_t kEnvMapSize = 1024;
	static constexpr uint32_t kIrradianceMapSize = 32;
	static constexpr uint32_t kBRDF_LUT_Size = 256;
	static constexpr uint32_t kEnvMapLevels = Utility::numMipmapLevels(kEnvMapSize, kEnvMapSize);
	static constexpr VkDeviceSize kUniformBufferSize = 64 * 1024;

	// Common descriptor set layouts
	struct {
		VkDescriptorSetLayout uniforms;
		VkDescriptorSetLayout pbr;
		VkDescriptorSetLayout skybox;
		VkDescriptorSetLayout tonemap;
		VkDescriptorSetLayout compute;
	} setLayout;

	// Friendly binding names for per-frame uniform blocks
	enum UniformsDescriptorSetBindingNames : uint32_t {
		Binding_TransformUniforms = 0,
		Binding_ShadingUniforms   = 1,
	};

	// Friendly binding names for compute pipeline descriptor set
	enum ComputeDescriptorSetBindingNames : uint32_t {
		Binding_InputTexture  = 0,
		Binding_OutputTexture = 1,
		Binding_OutputMipTail = 2,
	};

	// Create host-mapped uniform buffer for sub-allocation of uniform block ranges.
	m_uniformBuffer = createUniformBuffer(kUniformBufferSize);

	// Create samplers.
	VkSampler computeSampler;
	{
		VkSamplerCreateInfo createInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

		// Linear, non-anisotropic sampler, wrap address mode (post processing compute shaders)
		createInfo.minFilter = VK_FILTER_LINEAR;
		createInfo.magFilter = VK_FILTER_LINEAR;
		createInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		if(VKFAILED(vkCreateSampler(m_device, &createInfo, nullptr, &computeSampler))) {
			throw std::runtime_error("Failed to create pre-processing sampler");
		}
		
		// Linear, anisotropic sampler, wrap address mode (rendering)
		createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		createInfo.anisotropyEnable = VK_TRUE;
		createInfo.maxAnisotropy = m_phyDevice.properties.limits.maxSamplerAnisotropy;
		createInfo.minLod = 0.0f;
		createInfo.maxLod = FLT_MAX;
		if(VKFAILED(vkCreateSampler(m_device, &createInfo, nullptr, &m_defaultSampler))) {
			throw std::runtime_error("Failed to create default anisotropic sampler");
		}

		// Linear, non-anisotropic sampler, clamp address mode (sampling BRDF LUT)
		createInfo.anisotropyEnable = VK_FALSE;
		createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		if(VKFAILED(vkCreateSampler(m_device, &createInfo, nullptr, &m_spBRDFSampler))) {
			throw std::runtime_error("Failed to create BRDF LUT sampler");
		}
	}
	
	// Create temporary descriptor pool for pre-processing compute shaders.
	VkDescriptorPool computeDescriptorPool;
	{
		const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kEnvMapLevels },
		}};

		VkDescriptorPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		createInfo.maxSets = 2;
		createInfo.poolSizeCount = (uint32_t)poolSizes.size();
		createInfo.pPoolSizes = poolSizes.data();
		if(VKFAILED(vkCreateDescriptorPool(m_device, &createInfo, nullptr, &computeDescriptorPool))) {
			throw std::runtime_error("Failed to create setup descriptor pool");
		}
			
	}

	// Create common descriptor set & pipeline layout for pre-processing compute shaders.
	VkPipelineLayout computePipelineLayout;
	VkDescriptorSet computeDescriptorSet;
	{
		const std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings = {
			{ Binding_InputTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &computeSampler },
			{ Binding_OutputTexture, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ Binding_OutputMipTail, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kEnvMapLevels-1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		};

		setLayout.compute = createDescriptorSetLayout(&descriptorSetLayoutBindings);
		computeDescriptorSet = allocateDescriptorSet(computeDescriptorPool, setLayout.compute);

		const std::vector<VkDescriptorSetLayout> pipelineSetLayouts = {
			setLayout.compute,
		};
		const std::vector<VkPushConstantRange> pipelinePushConstantRanges = {
			{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SpecularFilterPushConstants) },
		};
		computePipelineLayout = createPipelineLayout(&pipelineSetLayouts, &pipelinePushConstantRanges);
	}
	
	// Create descriptor set layout for per-frame shader uniforms
	{
		const std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings = {
			{ Binding_TransformUniforms, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,   nullptr },
			{ Binding_ShadingUniforms,   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		setLayout.uniforms = createDescriptorSetLayout(&descriptorSetLayoutBindings);
	}

	// Allocate & update per-frame uniform buffer descriptor sets
	{
		m_uniformsDescriptorSets.resize(m_numFrames);
		for(uint32_t i=0; i<m_numFrames; ++i) {
			m_uniformsDescriptorSets[i] = allocateDescriptorSet(m_descriptorPool, setLayout.uniforms);

			// Sub-allocate storage for uniform blocks
			m_transformUniforms.push_back(allocFromUniformBuffer<TransformUniforms>(m_uniformBuffer));
			updateDescriptorSet(m_uniformsDescriptorSets[i], Binding_TransformUniforms, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { m_transformUniforms[i].descriptorInfo });

			m_shadingUniforms.push_back(allocFromUniformBuffer<ShadingUniforms>(m_uniformBuffer));
			updateDescriptorSet(m_uniformsDescriptorSets[i], Binding_ShadingUniforms, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { m_shadingUniforms[i].descriptorInfo });
		}
	}
	
	// Create render pass
	{
		enum AttachmentName : uint32_t {
			MainColorAttachment = 0,
			MainDepthStencilAttachment,
			SwapchainColorAttachment,
			ResolveColorAttachment,
		};

		std::vector<VkAttachmentDescription> attachments = {
			// Main color attachment (0)
			{
				0,
				m_renderTargets[0].colorFormat,
				static_cast<VkSampleCountFlagBits>(m_renderSamples),
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			},
			// Main depth-stencil attachment (1)
			{
				0,
				m_renderTargets[0].depthFormat,
				static_cast<VkSampleCountFlagBits>(m_renderSamples),
				VK_ATTACHMENT_LOAD_OP_CLEAR,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},
			// Swapchain color attachment (2)
			{
				0,
				VK_FORMAT_B8G8R8A8_UNORM,
				VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
		};
		if(m_renderSamples > 1) {
			// Resolve color attachment (3)
			const VkAttachmentDescription resolveAttachment = 
			{
				0,
				m_resolveRenderTargets[0].colorFormat,
				VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			attachments.push_back(resolveAttachment);
		};

		// Main subpass
		const std::array<VkAttachmentReference, 1> mainPassColorRefs = {
			{ MainColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, 
		};
		const std::array<VkAttachmentReference, 1> mainPassResolveRefs = {
			{ ResolveColorAttachment, VK_IMAGE_LAYOUT_GENERAL },
		};
		const VkAttachmentReference mainPassDepthStencilRef = {
			  MainDepthStencilAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		};
		VkSubpassDescription mainPass = {};
		mainPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		mainPass.colorAttachmentCount = (uint32_t)mainPassColorRefs.size();
		mainPass.pColorAttachments = mainPassColorRefs.data();
		mainPass.pDepthStencilAttachment = &mainPassDepthStencilRef;
		if(m_renderSamples > 1) {
			mainPass.pResolveAttachments = mainPassResolveRefs.data();
		}

		// Tonemapping subpass
		const std::array<VkAttachmentReference, 1> tonemapPassInputRefs = {
			{ MainColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		};
		const std::array<VkAttachmentReference, 1> tonemapPassMultisampleInputRefs = {
			{ ResolveColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		};
		const std::array<VkAttachmentReference, 1> tonemapPassColorRefs = {
			{ SwapchainColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		};
		VkSubpassDescription tonemapPass = {};
		tonemapPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		tonemapPass.colorAttachmentCount = (uint32_t)tonemapPassColorRefs.size();
		tonemapPass.pColorAttachments = tonemapPassColorRefs.data();
		if(m_renderSamples > 1) {
			tonemapPass.inputAttachmentCount = (uint32_t)tonemapPassMultisampleInputRefs.size();
			tonemapPass.pInputAttachments = tonemapPassMultisampleInputRefs.data();
		}
		else {
			tonemapPass.inputAttachmentCount = (uint32_t)tonemapPassInputRefs.size();
			tonemapPass.pInputAttachments = tonemapPassInputRefs.data();
		}

		const std::array<VkSubpassDescription, 2> subpasses = {
			mainPass,
			tonemapPass,
		};

		// Main->Tonemapping dependency
		const VkSubpassDependency mainToTonemapDependency = {
			0,
			1,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT,
		};

		VkRenderPassCreateInfo createInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		createInfo.attachmentCount = (uint32_t)attachments.size();
		createInfo.pAttachments = attachments.data();
		createInfo.subpassCount = (uint32_t)subpasses.size();
		createInfo.pSubpasses = subpasses.data();
		createInfo.dependencyCount = 1;
		createInfo.pDependencies = &mainToTonemapDependency;

		if(VKFAILED(vkCreateRenderPass(m_device, &createInfo, nullptr, &m_renderPass))) {
			throw std::runtime_error("Failed to create render pass");
		}
	}

	// Create framebuffers
	{
		m_framebuffers.resize(m_numFrames);
		for(uint32_t i=0; i<m_framebuffers.size(); ++i) {

			std::vector<VkImageView> attachments = {
				m_renderTargets[i].colorView,
				m_renderTargets[i].depthView,
				m_swapchainViews[i],
			};
			if(m_renderSamples > 1) {
				attachments.push_back(m_resolveRenderTargets[i].colorView);
			}

			VkFramebufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			createInfo.renderPass = m_renderPass;
			createInfo.attachmentCount = (uint32_t)attachments.size();
			createInfo.pAttachments = attachments.data();
			createInfo.width  = m_frameRect.extent.width;
			createInfo.height = m_frameRect.extent.height;
			createInfo.layers = 1;
			
			if(VKFAILED(vkCreateFramebuffer(m_device, &createInfo, nullptr, &m_framebuffers[i]))) {
				throw std::runtime_error("Failed to create framebuffer");
			}
		}
	}
	
	// Allocate common textures for later processing.
	{
		// Environment map (with pre-filtered mip chain)
		m_envTexture = createTexture(kEnvMapSize, kEnvMapSize, 6, VK_FORMAT_R16G16B16A16_SFLOAT, 0, VK_IMAGE_USAGE_STORAGE_BIT);
		// Irradiance map
		m_irmapTexture = createTexture(kIrradianceMapSize, kIrradianceMapSize, 6, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_USAGE_STORAGE_BIT);
		// 2D LUT for split-sum approximation
		m_spBRDF_LUT = createTexture(kBRDF_LUT_Size, kBRDF_LUT_Size, 1, VK_FORMAT_R16G16_SFLOAT, 1, VK_IMAGE_USAGE_STORAGE_BIT);
	}
	
	// Create graphics pipeline & descriptor set layout for tone mapping
	{
		const std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings = {
			{ 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		setLayout.tonemap = createDescriptorSetLayout(&descriptorSetLayoutBindings);

		const std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = {
			setLayout.tonemap,
		};
		m_tonemapPipelineLayout = createPipelineLayout(&pipelineDescriptorSetLayouts);
		m_tonemapPipeline = createGraphicsPipeline(
			1,
			"shaders/spirv/tonemap_vs.spv",
			"shaders/spirv/tonemap_fs.spv",
			m_tonemapPipelineLayout);
	}

	// Allocate & update descriptor sets for tone mapping input (per-frame)
	{
		m_tonemapDescriptorSets.resize(m_numFrames);
		for(uint32_t i=0; i<m_numFrames; ++i) {
			const VkDescriptorImageInfo imageInfo = {
				VK_NULL_HANDLE,
				(m_renderSamples > 1) ? m_resolveRenderTargets[i].colorView : m_renderTargets[i].colorView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			m_tonemapDescriptorSets[i] = allocateDescriptorSet(m_descriptorPool, setLayout.tonemap);
			updateDescriptorSet(m_tonemapDescriptorSets[i], 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, { imageInfo });
		}
	}
	
	// Load PBR model assets.
	m_pbrModel = createMeshBuffer(Mesh::fromFile("meshes/cerberus.fbx"));
	
	m_albedoTexture = createTexture(Image::fromFile("textures/cerberus_A.png"), VK_FORMAT_R8G8B8A8_SRGB);
	m_normalTexture = createTexture(Image::fromFile("textures/cerberus_N.png"), VK_FORMAT_R8G8B8A8_UNORM);
	m_metalnessTexture = createTexture(Image::fromFile("textures/cerberus_M.png", 1), VK_FORMAT_R8_UNORM);
	m_roughnessTexture = createTexture(Image::fromFile("textures/cerberus_R.png", 1), VK_FORMAT_R8_UNORM);
	
	// Create graphics pipeline & descriptor set layout for rendering PBR model
	{
		const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
			{ 0, sizeof(Mesh::Vertex), VK_VERTEX_INPUT_RATE_VERTEX },
		};
		const std::vector<VkVertexInputAttributeDescription> vertexAttributes = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0  }, // Position
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 }, // Normal
			{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24 }, // Tangent
			{ 3, 0, VK_FORMAT_R32G32B32_SFLOAT, 36 }, // Bitangent
			{ 4, 0, VK_FORMAT_R32G32_SFLOAT,    48 }, // Texcoord
		};

		const std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings = {
			{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_defaultSampler }, // Albedo texture
			{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_defaultSampler }, // Normal texture
			{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_defaultSampler }, // Metalness texture
			{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_defaultSampler }, // Roughness texture
			{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_defaultSampler }, // Specular env map texture
			{ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_defaultSampler }, // Irradiance map texture
			{ 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_spBRDFSampler },  // Specular BRDF LUT
		};
		setLayout.pbr = createDescriptorSetLayout(&descriptorSetLayoutBindings);

		const std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = {
			setLayout.uniforms,
			setLayout.pbr,
		};
		m_pbrPipelineLayout = createPipelineLayout(&pipelineDescriptorSetLayouts);

		VkPipelineMultisampleStateCreateInfo multisampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisampleState.rasterizationSamples = static_cast<VkSampleCountFlagBits>(m_renderTargets[0].samples);

		VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		m_pbrPipeline = createGraphicsPipeline(
			0,
			"shaders/spirv/pbr_vs.spv",
			"shaders/spirv/pbr_fs.spv",
			m_pbrPipelineLayout,
			&vertexInputBindings,
			&vertexAttributes,
			&multisampleState,
			&depthStencilState);
	}
	
	// Allocate & update descriptor set for PBR model
	{
		const std::vector<VkDescriptorImageInfo> textures = {
			{ VK_NULL_HANDLE, m_albedoTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ VK_NULL_HANDLE, m_normalTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ VK_NULL_HANDLE, m_metalnessTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ VK_NULL_HANDLE, m_roughnessTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ VK_NULL_HANDLE, m_envTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ VK_NULL_HANDLE, m_irmapTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ VK_NULL_HANDLE, m_spBRDF_LUT.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		};
		m_pbrDescriptorSet = allocateDescriptorSet(m_descriptorPool, setLayout.pbr);
		updateDescriptorSet(m_pbrDescriptorSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textures);
	}
	
	// Load skybox assets.
	m_skybox = createMeshBuffer(Mesh::fromFile("meshes/skybox.obj"));
	
	// Create graphics pipeline & descriptor set layout for skybox
	{
		const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
			{ 0, sizeof(Mesh::Vertex), VK_VERTEX_INPUT_RATE_VERTEX },
		};
		const std::vector<VkVertexInputAttributeDescription> vertexAttributes = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, // Position
		};

		const std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings = {
			{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_defaultSampler }, // Environment texture
		};
		setLayout.skybox = createDescriptorSetLayout(&descriptorSetLayoutBindings);

		const std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = {
			setLayout.uniforms,
			setLayout.skybox,
		};
		m_skyboxPipelineLayout = createPipelineLayout(&pipelineDescriptorSetLayouts);

		VkPipelineMultisampleStateCreateInfo multisampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisampleState.rasterizationSamples = static_cast<VkSampleCountFlagBits>(m_renderTargets[0].samples);

		VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthStencilState.depthTestEnable = VK_FALSE;

		m_skyboxPipeline = createGraphicsPipeline(0,
			"shaders/spirv/skybox_vs.spv",
			"shaders/spirv/skybox_fs.spv",
			m_skyboxPipelineLayout,
			&vertexInputBindings,
			&vertexAttributes,
			&multisampleState,
			&depthStencilState);
	}

	// Allocate & update descriptor set for skybox.
	{
		const VkDescriptorImageInfo skyboxTexture = { VK_NULL_HANDLE, m_envTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		m_skyboxDescriptorSet = allocateDescriptorSet(m_descriptorPool, setLayout.skybox);
		updateDescriptorSet(m_skyboxDescriptorSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { skyboxTexture });
	}

	// Load & pre-process environment map.
	{
		Texture envTextureUnfiltered = createTexture(kEnvMapSize, kEnvMapSize, 6, VK_FORMAT_R16G16B16A16_SFLOAT, 0, VK_IMAGE_USAGE_STORAGE_BIT);

		// Load & convert equirectangular envuronment map to cubemap texture
		{
			VkPipeline pipeline = createComputePipeline("shaders/spirv/equirect2cube_cs.spv", computePipelineLayout);

			Texture envTextureEquirect = createTexture(Image::fromFile("environment.hdr"), VK_FORMAT_R32G32B32A32_SFLOAT, 1);
			
			const VkDescriptorImageInfo inputTexture  = { VK_NULL_HANDLE, envTextureEquirect.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			const VkDescriptorImageInfo outputTexture = { VK_NULL_HANDLE, envTextureUnfiltered.view, VK_IMAGE_LAYOUT_GENERAL };
			updateDescriptorSet(computeDescriptorSet, Binding_InputTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { inputTexture });
			updateDescriptorSet(computeDescriptorSet, Binding_OutputTexture, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { outputTexture });

			VkCommandBuffer commandBuffer = beginImmediateCommandBuffer();
			{
				const auto preDispatchBarrier = ImageMemoryBarrier(envTextureUnfiltered, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL).mipLevels(0, 1);
				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { preDispatchBarrier });

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
				vkCmdDispatch(commandBuffer, kEnvMapSize/32, kEnvMapSize/32, 6);

				const auto postDispatchBarrier = ImageMemoryBarrier(envTextureUnfiltered, VK_ACCESS_SHADER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL).mipLevels(0, 1);
				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { postDispatchBarrier });
			}
			executeImmediateCommandBuffer(commandBuffer);

			vkDestroyPipeline(m_device, pipeline, nullptr);
			destroyTexture(envTextureEquirect);

			generateMipmaps(envTextureUnfiltered);
		}

		// Compute pre-filtered specular environment map.
		{
			const uint32_t numMipTailLevels = kEnvMapLevels - 1;

			VkPipeline pipeline;
			{
				const VkSpecializationMapEntry specializationMap = { 0, 0, sizeof(uint32_t) };
				const uint32_t specializationData[] = { numMipTailLevels };

				const VkSpecializationInfo specializationInfo = { 1, &specializationMap, sizeof(specializationData), specializationData };
				pipeline = createComputePipeline("shaders/spirv/spmap_cs.spv", computePipelineLayout, &specializationInfo);
			}

			VkCommandBuffer commandBuffer = beginImmediateCommandBuffer();

			// Copy base mipmap level into destination environment map.
			{
				const std::vector<ImageMemoryBarrier> preCopyBarriers = {
					ImageMemoryBarrier(envTextureUnfiltered, 0, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL).mipLevels(0, 1),
					ImageMemoryBarrier(m_envTexture, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
				};
				const std::vector<ImageMemoryBarrier> postCopyBarriers = {
					ImageMemoryBarrier(envTextureUnfiltered, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL).mipLevels(0, 1),
					ImageMemoryBarrier(m_envTexture, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
				};

				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, preCopyBarriers);

				VkImageCopy copyRegion = {};
				copyRegion.extent = { m_envTexture.width, m_envTexture.height, 1 };
				copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.srcSubresource.layerCount = m_envTexture.layers;
				copyRegion.dstSubresource = copyRegion.srcSubresource;
				vkCmdCopyImage(commandBuffer,
					envTextureUnfiltered.image.resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					m_envTexture.image.resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &copyRegion);

				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, postCopyBarriers);
			}
				
			// Pre-filter rest of the mip-chain.
			std::vector<VkImageView> envTextureMipTailViews;
			{
				std::vector<VkDescriptorImageInfo> envTextureMipTailDescriptors;
				const VkDescriptorImageInfo inputTexture  = { VK_NULL_HANDLE, envTextureUnfiltered.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
				updateDescriptorSet(computeDescriptorSet, Binding_InputTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { inputTexture });

				for(uint32_t level=1; level<kEnvMapLevels; ++level) {
					envTextureMipTailViews.push_back(createTextureView(m_envTexture, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, level, 1));
					envTextureMipTailDescriptors.push_back(VkDescriptorImageInfo{ VK_NULL_HANDLE, envTextureMipTailViews[level-1], VK_IMAGE_LAYOUT_GENERAL });
				}
				updateDescriptorSet(computeDescriptorSet, Binding_OutputMipTail, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, envTextureMipTailDescriptors);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);

				const float deltaRoughness = 1.0f / std::max(float(numMipTailLevels), 1.0f);
				for(uint32_t level=1, size=kEnvMapSize/2; level<kEnvMapLevels; ++level, size/=2) {
					const uint32_t numGroups = std::max<uint32_t>(1, size/32);

					const SpecularFilterPushConstants pushConstants = { level-1, level * deltaRoughness };
					vkCmdPushConstants(commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SpecularFilterPushConstants), &pushConstants);
					vkCmdDispatch(commandBuffer, numGroups, numGroups, 6);
				}

				const auto barrier = ImageMemoryBarrier(m_envTexture, VK_ACCESS_SHADER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { barrier });
			}

			executeImmediateCommandBuffer(commandBuffer);

			for(VkImageView mipTailView : envTextureMipTailViews) {
				vkDestroyImageView(m_device, mipTailView, nullptr);
			}
			vkDestroyPipeline(m_device, pipeline, nullptr);
			destroyTexture(envTextureUnfiltered);
		}

		// Compute diffuse irradiance cubemap
		{
			VkPipeline pipeline = createComputePipeline("shaders/spirv/irmap_cs.spv", computePipelineLayout);

			const VkDescriptorImageInfo inputTexture  = { VK_NULL_HANDLE, m_envTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			const VkDescriptorImageInfo outputTexture = { VK_NULL_HANDLE, m_irmapTexture.view, VK_IMAGE_LAYOUT_GENERAL };
			updateDescriptorSet(computeDescriptorSet, Binding_InputTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { inputTexture });
			updateDescriptorSet(computeDescriptorSet, Binding_OutputTexture, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { outputTexture });

			VkCommandBuffer commandBuffer = beginImmediateCommandBuffer();
			{
				const auto preDispatchBarrier = ImageMemoryBarrier(m_irmapTexture, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { preDispatchBarrier });

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
				vkCmdDispatch(commandBuffer, kIrradianceMapSize/32, kIrradianceMapSize/32, 6);

				const auto postDispatchBarrier = ImageMemoryBarrier(m_irmapTexture, VK_ACCESS_SHADER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { postDispatchBarrier });
			}
			executeImmediateCommandBuffer(commandBuffer);
			vkDestroyPipeline(m_device, pipeline, nullptr);
		}
		
		// Compute Cook-Torrance BRDF 2D LUT for split-sum approximation.
		{
			VkPipeline pipeline = createComputePipeline("shaders/spirv/spbrdf_cs.spv", computePipelineLayout);
			
			const VkDescriptorImageInfo outputTexture = { VK_NULL_HANDLE, m_spBRDF_LUT.view, VK_IMAGE_LAYOUT_GENERAL };
			updateDescriptorSet(computeDescriptorSet, Binding_OutputTexture, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { outputTexture });

			VkCommandBuffer commandBuffer = beginImmediateCommandBuffer();
			{
				const auto preDispatchBarrier = ImageMemoryBarrier(m_spBRDF_LUT, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { preDispatchBarrier });

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
				vkCmdDispatch(commandBuffer, kBRDF_LUT_Size/32, kBRDF_LUT_Size/32, 6);

				const auto postDispatchBarrier = ImageMemoryBarrier(m_spBRDF_LUT, VK_ACCESS_SHADER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { postDispatchBarrier });
			}
			executeImmediateCommandBuffer(commandBuffer);
			vkDestroyPipeline(m_device, pipeline, nullptr);
		}
	}
	
	// Clean up
	vkDestroyDescriptorSetLayout(m_device, setLayout.uniforms, nullptr);
	vkDestroyDescriptorSetLayout(m_device, setLayout.pbr, nullptr);
	vkDestroyDescriptorSetLayout(m_device, setLayout.skybox, nullptr);
	vkDestroyDescriptorSetLayout(m_device, setLayout.tonemap, nullptr);
	vkDestroyDescriptorSetLayout(m_device, setLayout.compute, nullptr);

	vkDestroySampler(m_device, computeSampler, nullptr);
	vkDestroyPipelineLayout(m_device, computePipelineLayout, nullptr);
	vkDestroyDescriptorPool(m_device, computeDescriptorPool, nullptr);
}
	
void Renderer::render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene)
{
	const VkDeviceSize zeroOffset = 0;

	glm::mat4 projectionMatrix = glm::perspectiveFov(view.fov, float(m_frameRect.extent.width), float(m_frameRect.extent.height), 1.0f, 1000.0f);
	projectionMatrix[1][1] *= -1.0f; // Vulkan uses right handed NDC with Y axis pointing down, compensate for that.
	
	const glm::mat4 viewRotationMatrix = glm::eulerAngleXY(glm::radians(view.pitch), glm::radians(view.yaw));
	const glm::mat4 sceneRotationMatrix = glm::eulerAngleXY(glm::radians(scene.pitch), glm::radians(scene.yaw));
	const glm::mat4 viewMatrix = glm::translate(glm::mat4(), {0.0f, 0.0f, -view.distance}) * viewRotationMatrix;
	const glm::vec3 eyePosition = glm::inverse(viewMatrix)[3];
	
	VkCommandBuffer commandBuffer = m_commandBuffers[m_frameIndex];
	VkImage swapchainImage = m_swapchainImages[m_frameIndex];
	VkFramebuffer framebuffer = m_framebuffers[m_frameIndex];

	VkDescriptorSet uniformsDescriptorSet = m_uniformsDescriptorSets[m_frameIndex];
	VkDescriptorSet tonemapDescriptorSet = m_tonemapDescriptorSets[m_frameIndex];

	// Update transform uniforms
	{
		TransformUniforms* const transformUniforms = m_transformUniforms[m_frameIndex].as<TransformUniforms>();
		transformUniforms->viewProjectionMatrix = projectionMatrix * viewMatrix;
		transformUniforms->skyProjectionMatrix  = projectionMatrix * viewRotationMatrix;
		transformUniforms->sceneRotationMatrix  = sceneRotationMatrix;
	}
	
	// Update shading uniforms
	{
		ShadingUniforms* const shadingUniforms = m_shadingUniforms[m_frameIndex].as<ShadingUniforms>();
		shadingUniforms->eyePosition = glm::vec4{eyePosition, 0.0f};
		for(int i=0; i<SceneSettings::NumLights; ++i) {
			const SceneSettings::Light& light = scene.lights[i];
			shadingUniforms->lights[i].direction = glm::vec4{light.direction, 0.0f};
			if(light.enabled) {
				shadingUniforms->lights[i].radiance = glm::vec4{light.radiance, 0.0f};
			}
			else {
				shadingUniforms->lights[i].radiance = glm::vec4{};
			}
		}
	}

	// Begin recording current frame command buffer.
	{
		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	
		vkResetCommandBuffer(commandBuffer, 0);
		vkBeginCommandBuffer(commandBuffer, &beginInfo);
	}

	// Begin render pass
	{
		std::array<VkClearValue, 2> clearValues = {};
		clearValues[1].depthStencil.depth = 1.0f;

		VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.renderPass = m_renderPass;
		beginInfo.framebuffer = framebuffer;
		beginInfo.renderArea = m_frameRect;
		beginInfo.clearValueCount = (uint32_t)clearValues.size();
		beginInfo.pClearValues = clearValues.data();

		// Begin in main draw subpass
		vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	}
	
	// Draw skybox
	{
		const std::array<VkDescriptorSet, 2> descriptorSets = {
			uniformsDescriptorSet,
			m_skyboxDescriptorSet,
		};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipelineLayout, 0, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_skybox.vertexBuffer.resource, &zeroOffset);
		vkCmdBindIndexBuffer(commandBuffer, m_skybox.indexBuffer.resource, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(commandBuffer, m_skybox.numElements, 1, 0, 0, 0);
	}

	// Draw PBR model
	{
		const std::array<VkDescriptorSet, 1> descriptorSets = {
			m_pbrDescriptorSet,
		};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbrPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pbrPipelineLayout, 1, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_pbrModel.vertexBuffer.resource, &zeroOffset);
		vkCmdBindIndexBuffer(commandBuffer, m_pbrModel.indexBuffer.resource, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(commandBuffer, m_pbrModel.numElements, 1, 0, 0, 0);
	}

	// Transition to tone mapping subpass
	vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);

	// Draw full screen triangle for postprocessing/tone mapping.
	{
		const std::array<VkDescriptorSet, 1> descriptorSets = {
			tonemapDescriptorSet
		};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipelineLayout, 0, (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
	}

	// End render pass & command buffer recording
	vkCmdEndRenderPass(commandBuffer);
	vkEndCommandBuffer(commandBuffer);

	// Submit command buffer to GPU queue for execution.
	{
		VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		vkQueueSubmit(m_queue, 1, &submitInfo, m_submitFences[m_frameIndex]);
	}

	presentFrame();
}
	
Resource<VkBuffer> Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags) const
{
	Resource<VkBuffer> buffer;

	VkBufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	createInfo.size = size;
	createInfo.usage = usage;
	createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if(VKFAILED(vkCreateBuffer(m_device, &createInfo, nullptr, &buffer.resource))) {
		throw std::runtime_error("Failed to create buffer");
	}

	VkMemoryRequirements memoryRequirements;
	vkGetBufferMemoryRequirements(m_device, buffer.resource, &memoryRequirements);

	VkMemoryAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = chooseMemoryType(memoryRequirements, memoryFlags);
	if(VKFAILED(vkAllocateMemory(m_device, &allocateInfo, nullptr, &buffer.memory))) {
		throw std::runtime_error("Failed to allocate device memory for buffer");
	}
	if(VKFAILED(vkBindBufferMemory(m_device, buffer.resource, buffer.memory, 0))) {
		throw std::runtime_error("Failed to bind device memory to buffer");
	}

	buffer.allocationSize = allocateInfo.allocationSize;
	buffer.memoryTypeIndex = allocateInfo.memoryTypeIndex;
	return buffer;
}
	
Resource<VkImage> Renderer::createImage(uint32_t width, uint32_t height, uint32_t layers, uint32_t levels, VkFormat format, uint32_t samples, VkImageUsageFlags usage) const
{
	assert(width > 0);
	assert(height > 0);
	assert(levels > 0);
	assert(layers == 1 || layers == 6);
	assert(samples > 0 && samples <= 64);

	Resource<VkImage> image;

	VkImageCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	createInfo.flags = (layers == 6) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	createInfo.imageType = VK_IMAGE_TYPE_2D;
	createInfo.format = format;
	createInfo.extent = { width, height, 1 };
	createInfo.mipLevels = levels;
	createInfo.arrayLayers = layers;
	createInfo.samples = static_cast<VkSampleCountFlagBits>(samples);
	createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	createInfo.usage = usage;
	createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if(VKFAILED(vkCreateImage(m_device, &createInfo, nullptr, &image.resource))) {
		throw std::runtime_error("Failed to create image");
	}

	VkMemoryRequirements memoryRequirements;
	vkGetImageMemoryRequirements(m_device, image.resource, &memoryRequirements);

	VkMemoryAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = chooseMemoryType(memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if(VKFAILED(vkAllocateMemory(m_device, &allocateInfo, nullptr, &image.memory))) {
		throw std::runtime_error("Failed to allocate device memory for image");
	}
	if(VKFAILED(vkBindImageMemory(m_device, image.resource, image.memory, 0))) {
		throw std::runtime_error("Failed to bind device memory to image");
	}

	image.allocationSize = allocateInfo.allocationSize;
	image.memoryTypeIndex = allocateInfo.memoryTypeIndex;

	return image;
}

void Renderer::destroyBuffer(Resource<VkBuffer>& buffer) const
{
	if(buffer.resource != VK_NULL_HANDLE) {
		vkDestroyBuffer(m_device, buffer.resource, nullptr);
	}
	if(buffer.memory != VK_NULL_HANDLE) {
		vkFreeMemory(m_device, buffer.memory, nullptr);
	}
	buffer = {};
}

void Renderer::destroyImage(Resource<VkImage>& image) const
{
	if(image.resource != VK_NULL_HANDLE) {
		vkDestroyImage(m_device, image.resource, nullptr);
	}
	if(image.memory != VK_NULL_HANDLE) {
		vkFreeMemory(m_device, image.memory, nullptr);
	}
	image = {};
}
	
MeshBuffer Renderer::createMeshBuffer(const std::shared_ptr<Mesh>& mesh) const
{
	assert(mesh);

	MeshBuffer buffer;
	buffer.numElements = static_cast<uint32_t>(mesh->faces().size() * 3);

	const size_t vertexDataSize = mesh->vertices().size() * sizeof(Mesh::Vertex);
	const size_t indexDataSize = mesh->faces().size() * sizeof(Mesh::Face);

	buffer.vertexBuffer = createBuffer(vertexDataSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	buffer.indexBuffer = createBuffer(indexDataSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	bool usingStagingForVertexBuffer = false;
	bool usingStagingForIndexBuffer = false;

	Resource<VkBuffer> stagingVertexBuffer = buffer.vertexBuffer;
	if(memoryTypeNeedsStaging(buffer.vertexBuffer.memoryTypeIndex)) {
		stagingVertexBuffer = createBuffer(buffer.vertexBuffer.allocationSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		usingStagingForVertexBuffer = true;
	}

	Resource<VkBuffer> stagingIndexBuffer  = buffer.indexBuffer;
	if(memoryTypeNeedsStaging(buffer.indexBuffer.memoryTypeIndex)) {
		stagingIndexBuffer = createBuffer(buffer.indexBuffer.allocationSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		usingStagingForIndexBuffer = true;
	}

	copyToDevice(stagingVertexBuffer.memory, mesh->vertices().data(), vertexDataSize);
	copyToDevice(stagingIndexBuffer.memory, mesh->faces().data(), indexDataSize);

	if(usingStagingForVertexBuffer || usingStagingForIndexBuffer) {
		VkCommandBuffer commandBuffer = beginImmediateCommandBuffer();
		if(usingStagingForVertexBuffer) {
			const VkBufferCopy bufferCopyRegion = { 0, 0, vertexDataSize };
			vkCmdCopyBuffer(commandBuffer, stagingVertexBuffer.resource, buffer.vertexBuffer.resource, 1, &bufferCopyRegion);
		}
		if(usingStagingForIndexBuffer) {
			const VkBufferCopy bufferCopyRegion = { 0, 0, indexDataSize };
			vkCmdCopyBuffer(commandBuffer, stagingIndexBuffer.resource, buffer.indexBuffer.resource, 1, &bufferCopyRegion);
		}
		executeImmediateCommandBuffer(commandBuffer);
	}

	if(usingStagingForVertexBuffer) {
		destroyBuffer(stagingVertexBuffer);
	}
	if(usingStagingForIndexBuffer) {
		destroyBuffer(stagingIndexBuffer);
	}

	return buffer;
}

void Renderer::destroyMeshBuffer(MeshBuffer& buffer) const
{
	destroyBuffer(buffer.vertexBuffer);
	destroyBuffer(buffer.indexBuffer);
	buffer = {};
}
	
Texture Renderer::createTexture(uint32_t width, uint32_t height, uint32_t layers, VkFormat format, uint32_t levels, VkImageUsageFlags additionalUsage) const
{
	assert(width > 0 && height > 0);
	assert(layers > 0);

	Texture texture;
	texture.width  = width;
	texture.height = height;
	texture.layers = layers;
	texture.levels = (levels > 0) ? levels : Utility::numMipmapLevels(width, height);

	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | additionalUsage;
	if(texture.levels > 1) {
		usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // For mipmap generation
	}

	texture.image = createImage(width, height, layers, texture.levels, format, 1, usage);
	texture.view = createTextureView(texture, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS);
	return texture;
}
	
Texture Renderer::createTexture(const std::shared_ptr<Image>& image, VkFormat format, uint32_t levels) const
{
	assert(image);

	Texture texture = createTexture(image->width(), image->height(), 1, format, levels);

	const size_t pixelDataSize = image->pitch() * image->height();
	Resource<VkBuffer> stagingBuffer = createBuffer(pixelDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	copyToDevice(stagingBuffer.memory, image->pixels<void>(), pixelDataSize);

	VkCommandBuffer commandBuffer = beginImmediateCommandBuffer();
	{
		const auto barrier = ImageMemoryBarrier(texture, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL).mipLevels(0, 1);
		pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, { barrier });
	}

	VkBufferImageCopy copyRegion = {};
	copyRegion.bufferOffset = 0;
	copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copyRegion.imageExtent = { texture.width, texture.height, 1 };
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.resource, texture.image.resource,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

	{
		// If we're going to generate mipmaps transition base mip to transfer src layout, otherwise use shader read only layout.
		const VkImageLayout finalBaseMipLayout = 
			(texture.levels > 1)
			? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
			: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		const auto barrier = ImageMemoryBarrier(texture, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalBaseMipLayout).mipLevels(0, 1);
		pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { barrier });
	}

	executeImmediateCommandBuffer(commandBuffer);
	destroyBuffer(stagingBuffer);

	if(texture.levels > 1) {
		generateMipmaps(texture);
	}

	return texture;
}
	
VkImageView Renderer::createTextureView(const Texture& texture, VkFormat format, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t numMipLevels) const
{
	VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewCreateInfo.image = texture.image.resource;
	viewCreateInfo.viewType = (texture.layers == 6) ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
	viewCreateInfo.format = format;
	viewCreateInfo.subresourceRange.aspectMask = aspectMask;
	viewCreateInfo.subresourceRange.baseMipLevel = baseMipLevel;
	viewCreateInfo.subresourceRange.levelCount = numMipLevels;
	viewCreateInfo.subresourceRange.baseArrayLayer = 0;
	viewCreateInfo.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

	VkImageView view;
	if(VKFAILED(vkCreateImageView(m_device, &viewCreateInfo, nullptr, &view))) {
		throw std::runtime_error("Failed to create texture image view");
	}
	return view;
}
	
void Renderer::generateMipmaps(const Texture& texture) const
{
	assert(texture.levels > 1);

	VkCommandBuffer commandBuffer = beginImmediateCommandBuffer();

	// Iterate through mip chain and consecutively blit from previous level to next level with linear filtering.
	for(uint32_t level=1, prevLevelWidth=texture.width, prevLevelHeight=texture.height; level<texture.levels; ++level, prevLevelWidth/=2, prevLevelHeight/=2) {

		const auto preBlitBarrier = ImageMemoryBarrier(texture, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL).mipLevels(level, 1);
		pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, { preBlitBarrier });

		VkImageBlit region = {};
		region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level-1, 0, texture.layers };
		region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level,   0, texture.layers };
		region.srcOffsets[1]  = { int32_t(prevLevelWidth),  int32_t(prevLevelHeight),   1 };
		region.dstOffsets[1]  = { int32_t(prevLevelWidth/2),int32_t(prevLevelHeight/2), 1 };
		vkCmdBlitImage(commandBuffer,
			texture.image.resource, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			texture.image.resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region, VK_FILTER_LINEAR);

		const auto postBlitBarrier = ImageMemoryBarrier(texture, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL).mipLevels(level, 1);
		pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, { postBlitBarrier });
	}

	// Transition whole mip chain to shader read only layout.
	{
		const auto barrier = ImageMemoryBarrier(texture, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		pipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { barrier });
	}

	executeImmediateCommandBuffer(commandBuffer);
}
	
void Renderer::destroyTexture(Texture& texture) const
{
	if(texture.view != VK_NULL_HANDLE) {
		vkDestroyImageView(m_device, texture.view, nullptr);
	}
	destroyImage(texture.image);
}
	
RenderTarget Renderer::createRenderTarget(uint32_t width, uint32_t height, uint32_t samples, VkFormat colorFormat, VkFormat depthFormat) const
{
	assert(samples > 0 && samples <= 64);
	
	RenderTarget target = {};
	target.width = width;
	target.height = height;
	target.samples = samples;
	target.colorFormat = colorFormat;
	target.depthFormat = depthFormat;

	VkImageUsageFlags colorImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if(samples == 1) {
		colorImageUsage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	}

	if(colorFormat != VK_FORMAT_UNDEFINED) {
		target.colorImage = createImage(width, height, 1, 1, colorFormat, samples, colorImageUsage);
	}
	if(depthFormat != VK_FORMAT_UNDEFINED) {
		target.depthImage = createImage(width, height, 1, 1, depthFormat, samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	}

	VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCreateInfo.subresourceRange.levelCount = 1;
	viewCreateInfo.subresourceRange.layerCount = 1;

	if(target.colorImage.resource != VK_NULL_HANDLE) {
		viewCreateInfo.image = target.colorImage.resource;
		viewCreateInfo.format = colorFormat;
		viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		if(VKFAILED(vkCreateImageView(m_device, &viewCreateInfo, nullptr, &target.colorView))) {
			throw std::runtime_error("Failed to create render target color image view");
		}
	}

	if(target.depthImage.resource != VK_NULL_HANDLE) {
		viewCreateInfo.image = target.depthImage.resource;
		viewCreateInfo.format = depthFormat;
		viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if(VKFAILED(vkCreateImageView(m_device, &viewCreateInfo, nullptr, &target.depthView))) {
			throw std::runtime_error("Failed to create render target depth-stencil image view");
		}
	}

	return target;
}

void Renderer::destroyRenderTarget(RenderTarget& target) const
{
	destroyImage(target.colorImage);
	destroyImage(target.depthImage);

	if(target.colorView != VK_NULL_HANDLE) {
		vkDestroyImageView(m_device, target.colorView, nullptr);
	}
	if(target.depthView != VK_NULL_HANDLE) {
		vkDestroyImageView(m_device, target.depthView, nullptr);
	}
	target = {};
}
	
UniformBuffer Renderer::createUniformBuffer(VkDeviceSize capacity) const
{
	assert(capacity > 0);

	UniformBuffer buffer = {};
	buffer.buffer   = createBuffer(capacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	buffer.capacity = capacity;
	
	if(VKFAILED(vkMapMemory(m_device, buffer.buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.hostMemoryPtr))) {
		throw std::runtime_error("Failed to map uniform buffer memory to host address space");
	}

	return buffer;
}

void Renderer::destroyUniformBuffer(UniformBuffer& buffer) const
{
	if(buffer.hostMemoryPtr != nullptr && buffer.buffer.memory != VK_NULL_HANDLE) {
		vkUnmapMemory(m_device, buffer.buffer.memory);
	}
	destroyBuffer(buffer.buffer);
	buffer = {};
}
	
UniformBufferAllocation Renderer::allocFromUniformBuffer(UniformBuffer& buffer, VkDeviceSize size) const
{
	const VkDeviceSize minAlignment = m_phyDevice.properties.limits.minUniformBufferOffsetAlignment;
	const VkDeviceSize alignedSize = Utility::roundToPowerOfTwo(size, (int)minAlignment);
	if(alignedSize > m_phyDevice.properties.limits.maxUniformBufferRange) {
		throw std::invalid_argument("Requested uniform buffer sub-allocation size exceeds maxUniformBufferRange of current physical device");
	}
	if(buffer.cursor + alignedSize > buffer.capacity) {
		throw std::overflow_error("Out of uniform buffer capacity while allocating memory");
	}

	UniformBufferAllocation allocation;
	allocation.descriptorInfo.buffer = buffer.buffer.resource;
	allocation.descriptorInfo.offset = buffer.cursor;
	allocation.descriptorInfo.range  = alignedSize;
	allocation.hostMemoryPtr = reinterpret_cast<uint8_t*>(buffer.hostMemoryPtr) + buffer.cursor;

	buffer.cursor += alignedSize;
	return allocation;
}
	
VkDescriptorSet Renderer::allocateDescriptorSet(VkDescriptorPool pool, VkDescriptorSetLayout layout) const
{
	VkDescriptorSet descriptorSet;
	VkDescriptorSetAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocateInfo.descriptorPool = pool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &layout;
	if(VKFAILED(vkAllocateDescriptorSets(m_device, &allocateInfo, &descriptorSet))) {
		throw std::runtime_error("Failed to allocate descriptor set");
	}
	return descriptorSet;
}
	
void Renderer::updateDescriptorSet(VkDescriptorSet dstSet, uint32_t dstBinding, VkDescriptorType descriptorType, const std::vector<VkDescriptorImageInfo>& descriptors) const
{
	VkWriteDescriptorSet writeDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	writeDescriptorSet.dstSet = dstSet;
	writeDescriptorSet.dstBinding = dstBinding;
	writeDescriptorSet.descriptorType = descriptorType;
	writeDescriptorSet.descriptorCount = (uint32_t)descriptors.size();
	writeDescriptorSet.pImageInfo = descriptors.data();
	vkUpdateDescriptorSets(m_device, 1, &writeDescriptorSet, 0, nullptr);
}

void Renderer::updateDescriptorSet(VkDescriptorSet dstSet, uint32_t dstBinding, VkDescriptorType descriptorType, const std::vector<VkDescriptorBufferInfo>& descriptors) const
{
	VkWriteDescriptorSet writeDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	writeDescriptorSet.dstSet = dstSet;
	writeDescriptorSet.dstBinding = dstBinding;
	writeDescriptorSet.descriptorType = descriptorType;
	writeDescriptorSet.descriptorCount = (uint32_t)descriptors.size();
	writeDescriptorSet.pBufferInfo = descriptors.data();
	vkUpdateDescriptorSets(m_device, 1, &writeDescriptorSet, 0, nullptr);
}

VkDescriptorSetLayout Renderer::createDescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>* bindings) const
{
	VkDescriptorSetLayout layout;
	VkDescriptorSetLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	if(bindings && bindings->size() > 0) {
		createInfo.bindingCount = (uint32_t)bindings->size();
		createInfo.pBindings = bindings->data();
	}
	if(VKFAILED(vkCreateDescriptorSetLayout(m_device, &createInfo, nullptr, &layout))) {
		throw std::runtime_error("Failed to create descriptor set layout");
	}
	return layout;
}
	
VkPipelineLayout Renderer::createPipelineLayout(const std::vector<VkDescriptorSetLayout>* setLayouts, const std::vector<VkPushConstantRange>* pushConstants) const
{
	VkPipelineLayout layout;
	VkPipelineLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	if(setLayouts && setLayouts->size() > 0) {
		createInfo.setLayoutCount = (uint32_t)setLayouts->size();
		createInfo.pSetLayouts = setLayouts->data();
	}
	if(pushConstants && pushConstants->size() > 0) {
		createInfo.pushConstantRangeCount = (uint32_t)pushConstants->size();
		createInfo.pPushConstantRanges = pushConstants->data();
	}
	if(VKFAILED(vkCreatePipelineLayout(m_device, &createInfo, nullptr, &layout))) {
		throw std::runtime_error("Failed to create pipeline layout");
	}
	return layout;
}

VkPipeline Renderer::createGraphicsPipeline(uint32_t subpass,
		const std::string& vs, const std::string& fs, VkPipelineLayout layout,
		const std::vector<VkVertexInputBindingDescription>* vertexInputBindings,
		const std::vector<VkVertexInputAttributeDescription>* vertexAttributes,
		const VkPipelineMultisampleStateCreateInfo* multisampleState,
		const VkPipelineDepthStencilStateCreateInfo* depthStencilState) const
{
	const VkViewport defaultViewport = { 
		0.0f,
		0.0f, 
		(float)m_frameRect.extent.width,
		(float)m_frameRect.extent.height,
		0.0f,
		1.0f
	};
	const VkPipelineMultisampleStateCreateInfo defaultMultisampleState = {
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		nullptr,
		0,
		VK_SAMPLE_COUNT_1_BIT,
	};
	
	VkPipelineColorBlendAttachmentState defaultColorBlendAttachmentState = {};
	defaultColorBlendAttachmentState.blendEnable = VK_FALSE;
	defaultColorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkShaderModule vertexShader = createShaderModuleFromFile(vs);
	VkShaderModule fragmentShader = createShaderModuleFromFile(fs);

	const VkPipelineShaderStageCreateInfo shaderStages[] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertexShader, "main", nullptr },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr },
	};

	VkPipelineVertexInputStateCreateInfo vertexInputState = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	if(vertexInputBindings) {
		vertexInputState.vertexBindingDescriptionCount = (uint32_t)vertexInputBindings->size();
		vertexInputState.pVertexBindingDescriptions = vertexInputBindings->data();
	}
	if(vertexAttributes) {
		vertexInputState.vertexAttributeDescriptionCount = (uint32_t)vertexAttributes->size();
		vertexInputState.pVertexAttributeDescriptions = vertexAttributes->data();
	}

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyState.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	viewportState.pViewports = &defaultViewport;
	viewportState.pScissors = &m_frameRect;

	VkPipelineRasterizationStateCreateInfo rasterizationState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationState.lineWidth = 1.0f;

	const VkPipelineColorBlendAttachmentState colorBlendAttachmentStates[] = {
		defaultColorBlendAttachmentState
	};
	VkPipelineColorBlendStateCreateInfo colorBlendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = colorBlendAttachmentStates;
	
	VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineCreateInfo.stageCount = 2;
	pipelineCreateInfo.pStages = shaderStages;
	pipelineCreateInfo.pVertexInputState = &vertexInputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pMultisampleState = (multisampleState != nullptr) ? multisampleState : &defaultMultisampleState;
	pipelineCreateInfo.pDepthStencilState = depthStencilState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.layout = layout;
	pipelineCreateInfo.renderPass = m_renderPass;
	pipelineCreateInfo.subpass = subpass;

	VkPipeline pipeline;
	if(VKFAILED(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline))) {
		throw std::runtime_error("Failed to create graphics pipeline");
	}

	vkDestroyShaderModule(m_device, vertexShader, nullptr);
	vkDestroyShaderModule(m_device, fragmentShader, nullptr);

	return pipeline;
}

VkPipeline Renderer::createComputePipeline(const std::string& cs, VkPipelineLayout layout,
	const VkSpecializationInfo* specializationInfo) const
{
	VkShaderModule computeShader = createShaderModuleFromFile(cs);

	const VkPipelineShaderStageCreateInfo shaderStage = {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, computeShader, "main", specializationInfo,
	};

	VkComputePipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	createInfo.stage = shaderStage;
	createInfo.layout = layout;

	VkPipeline pipeline;
	if(VKFAILED(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline))) {
		throw std::runtime_error("Failed to create compute pipeline");
	}

	vkDestroyShaderModule(m_device, computeShader, nullptr);
	
	return pipeline;
}
	
VkShaderModule Renderer::createShaderModuleFromFile(const std::string& filename) const
{
	std::printf("Loading SPIR-V shader module: %s\n", filename.c_str());

	const auto shaderCode = File::readBinary(filename);
	if(shaderCode.size() == 0) {
		throw std::runtime_error("Invalid shader module file");
	}

	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = shaderCode.size();
	createInfo.pCode    = reinterpret_cast<const uint32_t*>(&shaderCode[0]);

	VkShaderModule shaderModule = VK_NULL_HANDLE;
	if(VKFAILED(vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule))) {
		throw std::runtime_error("Failed to create shader module");
	}
	return shaderModule;
}
	
VkCommandBuffer Renderer::beginImmediateCommandBuffer() const
{
	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if(VKFAILED(vkBeginCommandBuffer(m_commandBuffers[m_frameIndex], &beginInfo))) {
		throw std::runtime_error("Failed to begin immediate command buffer (still in recording state?)");
	}
	return m_commandBuffers[m_frameIndex];
}
	
void Renderer::executeImmediateCommandBuffer(VkCommandBuffer commandBuffer) const
{
	if(VKFAILED(vkEndCommandBuffer(commandBuffer))) {
		throw std::runtime_error("Failed to end immediate command buffer");
	}

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(m_queue);

	if(VKFAILED(vkResetCommandBuffer(commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT))) {
		throw std::runtime_error("Failed to reset immediate command buffer");
	}
}
	
void Renderer::copyToDevice(VkDeviceMemory deviceMemory, const void* data, size_t size) const
{
	const VkMappedMemoryRange flushRange = {
		VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
		nullptr,
		deviceMemory,
		0,
		VK_WHOLE_SIZE
	};

	void* mappedMemory;
	if(VKFAILED(vkMapMemory(m_device, deviceMemory, 0, VK_WHOLE_SIZE, 0, &mappedMemory))) {
		throw std::runtime_error("Failed to map device memory to host address space");
	}
	std::memcpy(mappedMemory, data, size);
	vkFlushMappedMemoryRanges(m_device, 1, &flushRange);
	vkUnmapMemory(m_device, deviceMemory);
}
	
void Renderer::pipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, const std::vector<ImageMemoryBarrier>& barriers) const
{
	vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, (uint32_t)barriers.size(), reinterpret_cast<const VkImageMemoryBarrier*>(barriers.data()));
}

void Renderer::presentFrame()
{
	VkResult presentResult;

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_swapchain;
	presentInfo.pImageIndices = &m_frameIndex;
	presentInfo.pResults = &presentResult;
	if(VKFAILED(vkQueuePresentKHR(m_queue, &presentInfo)) || VKFAILED(presentResult)) {
		throw std::runtime_error("Failed to queue swapchain image presentation");
	}

	if(VKFAILED(vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, VK_NULL_HANDLE, m_presentationFence, &m_frameIndex))) {
		throw std::runtime_error("Failed to acquire next swapchain image");
	}

	const VkFence fences[] = {
		m_presentationFence,
		m_submitFences[m_frameIndex],
	};

	// Submit fence for the next frame will only be valid after that frame has already been submitted to the queue at least once.
	// Wait only for the presentation fence if next frame index is greater than the number of rendered frames so far.
	const uint32_t numFencesToWaitFor = (m_frameCount < m_frameIndex) ? 1 : 2;
	vkWaitForFences(m_device, numFencesToWaitFor, fences, VK_TRUE, UINT64_MAX);
	vkResetFences(m_device, numFencesToWaitFor, fences);

	++m_frameCount;
}
	
PhyDevice Renderer::choosePhyDevice(VkSurfaceKHR surface, const VkPhysicalDeviceFeatures& requiredFeatures, const std::vector<const char*>& requiredExtensions) const
{
	enum RankPriority {
		High   = 10,
		Low    = 1,
	};

	std::vector<VkPhysicalDevice> phyDevices;
	{
		uint32_t numPhyDevices = 0;
		if(VKFAILED(vkEnumeratePhysicalDevices(m_instance, &numPhyDevices, nullptr)) || numPhyDevices == 0) {
			throw std::runtime_error("No Vulkan capable physical devices found");
		}
		phyDevices.resize(numPhyDevices);
		if(VKFAILED(vkEnumeratePhysicalDevices(m_instance, &numPhyDevices, &phyDevices[0]))) {
			throw std::runtime_error("Failed to enumerate Vulkan physical devices");
		}
	}

	std::multimap<int, PhyDevice, std::greater<int>> rankedPhyDevices;
	for(auto phyDeviceHandle : phyDevices) {
		PhyDevice phyDevice = { phyDeviceHandle };
		phyDevice.queueFamilyIndex = -1;
		
		vkGetPhysicalDeviceProperties(phyDevice.handle, &phyDevice.properties);
		vkGetPhysicalDeviceMemoryProperties(phyDevice.handle, &phyDevice.memory);
		vkGetPhysicalDeviceFeatures(phyDevice.handle, &phyDevice.features);

		// Check if all required features are supported.
		bool requiredFeaturesSupported = true;
		for(size_t i=0; i<sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); ++i) {
			if(reinterpret_cast<const VkBool32*>(&requiredFeatures)[i] == VK_TRUE && reinterpret_cast<const VkBool32*>(&phyDevice.features)[i] == VK_FALSE) {
				requiredFeaturesSupported = false;
				break;
			}
		}
		if(!requiredFeaturesSupported) {
			continue;
		}

		// Enumerate current physical device extensions.
		std::vector<VkExtensionProperties> phyDeviceExtensions;
		{
			uint32_t numDeviceExtensions = 0;
			vkEnumerateDeviceExtensionProperties(phyDevice.handle, nullptr, &numDeviceExtensions, nullptr);
			if(numDeviceExtensions > 0) {
				phyDeviceExtensions.resize(numDeviceExtensions);
				vkEnumerateDeviceExtensionProperties(phyDevice.handle, nullptr, &numDeviceExtensions, &phyDeviceExtensions[0]);
			}
		}

		// Check if all required extensions are supported.
		bool requiredExtensionsSupported = true;
		for(const char* requiredExtension : requiredExtensions) {
			bool extensionFound = false;
			for(size_t i=0; i<phyDeviceExtensions.size(); ++i) {
				if(std::strcmp(phyDeviceExtensions[i].extensionName, requiredExtension) == 0) {
					extensionFound = true;
					break;
				}
			}
			if(!extensionFound) {
				requiredExtensionsSupported = false;
				break;
			}
		}
		if(!requiredExtensionsSupported) {
			continue;
		}

		// Check if all required image formats are supported.
		if(!checkPhyDeviceImageFormatsSupport(phyDevice)) {
			continue;
		}

		int rank = 0;

		// Rank discrete GPUs higher.
		switch(phyDevice.properties.deviceType) {
		case VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			rank += RankPriority::High;
			break;
		case VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			rank += RankPriority::Low;
			break;
		}

		// Enumerate queue families and pick one with both graphics & compute capability.
		std::vector<VkQueueFamilyProperties> queueFamilyProperties;
		uint32_t numQueueFamilyProperties = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(phyDevice.handle, &numQueueFamilyProperties, nullptr);
		if(numQueueFamilyProperties > 0) {
			queueFamilyProperties.resize(numQueueFamilyProperties);
			vkGetPhysicalDeviceQueueFamilyProperties(phyDevice.handle, &numQueueFamilyProperties, &queueFamilyProperties[0]);

			for(uint32_t queueFamilyIndex=0; queueFamilyIndex < queueFamilyProperties.size(); ++queueFamilyIndex) {
				const auto& properties = queueFamilyProperties[queueFamilyIndex];

				// VK_QUEUE_TRANSFER_BIT is implied for graphics capable queue families.
				if(!(properties.queueFlags & (VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT | VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT))) {
					continue;
				}
				// Check for presentation support.
				if(glfwGetPhysicalDevicePresentationSupport(m_instance, phyDevice.handle, queueFamilyIndex) != GLFW_TRUE) {
					continue;
				}
				// Check if current queue family supports WSI surface.
				VkBool32 surfaceSupported;
				if(VKFAILED(vkGetPhysicalDeviceSurfaceSupportKHR(phyDevice.handle, queueFamilyIndex, surface, &surfaceSupported)) || !surfaceSupported) {
					continue;
				}

				// Current queue family passed all checks, use it.
				phyDevice.queueFamilyIndex = queueFamilyIndex;
				break;
			}
		}

		// Consider this physical device only if suitable queue family has been found.
		if(phyDevice.queueFamilyIndex != -1) {
			rankedPhyDevices.insert(std::make_pair(rank, phyDevice));
		}
	}

	if(rankedPhyDevices.empty()) {
		throw std::runtime_error("Failed to find suitable Vulkan physical device");
	}
	return rankedPhyDevices.begin()->second;
}
	
void Renderer::queryPhyDeviceSurfaceCapabilities(PhyDevice& phyDevice, VkSurfaceKHR surface) const
{
	if(VKFAILED(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phyDevice.handle, surface, &phyDevice.surfaceCaps))) {
		throw std::runtime_error("Failed to retrieve physical device surface capabilities");
	}

	bool hasSurfaceFormats = false;
	uint32_t numSurfaceFormats = 0;
	if(VKSUCCESS(vkGetPhysicalDeviceSurfaceFormatsKHR(phyDevice.handle, surface, &numSurfaceFormats, nullptr)) && numSurfaceFormats > 0) {
		phyDevice.surfaceFormats.resize(numSurfaceFormats);
		if(VKSUCCESS(vkGetPhysicalDeviceSurfaceFormatsKHR(phyDevice.handle, surface, &numSurfaceFormats, &phyDevice.surfaceFormats[0]))) {
			hasSurfaceFormats = true;
		}
	}
	if(!hasSurfaceFormats) {
		throw std::runtime_error("Failed to retrieve physical device supported surface formats");
	}

	bool hasPresentModes = false;
	uint32_t numPresentModes = 0;
	if(VKSUCCESS(vkGetPhysicalDeviceSurfacePresentModesKHR(phyDevice.handle, surface, &numPresentModes, nullptr)) && numPresentModes > 0) {
		phyDevice.presentModes.resize(numPresentModes);
		if(VKSUCCESS(vkGetPhysicalDeviceSurfacePresentModesKHR(phyDevice.handle, surface, &numPresentModes, &phyDevice.presentModes[0]))) {
			hasPresentModes = true;
		}
	}
	if(!hasPresentModes) {
		throw std::runtime_error("Failed to retrieve physical device supported present modes");
	}
}
	
bool Renderer::checkPhyDeviceImageFormatsSupport(PhyDevice& phyDevice) const
{
	VkFormatProperties formatProperties;
	VkImageFormatProperties imageProperties;

	// Only checking non-mandatory format/usage/feature combinations.

	// Check for depth render target format support.
	if(VKFAILED(vkGetPhysicalDeviceImageFormatProperties(phyDevice.handle,
		VK_FORMAT_D32_SFLOAT, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &imageProperties))) {
		return false;
	}

	// Check for BRDF LUT format support.
	if(VKFAILED(vkGetPhysicalDeviceImageFormatProperties(phyDevice.handle,
		VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0, &imageProperties))) {
		return false;
	}

	// Check for equirect environment map format support.
	if(VKFAILED(vkGetPhysicalDeviceImageFormatProperties(phyDevice.handle,
		VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &imageProperties))) {
		return false;
	}
	// Check for linear sampling feature.
	vkGetPhysicalDeviceFormatProperties(phyDevice.handle, VK_FORMAT_R32G32B32A32_SFLOAT, &formatProperties);
	if(!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
		return false;
	}

	return true;
}
	
uint32_t Renderer::queryRenderTargetFormatMaxSamples(VkFormat format, VkImageUsageFlags usage) const
{
	VkImageFormatProperties properties;
	if(VKFAILED(vkGetPhysicalDeviceImageFormatProperties(m_phyDevice.handle, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties))) {
		return 0;
	}

	for(VkSampleCountFlags maxSampleCount = VK_SAMPLE_COUNT_64_BIT; maxSampleCount > VK_SAMPLE_COUNT_1_BIT; maxSampleCount >>= 1) {
		if(properties.sampleCounts & maxSampleCount) {
			return static_cast<uint32_t>(maxSampleCount);
		}
	}
	return 1;
}
	
uint32_t Renderer::chooseMemoryType(const VkMemoryRequirements& memoryRequirements, VkMemoryPropertyFlags preferredFlags, VkMemoryPropertyFlags requiredFlags) const
{
	auto findMemoryTypeWithFlags = [this](uint32_t memoryTypeBits, uint32_t flags) -> uint32_t
	{
		for(uint32_t index=0; index < VK_MAX_MEMORY_TYPES; ++index) {
			if(memoryTypeBits & (1 << index)) {
				const auto& memoryType = m_phyDevice.memory.memoryTypes[index];
				if((memoryType.propertyFlags & flags) == flags) {
					return index;
				}
			}
		}
		return -1;
	};

	if(requiredFlags == 0) {
		requiredFlags = preferredFlags;
	}

	uint32_t memoryType = findMemoryTypeWithFlags(memoryRequirements.memoryTypeBits, preferredFlags);
	if(memoryType == -1 && requiredFlags != preferredFlags) {
		memoryType = findMemoryTypeWithFlags(memoryRequirements.memoryTypeBits, requiredFlags);
	}
	return memoryType;
}
	
bool Renderer::memoryTypeNeedsStaging(uint32_t memoryTypeIndex) const
{
	assert(memoryTypeIndex < m_phyDevice.memory.memoryTypeCount);
	const VkMemoryPropertyFlags flags = m_phyDevice.memory.memoryTypes[memoryTypeIndex].propertyFlags;
	return (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0;
}

#if _DEBUG
VkBool32 Renderer::logMessage(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
	std::fprintf(stderr, "VK: %s\n", pMessage);
	return VK_FALSE;
}
#endif

} // Vulkan
