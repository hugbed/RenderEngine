#pragma once
// todo: move into CPP when refactoring MainLoop
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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

	template <class Func>
	void MainLoop(Func f)
	{
		while (glfwWindowShouldClose(m_window) != GLFW_TRUE) {
			glfwPollEvents();
			f();
		}
	}

private:
	GLFWwindow* m_window;
};
