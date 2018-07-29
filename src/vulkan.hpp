/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
 *
 * Vulkan 1.0 renderer.
 */

#pragma once

#if defined(ENABLE_VULKAN)

#include <cstdint>
#include <memory>
#include <vector>
#include <initializer_list>

#include <volk.h>

#include "common/renderer.hpp"

class Mesh;
class Image;

namespace Vulkan {

struct PhyDevice
{
	VkPhysicalDevice handle;
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceMemoryProperties memory;
	VkPhysicalDeviceFeatures features;
	VkSurfaceCapabilitiesKHR surfaceCaps;
	std::vector<VkSurfaceFormatKHR> surfaceFormats;
	std::vector<VkPresentModeKHR> presentModes;
	uint32_t queueFamilyIndex;
};

template<class T>
struct Resource
{
	T resource;
	VkDeviceMemory memory;
	VkDeviceSize allocationSize;
	uint32_t memoryTypeIndex;
};

struct MeshBuffer
{
	Resource<VkBuffer> vertexBuffer;
	Resource<VkBuffer> indexBuffer;
	uint32_t numElements;
};

struct Texture
{
	Resource<VkImage> image;
	VkImageView view;
	uint32_t width, height;
	uint32_t layers;
	uint32_t levels;
};

struct RenderTarget
{
	Resource<VkImage> colorImage;
	Resource<VkImage> depthImage;
	VkImageView colorView;
	VkImageView depthView;
	VkFormat colorFormat;
	VkFormat depthFormat;
	uint32_t width, height;
	uint32_t samples;
};

struct UniformBuffer
{
	Resource<VkBuffer> buffer;
	VkDeviceSize capacity;
	VkDeviceSize cursor;
	void* hostMemoryPtr;
};

struct UniformBufferAllocation
{
	VkDescriptorBufferInfo descriptorInfo;
	void* hostMemoryPtr;

	template<typename T> T* as() const
	{
		return reinterpret_cast<T*>(hostMemoryPtr);
	}
};

struct ImageMemoryBarrier
{
	ImageMemoryBarrier(const Texture& texture, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout)
	{
		barrier.srcAccessMask = srcAccessMask;
		barrier.dstAccessMask = dstAccessMask;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture.image.resource;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	}
	operator VkImageMemoryBarrier() const { return barrier; }
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

