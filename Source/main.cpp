
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <iostream>

#include "Window.h"
#include "Device.h"

int main()
{
	Window window({ 800, 600 }, "Vulkan");

	Device device(window);

	window.MainLoop();

	return 0;
}
