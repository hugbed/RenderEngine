#include "RenderLoop.h"

#include "Swapchain.h"
#include "GraphicsPipeline.h"
#include "Device.h"
#include "PhysicalDevice.h"

#include <thread>
#include <iostream>
#include <thread>

RenderLoop::RenderLoop(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
	: m_window(window)
	, m_surface(surface)
	, m_swapchain(std::make_unique<Swapchain>(surface, extent))
	, m_commandBufferPool(m_swapchain->GetImageCount(), kMaxFramesInFlight, g_physicalDevice->GetQueueFamilies().graphicsFamily.value())
{
	window.SetWindowResizeCallback(reinterpret_cast<void*>(this), OnResize);
}

void RenderLoop::Init()
{
	// Use any command buffer for init
	auto commandBuffer = m_commandBufferPool.ResetAndGetCommandBuffer();
	commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	{
		Init(commandBuffer);
	}
	commandBuffer.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	m_commandBufferPool.Submit(submitInfo);
}

void RenderLoop::Run()
{
	while (m_window.ShouldClose() == false)
	{
		m_window.PollEvents();

		while (std::chrono::high_resolution_clock::now() - m_lastUpdateTime < m_framePeriod)
			std::this_thread::yield();

		UpdateDeltaTime();

		Update();
		Render();
	}
	vkDeviceWaitIdle(static_cast<VkDevice>(g_device->Get()));
}

void RenderLoop::UpdateDeltaTime()
{
	auto now = std::chrono::high_resolution_clock::now();
	m_deltaTime = now - m_lastUpdateTime;
	m_lastUpdateTime = now;
}

void RenderLoop::OnResize(void* data, int w, int h)
{
	auto app = reinterpret_cast<RenderLoop*>(data);
	app->m_frameBufferResized = true;
}

void RenderLoop::Render()
{
	m_commandBufferPool.WaitUntilSubmitComplete();

	// Use C API because eErrorOutOfDateKHR throws
	uint32_t imageIndex = 0;
	auto result = vkAcquireNextImageKHR(
		static_cast<VkDevice>(g_device->Get()),
		static_cast<VkSwapchainKHR>(m_swapchain->Get()),
		UINT64_MAX,
		static_cast<VkSemaphore>(m_gpuSync.imageAvailableSemaphore.get()),
		VK_NULL_HANDLE, // fence
		&imageIndex);
	if (result == (VkResult)vk::Result::eErrorOutOfDateKHR) 
	{
		RecreateSwapchain();
		return;
	}

	auto commandBuffer = m_commandBufferPool.ResetAndGetCommandBuffer();
	commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	{
		RenderFrame(imageIndex, commandBuffer);
	}
	commandBuffer.end();

	// Submit command buffer on graphics queue
	vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
	vk::SubmitInfo submitInfo(
		1, &m_gpuSync.imageAvailableSemaphore.get(),
		waitStages,
		1, &m_commandBufferPool.GetCommandBuffer(),
		1, &m_gpuSync.renderFinishedSemaphore.get()
	);
	m_commandBufferPool.Submit(std::move(submitInfo));
	m_commandBufferPool.MoveToNext();

	// Presentation
	vk::PresentInfoKHR presentInfo = {};
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &m_gpuSync.renderFinishedSemaphore.get();

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_swapchain->Get();
	presentInfo.pImageIndices = &imageIndex;

	result = vkQueuePresentKHR(static_cast<VkQueue>(g_device->GetPresentQueue()), &static_cast<const VkPresentInfoKHR&>(presentInfo));

	if (result == (VkResult)vk::Result::eSuboptimalKHR ||
		result == (VkResult)vk::Result::eErrorOutOfDateKHR ||
		m_frameBufferResized)
	{
		m_frameBufferResized = false;
		RecreateSwapchain();
		return;
	}
	else if (result != (VkResult)vk::Result::eSuccess)
	{
		throw std::runtime_error("Failed to acquire swapchain image");
	}
}

void RenderLoop::RecreateSwapchain()
{
	vk::Extent2D extent = m_window.GetFramebufferSize();

	// Wait if window is minimized
	while (extent.width == 0 || extent.height == 0)
	{
		extent = m_window.GetFramebufferSize();
		m_window.WaitForEvents();
	}

	g_device->Get().waitIdle();

	// Recreate swapchain and render pass
	m_swapchain.reset();

	m_swapchain = std::make_unique<Swapchain>(m_surface, extent);

	m_commandBufferPool.Reset(m_swapchain->GetImageCount());

	OnSwapchainRecreated();
}
