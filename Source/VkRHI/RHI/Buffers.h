#pragma once

#include <defines.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <vector>

class CommandBuffer;

struct UniqueBuffer : public DeferredDestructible
{
	using value_type = vk::Buffer;

	UniqueBuffer(const vk::BufferCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo);
	
	~UniqueBuffer() override;

	IMPLEMENT_MOVABLE_ONLY(UniqueBuffer);

	void* GetMappedData() const { return m_allocationInfo.pMappedData; }

	vk::DeviceSize Size() const { return m_size; }

	value_type Get() const { return value_type(m_buffer); }

	// Required after writting to mapped data if memory is not HOST_COHERENT
	void Flush(VkDeviceSize offset, VkDeviceSize size) const;

private:
	size_t m_size;
	VkBuffer m_buffer;
	VmaAllocation m_allocation;
	VmaAllocationInfo m_allocationInfo;
};

// todo (hbedard): would be nice if this didn't need a dynamic allocation just to call the constructor later
class UniqueBufferWithStaging : public DeferredDestructible
{
public:
	using value_type = vk::Buffer;

	UniqueBufferWithStaging(size_t size, vk::BufferUsageFlags bufferUsage);

	IMPLEMENT_MOVABLE_ONLY(UniqueBufferWithStaging);

	UniqueBuffer* ReleaseStagingBuffer() { return m_stagingBuffer.release(); }

	void* GetStagingMappedData() const { return m_stagingBuffer->GetMappedData(); }

	void CopyStagingToGPU(vk::CommandBuffer& commandBuffer);

	vk::DeviceSize Size() const { return m_buffer.Size(); }

	value_type Get() const { return m_buffer.Get(); }

private:
	UniqueBuffer m_buffer;
	std::unique_ptr<UniqueBuffer> m_stagingBuffer;
};

struct UniqueImage : public DeferredDestructible
{
	using value_type = vk::Image;

	UniqueImage(const vk::ImageCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo);

	~UniqueImage() override;

	// For deferred initialization
	UniqueImage() = default;
	void Init(const vk::ImageCreateInfo& createInfo, const VmaAllocationCreateInfo& allocInfo);

	IMPLEMENT_MOVABLE_ONLY(UniqueImage);

	value_type Get() const { return value_type(m_image); }

private:
	VkImage m_image = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
	VmaAllocationInfo m_allocationInfo = {};
};
