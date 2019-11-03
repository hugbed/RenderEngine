#pragma once

#include <vulkan/vulkan.hpp>

#include "DebugUtils.h"

class Window;

class Instance
{
public:
	explicit Instance(const Window& window);

	explicit operator vk::Instance &() { return m_instance.get(); }
	explicit operator vk::Instance const &() const { return m_instance.get(); }

private:
	static std::vector<const char*> GetRequiredExtensions(const Window& window);

	vk::UniqueInstance m_instance;
	
#ifdef DEBUG_UTILS_ENABLED
	vk::UniqueDebugUtilsMessengerEXT m_debugUtilsMessenger;
#endif
};
