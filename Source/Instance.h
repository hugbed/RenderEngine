#pragma once

#include <vulkan/vulkan.hpp>

#include "DebugUtils.h"

class Window;

class Instance
{
public:
	explicit Instance(const Window& window);

	vk::Instance Get() { return m_instance.get(); }
	vk::Instance Get() const { return m_instance.get(); }

private:
	static std::vector<const char*> GetRequiredExtensions(const Window& window);

	vk::UniqueInstance m_instance;
	
#ifdef DEBUG_UTILS_ENABLED
	vk::UniqueDebugUtilsMessengerEXT m_debugUtilsMessenger;
#endif
};
