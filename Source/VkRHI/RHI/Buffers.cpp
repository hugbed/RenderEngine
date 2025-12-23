#include "Buffers.h"

#include "RHI/PhysicalDevice.h"
#include "RHI/Device.h"

UniqueBuffer::UniqueBuffer(const vk::BufferCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo)
	: m_size(createInfo.size)
{
	const VkBufferCreateInfo& bufferCreateInfo = createInfo;
	vmaCreateBuffer(g_device->GetAllocator(), &bufferCreateInfo, &allocInfo, &m_buffer, &m_allocation, nullptr);
	vmaGetAllocationInfo(g_device->GetAllocator(), m_allocation, &m_allocationInfo);
}

UniqueBuffer::~UniqueBuffer()
{
	vmaDestroyBuffer(g_device->GetAllocator(), m_buffer, m_allocation);
}

void UniqueBuffer::Flush(VkDeviceSize offset, VkDeviceSize size) const
{
	vmaFlushAllocation(g_device->GetAllocator(), m_allocation, offset, size);
}

UniqueBufferWithStaging::UniqueBufferWithStaging(size_t size, vk::BufferUsageFlags bufferUsage)
	: m_stagingBuffer(std::make_unique<UniqueBuffer>(
		vk::BufferCreateInfo({}, size,  vk::BufferUsageFlagBits::eTransferSrc),
		VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }))
	, m_buffer(
		vk::BufferCreateInfo({}, size, bufferUsage | vk::BufferUsageFlagBits::eTransferDst),
		{ {}, VMA_MEMORY_USAGE_GPU_ONLY })
{
}

void UniqueBufferWithStaging::CopyStagingToGPU(vk::CommandBuffer& commandBuffer)
{
	// Copy staging buffer to buffer
	vk::BufferCopy copyRegion(0, 0, m_buffer.Size());
	commandBuffer.copyBuffer(m_stagingBuffer->Get(), m_buffer.Get(), 1, &copyRegion);
}

UniqueImage::UniqueImage(const vk::ImageCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo)
{
	const VkImageCreateInfo& imageCreateInfo = createInfo;
	vmaCreateImage(g_device->GetAllocator(), &imageCreateInfo, &allocInfo, &m_image, &m_allocation, nullptr);
	vmaGetAllocationInfo(g_device->GetAllocator(), m_allocation, &m_allocationInfo);
}

UniqueImage::~UniqueImage()
{
	vmaDestroyImage(g_device->GetAllocator(), m_image, m_allocation);
}

void UniqueImage::Init(const vk::ImageCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo)
{
	ASSERT(!m_image);
	const VkImageCreateInfo& imageCreateInfo = createInfo;
	vmaCreateImage(g_device->GetAllocator(), &imageCreateInfo, &allocInfo, &m_image, &m_allocation, nullptr);
	vmaGetAllocationInfo(g_device->GetAllocator(), m_allocation, &m_allocationInfo);
}
