#pragma once

#include "Swapchain.h"

#include <vulkan/vulkan.hpp>

#include <fstream>

class RenderPass
{
public:
	RenderPass(vk::Device device, const Swapchain& swapchain);

	void SendRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

private:
	vk::Extent2D m_imageExtent;
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniqueRenderPass m_renderPass;
	vk::UniquePipeline m_graphicsPipeline;
	std::vector<vk::UniqueFramebuffer> m_framebuffers;
};
