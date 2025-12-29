#pragma once

#include <defines.h>
#include <vulkan/vulkan.hpp>

#include <optional>
#include <vector>
#include <cstdint>

// Physical Device singleton implementation. Init/Term is not thread-safe
// however, all other public functions should be const and thus thread-safe.
class PhysicalDevice
{
public:
	using value_type = vk::PhysicalDevice;

	// Singleton
	static void Init(vk::Instance instance, vk::SurfaceKHR surface);
	static void Term();

	IMPLEMENT_MOVABLE_ONLY(PhysicalDevice);

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

	vk::Format FindDepthFormat() const;

	vk::Format FindSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const;

	vk::SampleCountFlagBits GetMaxUsableSampleCount() const;

	vk::SampleCountFlagBits GetMsaaSamples() { return m_msaaSamples; }

	uint32_t GetMinUniformBufferOffsetAlignment() const { return m_minUniformBufferOffsetAlignment; }

	value_type Get() const { return value_type(m_physicalDevice); }

protected:
	vk::PhysicalDevice PickPhysicalDevice();
	bool IsPhysicalDeviceSuitable(vk::PhysicalDevice physicalDevice) const;

	QueueFamilyIndices FindQueueFamilies(vk::PhysicalDevice device) const;

	SwapChainSupportDetails QuerySwapchainSupport(vk::PhysicalDevice physicalDevice) const;

	bool CheckDeviceExtensionSupport(vk::PhysicalDevice device) const;

	uint32_t QueryMinUniformBufferOffsetAlignment() const;

private:
	PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface);

	vk::Instance m_instance;
	vk::SurfaceKHR m_surface;
	vk::PhysicalDevice m_physicalDevice;
	vk::SampleCountFlagBits m_msaaSamples;
	QueueFamilyIndices m_indices;
	uint32_t m_minUniformBufferOffsetAlignment;
};

extern PhysicalDevice* g_physicalDevice;
