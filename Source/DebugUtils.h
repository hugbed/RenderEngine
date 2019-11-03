#pragma once

#include <vulkan/vulkan.hpp>

#include <array>

namespace vk { class Instance; }

#ifdef _DEBUG
#define DEBUG_UTILS_ENABLED
#endif

namespace DebugUtils
{
	vk::UniqueDebugUtilsMessengerEXT SetupDebugMessenger(const vk::Instance& instance);

	inline constexpr std::array<const char*, 1> kValidationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};
}
