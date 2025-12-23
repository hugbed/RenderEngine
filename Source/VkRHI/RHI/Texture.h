#pragma once

#include <RHI/Image.h>
#include <vulkan/vulkan.hpp>

#include <memory>

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
		vk::ImageAspectFlags aspectFlags,
		vk::ImageViewType imageViewType = vk::ImageViewType::e2D,
		uint32_t mipLevels = 1,
		uint32_t layerCount = 1 // e.g. 6 for cube map
	);

	// Useful for one time upload
	UniqueBuffer* ReleaseStagingBuffer() { return m_stagingBuffer.release(); }

	void* GetStagingMappedData() const { return m_stagingBuffer->GetMappedData(); }

	void UploadStagingToGPU(vk::CommandBuffer& commandBuffer, vk::ImageLayout dstImageLayout);

	void GenerateMipmaps(vk::CommandBuffer& commandBuffer, vk::ImageLayout dstImageLayout);

private:
	uint32_t m_depth;
	std::unique_ptr<UniqueBuffer> m_stagingBuffer;
};
