#pragma once

#include "Buffers.h"
#include "Shader.h" // move to cpp

#include <vulkan/vulkan.hpp>

#include <vector>
#include <utility>

class ImageDescription;

struct DescriptorSetPool
{
	DescriptorSetPool() = default;
	
	DescriptorSetPool(const DescriptorSetPool&) = delete;

	DescriptorSetPool(DescriptorSetPool&&) = default;

	DescriptorSetPool& operator=(DescriptorSetPool&& other)
	{
		descriptorSets = std::move(other).descriptorSets;
		descriptorPool = std::move(other).descriptorPool;
		return *this;
	}

	~DescriptorSetPool()
	{
		// Clear descriptor sets before descriptor pool
		descriptorSets.clear();
		descriptorPool.reset();
	}

	// todo: descriptor sets could possibly use the same pool
	std::vector<vk::UniqueDescriptorSet> descriptorSets;
	vk::UniqueDescriptorPool descriptorPool;
};

class GraphicsPipeline
{
public:
	using value_type = vk::Pipeline;

	GraphicsPipeline(vk::RenderPass renderPass, vk::Extent2D viewportExtent, const Shader& vertexShader, const Shader& fragmentShader);

	vk::PipelineLayout GetLayout() const { return m_pipelineLayout.get(); }

	void Draw(
		vk::CommandBuffer& commandBuffer,
		uint32_t indexCount,
		vk::Buffer vertexBuffer,
		vk::Buffer indexBuffer,
		VkDeviceSize* vertexOffsets,
		vk::DescriptorSet descriptorSet);

	DescriptorSetPool CreateDescriptorSetPool(uint32_t size) const;

	value_type Get() const { return m_graphicsPipeline.get(); }

private:
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniquePipeline m_graphicsPipeline;

	// Optional in a generic render pass / for a graphics pipeline
	std::vector<vk::DescriptorSetLayoutBinding> m_descriptorSetLayoutBindings;
	vk::UniqueDescriptorSetLayout m_descriptorSetLayout;
};
