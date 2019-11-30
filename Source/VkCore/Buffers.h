#pragma once

#include "defines.h"

#include "vk_mem_alloc.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class CommandBuffer;

struct UniqueBuffer
{
	using value_type = vk::Buffer;

	UniqueBuffer(const vk::BufferCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo);
	
	~UniqueBuffer();

	IMPLEMENT_MOVABLE_ONLY(UniqueBuffer);

	void* GetMappedData() const { return m_allocationInfo.pMappedData; }

	vk::DeviceSize Size() { return m_allocationInfo.size; }

	value_type Get() const { return m_buffer; }

private:
	VkBuffer m_buffer;
	VmaAllocation m_allocation;
	VmaAllocationInfo m_allocationInfo;
};

class UniqueBufferWithStaging
{
public:
	UniqueBufferWithStaging(size_t size, vk::BufferUsageFlags bufferUsage);

	IMPLEMENT_MOVABLE_ONLY(UniqueBufferWithStaging);

	void* GetStagingMappedData() const { return m_stagingBuffer.GetMappedData(); }

	void CopyStagingToGPU(vk::CommandBuffer& commandBuffer);

	vk::Buffer Get() const { return m_buffer.Get(); }

private:
	UniqueBuffer m_buffer;
	UniqueBuffer m_stagingBuffer;
};

struct UniqueImage
{
	using value_type = vk::Image;

	UniqueImage() = default;

	UniqueImage(const vk::ImageCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo);

	~UniqueImage();

	void Reset(const vk::ImageCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo);

	IMPLEMENT_MOVABLE_ONLY(UniqueImage);

	const value_type& Get() const { return m_image; }

private:
	VkImage m_image = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
	VmaAllocationInfo m_allocationInfo = {};
};
