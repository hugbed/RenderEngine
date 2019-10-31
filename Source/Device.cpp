#include "Device.h"

#include "PhysicalDevice.h"

#include "DebugUtils.h"

#include "defines.h"

#include <cassert>
#include <iostream>
#include <set>

Device::Device(const PhysicalDevice& physicalDevice)
{
	float queuePriority = 1.0f;

	const auto& indices = physicalDevice.GetQueueFamilies();

	std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };
	
	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		queueCreateInfos.emplace_back(
			vk::DeviceQueueCreateFlags(),	// flags
			queueFamily,					// queueFamilyIndex
			1,								// queueCount
			&queuePriority					// pQueuePriorities
		);
	}

	vk::DeviceCreateInfo createInfo(
		vk::DeviceCreateFlags{},						// flags
		static_cast<uint32_t>(queueCreateInfos.size()),	// queueCreateInfoCount
		queueCreateInfos.data()							// pQueueCreateInfos
	);

	vk::PhysicalDeviceFeatures deviceFeatures;
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = 0;

	if (DebugUtils::kIsEnabled) {
		createInfo.enabledLayerCount = static_cast<uint32_t>(DebugUtils::kValidationLayers.size());
		createInfo.ppEnabledLayerNames = DebugUtils::kValidationLayers.data();
	} else {
		createInfo.enabledLayerCount = 0;
	}

	m_device = static_cast<vk::PhysicalDevice>(physicalDevice).createDeviceUnique(createInfo);
}

vk::Queue Device::GetQueue(uint32_t index)
{
	return m_device->getQueue(index, 0);
}
