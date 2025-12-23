#pragma once

#include <RHI/Device.h>
#include <vulkan/vulkan.hpp>

#include <vector>

class CommandBufferPool // todo: rename this
{
public:
	CommandBufferPool(size_t count, size_t nbConcurrentSubmit, uint32_t queueFamily, vk::CommandPoolCreateFlags flags = {});

	void Reset(size_t count);

	void Submit(vk::SubmitInfo submitInfo)
	{
		auto& submitFence = m_fences[m_fenceIndex].get();
		g_device->Get().resetFences(submitFence);
		g_device->GetGraphicsQueue().submit(submitInfo, submitFence);
	}

	size_t GetCount()
	{
		return m_commandBufferPools.size();
	}

	size_t GetNbConcurrentSubmits() const
	{
		return m_fences.size();
	}

	vk::CommandBuffer& GetCommandBuffer()
	{
		return m_commandBuffers[m_commandBufferIndex].get();
	}

	vk::CommandBuffer& ResetAndGetCommandBuffer()
	{
		// Clear command buffer using resetCommandPool
		g_device->Get().resetCommandPool(m_commandBufferPools[m_commandBufferIndex].get(), {});
		return m_commandBuffers[m_commandBufferIndex].get();
	}

	void MoveToNext()
	{
		m_commandBufferIndex = (m_commandBufferIndex + 1UL) % static_cast<uint32_t>(m_commandBuffers.size());
		m_fenceIndex = (m_fenceIndex + 1UL) % m_nbConcurrentSubmit;
	}

	void WaitUntilSubmitComplete()
	{
		auto& frameFence = m_fences[m_fenceIndex].get();
		vk::Result result = g_device->Get().waitForFences(
			frameFence,
			true, // wait for all fences (we only have 1 though)
			UINT64_MAX // indefinitely
		);
		assert(result == vk::Result::eSuccess);

		for (const auto& resource : m_resourcesToDestroy[m_fenceIndex])
			delete resource;

		m_resourcesToDestroy[m_fenceIndex].clear();
	}

	void DestroyAfterSubmit(DeferredDestructible* resource)
	{
		m_resourcesToDestroy[m_fenceIndex].push_back(resource);
	}

private:
	uint32_t m_queueFamily;
	uint32_t m_fenceIndex = 0;
	uint32_t m_nbConcurrentSubmit = 2;

	uint32_t m_commandBufferIndex = 0;
	std::vector<vk::UniqueCommandPool> m_commandBufferPools;
	std::vector<vk::UniqueCommandBuffer> m_commandBuffers;
	std::vector<vk::UniqueFence> m_fences; // to know when commands have completed
	std::vector<std::vector<DeferredDestructible*>> m_resourcesToDestroy; // once submission has completed
};
