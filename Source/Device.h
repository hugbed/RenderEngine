#pragma once

#include "Instance.h"

#include <vulkan/vulkan.hpp>

#include <optional>
#include <cstdint>

class PhysicalDevice;

class Device
{
public:
	Device(const PhysicalDevice& physicalDevice);
	
	vk::Queue GetQueue(uint32_t index);

	explicit operator vk::Device &() { return m_device.get(); }
	explicit operator vk::Device const &() const { return m_device.get(); }

private:
	vk::UniqueDevice m_device;
};
