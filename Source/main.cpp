#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

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

	App()
		: extent(800, 600)
		, window(extent, "Vulkan")
		, instance(window)
		, surface(window.CreateSurface(instance.Get()))
		, physicalDevice(instance.Get(), surface.get())
		, device(physicalDevice)
		, swapchain(std::make_unique<Swapchain>(device, physicalDevice, surface.get(), extent))
		, renderPass(std::make_unique<RenderPass>(device.Get(), *swapchain))
		, commandBuffers(
			device.Get(),
			renderPass->GetFrameBufferCount(),
			physicalDevice.GetQueueFamilies().graphicsFamily.value()
		 )
		, syncPrimitives(device.Get(), swapchain->GetImageCount(), kMaxFramesInFlight)
	{
		auto indices = physicalDevice.GetQueueFamilies();
		graphicsQueue = device.GetQueue(indices.graphicsFamily.value());
		presentQueue = device.GetQueue(indices.presentFamily.value());
		
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
		vkDeviceWaitIdle(device.Get());
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
			renderPass->AddRenderCommands(commandBuffer, i);
			commandBuffer.end();
		}
	}

	void Render()
	{
		auto frameFence = syncPrimitives.WaitForFrame();

		auto [result, imageIndex] = device.Get().acquireNextImageKHR(
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
		device.Get().resetFences(frameFence); // reset right before submit
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

		device.Get().waitIdle();

		// Recreate swapchain and render pass
		renderPass.reset();
		swapchain.reset();

		swapchain = std::make_unique<Swapchain>(
			device,
			physicalDevice,
			surface.get(),
			extent
		);
		extent = swapchain->GetImageExtent();

		renderPass = std::make_unique<RenderPass>(device.Get(), *swapchain);

		commandBuffers.Reset(device.Get(), swapchain->GetImageCount());

		RecordRenderPassCommands();
	}

private:
	vk::Extent2D extent;
	Window window;
	bool frameBufferResized{ false };
	Instance instance;
	vk::UniqueSurfaceKHR surface;
	PhysicalDevice physicalDevice;
	Device device;
	
	std::unique_ptr<Swapchain> swapchain;
	std::unique_ptr<RenderPass> renderPass;
	CommandBuffers commandBuffers;

	vk::Queue graphicsQueue;
	vk::Queue presentQueue;

	// Synchronization
	SynchronizationPrimitives syncPrimitives;
};

int main()
{
	App app;
	app.Run();
	return 0;
}
