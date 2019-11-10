#include <iostream>

#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "CommandBuffers.h"
#include "SynchronizationPrimitives.h"
#include "Image.h"

#include "vk_utils.h"

// For Uniform Buffer
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

// For Texture
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

const std::vector<Vertex> vertices = {
	{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
	{{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
	{{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
	{{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
};;

const std::vector<uint16_t> indices = {
	0, 1, 2, 2, 3, 0
};

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
		, m_renderCommandBuffers(renderPass->GetFrameBufferCount(), g_physicalDevice->GetQueueFamilies().graphicsFamily.value())
		, m_uploadCommandBuffers(1, g_physicalDevice->GetQueueFamilies().graphicsFamily.value(), vk::CommandPoolCreateFlagBits::eTransient)
		, syncPrimitives(swapchain->GetImageCount(), kMaxFramesInFlight)
		, m_vertexBuffer(
			sizeof(Vertex) * vertices.size(),
			vk::BufferUsageFlagBits::eVertexBuffer)
		, m_indexBuffer(
			sizeof(uint16_t) * indices.size(),
			vk::BufferUsageFlagBits::eIndexBuffer)
	{
		window.SetWindowResizeCallback(reinterpret_cast<void*>(this), OnResize);

		CreateUniformBuffers();
		CreateDescriptorSets();

		{
			SingleTimeCommandBuffer initCommandBuffer(m_uploadCommandBuffers.Get(0));
			UploadGeometry(initCommandBuffer.Get());
			CreateTextureImage(initCommandBuffer.Get());
		}

		// Bind shader variables
		renderPass->BindIndexBuffer(m_indexBuffer.Get());
		renderPass->BindVertexBuffer(m_vertexBuffer.Get());
		renderPass->BindDescriptorSets(vk_utils::remove_unique(m_descriptorSets));

		RecordRenderPassCommands();
	}

	void CreateUniformBuffers()
	{
		m_uniformBuffers.reserve(swapchain->GetImageCount());
		for (uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
		{
			m_uniformBuffers.emplace_back(
				sizeof(UniformBufferObject),
				vk::BufferUsageFlagBits::eUniformBuffer,
				vk::MemoryPropertyFlagBits::eHostCoherent |
					vk::MemoryPropertyFlagBits::eHostCoherent);
		}
	}

	void CreateDescriptorSets()
	{
		vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBuffer, swapchain->GetImageCount());
		m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, swapchain->GetImageCount(), 1, &poolSize
		));
	
		std::vector<vk::DescriptorSetLayout> layouts(swapchain->GetImageCount(), renderPass->GetDescriptorSetLayout());

		m_descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(m_descriptorPool.get(), swapchain->GetImageCount(), layouts.data()));

		for (uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
		{
			vk::DescriptorBufferInfo descriptorBufferInfo(m_uniformBuffers[i].Get(), 0, sizeof(UniformBufferObject));
			g_device->Get().updateDescriptorSets(
				vk::WriteDescriptorSet(
					m_descriptorSets[i].get(), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
				), {}
			);
		}
	}

	// todo: extract reusable code from here and maybe move into a Image factory or something
	void CreateTextureImage(vk::CommandBuffer& commandBuffer)
	{
		// Read image from file
		int texWidth = 0, texHeight = 0, texChannels = 0;
		stbi_uc* pixels = stbi_load("texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0) {
			throw std::runtime_error("failed to load texture image!");
		}

		// Texture image
		m_image = std::make_unique<Image>(
			texWidth, texHeight, 4UL, // R8G8B8A8, depth = 4
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		// Copy image data to staging buffer
		m_image->TransitionLayout(
			commandBuffer,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal
		);
		{
			m_image->Overwrite(commandBuffer, reinterpret_cast<const void*>(pixels));
		}
		m_image->TransitionLayout(
			commandBuffer,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal
		);

		stbi_image_free(pixels);
	}

	void UploadGeometry(vk::CommandBuffer& commandBuffer)
	{
		m_vertexBuffer.Overwrite(commandBuffer, reinterpret_cast<const void*>(vertices.data()));
		m_indexBuffer.Overwrite(commandBuffer, reinterpret_cast<const void*>(indices.data()));
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
			auto& commandBuffer = m_renderCommandBuffers.Get(i);
			commandBuffer.begin(vk::CommandBufferBeginInfo());
			renderPass->PopulateRenderCommands(commandBuffer, i);
			commandBuffer.end();
		}
	}

	// We should inherit from something so we can ignore these low level details about synchronization
	// and only implement like Update, Upload, Render, somethings
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

		m_renderCommandBuffers.Reset(swapchain->GetImageCount());

		RecordRenderPassCommands();
	}

	void UpdateImageResources(uint32_t imageIndex)
	{
		UpdateUniformBuffer(imageIndex);
	}

	void UpdateUniformBuffer(uint32_t imageIndex)
	{
		using namespace std::chrono;

		static auto startTime = high_resolution_clock::now();

		auto currentTime = high_resolution_clock::now();
		float time = duration<float, seconds::period>(currentTime - startTime).count();

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.proj = glm::perspective(glm::radians(45.0f), extent.width / (float)extent.height, 0.1f, 10.0f);

		ubo.proj[1][1] *= -1; // inverse Y for OpenGL -> Vulkan (clip coordinates)

		// Upload to GPU (inefficient, use push-constants instead)
		m_uniformBuffers[imageIndex].Overwrite(reinterpret_cast<const void*>(&ubo));
	}

private:
	// --- All these could be in a base class

	vk::Extent2D extent;
	Window& window;
	bool frameBufferResized{ false };
	vk::SurfaceKHR surface;
	
	std::unique_ptr<Swapchain> swapchain;
	std::unique_ptr<RenderPass> renderPass;
	CommandBuffers m_renderCommandBuffers;
	CommandBuffers m_uploadCommandBuffers;

	SynchronizationPrimitives syncPrimitives;

	// ---

	BufferWithStaging m_vertexBuffer;
	BufferWithStaging m_indexBuffer;
	std::vector<Buffer> m_uniformBuffers; // one per image since these change every frame

	vk::UniqueDescriptorPool m_descriptorPool;
	std::vector<vk::UniqueDescriptorSet> m_descriptorSets;

	std::unique_ptr<Image> m_image;
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