	ImageMemoryBarrier& aspectMask(VkImageAspectFlags aspectMask)
	{ 
		barrier.subresourceRange.aspectMask = aspectMask;
		return *this;
	}
	ImageMemoryBarrier& mipLevels(uint32_t baseMipLevel, uint32_t levelCount = VK_REMAINING_MIP_LEVELS)
	{
		barrier.subresourceRange.baseMipLevel = baseMipLevel;
		barrier.subresourceRange.levelCount = levelCount;
		return *this;
	}
	ImageMemoryBarrier& arrayLayers(uint32_t baseArrayLayer, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS)
	{
		barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
		barrier.subresourceRange.layerCount = layerCount;
		return *this;
	}
};

class Renderer final : public RendererInterface
{
public:
	GLFWwindow* initialize(int width, int height, int maxSamples) override;
	void shutdown() override;
	void setup() override;
	void render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene) override;

private:
	Resource<VkBuffer> createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags) const;
	Resource<VkImage> createImage(uint32_t width, uint32_t height, uint32_t layers, uint32_t levels, VkFormat format, uint32_t samples, VkImageUsageFlags usage) const;
	void destroyBuffer(Resource<VkBuffer>& buffer) const;
	void destroyImage(Resource<VkImage>& image) const;

	MeshBuffer createMeshBuffer(const std::shared_ptr<Mesh>& mesh) const;
	void destroyMeshBuffer(MeshBuffer& buffer) const;

	Texture createTexture(uint32_t width, uint32_t height, uint32_t layers, VkFormat format, uint32_t levels=0, VkImageUsageFlags additionalUsage=0) const;
	Texture createTexture(const std::shared_ptr<Image>& image, VkFormat format, uint32_t levels=0) const;
	VkImageView createTextureView(const Texture& texture, VkFormat format, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t numMipLevels) const;
	void generateMipmaps(const Texture& texture) const;
	void destroyTexture(Texture& texture) const;

	RenderTarget createRenderTarget(uint32_t width, uint32_t height, uint32_t samples, VkFormat colorFormat, VkFormat depthFormat) const;
	void destroyRenderTarget(RenderTarget& rt) const;

	UniformBuffer createUniformBuffer(VkDeviceSize capacity) const;
	void destroyUniformBuffer(UniformBuffer& buffer) const;

	UniformBufferAllocation allocFromUniformBuffer(UniformBuffer& buffer, VkDeviceSize size) const;
	template<typename T> UniformBufferAllocation allocFromUniformBuffer(UniformBuffer& buffer) const
	{
		return allocFromUniformBuffer(buffer, sizeof(T));
	}

	VkDescriptorSet allocateDescriptorSet(VkDescriptorPool pool, VkDescriptorSetLayout layout) const;
	void updateDescriptorSet(VkDescriptorSet dstSet, uint32_t dstBinding, VkDescriptorType descriptorType, const std::vector<VkDescriptorImageInfo>& descriptors) const;
	void updateDescriptorSet(VkDescriptorSet dstSet, uint32_t dstBinding, VkDescriptorType descriptorType, const std::vector<VkDescriptorBufferInfo>& descriptors) const;

	VkDescriptorSetLayout createDescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>* bindings=nullptr) const;
	VkPipelineLayout createPipelineLayout(const std::vector<VkDescriptorSetLayout>* setLayouts=nullptr, const std::vector<VkPushConstantRange>* pushConstants=nullptr) const;

	VkPipeline createGraphicsPipeline(uint32_t subpass,
		const std::string& vs, const std::string& fs, VkPipelineLayout layout,
		const std::vector<VkVertexInputBindingDescription>* vertexInputBindings = nullptr,
		const std::vector<VkVertexInputAttributeDescription>* vertexAttributes = nullptr,
		const VkPipelineMultisampleStateCreateInfo* multisampleState = nullptr,
		const VkPipelineDepthStencilStateCreateInfo* depthStencilState = nullptr) const;

	VkPipeline createComputePipeline(const std::string& cs, VkPipelineLayout layout,
		const VkSpecializationInfo* specializationInfo=nullptr) const;

	VkShaderModule createShaderModuleFromFile(const std::string& filename) const;

	VkCommandBuffer beginImmediateCommandBuffer() const;
	void executeImmediateCommandBuffer(VkCommandBuffer commandBuffer) const;
	void copyToDevice(VkDeviceMemory deviceMemory, const void* data, size_t size) const;
	void pipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, const std::vector<ImageMemoryBarrier>& barriers) const;

	void presentFrame();

	PhyDevice choosePhyDevice(VkSurfaceKHR surface, const VkPhysicalDeviceFeatures& requiredFeatures, const std::vector<const char*>& requiredExtensions) const;
	void queryPhyDeviceSurfaceCapabilities(PhyDevice& phyDevice, VkSurfaceKHR surface) const;
	bool checkPhyDeviceImageFormatsSupport(PhyDevice& phyDevice) const;
	
	uint32_t queryRenderTargetFormatMaxSamples(VkFormat format, VkImageUsageFlags usage) const;
	uint32_t chooseMemoryType(const VkMemoryRequirements& memoryRequirements, VkMemoryPropertyFlags preferredFlags, VkMemoryPropertyFlags requiredFlags=0) const;
	bool memoryTypeNeedsStaging(uint32_t memoryTypeIndex) const;

#if _DEBUG
	static VkBool32 logMessage(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData);
	VkDebugReportCallbackEXT m_logCallback;
#endif

	VkInstance m_instance;
	VkDevice m_device;
	VkQueue m_queue;
	PhyDevice m_phyDevice;

	VkCommandPool m_commandPool;
	VkDescriptorPool m_descriptorPool;

	VkRenderPass m_renderPass;
	
	VkDescriptorSet m_pbrDescriptorSet;
	VkPipelineLayout m_pbrPipelineLayout;
	VkPipeline m_pbrPipeline;

	VkDescriptorSet m_skyboxDescriptorSet;
	VkPipelineLayout m_skyboxPipelineLayout;
	VkPipeline m_skyboxPipeline;

	std::vector<VkDescriptorSet> m_tonemapDescriptorSets;
	VkPipelineLayout m_tonemapPipelineLayout;
	VkPipeline m_tonemapPipeline;

	VkSampler m_defaultSampler;
	VkSampler m_spBRDFSampler;

	VkSurfaceKHR m_surface;
	VkSwapchainKHR m_swapchain;

	uint32_t m_numFrames;
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainViews;
	std::vector<VkFramebuffer> m_framebuffers;
	std::vector<VkCommandBuffer> m_commandBuffers;
	std::vector<VkFence> m_submitFences;
	std::vector<RenderTarget> m_renderTargets;
	std::vector<RenderTarget> m_resolveRenderTargets;

	VkFence m_presentationFence;

	uint32_t m_renderSamples;
	VkRect2D m_frameRect;
	uint32_t m_frameIndex;
	uint32_t m_frameCount;

	UniformBuffer m_uniformBuffer;
	std::vector<UniformBufferAllocation> m_transformUniforms;
	std::vector<UniformBufferAllocation> m_shadingUniforms;
	std::vector<VkDescriptorSet> m_uniformsDescriptorSets;

	MeshBuffer m_pbrModel;
	MeshBuffer m_skybox;

	Texture m_albedoTexture;
	Texture m_normalTexture;
	Texture m_metalnessTexture;
	Texture m_roughnessTexture;

	Texture m_envTexture;
	Texture m_irmapTexture;
	Texture m_spBRDF_LUT;
};

} // Vulkan

#endif // ENABLE_VULKAN
