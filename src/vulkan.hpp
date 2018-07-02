/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
 */

#pragma once

#include <cstdint>
#include <volk.h>

#include "common/renderer.hpp"

namespace Vulkan {

class Renderer final : public RendererInterface
{
public:
	GLFWwindow* initialize(int width, int height, int maxSamples) override;
	void shutdown() override;
	void setup() override;
	void render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene) override;

private:
	struct PhyDeviceInfo
	{
		VkPhysicalDevice phyDevice;
		VkPhysicalDeviceProperties properties;
		uint32_t queueFamilyIndex;
	};
	PhyDeviceInfo choosePhyDevice() const;

#if _DEBUG
	static VkBool32 logMessage(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData);
	VkDebugReportCallbackEXT m_logCallback;
#endif

	VkInstance m_instance;
	VkPhysicalDevice m_phyDevice;
	VkDevice m_device;
};

} // Vulkan
