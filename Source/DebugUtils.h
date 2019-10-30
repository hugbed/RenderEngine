#pragma once

#include <vulkan/vulkan.hpp>

#include <array>

namespace vk { class Instance; }

namespace DebugUtils
{
	inline constexpr bool kIsEnabled =
#ifdef _DEBUG
		true;
#else
		false;
#endif

	void SetupDebugMessenger(const vk::Instance& instance);

	static const std::array<const char*, 1> kValidationLayers;
}
