#pragma once

#include "Swapchain.h"

#include <vulkan/vulkan.hpp>

#include <vector>

#include "Device.h"
#include "PhysicalDevice.h"

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

	void Overwrite(void* dataToCopy)
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

#include "CommandBuffers.h"
class BufferStaging
{
public:
	BufferStaging(size_t size, vk::BufferUsageFlags bufferUsage)
		: m_buffer(
			size,
			bufferUsage | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal)
		, m_stagingBuffer(
			size,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible |
				vk::MemoryPropertyFlagBits::eHostCoherent)
		, m_commandBuffers(
			1,
			g_physicalDevice->GetQueueFamilies().graphicsFamily.value(),
			vk::CommandPoolCreateFlagBits::eTransient)
	{
	}

	void Overwrite(void* dataToCopy)
	{
		// Copy data to staging buffer
		void* data;
		g_device->Get().mapMemory(m_stagingBuffer.GetMemory(), 0, m_stagingBuffer.size(), {}, &data);
			memcpy(data, dataToCopy, m_stagingBuffer.size());
		g_device->Get().unmapMemory(m_stagingBuffer.GetMemory());

		// Copy staging buffer to buffer
		auto& commandBuffer = m_commandBuffers[0];

		commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		{
			vk::BufferCopy copyRegion(0, 0, m_stagingBuffer.size());
			commandBuffer.copyBuffer(m_stagingBuffer.Get(), m_buffer.Get(), 1, &copyRegion);
		}
		commandBuffer.end();

		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		// We should batch all memory transfer operations in a frame
		// so we can wait while doing all of them asynchroneously
		auto graphicsQueue = g_device->GetGraphicsQueue();
		graphicsQueue.submit(submitInfo, nullptr);
		graphicsQueue.waitIdle();
	}

	vk::Buffer Get() const { return m_buffer.Get(); }

private:
	// This should be somewhere else, we should have a command pool for short lived buffers
	// note: command pools are not thread safe, so I guess one per thread requiring memory updates
	CommandBuffers m_commandBuffers;

	Buffer m_buffer;
	Buffer m_stagingBuffer;
};

class RenderPass
{
public:
	RenderPass(const Swapchain& swapchain);

	void PopulateRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

	size_t GetFrameBufferCount() const { return m_framebuffers.size(); }

private:
	vk::Extent2D m_imageExtent;
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniqueRenderPass m_renderPass;
	vk::UniquePipeline m_graphicsPipeline;
	std::vector<vk::UniqueFramebuffer> m_framebuffers;

	// Put this here for now:
	BufferStaging m_vertexBuffer;
};
