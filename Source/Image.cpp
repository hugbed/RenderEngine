#include "Image.h"

#include "PhysicalDevice.h"

Image::Image(
	uint32_t width, uint32_t height, uint32_t depth,
	vk::Format format,
	vk::ImageTiling tiling,
	vk::ImageUsageFlags usage,
	vk::MemoryPropertyFlags properties,
	vk::ImageAspectFlags aspectFlags,
	uint32_t mipLevels,
	vk::SampleCountFlagBits nbSamples
)
	: m_format(format)
	, m_mipLevels(mipLevels)
	, m_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1)
{
	CreateImage(tiling, usage, nbSamples);
	InitImageMemory(properties);
	CreateImageView(aspectFlags);
}

void Image::CreateImage(vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::SampleCountFlagBits nbSamples)
{
	uint32_t queueFamilies[] = { g_physicalDevice->GetQueueFamilies().graphicsFamily.value() };
	vk::ImageCreateInfo imageInfo(
		{}, // flags
		vk::ImageType::e2D,
		m_format,
		m_extent,
		m_mipLevels, // mipLevels
		1, // layerCount
		nbSamples,
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
		vk::AccessFlags(),
		vk::AccessFlags(),
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
