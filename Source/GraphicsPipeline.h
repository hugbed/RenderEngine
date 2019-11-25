#pragma once

#include "Buffers.h"

#include <vulkan/vulkan.hpp>

#include <vector>
#include <utility>

class ImageDescription;

class GraphicsPipeline
{
public:
	using value_type = vk::Pipeline;

	// todo: does not require the whole swapchain, only the number of images + depth images
	// todo: pass shaders here
	GraphicsPipeline(vk::RenderPass renderPass, vk::Extent2D viewportExtent);

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

	Descriptors CreateDescriptorSets(std::vector<vk::Buffer> uniformBuffers, size_t uniformBufferSize, vk::ImageView textureImageView, vk::Sampler textureSampler);

private:
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniquePipeline m_graphicsPipeline;

	// Optional in a generic render pass / for a graphics pipeline
	vk::UniqueDescriptorSetLayout m_descriptorSetLayout;
};
