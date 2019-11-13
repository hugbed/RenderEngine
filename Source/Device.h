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
	using value_type = vk::Device;

	static void Init(const PhysicalDevice& physicalDevice);
	static void Term();
	
	vk::Queue GetQueue(uint32_t index) const;
	vk::Queue GetGraphicsQueue() const;
	vk::Queue GetPresentQueue() const;

	value_type Get() const { return m_device.get(); }

private:
	Device(const PhysicalDevice& physicalDevice);

	vk::UniqueDevice m_device;
};

extern Device* g_device;
