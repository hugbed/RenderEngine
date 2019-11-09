#pragma once

#include "CommandBuffers.h"

#include "PhysicalDevice.h"
#include "Device.h"

#include <vulkan/vulkan.hpp>

class Buffer
{
public:
	Buffer(size_t size, vk::BufferUsageFlags bufferUsage, vk::MemoryPropertyFlags memoryProperties)
		: m_size(size)
	{
		vk::BufferCreateInfo bufferInfo({}, size, bufferUsage, vk::SharingMode::eExclusive);
		m_buffer = g_device->Get().createBufferUnique(bufferInfo);

		vk::MemoryRequirements memRequirements = g_device->Get().getBufferMemoryRequirements(m_buffer.get());
		vk::MemoryAllocateInfo allocInfo(
			memRequirements.size,
			g_physicalDevice->FindMemoryType(
				memRequirements.memoryTypeBits,
				memoryProperties
			)
		);
		m_deviceMemory = g_device->Get().allocateMemoryUnique(allocInfo);
		g_device->Get().bindBufferMemory(m_buffer.get(), m_deviceMemory.get(), 0);
	}

	void Overwrite(const void* dataToCopy)
	{
		void* data;
		g_device->Get().mapMemory(m_deviceMemory.get(), 0, m_size, {}, &data);
		memcpy(data, dataToCopy, m_size);
		g_device->Get().unmapMemory(m_deviceMemory.get());
	}

	vk::Buffer Get() const { return m_buffer.get(); }
	vk::DeviceMemory GetMemory() const { return m_deviceMemory.get(); }
	size_t size() const { return m_size; }

private:
	size_t m_size; // this one might not be necessary
	vk::UniqueBuffer m_buffer;
	vk::UniqueDeviceMemory m_deviceMemory;
};

class BufferWithStaging
{
public:
	BufferWithStaging(size_t size, vk::BufferUsageFlags bufferUsage)
		: m_buffer(
			size,
			bufferUsage | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal)
		, m_stagingBuffer(
			size,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent)
	{
	}

	void Overwrite(vk::CommandBuffer& commandBuffer, const void* dataToCopy)
	{
		// Copy data to staging buffer
		void* data;
		g_device->Get().mapMemory(m_stagingBuffer.GetMemory(), 0, m_stagingBuffer.size(), {}, &data);
		memcpy(data, dataToCopy, m_stagingBuffer.size());
		g_device->Get().unmapMemory(m_stagingBuffer.GetMemory());

		// Copy staging buffer to buffer
		vk::BufferCopy copyRegion(0, 0, m_stagingBuffer.size());
		commandBuffer.copyBuffer(m_stagingBuffer.Get(), m_buffer.Get(), 1, &copyRegion);
	}

	vk::Buffer Get() const { return m_buffer.Get(); }

private:
	Buffer m_buffer;
	Buffer m_stagingBuffer;
};
