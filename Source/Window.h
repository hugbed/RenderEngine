#pragma once

#include <vulkan/vulkan.hpp>

#include <string_view>
#include <vector>

class GLFWwindow;

class Window
{
public:
	Window(vk::Extent2D extent, std::string_view apiName);

	~Window();

	std::vector<const char*> GetRequiredExtensions() const;

	vk::UniqueSurfaceKHR CreateSurface(vk::Instance instance);

	vk::Extent2D GetFramebufferSize() const;

	bool ShouldClose() const;
	void PollEvents() const;
	void WaitForEvents() const;

	using FramebufferResizedCallback = void (*)(void *obj, int w, int h);
	void SetWindowResizeCallback(void* subscriber, FramebufferResizedCallback callback);

private:
	static void OnResize(GLFWwindow* window, int, int);

	GLFWwindow* m_window;

	FramebufferResizedCallback m_resizeCallback{ nullptr };
	void* m_resizeSubscriber{ nullptr };
};
