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
	GraphicsPipelineInfo();

	vk::PrimitiveTopology primitiveTopology = vk::PrimitiveTopology::eTriangleList;
	bool blendEnable = false;
	bool depthTestEnable = true;
	bool depthWriteEnable = true;
	vk::SampleCountFlagBits sampleCount;
	vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eBack;
};

class GraphicsPipeline
{
public:
	using value_type = vk::Pipeline;

	GraphicsPipeline(
		vk::RenderPass renderPass,
		vk::Extent2D viewportExtent,
		const ShaderSystem& shaderSystem,
		ShaderInstanceID vertexShaderID, ShaderInstanceID fragmentShaderID,
		const GraphicsPipelineInfo& info
	);

	GraphicsPipeline(
		vk::RenderPass renderPass,
		vk::Extent2D viewportExtent,
		const ShaderSystem& shaderSystem,
		ShaderInstanceID vertexShaderID, ShaderInstanceID fragmentShaderID
	);

	const ShaderSystem* m_shaderSystem = nullptr;

	const vk::DescriptorSetLayout& GetDescriptorSetLayout(size_t set) const
	{
		return m_descriptorSetLayouts[set].get();
	}

	const std::vector<vk::DescriptorSetLayoutBinding>& GetDescriptorSetLayoutBindings(size_t set) const 
	{
		return m_descriptorSetLayoutBindings[set];
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
		ShaderInstanceID vertexShaderID, ShaderInstanceID fragmentShaderID,
		const GraphicsPipelineInfo& info
	);

	vk::UniquePipeline m_graphicsPipeline;

	// A list of DescriptorSetLayoutBinding per descriptor set
	std::vector<uint64_t> m_pipelineCompatibility; // for each set, hash of bindings and constants
	std::vector<std::vector<vk::DescriptorSetLayoutBinding>> m_descriptorSetLayoutBindings;
	std::vector<vk::UniquePipelineLayout> m_pipelineLayouts; // i is for sets 0..i
	std::vector<vk::UniqueDescriptorSetLayout> m_descriptorSetLayouts;
};
};
