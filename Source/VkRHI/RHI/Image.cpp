#include <RHI/Image.h>

#include <RHI/Device.h>
#include <RHI/PhysicalDevice.h>

Image::Image(
	uint32_t width, uint32_t height,
	vk::Format format,
	vk::ImageTiling tiling,
	vk::ImageUsageFlags usage,
	vk::ImageAspectFlags aspectFlags,
	vk::ImageViewType imageViewType,
	uint32_t mipLevels,
	uint32_t layerCount,
	vk::SampleCountFlagBits nbSamples
)
	: m_format(format)
	, m_imageViewType(imageViewType)
	, m_mipLevels(mipLevels)
	, m_layerCount(layerCount)
	, m_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1)
{
	CreateImage(tiling, usage, nbSamples);
	CreateImageView(aspectFlags);
}

void Image::CreateImage(vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::SampleCountFlagBits nbSamples)
{
	uint32_t queueFamilies[] = { g_physicalDevice->GetQueueFamilies().graphicsFamily.value() };
	vk::ImageCreateInfo imageInfo(
		m_imageViewType == vk::ImageViewType::eCube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlagBits{},
		vk::ImageType::e2D,
		m_format,
		m_extent,
		m_mipLevels,
		m_layerCount,
		nbSamples,
		tiling,
		usage,
		vk::SharingMode::eExclusive,
		1, queueFamilies,
		vk::ImageLayout::eUndefined
	);
	m_image.Init(imageInfo, VmaAllocationCreateInfo{ {}, VMA_MEMORY_USAGE_GPU_ONLY });
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
		m_image.Get(),
		vk::ImageSubresourceRange(
			vk::ImageAspectFlagBits::eColor, // aspect mask
			0, m_mipLevels,
			0, m_layerCount
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
		m_image.Get(),
		m_imageViewType,
		m_format,
		vk::ComponentMapping(vk::ComponentSwizzle::eIdentity),
		vk::ImageSubresourceRange(
			aspectFlags,
			0, m_mipLevels,
			0, m_layerCount
		)
	);
	m_imageView = g_device->Get().createImageViewUnique(createInfo);
}
