#pragma once

#include <RHI/CommandRingBuffer.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <vector>
#include <optional>
#include <cstdint>

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
		VkRenderPass renderPass;
	};

	ImGuiVulkan(const Resources& resources, vk::CommandBuffer commandBuffer);
	~ImGuiVulkan();

	void Reset(const Resources& resources, vk::CommandBuffer commandBuffer);

	void BeginFrame();
	void RecordCommands(uint32_t frameIndex, VkFramebuffer framebuffer);
	void EndFrame();

	vk::CommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return m_imguiCommandBuffers[frameIndex].get(); }

private:
	VkDevice m_device = VK_NULL_HANDLE;
	VkRenderPass m_renderPass = VK_NULL_HANDLE;

	ImGui_ImplVulkanH_Window m_imguiWindow;
	VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;
	std::vector<vk::UniqueCommandBuffer> m_imguiCommandBuffers;
	vk::UniqueCommandPool m_secondaryCommandPool;
};
