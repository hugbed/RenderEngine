#pragma once

#include "Buffers.h"

#include <vulkan/vulkan.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <vector>
#include <utility>

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

class GraphicsPipeline
{
public:
	using value_type = vk::Pipeline;

	// todo: does not require the whole swapchain, only the number of images + depth images
	// todo: pass shaders here
	GraphicsPipeline(vk::Extent2D imageExtent, vk::Format imageFormat, vk::RenderPass renderPass);

	vk::PipelineLayout GetLayout() const { return m_pipelineLayout.get(); }

	void Draw(
		vk::CommandBuffer& commandBuffer,
		uint32_t indexCount,
		vk::Buffer vertexBuffer,
		vk::Buffer indexBuffer,
		VkDeviceSize* vertexOffsets,
		vk::DescriptorSet descriptorSet);

	value_type Get() const { return m_graphicsPipeline.get(); }

	struct Descriptors
	{
		Descriptors() = default;
		Descriptors(Descriptors&&) = default;
		Descriptors& operator=(Descriptors&& other)
		{
			descriptorSets = std::move(other).descriptorSets;
			descriptorPool = std::move(other).descriptorPool;
			return *this;
		}

		~Descriptors()
		{
			// Clear descriptor sets before descriptor pool
			descriptorSets.clear();
			descriptorPool.reset();
		}

		// todo: descriptor sets could possibly use the same pool
		std::vector<vk::UniqueDescriptorSet> descriptorSets;
		vk::UniqueDescriptorPool descriptorPool;
	};

	Descriptors CreateDescriptorSets(std::vector<vk::Buffer> uniformBuffers, vk::ImageView textureImageView, vk::Sampler textureSampler);

private:
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniquePipeline m_graphicsPipeline;

	// Optional in a generic render pass / for a graphics pipeline
	vk::UniqueDescriptorSetLayout m_descriptorSetLayout;
};
