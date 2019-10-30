#pragma once

#include <vulkan/vulkan.hpp>

#include <optional>
#include <cstdint>

class PhysicalDevice
{
public:
	struct QueueFamilyIndices {
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		bool IsComplete() {
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	};

	PhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface);

	const QueueFamilyIndices& GetQueueFamilies() const { return m_indices; }

	explicit operator vk::PhysicalDevice() { return m_physicalDevice; }
	explicit operator vk::PhysicalDevice() const { return m_physicalDevice; }

protected:
	QueueFamilyIndices FindQueueFamilies(vk::PhysicalDevice device);

	vk::PhysicalDevice PickPhysicalDevice();

	bool IsPhysicalDeviceSuitable(vk::PhysicalDevice physicalDevice);

private:
	vk::Instance m_instance;
	vk::SurfaceKHR m_surface;
	vk::PhysicalDevice m_physicalDevice;

	QueueFamilyIndices m_indices;
};
