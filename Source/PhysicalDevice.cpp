#include "PhysicalDevice.h"

#include "defines.h"

#include <set>

PhysicalDevice::PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface)
	: m_instance(instance)
	, m_surface(surface)
{
	m_physicalDevice = PickPhysicalDevice();
	m_indices = FindQueueFamilies(m_physicalDevice);
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
	if (indices.IsComplete() == false)
		return false;

	bool extensionsSupported = CheckDeviceExtensionSupport(physicalDevice);
	if (extensionsSupported == false)
		return false;

	SwapChainSupportDetails swapChainSupport = QuerySwapchainSupport(physicalDevice);
	if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty())
		return false;

	return true;
}

PhysicalDevice::QueueFamilyIndices PhysicalDevice::FindQueueFamilies(vk::PhysicalDevice device) const
{
	QueueFamilyIndices indices;

	std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

	int i = 0;
	for (const auto& queueFamily : queueFamilies)
	{
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

bool PhysicalDevice::CheckDeviceExtensionSupport(vk::PhysicalDevice device)
{
	auto availableExtensions = device.enumerateDeviceExtensionProperties();
	auto deviceExtensions = GetDeviceExtensions();

	std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
	for (const auto& extension : availableExtensions)
		requiredExtensions.erase(extension.extensionName);

	return requiredExtensions.empty();
}

PhysicalDevice::SwapChainSupportDetails PhysicalDevice::QuerySwapchainSupport() const
{
	return QuerySwapchainSupport(m_physicalDevice);
}

PhysicalDevice::SwapChainSupportDetails PhysicalDevice::QuerySwapchainSupport(vk::PhysicalDevice physicalDevice) const
{
	SwapChainSupportDetails details;
	details.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(m_surface);
	details.formats = physicalDevice.getSurfaceFormatsKHR(m_surface);
	details.presentModes = physicalDevice.getSurfacePresentModesKHR(m_surface);
	return details;
}

std::vector<const char*> PhysicalDevice::GetDeviceExtensions() const
{
	return {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
}
