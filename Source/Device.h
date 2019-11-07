#pragma once

#include "Instance.h"

#include <vulkan/vulkan.hpp>

#include <cstdint>

class PhysicalDevice;

// Physical Device singleton implementation. Init/Term is not thread-safe
// however, all other public functions should be const and thus thread-safe.
class Device
{
public:
	static void Init(const PhysicalDevice& physicalDevice);
	static void Term();
	
	vk::Queue GetQueue(uint32_t index);

	vk::Device Get() { return m_device.get(); }
	vk::Device Get() const { return m_device.get(); }

private:
	Device(const PhysicalDevice& physicalDevice);

	vk::UniqueDevice m_device;
};

extern Device* g_device;
