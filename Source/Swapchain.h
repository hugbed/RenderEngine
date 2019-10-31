#pragma once

#include "Device.h"
#include "PhysicalDevice.h"

#include <vulkan/vulkan.hpp>

#include "PhysicalDevice.h"

#include "defines.h"

#include <vector>

class Swapchain
{
public:
	Swapchain(
		Device& device,
		PhysicalDevice& physicalDevice,
		vk::SurfaceKHR surface,
		vk::Extent2D desiredExtent
	);

private:
	std::vector<vk::ImageView> CreateImageViews(vk::Device device);

	// Swapchain
	vk::UniqueSwapchainKHR m_swapchain;

	// Images
	std::vector<vk::Image> m_images;
	vk::Format m_imageFormat;
	vk::Extent2D m_imageExtent;
	std::vector<vk::ImageView> m_imageViews;
};
