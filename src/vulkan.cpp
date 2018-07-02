/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
 */

#include <stdexcept>
#include <vector>
#include <map>

#include "vulkan.hpp"
#include <GLFW/glfw3.h>

#define VKFAILED(x) ((x) != VK_SUCCESS)

namespace Vulkan {

GLFWwindow* Renderer::initialize(int width, int height, int maxSamples)
{
	if(VKFAILED(volkInitialize())) {
		throw std::runtime_error("Vulkan loader has not been found");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(width, height, "Physically Based Rendering (Vulkan)", nullptr, nullptr);
	if(!window) {
		throw std::runtime_error("Failed to create window");
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

	// Create logical device
	PhyDeviceInfo phyDeviceInfo = choosePhyDevice();
	m_phyDevice = phyDeviceInfo.phyDevice;
	{
		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueCreateInfo.queueFamilyIndex = phyDeviceInfo.queueFamilyIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		
		VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		if(VKFAILED(vkCreateDevice(m_phyDevice, &createInfo, nullptr, &m_device))) {
			throw std::runtime_error("Failed to create Vulkan logical device");
		}
		volkLoadDevice(m_device);
	}

	std::printf("Vulkan 1.0 Renderer [%s]\n", phyDeviceInfo.properties.deviceName);
	return window;
}
	
void Renderer::shutdown()
{
	vkDeviceWaitIdle(m_device);
	vkDestroyDevice(m_device, nullptr);

#if _DEBUG
	vkDestroyDebugReportCallbackEXT(m_instance, m_logCallback, nullptr);
#endif
	vkDestroyInstance(m_instance, nullptr);
}
	
void Renderer::setup()
{

}
	
void Renderer::render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene)
{

}
	
Renderer::PhyDeviceInfo Renderer::choosePhyDevice() const
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

	std::multimap<int, PhyDeviceInfo, std::greater<int>> rankedPhyDevices;
	for(auto phyDevice : phyDevices) {
		PhyDeviceInfo info = { phyDevice, 0, uint32_t(-1) };
		vkGetPhysicalDeviceProperties(phyDevice, &info.properties);

		int rank = 0;

		// Rank discrete GPUs higher.
		switch(info.properties.deviceType) {
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
		vkGetPhysicalDeviceQueueFamilyProperties(phyDevice, &numQueueFamilyProperties, nullptr);
		if(numQueueFamilyProperties > 0) {
			queueFamilyProperties.resize(numQueueFamilyProperties);
			vkGetPhysicalDeviceQueueFamilyProperties(phyDevice, &numQueueFamilyProperties, &queueFamilyProperties[0]);

			for(size_t queueFamilyIndex=0; queueFamilyIndex < queueFamilyProperties.size(); ++queueFamilyIndex) {
				const auto& properties = queueFamilyProperties[queueFamilyIndex];
				// VK_QUEUE_TRANSFER_BIT is implied for graphics capable queue families.
				if(properties.queueFlags & (VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT | VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT)) {
					info.queueFamilyIndex = (uint32_t)queueFamilyIndex;
					break;
				}
			}
		}

		// Consider this physical device only if suitable queue family has been found.
		if(info.queueFamilyIndex != -1) {
			rankedPhyDevices.insert(std::make_pair(rank, info));
		}
	}

	if(rankedPhyDevices.empty()) {
		throw std::runtime_error("Failed to find suitable Vulkan physical device");
	}
	return rankedPhyDevices.begin()->second;
}
	
VkBool32 Renderer::logMessage(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
	std::fprintf(stderr, "VK: %s\n", pMessage);
	return VK_FALSE;
}

} // Vulkan
