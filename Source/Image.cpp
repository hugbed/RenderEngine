#include "Image.h"

#include "PhysicalDevice.h"

Image::Image(
	uint32_t width, uint32_t height, uint32_t depth,
	vk::Format format,
	vk::ImageTiling tiling,
	vk::ImageUsageFlags usage,
	vk::MemoryPropertyFlags properties,
	vk::ImageAspectFlags aspectFlags,
	uint32_t mipLevels
)
	: m_format(format)
	, m_depth(depth)
	, m_mipLevels(mipLevels)
	, m_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1)
{
	CreateImage(tiling, usage);
	InitImageMemory(properties);
	CreateImageView(aspectFlags);
}

void Image::CreateImage(vk::ImageTiling tiling, vk::ImageUsageFlags usage)
{
	uint32_t queueFamilies[] = { g_physicalDevice->GetQueueFamilies().graphicsFamily.value() };
	vk::ImageCreateInfo imageInfo(
		{}, // flags
		vk::ImageType::e2D,
		m_format,
		m_extent,
		m_mipLevels, // mipLevels
		1, // layerCount
		vk::SampleCountFlagBits::e1,
		tiling,
		usage,
		vk::SharingMode::eExclusive,
		1, queueFamilies,
		vk::ImageLayout::eUndefined
	);
	m_image = g_device->Get().createImageUnique(imageInfo);
}

void Image::InitImageMemory(vk::MemoryPropertyFlags properties)
{
	vk::MemoryRequirements memRequirements = g_device->Get().getImageMemoryRequirements(m_image.get());
	m_memory = g_device->Get().allocateMemoryUnique(
		vk::MemoryAllocateInfo(
			memRequirements.size,
			g_physicalDevice->FindMemoryType(memRequirements.memoryTypeBits, properties)
		)
	);
	g_device->Get().bindImageMemory(m_image.get(), m_memory.get(), 0);
}

void Image::TransitionLayout(vk::CommandBuffer& commandBuffer, vk::ImageLayout newLayout)
{
	vk::PipelineStageFlags sourceStage;
	vk::PipelineStageFlags destinationStage;

	vk::ImageMemoryBarrier barrier(
		vk::AccessFlags() /*todo*/,
		vk::AccessFlags() /*todo*/,
		m_imageLayout, newLayout,
		0, 0,
		m_image.get(),
		vk::ImageSubresourceRange(
			vk::ImageAspectFlagBits::eColor, // aspect mask
			0, // baseMipLevel
			m_mipLevels, // levelCount
			0, // baseArrayLayer,
			1 // layerCount
		)
	);

	if (m_imageLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlags();
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
		destinationStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (m_imageLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		sourceStage = vk::PipelineStageFlagBits::eTransfer;
		destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else if (m_imageLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlags();
		barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
		destinationStage = vk::PipelineStageFlagBits::eLateFragmentTests;
	}
	else
	{
		throw std::invalid_argument("unsupported layout transition!");
	}

	commandBuffer.pipelineBarrier(
		sourceStage, // srcStageMask,
		destinationStage, // dstStageMask,
		{}, // DependencyFlags dependencyFlags
		{}, {}, // memoryBarriers
		{}, {}, // bufferMemoryBarriers
		1, &barrier
	);

	m_imageLayout = newLayout;
}

void Image::CreateStagingBuffer()
{
	m_stagingBuffer = std::make_unique<Buffer>(
		static_cast<size_t>(m_extent.width)* m_extent.height * m_depth,
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible |
		vk::MemoryPropertyFlagBits::eHostCoherent);
}

void Image::Overwrite(vk::CommandBuffer& commandBuffer, const void* pixels, vk::ImageLayout dstImageLayout)
{
	if (m_stagingBuffer == nullptr)
		CreateStagingBuffer();

	if (m_imageLayout == vk::ImageLayout::eUndefined)
		TransitionLayout(commandBuffer, vk::ImageLayout::eTransferDstOptimal);

	// Copy data to staging buffer
	void* data;
	g_device->Get().mapMemory(m_stagingBuffer->GetMemory(), 0, m_stagingBuffer->size(), {}, &data);
		memcpy(data, pixels, m_stagingBuffer->size());
	g_device->Get().unmapMemory(m_stagingBuffer->GetMemory());

	// Copy staging buffer to image
	vk::BufferImageCopy region(
		vk::DeviceSize(0), // bufferOffset
		0UL, // bufferRowLength
		0UL, // bufferImageHeight
		vk::ImageSubresourceLayers(
			vk::ImageAspectFlagBits::eColor,
			0, // mipLevel
			0, // baseArrayLayer
			1  // layerCount
		),
		vk::Offset3D(0, 0, 0), m_extent
	);
	commandBuffer.copyBufferToImage(m_stagingBuffer->Get(), m_image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &region);

	if (m_mipLevels > 1)
		GenerateMipmaps(commandBuffer, dstImageLayout); // transfers the image layout for each mip level
	else
		TransitionLayout(commandBuffer, dstImageLayout);
}

void Image::GenerateMipmaps(vk::CommandBuffer& commandBuffer, vk::ImageLayout dstImageLayout)
{
	// Validate that we support blit
	auto formatProperties = g_physicalDevice->Get().getFormatProperties(m_format);
	if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
		throw std::runtime_error("texture image format does not support linear blitting!");
	}

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = m_image.get();
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = m_extent.width;
	int32_t mipHeight = m_extent.height;

	for (uint32_t i = 1; i < m_mipLevels; i++)
	{
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		VkImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		// todo: generating mip maps at runtime is not a good idea
		// usually mipmaps are loaded directly from files
		vkCmdBlitImage(commandBuffer,
			m_image.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			m_image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = m_mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, nullptr,
		0, nullptr,
		1, &barrier);

	m_imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

void Image::CreateImageView(vk::ImageAspectFlags aspectFlags)
{
	vk::ImageViewCreateInfo createInfo(
		vk::ImageViewCreateFlags(),
		m_image.get(),
		vk::ImageViewType::e2D,
		m_format,
		vk::ComponentMapping(vk::ComponentSwizzle::eIdentity),
		vk::ImageSubresourceRange(
			aspectFlags,
			0, // baseMipLevel
			m_mipLevels, // mipLevelsCount
			0, // baseArrayLayer
			1 // layerCount
		)
	);
	m_imageView = g_device->Get().createImageViewUnique(createInfo);
}
