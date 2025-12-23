#pragma once

#include <RHI/debug_utils.h>
#include <vulkan/vulkan.hpp>

class Window;

class Instance
{
public:
	using value_type = vk::Instance;

	explicit Instance(const Window& window);

	value_type Get() const { return m_instance.get(); }

private:
	static std::vector<const char*> GetRequiredExtensions(const Window& window);

	vk::UniqueInstance m_instance;
	
#ifdef DEBUG_UTILS_ENABLED
	vk::UniqueDebugUtilsMessengerEXT m_debugUtilsMessenger;
#endif
};
