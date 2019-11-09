#pragma once

#include "Device.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class CommandBuffers
{
public:
	CommandBuffers(size_t count, uint32_t queueFamily, vk::CommandPoolCreateFlags flags = {})
	{
		// Pool
		vk::CommandPoolCreateInfo poolInfo(flags, queueFamily);
		m_commandPool = g_device->Get().createCommandPoolUnique(poolInfo);
		Reset(count);
	}

	vk::CommandBuffer Get(uint32_t imageIndex) const
	{
		return m_commandBuffers[imageIndex].get();
	}

	vk::CommandBuffer operator[](uint32_t imageIndex) const
	{
		return m_commandBuffers[imageIndex].get();
	}

	void Reset(size_t count)
	{
		// Buffer
		vk::CommandBufferAllocateInfo commandBufferAllocateInfo(
			m_commandPool.get(), vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(count)
		);

		// todo: use VulkanMemoryAllocator instead using separate memory allocations every time
		// or we'll run out very soon

		// Command buffers
		m_commandBuffers = g_device->Get().allocateCommandBuffersUnique(commandBufferAllocateInfo);
	}

private:
	vk::UniqueCommandPool m_commandPool;
	std::vector<vk::UniqueCommandBuffer> m_commandBuffers;
};
