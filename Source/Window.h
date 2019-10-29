#pragma once

#include <vulkan/vulkan.hpp>

#include <string_view>
#include <vector>

class GLFWwindow;

class Window
{
public:
	struct Size
	{
		int height;
		int width;
	};

	Window(Size size, std::string_view apiName);

	~Window();

	std::vector<const char*> GetRequiredExtensions();

	vk::UniqueSurfaceKHR CreateSurface(vk::Instance instance);

	void MainLoop();

private:
	GLFWwindow* m_window;
};
