#include <iostream>

#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "CommandBuffers.h"
#include "SynchronizationPrimitives.h"

class App
{
public:
	static constexpr size_t kMaxFramesInFlight = 2;

	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
		: window(window)
		, surface(surface)
		, extent(extent)
		, swapchain(std::make_unique<Swapchain>(surface, extent))
		, renderPass(std::make_unique<RenderPass>(*swapchain))
		, commandBuffers(renderPass->GetFrameBufferCount(), g_physicalDevice->GetQueueFamilies().graphicsFamily.value())
		, syncPrimitives(swapchain->GetImageCount(), kMaxFramesInFlight)
	{
		auto indices = g_physicalDevice->GetQueueFamilies();
		graphicsQueue = g_device->GetQueue(indices.graphicsFamily.value());
		presentQueue = g_device->GetQueue(indices.presentFamily.value());
		
		window.SetWindowResizeCallback(reinterpret_cast<void*>(this), OnResize);

		RecordRenderPassCommands();
	}

	void Run()
	{
		while (window.ShouldClose() == false)
		{
			window.PollEvents();
			Render();
		}
		vkDeviceWaitIdle(g_device->Get());
	}

	static void OnResize(void* data, int w, int h)
	{
		auto app = reinterpret_cast<App*>(data);
		app->frameBufferResized = true;
	}

	void RecordRenderPassCommands()
	{
		// Record commands
		for (size_t i = 0; i < swapchain->GetImageCount(); i++)
		{
			auto& commandBuffer = commandBuffers.Get(i);
			commandBuffer.begin(vk::CommandBufferBeginInfo());
			renderPass->PopulateRenderCommands(commandBuffer, i);
			commandBuffer.end();
		}
	}

	void Render()
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

		syncPrimitives.WaitUntilImageIsAvailable(imageIndex);

		// Submit command buffer on graphics queue
		vk::Semaphore imageAvailableSemaphores[] = { syncPrimitives.GetImageAvailableSemaphore() };
		vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
		vk::Semaphore renderFinishedSemaphores[] = { syncPrimitives.GetRenderFinishedSemaphore() };
		vk::SubmitInfo submitInfo(
			1, imageAvailableSemaphores,
			waitStages,
			1, &commandBuffers.Get(imageIndex),
			1, renderFinishedSemaphores
		);
		g_device->Get().resetFences(frameFence); // reset right before submit
		graphicsQueue.submit(submitInfo, frameFence);

		// Presentation
		vk::PresentInfoKHR presentInfo = {};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = renderFinishedSemaphores;

		vk::SwapchainKHR swapChains[] = { swapchain->Get() };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;

		result = presentQueue.presentKHR(presentInfo);
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

	void RecreateSwapchain()
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
		renderPass.reset();
		swapchain.reset();

		swapchain = std::make_unique<Swapchain>(surface, extent);
		extent = swapchain->GetImageExtent();

		renderPass = std::make_unique<RenderPass>(*swapchain);

		commandBuffers.Reset(swapchain->GetImageCount());

		RecordRenderPassCommands();
	}

private:
	vk::Extent2D extent;
	Window& window;
	bool frameBufferResized{ false };
	vk::SurfaceKHR surface;
	
	std::unique_ptr<Swapchain> swapchain;
	std::unique_ptr<RenderPass> renderPass;
	CommandBuffers commandBuffers;

	vk::Queue graphicsQueue;
	vk::Queue presentQueue;

	SynchronizationPrimitives syncPrimitives;
};

int main()
{
	vk::Extent2D extent(800, 600);
	Window window(extent, "Vulkan");
	Instance instance(window);
	vk::UniqueSurfaceKHR surface(window.CreateSurface(instance.Get()));

	PhysicalDevice::Init(instance.Get(), surface.get());
	Device::Init(*g_physicalDevice);
	{
		App app(surface.get(), extent, window);
		app.Run();
	}
	Device::Term();
	PhysicalDevice::Term();

	return 0;
}
