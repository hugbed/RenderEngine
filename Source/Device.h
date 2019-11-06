#pragma once

#include "Instance.h"

#include <vulkan/vulkan.hpp>

#include <cstdint>

class PhysicalDevice;

class Device
{
public:
	Device(const PhysicalDevice& physicalDevice);
	
	vk::Queue GetQueue(uint32_t index);

	vk::Device Get() { return m_device.get(); }
	vk::Device Get() const { return m_device.get(); }

private:
	vk::UniqueDevice m_device;
};
