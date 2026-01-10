#pragma once

#include <RHI/CommandRingBuffer.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <vector>
#include <optional>
#include <cstdint>

class Swapchain;

class ImGuiVulkan
{
public:
	struct Resources
	{
		GLFWwindow* window;
		VkInstance instance;
		VkPhysicalDevice physicalDevice;
		VkDevice device;
		uint32_t queueFamily;
		VkQueue queue;
		uint32_t imageCount;
		VkSampleCountFlagBits MSAASamples;
		vk::Extent2D extent;
		vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo;
	};

	ImGuiVulkan(const Resources& resources);
	~ImGuiVulkan();

	void Reset(const Resources& resources);

	void BeginFrame();
	void Render(vk::CommandBuffer commandBuffer, uint32_t imageIndex, const Swapchain& swapchain);
	void EndFrame();

private:
	VkDevice m_device = VK_NULL_HANDLE;
	ImGui_ImplVulkanH_Window m_imguiWindow;
	VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;

	void Init(const Resources& resources);
	void Shutdown();
};
