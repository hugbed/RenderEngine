#include <RHI/Texture.h>

#include <RHI/Device.h>
#include <RHI/PhysicalDevice.h>

Texture::Texture(
	uint32_t width, uint32_t height, uint32_t depth,
	vk::Format format,
	vk::ImageTiling tiling,
	vk::ImageUsageFlags usage,
	vk::ImageAspectFlags aspectFlags,
	vk::ImageViewType imageViewType,
	uint32_t mipLevels,
	uint32_t layerCount
)
	: Image(width, height, format, tiling, usage, aspectFlags, imageViewType, mipLevels, layerCount)
	, m_depth(depth)
	, m_stagingBuffer(std::make_unique<UniqueBuffer>(
		vk::BufferCreateInfo({}, static_cast<size_t>(width) * height * depth * layerCount, vk::BufferUsageFlagBits::eTransferSrc),
		VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, }
	))
{
}

void Texture::UploadStagingToGPU(vk::CommandBuffer& commandBuffer, vk::ImageLayout dstImageLayout)
{
	if (m_imageLayout == vk::ImageLayout::eUndefined)
		TransitionLayout(commandBuffer, vk::ImageLayout::eTransferDstOptimal);

	// Copy staging buffer to image
	vk::BufferImageCopy region(
		vk::DeviceSize(0), // bufferOffset
		0UL, // bufferRowLength
		0UL, // bufferImageHeight
		vk::ImageSubresourceLayers(
			vk::ImageAspectFlagBits::eColor,
			0, // mipLevel
			0, m_layerCount
		),
		vk::Offset3D(0, 0, 0), m_extent
	);
	commandBuffer.copyBufferToImage(m_stagingBuffer->Get(), m_image.Get(), vk::ImageLayout::eTransferDstOptimal, 1, &region);

	if (m_mipLevels > 1)
		GenerateMipmaps(commandBuffer, dstImageLayout); // transfers the image layout for each mip level
	else
		TransitionLayout(commandBuffer, dstImageLayout);
}

void Texture::GenerateMipmaps(vk::CommandBuffer& commandBuffer, vk::ImageLayout dstImageLayout)
{
	// Validate that we support blit
	auto formatProperties = g_physicalDevice->Get().getFormatProperties(m_format);
	if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
		throw std::runtime_error("texture image format does not support linear blitting!");
	}

	vk::ImageMemoryBarrier barrier;

	barrier.image = m_image.Get();
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;// VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = m_extent.width;
	int32_t mipHeight = m_extent.height;

	for (uint32_t i = 1; i < m_mipLevels; i++)
	{
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {},
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		vk::ImageBlit blit;
		blit.srcOffsets[0] = vk::Offset3D(0, 0, 0);
		blit.srcOffsets[1] = vk::Offset3D(mipWidth, mipHeight, 1);
		blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
		blit.dstOffsets[1] = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1);
		blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		commandBuffer.blitImage(
			m_image.Get(), vk::ImageLayout::eTransferSrcOptimal,
			m_image.Get(), vk::ImageLayout::eTransferDstOptimal,
			1, &blit,
			vk::Filter::eLinear
		);

		barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {},
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = m_mipLevels - 1;
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

	commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {},
		0, nullptr,
		0, nullptr,
		1, &barrier
	);

	// Everything should be transitioned to this state now
	m_imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
}
