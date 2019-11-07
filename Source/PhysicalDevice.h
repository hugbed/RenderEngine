#pragma once

#include <vulkan/vulkan.hpp>

#include <optional>
#include <vector>
#include <cstdint>

// This should be a singleton, we won't have 2 devices
class PhysicalDevice
{
public:
	PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface);

	struct QueueFamilyIndices {
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		bool IsComplete() {
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	};

	QueueFamilyIndices GetQueueFamilies() const { return m_indices; }

	struct SwapChainSupportDetails {
		vk::SurfaceCapabilitiesKHR capabilities;
		std::vector<vk::SurfaceFormatKHR> formats;
		std::vector<vk::PresentModeKHR> presentModes;
	};
	SwapChainSupportDetails QuerySwapchainSupport() const;

	std::vector<const char*> GetDeviceExtensions() const;

	uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const
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

	vk::PhysicalDevice Get() { return m_physicalDevice; }
	vk::PhysicalDevice Get() const { return m_physicalDevice; }

protected:
	vk::PhysicalDevice PickPhysicalDevice();
	bool IsPhysicalDeviceSuitable(vk::PhysicalDevice physicalDevice);

	QueueFamilyIndices FindQueueFamilies(vk::PhysicalDevice device) const;

	SwapChainSupportDetails QuerySwapchainSupport(vk::PhysicalDevice physicalDevice) const;

	bool CheckDeviceExtensionSupport(vk::PhysicalDevice device);

private:
	vk::Instance m_instance;
	vk::SurfaceKHR m_surface;
	vk::PhysicalDevice m_physicalDevice;

	QueueFamilyIndices m_indices;
};
