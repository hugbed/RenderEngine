#include "Device.h"

#include "DebugUtils.h"

#include "defines.h"

#include <cassert>
#include <iostream>
#include <set>

Device::Device(Window& window)
	: m_window(window)
{
	CreateInstance(window);

	DebugUtils::SetupDebugMessenger(m_instance.get());

	CreateLogicalDevice(PickPhysicalDevice());

	m_surface = window.CreateSurface(m_instance.get());
}

std::vector<const char*> Device::GetRequiredExtensions()
{
	auto extensions = m_window.GetRequiredExtensions();
	
	if (DebugUtils::kIsEnabled)
		extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	return extensions;
}

void Device::CreateInstance(Window& window)
{
	vk::ApplicationInfo appInfo("RenderEngine");

	auto extensions = GetRequiredExtensions();

	vk::InstanceCreateInfo instanceInfo;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledExtensionCount = extensions.size();
	instanceInfo.ppEnabledExtensionNames = extensions.data();
	instanceInfo.enabledLayerCount = 0;

	m_instance = vk::createInstanceUnique(instanceInfo);
}

Device::QueueFamilyIndices Device::FindQueueFamilies(vk::PhysicalDevice device)
{
	QueueFamilyIndices indices;

	std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
			indices.graphicsFamily = i;

		if (device.getSurfaceSupportKHR(i, m_surface.get()))
			indices.presentFamily = i;

		if (indices.IsComplete())
			break;
		
		i++;
	}

	return indices;
}


vk::PhysicalDevice Device::PickPhysicalDevice()
{
	auto physicalDevices = m_instance->enumeratePhysicalDevices();
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

bool Device::IsPhysicalDeviceSuitable(vk::PhysicalDevice physicalDevice)
{
	QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);
	return indices.IsComplete();
}

void Device::CreateLogicalDevice(vk::PhysicalDevice physicalDevice)
{
	QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);

	std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

	float queuePriority = 1.0f;

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

	m_device = physicalDevice.createDeviceUnique(createInfo);

	m_graphicsQueue = m_device->getQueue(indices.graphicsFamily.value(), 0);
	m_presentQueue = m_device->getQueue(indices.presentFamily.value(), 0);
}
