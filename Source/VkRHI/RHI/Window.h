#pragma once

#include <vulkan/vulkan.hpp>

#include <string_view>
#include <vector>

struct GLFWwindow;

class Window
{
public:
	Window(vk::Extent2D extent, std::string_view apiName);

	~Window();

	std::vector<const char*> GetRequiredExtensions() const;

	vk::UniqueSurfaceKHR CreateSurface(vk::Instance instance);

	vk::Extent2D GetFramebufferSize() const;

	GLFWwindow* GetGLFWWindow() const { return m_window; }

	bool ShouldClose() const;
	void PollEvents() const;
	void WaitForEvents() const;
	void SetInputMode(int mode, int value) const;

	void GetSize(int* width, int* height);

	using FramebufferResizedCallback = void (*)(void *obj, int w, int h);
	using MouseButtonEventCallback = void(*)(void* obj, int button, int action, int mods);
	using MouseScrollEventCallback = void(*)(void* obj, double xoffset, double yoffset);
	using CursorPositionEventCallback = void(*)(void* obj, double xpos, double ypos);
	using KeyEventCallback = void(*)(void* obj, int key, int scancode, int action, int mods);
	void SetWindowResizeCallback(void* subscriber, FramebufferResizedCallback callback);
	void SetMouseButtonCallback(void* subscriber, MouseButtonEventCallback callback);
	void SetMouseScrollCallback(void* subscriber, MouseScrollEventCallback callback);
	void SetCursorPositionCallback(void* subscriber, CursorPositionEventCallback callback);
	void SetKeyCallback(void* subscriber, KeyEventCallback callback);

private:
	static void OnResize(GLFWwindow* window, int, int);
	static void OnMouseButton(GLFWwindow* window, int button, int action, int mods);
	static void OnMouseScroll(GLFWwindow* window, double xoffset, double yoffset);
	static void OnCursorPosition(GLFWwindow* window, double xpos, double ypos);
	static void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods);

	GLFWwindow* m_window;

	FramebufferResizedCallback m_resizeCallback{ nullptr };
	MouseButtonEventCallback m_mouseButtonCallback{ nullptr };
	MouseScrollEventCallback m_mouseScrollCallback{ nullptr };
	CursorPositionEventCallback m_cursorPositionCallback{ nullptr };
	KeyEventCallback m_keyCallback{ nullptr };
	void* m_owner{ nullptr };
};
