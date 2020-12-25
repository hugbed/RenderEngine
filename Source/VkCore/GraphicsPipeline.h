#pragma once

#include "Buffers.h"
#include "Shader.h" // move to cpp

#include "SmallVector.h"

#include <gsl/pointers>

#include <vulkan/vulkan.hpp>

#include <vector>
#include <utility>
#include <map>

struct ImageDescription;

struct GraphicsPipelineInfo
{
	GraphicsPipelineInfo(vk::RenderPass renderPass, vk::Extent2D viewportExtent);

	vk::PrimitiveTopology primitiveTopology = vk::PrimitiveTopology::eTriangleList;
	vk::SampleCountFlagBits sampleCount;
	vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eBack;
	vk::Extent2D viewportExtent;
	vk::RenderPass renderPass; // could be an internal RenderPassID
	bool blendEnable = false;
	bool depthTestEnable = true;
	bool depthWriteEnable = true;
};

using GraphicsPipelineID = uint32_t;

class GraphicsPipelineSystem
{
public:
	GraphicsPipelineSystem(ShaderSystem& shaderSystem);

	ShaderSystem& GetShaderSystem() const { return *m_shaderSystem; }

	auto CreateGraphicsPipeline(
		ShaderInstanceID vertexShaderID,
		ShaderInstanceID fragmentShaderID,
		const GraphicsPipelineInfo& info
	) -> GraphicsPipelineID;

	void ResetGraphicsPipeline(
		GraphicsPipelineID graphicsPipelineID,
		const GraphicsPipelineInfo& info
	);

	auto GetDescriptorSetLayoutBindings(
	) const -> const std::vector<SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>>&
	{
		return m_descriptorSetLayoutBindings;
	}

	auto GetDescriptorSetLayoutBindings(
		GraphicsPipelineID id
	) const -> const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>&
	{
		return m_descriptorSetLayoutBindings[id];
	}

	auto GetDescriptorSetLayoutBindings(
		GraphicsPipelineID id,
		uint8_t set
	) const -> const SmallVector<vk::DescriptorSetLayoutBinding>&
	{
		return m_descriptorSetLayoutBindings[id][set];
	}

	auto GetDescriptorSetLayout(
		GraphicsPipelineID id,
		uint8_t set
	) const -> vk::DescriptorSetLayout
	{
		return m_descriptorSetLayouts[id][set].get();
	}

	// --- todo: reorganize calls to navigate the arrays instead --- //

	bool IsSetLayoutCompatible(GraphicsPipelineID a, GraphicsPipelineID b, uint8_t set) const
	{
		const auto& compatibility = m_pipelineCompatibility[a];
		const auto& otherCompatibility = m_pipelineCompatibility[b];
		if (set >= compatibility.size() || set >= otherCompatibility.size())
			return false;

		return compatibility[set] == otherCompatibility[set];
	}

	vk::Pipeline GetPipeline(GraphicsPipelineID id) const { return m_pipelines[id].get(); }

	vk::PipelineLayout GetPipelineLayout(GraphicsPipelineID id, uint8_t set) const { return m_pipelineLayouts[id][set].get(); }

private:
	gsl::not_null<ShaderSystem*> m_shaderSystem;

	struct GraphicsPipelineShaders
	{
		ShaderInstanceID vertexShader;
		ShaderInstanceID fragmentShader;
	};

	// GrapicsPipelineID -> Array Index
	std::vector<GraphicsPipelineShaders> m_shaders; // [id]
	std::vector<SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>> m_descriptorSetLayoutBindings; // [id][set][binding]
	std::vector<SetVector<uint64_t>> m_pipelineCompatibility; // [id][set] (for each set, hash of bindings and constants)
	std::vector<SetVector<vk::UniqueDescriptorSetLayout>> m_descriptorSetLayouts; // [id][set]
	std::vector<SetVector<vk::UniquePipelineLayout>> m_pipelineLayouts; // [id][set]
	std::vector<vk::UniquePipeline> m_pipelines; // [id]

	GraphicsPipelineID m_nextID = 0;
};
