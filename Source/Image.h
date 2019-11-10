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
		vk::MemoryPropertyFlags properties
	);

	void TransitionLayout(vk::CommandBuffer& commandBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

	void Overwrite(vk::CommandBuffer& commandBuffer, const void* pixels);

private:
	vk::Extent3D m_extent;
	vk::Format m_format;
	Buffer m_stagingBuffer;
	vk::UniqueImage m_image;
	vk::UniqueDeviceMemory m_memory;
};
