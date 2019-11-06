#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

class CommandBuffers
{
public:
	CommandBuffers(vk::Device device, size_t count, uint32_t queueFamily)
	{
		// Pool
		vk::CommandPoolCreateInfo poolInfo({}, queueFamily);
		m_commandPool = device.createCommandPoolUnique(poolInfo);

		Reset(device, count);
	}

	vk::CommandBuffer Get(uint32_t imageIndex)
	{
		return m_commandBuffers[imageIndex].get();
	}

	void Reset(vk::Device device, size_t count)
	{
		// Buffer
		vk::CommandBufferAllocateInfo commandBufferAllocateInfo(
			m_commandPool.get(), vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(count)
		);

		// Command buffers
		m_commandBuffers = device.allocateCommandBuffersUnique(commandBufferAllocateInfo);
	}

private:
	vk::UniqueCommandPool m_commandPool;
	std::vector<vk::UniqueCommandBuffer> m_commandBuffers;
};
