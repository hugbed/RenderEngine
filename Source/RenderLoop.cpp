#include "RenderLoop.h"

#include "Swapchain.h"
#include "GraphicsPipeline.h"
#include "Device.h"
#include "PhysicalDevice.h"

#include <iostream>

RenderLoop::RenderLoop(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
	: m_window(window)
	, m_surface(surface)
	, m_swapchain(std::make_unique<Swapchain>(surface, extent))
	, m_renderCommandBuffers(m_swapchain->GetImageCount(), kMaxFramesInFlight, g_physicalDevice->GetQueueFamilies().graphicsFamily.value())
{
	window.SetWindowResizeCallback(reinterpret_cast<void*>(this), OnResize);
}

void RenderLoop::Init()
{
	// Use any command buffer for init
	auto commandBuffer = m_renderCommandBuffers.GetCommandBuffer();
	{
		SingleTimeCommandBuffer singleTimeCommandBuffer(commandBuffer);
		Init(commandBuffer);
	}
	// But make sure to reset it once we're done
	m_renderCommandBuffers.ResetCommandBuffer();
}

void RenderLoop::Run()
{
	while (m_window.ShouldClose() == false)
	{
		m_window.PollEvents();

		UpdateDeltaTime();

		Update();
		Render();
	}
	vkDeviceWaitIdle(g_device->Get());
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
	auto& submitFence = m_renderCommandBuffers.WaitUntilSubmitComplete();

	auto [result, imageIndex] = g_device->Get().acquireNextImageKHR(
		m_swapchain->Get(),
		UINT64_MAX, // max timeout
		m_gpuSync.imageAvailableSemaphore.get(),
		nullptr
	);
	if (result == vk::Result::eErrorOutOfDateKHR) // todo: this one will throw
	{
		RecreateSwapchain();
		return;
	}

	auto commandBuffer = m_renderCommandBuffers.ResetCommandBuffer();
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
		1, &m_renderCommandBuffers.GetCommandBuffer(),
		1, &m_gpuSync.renderFinishedSemaphore.get()
	);
	g_device->Get().resetFences(submitFence); // reset right before submit
	g_device->GetGraphicsQueue().submit(submitInfo, submitFence);

	// Presentation
	vk::PresentInfoKHR presentInfo = {};
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &m_gpuSync.renderFinishedSemaphore.get();

	vk::SwapchainKHR swapChains[] = { m_swapchain->Get() };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;

	result = g_device->GetPresentQueue().presentKHR(presentInfo);
	if (result == vk::Result::eSuboptimalKHR ||
		result == vk::Result::eErrorOutOfDateKHR || // todo: this one will throw
		m_frameBufferResized)
	{
		m_frameBufferResized = false;
		RecreateSwapchain();
		return;
	}
	else if (result != vk::Result::eSuccess)
	{
		throw std::runtime_error("Failed to acquire swapchain image");
	}

	m_renderCommandBuffers.MoveToNext();
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

	m_renderCommandBuffers.Reset(m_swapchain->GetImageCount());

	OnSwapchainRecreated(m_renderCommandBuffers);
}
