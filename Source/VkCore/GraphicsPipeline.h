#pragma once

#include "Buffers.h"
#include "Shader.h" // move to cpp

#include <vulkan/vulkan.hpp>

#include <vector>
#include <utility>
#include <map>

struct ImageDescription;

struct GraphicsPipelineInfo
{
	bool blendEnable = false;
	bool depthWriteEnable = true;
};

class GraphicsPipeline
{
public:
	using value_type = vk::Pipeline;

	GraphicsPipeline(
		vk::RenderPass renderPass,
		vk::Extent2D viewportExtent,
		const Shader& vertexShader, const Shader& fragmentShader
	);

	GraphicsPipeline(
		vk::RenderPass renderPass,
		vk::Extent2D viewportExtent,
		const Shader& vertexShader, const Shader& fragmentShader,
		const GraphicsPipelineInfo& info
	);

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

	vk::PipelineLayout GetPipelineLayout(size_t set) const
	{
		if (set >= m_pipelineLayouts.size())
			return {};

		return m_pipelineLayouts[set].get();
	}

	bool IsLayoutCompatible(const GraphicsPipeline& other, size_t set) const
	{
		if (set >= m_pipelineCompatibility.size() || set >= other.m_pipelineCompatibility.size())
			return false;

		return m_pipelineCompatibility[set] == other.m_pipelineCompatibility[set];
	}

	const value_type& Get() const { return m_graphicsPipeline.get(); }

private:
	void Init(
		vk::RenderPass renderPass,
		vk::Extent2D viewportExtent,
		const Shader& vertexShader, const Shader& fragmentShader,
		const GraphicsPipelineInfo& info
	);

	vk::UniquePipeline m_graphicsPipeline;

	// A list of DescriptorSetLayoutBinding per descriptor set
	std::vector<uint64_t> m_pipelineCompatibility; // for each set, hash of 
	std::vector<std::vector<vk::DescriptorSetLayoutBinding>> m_descriptorSetLayoutBindings;
	std::vector<vk::UniquePipelineLayout> m_pipelineLayouts; // i is for sets 0..i
	std::vector<vk::UniqueDescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<vk::PushConstantRange> m_pushConstantRanges;
};
