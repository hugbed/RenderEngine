#include "RenderLoop.h"

#include "Swapchain.h"
#include "RenderPass.h"

RenderLoop::RenderLoop(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
	: window(window)
	, surface(surface)
	, extent(extent)
	, swapchain(std::make_unique<Swapchain>(surface, extent))
	, m_renderCommandBuffers(swapchain->GetImageCount(), g_physicalDevice->GetQueueFamilies().graphicsFamily.value())
	, m_uploadCommandBuffers(1, g_physicalDevice->GetQueueFamilies().graphicsFamily.value(), vk::CommandPoolCreateFlagBits::eTransient)
	, syncPrimitives(swapchain->GetImageCount(), kMaxFramesInFlight)
{
	window.SetWindowResizeCallback(reinterpret_cast<void*>(this), OnResize);
}

void RenderLoop::Init()
{
	SingleTimeCommandBuffer initCommandBuffer(m_uploadCommandBuffers.Get(0));
	Init(initCommandBuffer.Get());
}

void RenderLoop::Run()
{
	while (window.ShouldClose() == false)
	{
		window.PollEvents();
		Render();
	}
	vkDeviceWaitIdle(g_device->Get());
}

void RenderLoop::OnResize(void* data, int w, int h)
{
	auto app = reinterpret_cast<RenderLoop*>(data);
	app->frameBufferResized = true;
}

void RenderLoop::Render()
{
	auto frameFence = syncPrimitives.WaitForFrame();

	auto [result, imageIndex] = g_device->Get().acquireNextImageKHR(
		swapchain->Get(),
		UINT64_MAX, // max timeout
		syncPrimitives.GetImageAvailableSemaphore(),
		nullptr
	);
	if (result == vk::Result::eErrorOutOfDateKHR) // todo: this one will throw
	{
		RecreateSwapchain();
		return;
	}

	UpdateImageResources(imageIndex);

	syncPrimitives.WaitUntilImageIsAvailable(imageIndex);

	// Submit command buffer on graphics queue
	vk::Semaphore imageAvailableSemaphores[] = { syncPrimitives.GetImageAvailableSemaphore() };
	vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
	vk::Semaphore renderFinishedSemaphores[] = { syncPrimitives.GetRenderFinishedSemaphore() };
	vk::SubmitInfo submitInfo(
		1, imageAvailableSemaphores,
		waitStages,
		1, &m_renderCommandBuffers.Get(imageIndex),
		1, renderFinishedSemaphores
	);
	g_device->Get().resetFences(frameFence); // reset right before submit
	g_device->GetGraphicsQueue().submit(submitInfo, frameFence);

	// Presentation
	vk::PresentInfoKHR presentInfo = {};
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = renderFinishedSemaphores;

	vk::SwapchainKHR swapChains[] = { swapchain->Get() };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;

	result = g_device->GetPresentQueue().presentKHR(presentInfo);
	if (result == vk::Result::eSuboptimalKHR ||
		result == vk::Result::eErrorOutOfDateKHR || // todo: this one will throw
		frameBufferResized)
	{
		frameBufferResized = false;
		RecreateSwapchain();
		return;
	}
	else if (result != vk::Result::eSuccess)
	{
		throw std::runtime_error("Failed to acquire swapchain image");
	}

	syncPrimitives.MoveToNextFrame();
}

void RenderLoop::RecreateSwapchain()
{
	extent = window.GetFramebufferSize();

	// Wait if window is minimized
	while (extent.width == 0 || extent.height == 0)
	{
		extent = window.GetFramebufferSize();
		window.WaitForEvents();
	}

	g_device->Get().waitIdle();

	// Recreate swapchain and render pass
	swapchain.reset();

	swapchain = std::make_unique<Swapchain>(surface, extent);
	extent = swapchain->GetImageExtent();

	m_renderCommandBuffers.Reset(swapchain->GetImageCount());

	OnSwapchainRecreated(m_renderCommandBuffers);
}
