#include "PhysicalDevice.h"

#include "defines.h"

PhysicalDevice::PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface)
	: m_instance(instance)
	, m_surface(surface)
{
	m_physicalDevice = PickPhysicalDevice();
	m_indices = FindQueueFamilies(m_physicalDevice);
}

PhysicalDevice::QueueFamilyIndices PhysicalDevice::FindQueueFamilies(vk::PhysicalDevice device)
{
	QueueFamilyIndices indices;

	std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
			indices.graphicsFamily = i;

		if (device.getSurfaceSupportKHR(i, m_surface))
			indices.presentFamily = i;

		if (indices.IsComplete())
			break;

		i++;
	}

	return indices;
}

vk::PhysicalDevice PhysicalDevice::PickPhysicalDevice()
{
	auto physicalDevices = m_instance.enumeratePhysicalDevices();
	ASSERT(physicalDevices.size() > 0 && "Failed to find GPU with Vulkan support");

	vk::PhysicalDevice suitablePhysicalDevice;
	for (const auto& device : physicalDevices)
	{
		if (IsPhysicalDeviceSuitable(device))
		{
			suitablePhysicalDevice = device;
			break;
		}
	}
	ASSERT(static_cast<VkPhysicalDevice>(suitablePhysicalDevice) != VK_NULL_HANDLE && "Failed to find suitable GPU");

	return suitablePhysicalDevice;
}

bool PhysicalDevice::IsPhysicalDeviceSuitable(vk::PhysicalDevice physicalDevice)
{
	QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);
	return indices.IsComplete();
}
