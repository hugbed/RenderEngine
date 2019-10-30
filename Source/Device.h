#pragma once

#include "Instance.h"

#include <vulkan/vulkan.hpp>

#include <optional>

class PhysicalDevice;

class Device
{
public:
	Device(const PhysicalDevice& physicalDevice);
	
	explicit operator vk::Device() { return m_device.get(); }
	explicit operator vk::Device() const { return m_device.get(); }

private:
	const PhysicalDevice& m_physicalDevice;

	vk::UniqueDevice m_device;
	vk::Queue m_graphicsQueue;
	vk::Queue m_presentQueue;
};
