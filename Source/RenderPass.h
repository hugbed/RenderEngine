#pragma once

#include "Swapchain.h"
#include "Buffers.h"

#include <vulkan/vulkan.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <vector>

struct UniformBufferObject {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	bool operator==(const Vertex& other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord;
	}

	static vk::VertexInputBindingDescription GetBindingDescription();
	static std::array<vk::VertexInputAttributeDescription, 3> GetAttributeDescriptions();
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}

class RenderPass
{
public:
	RenderPass(const Swapchain& swapchain);

	void PopulateRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

	size_t GetFrameBufferCount() const { return m_framebuffers.size(); }

	vk::DescriptorSetLayout GetDescriptorSetLayout() const { return m_descriptorSetLayout.get(); }

	void BindVertexBuffer(vk::Buffer buffer) { m_vertexBuffer = buffer; }
	void BindIndexBuffer(vk::Buffer buffer, size_t nbIndices) { m_indexBuffer = buffer; m_nbIndices = nbIndices; }
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
	size_t m_nbIndices{ 0 };
	vk::Buffer m_vertexBuffer{ nullptr };
	vk::Buffer m_indexBuffer{ nullptr };
	std::vector<vk::DescriptorSet> m_descriptorSets;
};
