#pragma once

#include "Buffers.h"

#include <vulkan/vulkan.hpp>

#include <cstdint>

struct Image
{
public:
	Image(
		uint32_t width, uint32_t height, uint32_t depth,
		vk::Format format,
		vk::ImageTiling tiling,
		vk::ImageUsageFlags usage,
		vk::MemoryPropertyFlags properties,
		vk::ImageAspectFlags aspectFlags,
		uint32_t mipLevels = 1
	);

	void CreateStagingBuffer();

	// To reserve a staging buffer for memory overwrite, use CreateStagingBuffer
	// else, it will be created when calling this function
	void Overwrite(vk::CommandBuffer& commandBuffer, const void* pixels, vk::ImageLayout dstImageLayout);

	uint32_t GetMipLevels() const { return m_mipLevels; }

	vk::ImageView GetImageView() const { return m_imageView.get(); }

private:
	void TransitionLayout(vk::CommandBuffer& commandBuffer, vk::ImageLayout newLayout);

	void CreateImage(vk::ImageTiling tiling, vk::ImageUsageFlags usage);
	void InitImageMemory(vk::MemoryPropertyFlags properties);
	void CreateImageView(vk::ImageAspectFlags aspectFlags);
	void GenerateMipmaps(vk::CommandBuffer& commandBuffer, vk::ImageLayout dstImageLayout);

	vk::Extent3D m_extent;
	vk::Format m_format;
	vk::ImageLayout m_imageLayout{ vk::ImageLayout::eUndefined };
	vk::UniqueImage m_image;
	vk::UniqueDeviceMemory m_memory;
	vk::UniqueImageView m_imageView;

	// Should go in texture
	uint32_t m_depth;
	uint32_t m_mipLevels;
	std::unique_ptr<Buffer> m_stagingBuffer{ nullptr };
};
