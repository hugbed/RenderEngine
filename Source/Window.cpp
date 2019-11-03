#include "Window.h"

#include <vulkan/vulkan.hpp>

#include "defines.h"

Window::Window(vk::Extent2D extent, std::string_view apiName)
{
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	m_window = glfwCreateWindow(extent.width, extent.height, apiName.data(), nullptr, nullptr);
}

Window::~Window()
{
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

std::vector<const char*> Window::GetRequiredExtensions() const
{
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
}

vk::UniqueSurfaceKHR Window::CreateSurface(vk::Instance instance)
{
	VkSurfaceKHR surfaceKHR;
	if (glfwCreateWindowSurface(static_cast<VkInstance>(instance), m_window, nullptr, &surfaceKHR) != VK_SUCCESS)
		ASSERT(false && "failed to create window surface");
	
	vk::ObjectDestroy<vk::Instance, vk::DispatchLoaderStatic> surfaceDeleter(instance);
	return vk::UniqueSurfaceKHR(vk::SurfaceKHR(surfaceKHR), surfaceDeleter );
}
