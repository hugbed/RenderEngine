#pragma once

#include "Image.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class Device;
class PhysicalDevice;

class Swapchain
{
public:
	using value_type = vk::SwapchainKHR;

	Swapchain(
		vk::SurfaceKHR surface,
		vk::Extent2D desiredExtent
	);

	vk::Format GetImageFormat() const { return m_imageFormat; }

	vk::Extent2D GetImageExtent() const { return m_imageExtent; }

	uint32_t GetImageCount() const { return m_images.size(); }

	std::vector<vk::ImageView> GetImageViews() const
	{
		std::vector<vk::ImageView> imageViews;
		for (auto& view : m_imageViews)
			imageViews.push_back(view.get());
		return imageViews;
	}

	vk::ImageView GetColorImageView() const { return m_colorImage->GetImageView(); }

	vk::ImageView GetDepthImageView() const { return m_depthImage->GetImageView(); }

	value_type Get() const { return m_swapchain.get(); }

private:
	void CreateImageViews();

	// Swapchain
	vk::UniqueSwapchainKHR m_swapchain;

	// Images
	std::vector<vk::Image> m_images;
	vk::Format m_imageFormat;
	vk::Extent2D m_imageExtent;
	std::vector<vk::UniqueImageView> m_imageViews;

	// Depth buffer
	std::unique_ptr<Image> m_depthImage;
	std::unique_ptr<Image> m_colorImage;
};
