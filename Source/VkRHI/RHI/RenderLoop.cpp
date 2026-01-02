#include <RHI/RenderLoop.h>

#include <RHI/Swapchain.h>
#include <RHI/Device.h>
#include <RHI/PhysicalDevice.h>

#include <thread>
#include <iostream>

RenderLoop::RenderLoop(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
	: m_deltaTime({})
	, m_window(window)
	, m_surface(surface)
	, m_swapchain(std::make_unique<Swapchain>(surface, extent))
	, m_commandRingBuffer(m_swapchain->GetImageCount(), kMaxFramesInFlight, g_physicalDevice->GetQueueFamilies().graphicsFamily.value())
{
	window.SetWindowResizeCallback(reinterpret_cast<void*>(this), OnResize);
	
	m_renderFinishedSemaphores.reserve(m_swapchain->GetImageCount());
	for (int i = 0; i < m_swapchain->GetImageCount(); ++i)
	{
		m_renderFinishedSemaphores.push_back(g_device->Get().createSemaphoreUnique({}));
	}

	for (int i = 0; i < kMaxFramesInFlight; ++i)
	{
		m_imageAvailableSemaphores[i] = g_device->Get().createSemaphoreUnique({});
	}
}

CommandRingBuffer& RenderLoop::GetCommandRingBuffer()
{
	return m_commandRingBuffer;
}

void RenderLoop::Init()
{
	// Use any command buffer for init
	auto commandBuffer = m_commandRingBuffer.ResetAndGetCommandBuffer();
	commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	{
		OnInit();
	}
	commandBuffer.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	m_commandRingBuffer.Submit(submitInfo);
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
	m_commandRingBuffer.WaitUntilSubmitComplete();

	// Use C API because eErrorOutOfDateKHR throws
	m_imageIndex = 0;
	auto result = vkAcquireNextImageKHR(
		static_cast<VkDevice>(g_device->Get()),
		static_cast<VkSwapchainKHR>(m_swapchain->Get()),
		UINT64_MAX,
		static_cast<VkSemaphore>(m_imageAvailableSemaphores[m_frameIndex] .get()),
		VK_NULL_HANDLE, // fence
		&m_imageIndex);
	if (result == (VkResult)vk::Result::eErrorOutOfDateKHR) 
	{
		RecreateSwapchain();
		return;
	}

	auto commandBuffer = m_commandRingBuffer.ResetAndGetCommandBuffer();
	commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	{
		Render(commandBuffer, m_imageIndex);
	}
	commandBuffer.end();

	// Submit command buffer on graphics queue
	vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
	vk::SubmitInfo submitInfo(
		1, &m_imageAvailableSemaphores[m_frameIndex].get(),
		waitStages,
		1, &commandBuffer,
		1, &m_renderFinishedSemaphores[m_imageIndex].get()
	);
	m_commandRingBuffer.Submit(std::move(submitInfo));
	m_commandRingBuffer.MoveToNext();

	// Presentation
	vk::PresentInfoKHR presentInfo = {};
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_imageIndex].get();

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_swapchain->Get();
	presentInfo.pImageIndices = &m_imageIndex;

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

	m_frameIndex = (m_frameIndex + 1) % kMaxFramesInFlight;
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

	m_commandRingBuffer.Reset(m_swapchain->GetImageCount());

	OnSwapchainRecreated();
}
