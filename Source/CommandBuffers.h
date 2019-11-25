#pragma once

#include "Device.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class CommandBufferPool
{
public:
	CommandBufferPool(size_t count, uint32_t queueFamily, vk::CommandPoolCreateFlags flags = {})
	{
		// Pool
		vk::CommandPoolCreateInfo poolInfo(flags, queueFamily);
		m_commandPool = g_device->Get().createCommandPoolUnique(poolInfo);
		Reset(count);
	}

	vk::CommandBuffer Get(size_t imageIndex)
	{
		return m_commandBuffers[imageIndex].get();
	}

	void Reset(size_t count)
	{
		// Buffer
		vk::CommandBufferAllocateInfo commandBufferAllocateInfo(
			m_commandPool.get(), vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(count)
		);

		// todo: use VulkanMemoryAllocator

		// Command buffers
		m_commandBuffers = g_device->Get().allocateCommandBuffersUnique(commandBufferAllocateInfo);
	}

private:
	vk::UniqueCommandPool m_commandPool;
	std::vector<vk::UniqueCommandBuffer> m_commandBuffers;
};

struct SingleTimeCommandBuffer
{
	SingleTimeCommandBuffer(vk::CommandBuffer& commandBuffer)
		: m_commandBuffer(commandBuffer)
	{
		m_commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	}

	// Prevent unwanted copies
	SingleTimeCommandBuffer(const SingleTimeCommandBuffer&) = delete;
	SingleTimeCommandBuffer& operator= (const SingleTimeCommandBuffer&) = delete;
	
	// Allow move only
	SingleTimeCommandBuffer(SingleTimeCommandBuffer&&) = default;
	SingleTimeCommandBuffer& operator= (SingleTimeCommandBuffer&&) = default;

	~SingleTimeCommandBuffer()
	{
		m_commandBuffer.end();

		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_commandBuffer;

		auto graphicsQueue = g_device->GetGraphicsQueue();
		graphicsQueue.submit(submitInfo, nullptr);
		graphicsQueue.waitIdle();
	}

	vk::CommandBuffer& Get() { return m_commandBuffer; }

private:
	vk::CommandBuffer& m_commandBuffer;
};
