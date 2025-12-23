#include "Window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "defines.h"

Window::Window(vk::Extent2D extent, std::string_view apiName)
{
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	
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
	
	vk::detail::ObjectDestroy<vk::Instance, vk::detail::DispatchLoaderStatic> surfaceDeleter(instance);
	return vk::UniqueSurfaceKHR(vk::SurfaceKHR(surfaceKHR), surfaceDeleter );
}

vk::Extent2D Window::GetFramebufferSize() const
{
	int width, height;
	glfwGetFramebufferSize(m_window, &width, &height);
	return vk::Extent2D(width, height);
}

bool Window::ShouldClose() const
{
	return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

void Window::PollEvents() const
{
	glfwPollEvents();
}

void Window::SetInputMode(int mode, int value) const
{
	glfwSetInputMode(m_window, mode, value);
}

void Window::WaitForEvents() const
{
	glfwWaitEvents();
}

void Window::OnResize(GLFWwindow* glfwWindow, int w, int h)
{
	auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));

	if (window->m_resizeCallback != nullptr)
		window->m_resizeCallback(window->m_owner, w, h);
}

void Window::OnMouseButton(GLFWwindow* glfwWindow, int button, int action, int mods)
{
	auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));

	if (window->m_mouseButtonCallback != nullptr)
		window->m_mouseButtonCallback(window->m_owner, button, action, mods);
}

void Window::OnMouseScroll(GLFWwindow* glfwWindow, double xoffset, double yoffset)
{
	auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));

	if (window->m_mouseScrollCallback != nullptr)
		window->m_mouseScrollCallback(window->m_owner, xoffset, yoffset);
}

void Window::OnCursorPosition(GLFWwindow* glfwWindow, double xPos, double yPos)
{
	auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));

	if (window->m_cursorPositionCallback != nullptr)
		window->m_cursorPositionCallback(window->m_owner, xPos, yPos);
}

void Window::OnKey(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods)
{
	auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));

	if (window->m_keyCallback != nullptr)
		window->m_keyCallback(window->m_owner, key, scancode, action, mods);
}

void Window::SetWindowResizeCallback(void* subscriber, FramebufferResizedCallback callback)
{
	m_owner = subscriber;
	m_resizeCallback = callback;

	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, OnResize);
}

void Window::SetMouseButtonCallback(void* subscriber, MouseButtonEventCallback callback)
{
	m_owner = subscriber;
	m_mouseButtonCallback = callback;

	glfwSetWindowUserPointer(m_window, this);
	glfwSetMouseButtonCallback(m_window, OnMouseButton);
}

void Window::SetMouseScrollCallback(void* subscriber, MouseScrollEventCallback callback)
{
	m_owner = subscriber;
	m_mouseScrollCallback = callback;

	glfwSetWindowUserPointer(m_window, this);
	glfwSetScrollCallback(m_window, OnMouseScroll);
}


void Window::SetCursorPositionCallback(void* subscriber, CursorPositionEventCallback callback)
{
	m_owner = subscriber;
	m_cursorPositionCallback = callback;

	glfwSetWindowUserPointer(m_window, this);
	glfwSetCursorPosCallback(m_window, OnCursorPosition);
}
void Window::SetKeyCallback(void* subscriber, KeyEventCallback callback)
{
	m_owner = subscriber;
	m_keyCallback = callback;

	glfwSetWindowUserPointer(m_window, this);
	glfwSetKeyCallback(m_window, OnKey);
}

void Window::GetSize(int* width, int* height)
{
	glfwGetWindowSize(m_window, width, height);
}


