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

class CommandBuffers
{
public:
	CommandBuffers(vk::Device device, size_t count, uint32_t queueFamily)
	{
		// Pool
		vk::CommandPoolCreateInfo poolInfo({}, queueFamily);
		m_commandPool = device.createCommandPoolUnique(poolInfo);
		
		Reset(device, count);
	}

	vk::CommandBuffer Get(uint32_t imageIndex)
	{
		return m_commandBuffers[imageIndex].get();
	}

	void Reset(vk::Device device, size_t count)
	{
		// Buffer
		vk::CommandBufferAllocateInfo commandBufferAllocateInfo(
			m_commandPool.get(), vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(count)
		);

		// Command buffers
		m_commandBuffers = device.allocateCommandBuffersUnique(commandBufferAllocateInfo);
	}

private:
	vk::UniqueCommandPool m_commandPool;
	std::vector<vk::UniqueCommandBuffer> m_commandBuffers;
};

class SynchronizationPrimitives
{
public:
	SynchronizationPrimitives(vk::Device device, uint32_t swapchainImagesCount, size_t maxFramesInFlight)
		: m_maxFramesInFlight(maxFramesInFlight)
		, device(device)
	{
		m_imageAvailableSemaphores.reserve(maxFramesInFlight);
		m_renderFinishedSemaphores.reserve(maxFramesInFlight);
		m_inFlightFences.reserve(maxFramesInFlight);
		m_imagesInFlight.resize(swapchainImagesCount);

		for (size_t i = 0; i < maxFramesInFlight; ++i)
		{
			m_imageAvailableSemaphores.push_back(device.createSemaphoreUnique({}));
			m_renderFinishedSemaphores.push_back(device.createSemaphoreUnique({}));
			m_inFlightFences.push_back(device.createFenceUnique(
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
		device.waitForFences(
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
			device.waitForFences(m_imagesInFlight[imageIndex], true, UINT64_MAX);
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
	vk::Device device;

	size_t m_currentFrame = 0;
	int m_maxFramesInFlight = 2;
	std::vector<vk::UniqueSemaphore> m_imageAvailableSemaphores;
	std::vector<vk::UniqueSemaphore> m_renderFinishedSemaphores;
	std::vector<vk::UniqueFence> m_inFlightFences;
	std::vector<vk::Fence> m_imagesInFlight;
};

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
			(*renderPass).GetFrameBufferCount(),
			physicalDevice.GetQueueFamilies().graphicsFamily.value()
		 )
		, syncPrimitives(device.Get(), swapchain->GetImageCount(), kMaxFramesInFlight)
	{
		auto indices = physicalDevice.GetQueueFamilies();
		graphicsQueue = device.GetQueue(indices.graphicsFamily.value());
		presentQueue = device.GetQueue(indices.presentFamily.value());
		
		window.SetWindowResizeCallback(reinterpret_cast<void*>(this), OnResize);

		RecordRenderPass();
	}

	void Run()
	{
		window.MainLoop([this]() { Render(); });

		vkDeviceWaitIdle(device.Get());
	}

	static void OnResize(void* data, int w, int h)
	{
		auto app = reinterpret_cast<App*>(data);
		app->frameBufferResized = true;
	}

	void RecordRenderPass()
	{
		// Record commands
		for (size_t i = 0; i < swapchain->GetImageCount(); i++)
		{
			auto& commandBuffer = commandBuffers.Get(i);
			commandBuffer.begin(vk::CommandBufferBeginInfo());
			renderPass->SendRenderCommands(commandBuffer, i);
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

		// Subpass dependencies
		vk::SubpassDependency dependency;
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.srcAccessMask = vk::AccessFlags();
		dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

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
		RecordRenderPass();
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
