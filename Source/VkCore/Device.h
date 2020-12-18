#pragma once

#include "Instance.h"
#include "defines.h"

#include "vk_mem_alloc.h"

#include <vulkan/vulkan.hpp>

#include <cstdint>

class PhysicalDevice;

// Device singleton implementation. Init/Term is not thread-safe
// however, all other public functions should be const and thus thread-safe.
class Device
{
public:
	using value_type = vk::Device;

	~Device();

	IMPLEMENT_MOVABLE_ONLY(Device);

	static void Init(const PhysicalDevice& physicalDevice);
	static void Term();
	
	value_type Get() const { return value_type(m_device.get()); }

	vk::Queue GetQueue(uint32_t index) const;
	vk::Queue GetGraphicsQueue() const;
	vk::Queue GetPresentQueue() const;

	VmaAllocator GetAllocator() const { return m_allocator; }

private:
	Device(const PhysicalDevice& physicalDevice);

	VmaAllocator m_allocator;
	vk::UniqueDevice m_device;
};

extern Device* g_device;
