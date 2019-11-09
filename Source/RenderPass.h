#pragma once

#include "Swapchain.h"
#include "Buffers.h"

#include <vulkan/vulkan.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <vector>

struct UniformBufferObject {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

struct Vertex {
	glm::vec2 pos;
	glm::vec3 color;

	static vk::VertexInputBindingDescription GetBindingDescription();
	static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions();
};

class RenderPass
{
public:
	RenderPass(const Swapchain& swapchain);

	void PopulateRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

	size_t GetFrameBufferCount() const { return m_framebuffers.size(); }

	vk::DescriptorSetLayout GetDescriptorSetLayout() const { return m_descriptorSetLayout.get(); }

	void BindVertexBuffer(vk::Buffer buffer) { m_vertexBuffer = buffer; }
	void BindIndexBuffer(vk::Buffer buffer) { m_indexBuffer = buffer; }
	void BindDescriptorSets(std::vector<vk::DescriptorSet> descriptorSets) { m_descriptorSets = std::move(descriptorSets); }

private:
	vk::Extent2D m_imageExtent;
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniqueRenderPass m_renderPass;
	vk::UniquePipeline m_graphicsPipeline;
	std::vector<vk::UniqueFramebuffer> m_framebuffers;

	// Optional in a generic render pass / for a graphics pipeline
	vk::UniqueDescriptorSetLayout m_descriptorSetLayout;

	// Bind points
	vk::Buffer m_vertexBuffer{ nullptr };
	vk::Buffer m_indexBuffer{ nullptr };
	std::vector<vk::DescriptorSet> m_descriptorSets;
};
