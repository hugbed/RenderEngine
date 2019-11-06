#pragma once

#include "Swapchain.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class RenderPass
{
public:
	RenderPass(vk::Device device, const Swapchain& swapchain);

	void AddRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

	size_t GetFrameBufferCount() const { return m_framebuffers.size(); }

private:
	vk::Extent2D m_imageExtent;
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniqueRenderPass m_renderPass;
	vk::UniquePipeline m_graphicsPipeline;
	std::vector<vk::UniqueFramebuffer> m_framebuffers;
};
