#pragma once

#include <vulkan/vulkan.hpp>

class Window;

class Instance
{
public:
	explicit Instance(const Window& window);

	explicit operator vk::Instance() { return m_instance.get(); }
	explicit operator vk::Instance() const { return m_instance.get(); }

private:
	static std::vector<const char*> GetRequiredExtensions(const Window& window);

	vk::UniqueInstance m_instance;
};
