#include "Image.h"

Image::Image(
	uint32_t width, uint32_t height, uint32_t depth,
	vk::Format format,
	vk::ImageTiling tiling,
	vk::ImageUsageFlags usage,
	vk::MemoryPropertyFlags properties
)
	: m_format(format)
	, m_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1)
	, m_stagingBuffer(
		static_cast<size_t>(width)* height* depth,
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible |
		vk::MemoryPropertyFlagBits::eHostCoherent)
{
	uint32_t queueFamilies[] = { g_physicalDevice->GetQueueFamilies().graphicsFamily.value() };
	vk::ImageCreateInfo imageInfo(
		{}, // flags
		vk::ImageType::e2D,
		m_format,
		m_extent,
		1, // mipLevels
		1, //samples
		vk::SampleCountFlagBits::e1,
		tiling,
		usage,
		vk::SharingMode::eExclusive,
		1, queueFamilies,
		vk::ImageLayout::eUndefined
	);
	m_image = g_device->Get().createImageUnique(imageInfo);

	// Image memory
	vk::MemoryRequirements memRequirements = g_device->Get().getImageMemoryRequirements(m_image.get());
	m_memory = g_device->Get().allocateMemoryUnique(
		vk::MemoryAllocateInfo(
			memRequirements.size,
			g_physicalDevice->FindMemoryType(memRequirements.memoryTypeBits, properties)
		)
	);
	g_device->Get().bindImageMemory(m_image.get(), m_memory.get(), 0);
}

void Image::TransitionLayout(vk::CommandBuffer& commandBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout)
{
	vk::PipelineStageFlags sourceStage;
	vk::PipelineStageFlags destinationStage;

	vk::ImageMemoryBarrier barrier(
		vk::AccessFlags() /*todo*/,
		vk::AccessFlags() /*todo*/,
		oldLayout, newLayout,
		0, 0,
		m_image.get(),
		vk::ImageSubresourceRange(
			vk::ImageAspectFlagBits::eColor, // aspect mask
			0, // baseMipLevel
			1, // levelCount
			0, // baseArrayLayer,
			1 // layerCount
		)
	);

	if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlags();
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
		destinationStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		sourceStage = vk::PipelineStageFlagBits::eTransfer;
		destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
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
}

void Image::Overwrite(vk::CommandBuffer& commandBuffer, const void* pixels)
{
	// Copy data to staging buffer
	void* data;
	g_device->Get().mapMemory(m_stagingBuffer.GetMemory(), 0, m_stagingBuffer.size(), {}, &data);
	memcpy(data, pixels, m_stagingBuffer.size());
	g_device->Get().unmapMemory(m_stagingBuffer.GetMemory());

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
	commandBuffer.copyBufferToImage(m_stagingBuffer.Get(), m_image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &region);
}