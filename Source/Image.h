#pragma once

#include "Buffers.h"

#include <vulkan/vulkan.hpp>

#include <cstdint>

struct Image
{
public:
	using value_type = vk::Image;

	Image(
		uint32_t width, uint32_t height, uint32_t depth,
		vk::Format format,
		vk::ImageTiling tiling,
		vk::ImageUsageFlags usage,
		vk::MemoryPropertyFlags properties,
		vk::ImageAspectFlags aspectFlags,
		uint32_t mipLevels = 1
	);

	vk::ImageView GetImageView() const { return m_imageView.get(); }

	uint32_t GetMipLevels() const { return m_mipLevels; }

	value_type Get() const { return m_image.get(); }

protected:
	void TransitionLayout(vk::CommandBuffer& commandBuffer, vk::ImageLayout newLayout);

	void CreateImage(vk::ImageTiling tiling, vk::ImageUsageFlags usage);
	void InitImageMemory(vk::MemoryPropertyFlags properties);
	void CreateImageView(vk::ImageAspectFlags aspectFlags);

	vk::Extent3D m_extent;
	vk::Format m_format;
	uint32_t m_mipLevels;
	vk::ImageLayout m_imageLayout{ vk::ImageLayout::eUndefined };
	vk::UniqueImage m_image;
	vk::UniqueDeviceMemory m_memory;
	vk::UniqueImageView m_imageView;
};
