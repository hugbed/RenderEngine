#pragma once

#include <RHI/Image.h>
#include <RHI/constants.h>
#include <RHI/vk_structs.h>

#include <vector>
#include <optional>

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

	void TransitionImageForRendering(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

	void TransitionImageForPresentation(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

	ImageDescription GetImageDescription() const { return m_imageDescription; }

	size_t GetImageCount() const { return m_images.size(); }

	vk::ImageView GetImageView(uint32_t imageIndex) const
	{
		assert(imageIndex < m_imageViews.size());
		return m_imageViews[imageIndex].get();
	}

	vk::Image GetImage(uint32_t imageIndex) const
	{
		assert(imageIndex < m_imageViews.size());
		return m_images[imageIndex];
	}

	std::vector<vk::ImageView> GetImageViews() const
	{
		std::vector<vk::ImageView> imageViews;
		for (auto& view : m_imageViews)
			imageViews.push_back(view.get());
		return imageViews;
	}

	RenderingInfo GetRenderingInfo(
		uint32_t imageIndex,
		std::optional<vk::ClearColorValue> clearColorValue = std::nullopt,
		std::optional<vk::ClearDepthStencilValue> clearDepthStencilValue = std::nullopt) const;

	PipelineRenderingCreateInfo GetPipelineRenderingCreateInfo() const;

	vk::ImageView GetColorImageView() const { return m_colorImage->GetImageView(); }

	const vk::Format& GetColorAttachmentFormat() const { return m_colorImage->GetFormat(); }

	vk::ImageView GetDepthImageView() const { return m_depthImage->GetImageView(); }
	
	const vk::Format& GetDepthAttachmentFormat() const { return m_depthImage->GetFormat(); }

	vk::SurfaceFormatKHR GetSurfaceFormat() const { return m_surfaceFormat; }

	vk::PresentModeKHR GetPresentMode() const { return m_presentMode; }

	vk::Extent2D GetImageExtent() const { return m_imageDescription.extent; }

	const value_type& Get() const { return m_swapchain.get(); }

private:
	void CreateImageViews();

	// Surface
	vk::SurfaceFormatKHR m_surfaceFormat;
	vk::PresentModeKHR m_presentMode;

	// Swapchain
	vk::UniqueSwapchainKHR m_swapchain;

	// Images
	ImageDescription m_imageDescription;
	std::vector<vk::Image> m_images;
	std::vector<vk::UniqueImageView> m_imageViews;

	// Depth buffer
	std::unique_ptr<Image> m_depthImage;
	std::unique_ptr<Image> m_colorImage;
};
