#include "Device.h"

#include "PhysicalDevice.h"

#include "debug_utils.h"
#include "defines.h"

#include <set>

Device* g_device;

void Device::Init(const PhysicalDevice& physicalDevice)
{
	if (g_device == nullptr)
		g_device = new Device(physicalDevice);
}

void Device::Term()
{
	if (g_device != nullptr)
		delete g_device;
}

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
	deviceFeatures.samplerAnisotropy = true;
	createInfo.pEnabledFeatures = &deviceFeatures;

	auto deviceExtensions = physicalDevice.GetDeviceExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = deviceExtensions.data();

#ifdef DEBUG_UTILS_ENABLED
	createInfo.enabledLayerCount = static_cast<uint32_t>(debug_utils::kValidationLayers.size());
	createInfo.ppEnabledLayerNames = debug_utils::kValidationLayers.data();
#else
	createInfo.enabledLayerCount = 0;
#endif

	m_device = physicalDevice.Get().createDeviceUnique(createInfo);
}

// Could be in header
vk::Queue Device::GetQueue(uint32_t index) const
{
	return m_device->getQueue(index, 0);
}

vk::Queue Device::GetGraphicsQueue() const
{
	auto queueFamilies = g_physicalDevice->GetQueueFamilies();
	return GetQueue(queueFamilies.graphicsFamily.value());
}

vk::Queue Device::GetPresentQueue() const
{
	auto queueFamilies = g_physicalDevice->GetQueueFamilies();
	return GetQueue(queueFamilies.presentFamily.value());
}
