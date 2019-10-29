#pragma once

#include <vulkan/vulkan.hpp>

#include "Window.h" // todo: forward declare

#include <optional>

class Device
{
public:
	Device(Window& window);
	
protected:
	struct QueueFamilyIndices {
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		bool IsComplete() {
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	};
	
	void CreateInstance(Window& window);
	
	std::vector<const char*> GetRequiredExtensions();

	QueueFamilyIndices FindQueueFamilies(vk::PhysicalDevice device);
	vk::PhysicalDevice PickPhysicalDevice();
	bool IsPhysicalDeviceSuitable(vk::PhysicalDevice physicalDevice);
	void CreateLogicalDevice(vk::PhysicalDevice physicalDevice);

private:
	Window& m_window;

	vk::UniqueInstance m_instance;
	vk::UniqueDevice m_device;
	vk::UniqueSurfaceKHR m_surface;
	vk::Queue m_graphicsQueue;
	vk::Queue m_presentQueue;
};
