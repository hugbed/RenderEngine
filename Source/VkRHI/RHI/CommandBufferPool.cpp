#include "RHI/CommandBufferPool.h"

CommandBufferPool::CommandBufferPool(size_t count, size_t nbConcurrentSubmit, uint32_t queueFamily, vk::CommandPoolCreateFlags flags)
	: m_queueFamily(queueFamily)
	, m_nbConcurrentSubmit(static_cast<uint32_t>(nbConcurrentSubmit))
{
	// Pools (1 pool per concurrent submit)
	m_commandBufferPools.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		m_commandBufferPools.push_back(g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
			vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_queueFamily
		)));

		// Command Buffers (1 per pool)
		auto commandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
			m_commandBufferPools[i].get(), vk::CommandBufferLevel::ePrimary, 1
		));
		m_commandBuffers.push_back(std::move(commandBuffers[0]));
	}

	// Fences
	m_fences.reserve(m_nbConcurrentSubmit);
	m_resourcesToDestroy.resize(m_nbConcurrentSubmit);
	for (size_t i = 0; i < m_nbConcurrentSubmit; ++i)
	{
		m_fences.push_back(g_device->Get().createFenceUnique(
			vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)
		));
	}
}

void CommandBufferPool::Reset(size_t count)
{
	m_commandBuffers.clear();
	m_commandBufferPools.clear();

	m_commandBufferPools.reserve(count);
	m_commandBuffers.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		m_commandBufferPools.push_back(g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
			vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_queueFamily
		)));

		auto commandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
			m_commandBufferPools[i].get(), vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(count)
		));
		m_commandBuffers.push_back(std::move(commandBuffers[0]));
	}

	// Fence
	m_fences.clear();
	m_fences.reserve(m_nbConcurrentSubmit);
	m_resourcesToDestroy.resize(m_nbConcurrentSubmit);
	for (size_t i = 0; i < m_nbConcurrentSubmit; ++i)
	{
		m_fences.push_back(g_device->Get().createFenceUnique(
			vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)
		));
	}
}
