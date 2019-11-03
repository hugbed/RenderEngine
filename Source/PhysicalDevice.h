#pragma once

#include <vulkan/vulkan.hpp>

#include <optional>
#include <vector>
#include <cstdint>

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

	explicit operator vk::PhysicalDevice &() { return m_physicalDevice; }
	explicit operator vk::PhysicalDevice const &() const { return m_physicalDevice; }

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
