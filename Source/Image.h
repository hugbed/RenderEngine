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
		vk::ImageAspectFlags aspectFlags
	);

	void TransitionLayout(vk::CommandBuffer& commandBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

	void CreateStagingBuffer();

	// To reserve a staging buffer for memory overwrite, use CreateStagingBuffer
	// else, it will be created when calling this function
	void Overwrite(vk::CommandBuffer& commandBuffer, const void* pixels);

	vk::ImageView GetImageView() const { return m_imageView.get(); }

private:
	void CreateImage(vk::ImageTiling tiling, vk::ImageUsageFlags usage);
	void InitImageMemory(vk::MemoryPropertyFlags properties);
	void CreateImageView(vk::ImageAspectFlags aspectFlags);

	vk::Extent3D m_extent;
	vk::Format m_format;
	uint32_t m_depth;
	std::unique_ptr<Buffer> m_stagingBuffer{ nullptr }; // optional, useful for textures
	vk::UniqueImage m_image;
	vk::UniqueDeviceMemory m_memory;
	vk::UniqueImageView m_imageView;
};
