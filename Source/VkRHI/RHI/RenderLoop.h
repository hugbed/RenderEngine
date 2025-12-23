#pragma once

#include "RHI/Window.h"
#include "RHI/CommandBufferPool.h"

#include <vulkan/vulkan.hpp>

#include <chrono>

class Swapchain;
class GraphicsPipeline;

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
	virtual void OnSwapchainRecreated() = 0;
	virtual void Update() = 0;
	virtual void RenderFrame(uint32_t imageIndex, vk::CommandBuffer commandBuffer) = 0;

	static void OnResize(void* data, int w, int h);

	std::chrono::high_resolution_clock::duration GetDeltaTime() const { return m_deltaTime; }

private:
	void Render();
	void RecreateSwapchain();
	void UpdateDeltaTime();

	std::chrono::duration<float> m_framePeriod{ 1.0f / 60.0f };
	std::chrono::high_resolution_clock::duration m_deltaTime;
	std::chrono::time_point<std::chrono::high_resolution_clock> m_lastUpdateTime;

protected:
	Window& m_window;
	bool m_frameBufferResized{ false };
	vk::SurfaceKHR m_surface;
	std::unique_ptr<Swapchain> m_swapchain;
	CommandBufferPool m_commandBufferPool;

	vk::UniqueSemaphore m_imageAvailableSemaphores[kMaxFramesInFlight];
	std::vector<vk::UniqueSemaphore> m_renderFinishedSemaphores; // num of swapchain images
	uint8_t m_frameIndex = 0;
};
