#include "Window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include "defines.h"

Window::Window(Size size, std::string_view apiName)
{
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	m_window = glfwCreateWindow(size.width, size.height, apiName.data(), nullptr, nullptr);
}

Window::~Window()
{
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

std::vector<const char*> Window::GetRequiredExtensions()
{
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
}

void Window::MainLoop()
{
	while (glfwWindowShouldClose(m_window) != GLFW_TRUE) {
		glfwPollEvents();
	}
}

vk::UniqueSurfaceKHR Window::CreateSurface(vk::Instance instance)
{
	VkSurfaceKHR surfaceKHR;
	if (glfwCreateWindowSurface(instance, m_window, nullptr, &surfaceKHR) != VK_SUCCESS) {
		ASSERT(false && "failed to create window surface");
	}
	return vk::UniqueSurfaceKHR( surfaceKHR );
}
