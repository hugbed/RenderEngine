
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <iostream>

#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "Swapchain.h"

int main()
{
	Window window({ 800, 600 }, "Vulkan");

	Instance instance(window);

	vk::UniqueSurfaceKHR surface = window.CreateSurface(static_cast<vk::Instance>(instance));

	PhysicalDevice physicalDevice(static_cast<vk::Instance>(instance), surface.get());

	Device device(physicalDevice);

	// Queues (should this be done in Device?)
	auto indices = physicalDevice.GetQueueFamilies();
	auto graphicsQueue = device.GetQueue(indices.graphicsFamily.value());
	auto presentQueue = device.GetQueue(indices.presentFamily.value());

	Swapchain swapchain(
		device,
		physicalDevice,
		surface.get(),
		vk::Extent2D(800, 600)
	);

	window.MainLoop();

	return 0;
}
