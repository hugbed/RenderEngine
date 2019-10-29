#include "DebugUtils.h"

#include "defines.h"

#include <vulkan/vulkan.hpp>

#include <iostream>

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

	return VK_FALSE;
}

PFN_vkCreateDebugUtilsMessengerEXT pfnVkCreateDebugUtilsMessengerEXT;
PFN_vkDestroyDebugUtilsMessengerEXT pfnVkDestroyDebugUtilsMessengerEXT;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger)
{
	return pfnVkCreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, pMessenger);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, VkAllocationCallbacks const* pAllocator)
{
	return pfnVkDestroyDebugUtilsMessengerEXT(instance, messenger, pAllocator);
}

class DynamicLoadError : public vk::SystemError
{
public:
	DynamicLoadError(std::string const& message)
		: SystemError({}, message) {}
	DynamicLoadError(char const* message)
		: SystemError({}, message) {}
};

std::array<const char*, 1> kValidationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

bool CheckValidationLayerSupport()
{
	std::vector<vk::LayerProperties> layerProperties = vk::enumerateInstanceLayerProperties();

	bool layerFound = false;
	for (const char* layerName : kValidationLayers)
	{
		bool layerFound = false;
		for (auto& layerProperty : layerProperties)
		{
			if (strcmp(layerName, layerProperty.layerName) == 0)
			{
				layerFound = true;
				break;
			}
		}
	}
	return layerFound;
}

namespace DebugUtils
{
	void SetupDebugMessenger(const vk::Instance& instance)
	{
		if (DebugUtils::kIsEnabled == false)
		{
			return;
		}
		else if (CheckValidationLayerSupport() == false)
		{
			ASSERT("validation layers requested, but not available!");
			return;
		}

		pfnVkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
		if (!pfnVkCreateDebugUtilsMessengerEXT)
		{
			throw DynamicLoadError("Error trying to load vkCreateDebugUtilsMessengerEXT. Is VK_EXT_DEBUG_UTILS_EXTENSION_NAME enabled?");
		}

		pfnVkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
		if (!pfnVkDestroyDebugUtilsMessengerEXT)
		{
			throw DynamicLoadError("Error trying to load vkCreateDebugUtilsMessengerEXT. Is VK_EXT_DEBUG_UTILS_EXTENSION_NAME enabled?");
		}

		vk::DebugUtilsMessageSeverityFlagsEXT severityFlags =
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;

		vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags =
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
			vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;

		vk::UniqueDebugUtilsMessengerEXT debugUtilsMessenger = instance.createDebugUtilsMessengerEXTUnique(
			vk::DebugUtilsMessengerCreateInfoEXT({}, severityFlags, messageTypeFlags, &debugCallback)
		);
	}
}