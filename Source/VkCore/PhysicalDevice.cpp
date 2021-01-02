#include "PhysicalDevice.h"

#include "defines.h"

#include <set>

PhysicalDevice* g_physicalDevice;

void PhysicalDevice::Init(vk::Instance instance, vk::SurfaceKHR surface)
{
	if (g_physicalDevice == nullptr)
		g_physicalDevice = new PhysicalDevice(instance, surface);
}

void PhysicalDevice::Term()
{
	if (g_physicalDevice != nullptr)
		delete g_physicalDevice;
}

PhysicalDevice::PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface)
	: m_instance(instance)
	, m_surface(surface)
{
	m_physicalDevice = PickPhysicalDevice();
	m_indices = FindQueueFamilies(m_physicalDevice);
	m_msaaSamples = GetMaxUsableSampleCount();
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

bool PhysicalDevice::IsPhysicalDeviceSuitable(vk::PhysicalDevice physicalDevice) const
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

	auto supportedFeatures = physicalDevice.getFeatures();
	if (!supportedFeatures.samplerAnisotropy)
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

bool PhysicalDevice::CheckDeviceExtensionSupport(vk::PhysicalDevice device) const
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
	return std::vector<const char*>{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
	};
}

uint32_t PhysicalDevice::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const
{
	vk::PhysicalDeviceMemoryProperties memProperties = m_physicalDevice.getMemoryProperties();

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type");
	return UINT32_MAX;
}

vk::Format PhysicalDevice::FindDepthFormat() const
{
	return FindSupportedFormat(
		{ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
		vk::ImageTiling::eOptimal,
		vk::FormatFeatureFlagBits::eDepthStencilAttachment
	);
}

vk::Format PhysicalDevice::FindSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const
{
	for (vk::Format format : candidates)
	{
		auto props = m_physicalDevice.getFormatProperties(format);
		if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
			return format;
		else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
			return format;
	}

	throw std::runtime_error("Failed to find supported format");
	return {};
}

vk::SampleCountFlagBits PhysicalDevice::GetMaxUsableSampleCount() const
{
	auto properties = m_physicalDevice.getProperties();

	vk::SampleCountFlags counts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;

	if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
	if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
	if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
	if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
	if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
	if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }

	return vk::SampleCountFlagBits::e1;
}
