#pragma once

#include "Buffers.h"
#include "Shader.h" // move to cpp

#include <vulkan/vulkan.hpp>

#include <vector>
#include <utility>
#include <map>

struct ImageDescription;

class GraphicsPipeline
{
public:
	using value_type = vk::Pipeline;

	GraphicsPipeline(vk::RenderPass renderPass, vk::Extent2D viewportExtent, const Shader& vertexShader, const Shader& fragmentShader);

	vk::PipelineLayout GetLayout() const { return m_pipelineLayout.get(); }

	const vk::DescriptorSetLayout& GetDescriptorSetLayout(size_t set) const
	{
		return m_descriptorSetLayouts[set].get();
	}

	const std::vector<vk::DescriptorSetLayoutBinding>& GetDescriptorSetLayoutBindings(size_t set) const 
	{
		return m_descriptorSetLayoutBindings[set];
	}

	const std::vector<vk::PushConstantRange> GetPushConstantRanges() const
	{
		return m_pushConstantRanges;
	}

	vk::PipelineLayout GetPipelineLayout() const
	{
		return m_pipelineLayout.get();
	}

	const value_type& Get() const { return m_graphicsPipeline.get(); }

private:
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniquePipeline m_graphicsPipeline;

	// A list of DescriptorSetLayoutBinding per descriptor set
	std::vector<std::vector<vk::DescriptorSetLayoutBinding>> m_descriptorSetLayoutBindings;
	std::vector<vk::UniqueDescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<vk::PushConstantRange> m_pushConstantRanges;
};
