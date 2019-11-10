#pragma once

#include "Window.h"
#include "CommandBuffers.h"
#include "SynchronizationPrimitives.h"

#include <vulkan/vulkan.hpp>

class Swapchain;
class RenderPass;

// BaseApp:
//   1) Initialize resources
//   2) Uploads data
//   3) Populates render commands
//   4) Render Loop
//      --> Wait for frame
//      --> Update uniforms
//      --> Render
//
class RenderLoop
{
public:
	static constexpr size_t kMaxFramesInFlight = 2;

	RenderLoop(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window);

	void Init();
	void Run();

protected:
	virtual void Init(vk::CommandBuffer& commandBuffer) = 0;
	virtual void RecordRenderPassCommands(CommandBuffers& commandBuffers) = 0;
	virtual void UpdateImageResources(uint32_t imageIndex) = 0;

	static void OnResize(void* data, int w, int h);

	void Render();

	void RecreateSwapchain();

	vk::Extent2D extent;
	Window& window;
	bool frameBufferResized{ false };
	vk::SurfaceKHR surface;

	std::unique_ptr<Swapchain> swapchain;
	std::unique_ptr<RenderPass> renderPass;
	CommandBuffers m_renderCommandBuffers;
	CommandBuffers m_uploadCommandBuffers;

	SynchronizationPrimitives syncPrimitives;
};
