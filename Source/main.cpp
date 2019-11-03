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

int main()
{
	vk::Extent2D extent{ 800, 600 };

	Window window(extent, "Vulkan");

	Instance instance(window);

	vk::UniqueSurfaceKHR surface = window.CreateSurface(instance.Get());

	PhysicalDevice physicalDevice(instance.Get(), surface.get());

	Device device(physicalDevice);

	// Queues
	auto indices = physicalDevice.GetQueueFamilies();
	auto graphicsQueue = device.GetQueue(indices.graphicsFamily.value());
	auto presentQueue = device.GetQueue(indices.presentFamily.value());

	Swapchain swapchain(
		device,
		physicalDevice,
		surface.get(),
		extent
	);

	RenderPass renderPass(device.Get(), swapchain);

	// Commands

	// Pool
	vk::CommandPoolCreateInfo poolInfo({}, indices.graphicsFamily.value());
	vk::UniqueCommandPool commandPool = device.Get().createCommandPoolUnique(poolInfo);

	// Buffer
	vk::CommandBufferAllocateInfo commandBufferAllocateInfo(
		commandPool.get(), vk::CommandBufferLevel::ePrimary, swapchain.GetImageCount()
	);

	// Create command buffers
	std::vector<vk::UniqueCommandBuffer> commandBuffers = device.Get().allocateCommandBuffersUnique(commandBufferAllocateInfo);

	// Record commands
	for (size_t i = 0; i < commandBuffers.size(); i++)
	{
		auto& commandBuffer = commandBuffers[i];
		commandBuffer->begin(vk::CommandBufferBeginInfo());
		renderPass.SendRenderCommands(commandBuffer.get(), i);
		commandBuffer->end();
	}

	// Synchronization

	vk::UniqueSemaphore imageAvailableSemaphore = device.Get().createSemaphoreUnique({});
	vk::UniqueSemaphore renderFinishedSemaphore = device.Get().createSemaphoreUnique({});

	window.MainLoop(
		[&device, &swapchain, &imageAvailableSemaphore, &renderFinishedSemaphore, &commandBuffers, &graphicsQueue, &presentQueue]()
		{
			// Acquire image from swapchain (this could be in swapchain?)
			auto [result, imageIndex] = device.Get().acquireNextImageKHR(swapchain.Get(), uint64_t(UINT64_MAX), imageAvailableSemaphore.get(), nullptr);
			if (result != vk::Result::eSuccess)
				return;

			// Submit command buffer on graphics queue
			vk::Semaphore waitSemaphores[] = { imageAvailableSemaphore.get() };
			vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
			vk::Semaphore signalSemaphores[] = { renderFinishedSemaphore.get() };
			vk::SubmitInfo submitInfo(
				1, waitSemaphores, waitStages,
				1, &commandBuffers[imageIndex].get(),
				1, signalSemaphores
			);
			graphicsQueue.submit(submitInfo, nullptr);

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
			presentInfo.pWaitSemaphores = signalSemaphores;
			
			vk::SwapchainKHR swapChains[] = { swapchain.Get() };
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = swapChains;
			presentInfo.pImageIndices = &imageIndex;

			result = presentQueue.presentKHR(presentInfo);
			if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
				return; // recreate swapchain

			vkQueueWaitIdle(presentQueue);
		}
	);

	return 0;
}
