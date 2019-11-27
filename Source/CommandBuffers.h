#pragma once

#include "Device.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class CommandBufferPool
{
public:
	CommandBufferPool(size_t count, size_t nbConcurrentSubmit, uint32_t queueFamily, vk::CommandPoolCreateFlags flags = {})
		: m_nbConcurrentSubmit(nbConcurrentSubmit)
	{
		// Pool
		m_commandPool = g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
			flags, queueFamily
		));

		// Command Buffers
		m_commandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
			m_commandPool.get(), vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(count)
		));

		// Fences
		m_fences.reserve(m_nbConcurrentSubmit);
		for (size_t i = 0; i < m_nbConcurrentSubmit; ++i)
		{
			m_fences.push_back(g_device->Get().createFenceUnique(
				vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)
			));
		}
	}

	void Reset(size_t count)
	{
		// Command buffers
		m_commandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
			m_commandPool.get(), vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(count)
		));

		// Fence
		m_fences.clear();
		for (size_t i = 0; i < m_nbConcurrentSubmit; ++i)
		{
			m_fences.push_back(g_device->Get().createFenceUnique(
				vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)
			));
		}
	}

	uint32_t GetCount()
	{
		return m_commandBuffers.size();
	}

	vk::CommandBuffer GetCommandBuffer()
	{
		return m_commandBuffers[m_commandBufferIndex].get();
	}

	void MoveToNext()
	{
		m_commandBufferIndex = (m_commandBufferIndex + 1UL) % static_cast<uint32_t>(m_commandBuffers.size());
		m_fenceIndex = (m_fenceIndex + 1UL) % m_nbConcurrentSubmit;
	}

	vk::Fence& WaitUntilSubmitComplete()
	{
		auto& frameFence = m_fences[m_fenceIndex].get();
		g_device->Get().waitForFences(
			frameFence,
			true, // wait for all fences (we only have 1 though)
			UINT64_MAX // indefinitely
		);
		return frameFence;
	}

private:
	uint32_t m_fenceIndex = 0;
	uint32_t m_nbConcurrentSubmit = 2;

	uint32_t m_commandBufferIndex = 0;
	vk::UniqueCommandPool m_commandPool;
	std::vector<vk::UniqueCommandBuffer> m_commandBuffers;
	std::vector<vk::UniqueFence> m_fences; // to know when commands have completed
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
