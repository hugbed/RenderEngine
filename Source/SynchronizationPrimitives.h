#pragma once

#include "Device.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class SynchronizationPrimitives
{
public:
	SynchronizationPrimitives(uint32_t swapchainImagesCount, size_t maxFramesInFlight)
		: m_maxFramesInFlight(maxFramesInFlight)
	{
		m_imageAvailableSemaphores.reserve(maxFramesInFlight);
		m_renderFinishedSemaphores.reserve(maxFramesInFlight);
		m_inFlightFences.reserve(maxFramesInFlight);
		m_imagesInFlight.resize(swapchainImagesCount);

		for (size_t i = 0; i < maxFramesInFlight; ++i)
		{
			m_imageAvailableSemaphores.push_back(g_device->Get().createSemaphoreUnique({}));
			m_renderFinishedSemaphores.push_back(g_device->Get().createSemaphoreUnique({}));
			m_inFlightFences.push_back(g_device->Get().createFenceUnique(
				vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)
			));
		}
	}

	const vk::Semaphore& GetImageAvailableSemaphore() const
	{
		return m_imageAvailableSemaphores[m_currentFrame].get();
	}

	const vk::Semaphore& GetRenderFinishedSemaphore() const
	{
		return m_renderFinishedSemaphores[m_currentFrame].get();
	}

	vk::Fence& WaitForFrame()
	{
		auto& frameFence = m_inFlightFences[m_currentFrame].get();
		g_device->Get().waitForFences(
			frameFence,
			true, // wait for all fences (we only have 1 though)
			UINT64_MAX // indefinitely
		);
		return frameFence;
	}

	void WaitUntilImageIsAvailable(uint32_t imageIndex)
	{
		// Check if a previous frame is using this image (i.e. there is its fence to wait on)
		if (m_imagesInFlight[imageIndex])
		{
			g_device->Get().waitForFences(m_imagesInFlight[imageIndex], true, UINT64_MAX);
		}

		// Mark the image as now being in use by this frame
		m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame].get();
	}

	const vk::Fence& GetFrameFence()
	{
		return m_inFlightFences[m_currentFrame].get();
	}

	void MoveToNextFrame()
	{
		m_currentFrame = (m_currentFrame + 1) % m_maxFramesInFlight;
	}

private:
	size_t m_currentFrame = 0;
	int m_maxFramesInFlight = 2;
	std::vector<vk::UniqueSemaphore> m_imageAvailableSemaphores;
	std::vector<vk::UniqueSemaphore> m_renderFinishedSemaphores;
	std::vector<vk::UniqueFence> m_inFlightFences;
	std::vector<vk::Fence> m_imagesInFlight;
};
