#pragma once

#include <vulkan/vulkan.hpp>

#include <optional>
#include <vector>
#include <cstdint>

// Physical Device singleton implementation. Init/Term is not thread-safe
// however, all other public functions should be const and thus thread-safe.
class PhysicalDevice
{
public:
	// Singleton
	static void Init(vk::Instance instance, vk::SurfaceKHR surface);
	static void Term();

	PhysicalDevice(const PhysicalDevice& other) = delete;
	PhysicalDevice& operator=(const PhysicalDevice) = delete;

public:
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

	uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

	vk::PhysicalDevice Get() const { return m_physicalDevice; }

protected:
	vk::PhysicalDevice PickPhysicalDevice();
	bool IsPhysicalDeviceSuitable(vk::PhysicalDevice physicalDevice) const;

	QueueFamilyIndices FindQueueFamilies(vk::PhysicalDevice device) const;

	SwapChainSupportDetails QuerySwapchainSupport(vk::PhysicalDevice physicalDevice) const;

	bool CheckDeviceExtensionSupport(vk::PhysicalDevice device) const;

private:
	PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface);

	vk::Instance m_instance;
	vk::SurfaceKHR m_surface;
	vk::PhysicalDevice m_physicalDevice;

	QueueFamilyIndices m_indices;
};

extern PhysicalDevice* g_physicalDevice;
