#pragma once

#include "Buffers.h"

#include <vulkan/vulkan.hpp>

#include <cstdint>

struct ImageDescription
{
	vk::Extent2D extent;
	vk::Format format;
};

class Image
{
public:
	using value_type = vk::Image;

	Image(
		uint32_t width, uint32_t height, uint32_t depth,
		vk::Format format,
		vk::ImageTiling tiling,
		vk::ImageUsageFlags usage,
		vk::ImageAspectFlags aspectFlags,
		vk::ImageViewType imageViewType,
		uint32_t mipLevels = 1,
		uint32_t layerCount = 1, // e.g. 6 for cube map
		vk::SampleCountFlagBits nbSamples = vk::SampleCountFlagBits::e1
	);

	vk::ImageView GetImageView() const { return m_imageView.get(); }

	uint32_t GetMipLevels() const { return m_mipLevels; }

	value_type Get() const { return m_image.Get(); }

protected:
	void TransitionLayout(vk::CommandBuffer& commandBuffer, vk::ImageLayout newLayout);

	void CreateImage(vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::SampleCountFlagBits nbSamples);

	void CreateImageView(vk::ImageAspectFlags aspectFlags);

	vk::Extent3D m_extent;
	vk::Format m_format;
	uint32_t m_mipLevels;
	uint32_t m_layerCount;
	vk::ImageLayout m_imageLayout{ vk::ImageLayout::eUndefined };
	vk::ImageViewType m_imageViewType;
	vk::UniqueImageView m_imageView;
	UniqueImage m_image;
};
