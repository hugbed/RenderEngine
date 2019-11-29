#pragma once

#include "Image.h"

#include <vulkan/vulkan.hpp>

// An extension of Image to support mipmaps and copying data to the image buffer
class Texture : public Image
{
public:
	using value_type = vk::Image;

	// This could maybe be simplified for textures
	Texture(
		uint32_t width, uint32_t height, uint32_t depth,
		vk::Format format,
		vk::ImageTiling tiling,
		vk::ImageUsageFlags usage,
		vk::MemoryPropertyFlags properties,
		vk::ImageAspectFlags aspectFlags,
		uint32_t mipLevels = 1
	);

	void Overwrite(vk::CommandBuffer& commandBuffer, const void* pixels, vk::ImageLayout dstImageLayout);

	void GenerateMipmaps(vk::CommandBuffer& commandBuffer, vk::ImageLayout dstImageLayout);

private:
	uint32_t m_depth;
	Buffer m_stagingBuffer;
};
