#pragma once

#include <RHI/ShaderCache.h>
#include <RHI/Buffers.h>
#include "SmallVector.h"
#include <gsl/pointers>
#include <vulkan/vulkan.hpp>

#include <vector>
#include <utility>
#include <map>

struct ImageDescription;

namespace GraphicsPipelineHelpers
{
	[[nodiscard]] uint64_t HashPipelineLayout(
		const VectorView<vk::DescriptorSetLayoutBinding>& bindings,
		const VectorView<vk::PushConstantRange>& pushConstants);

	[[nodiscard]] SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> CombineDescriptorSetLayoutBindings(
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& bindings1, // bindings[set][binding]
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& bindings2);

	[[nodiscard]] SetVector<vk::UniqueDescriptorSetLayout> CreateDescriptorSetLayoutsFromBindings(
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetsLayoutBindings);

	[[nodiscard]] SetVector<vk::UniquePipelineLayout> CreatePipelineLayoutsFromDescriptorSetLayouts(
		const SetVector<vk::UniqueDescriptorSetLayout>& descriptorSetLayouts,
		SmallVector<vk::PushConstantRange> pushConstantRanges);

	[[nodiscard]] SmallVector<vk::PushConstantRange> CombinePushConstantRanges(
		const SmallVector<vk::PushConstantRange>& pushConstantRange1,
		const SmallVector<vk::PushConstantRange>& pushConstantRange2);
}

struct GraphicsPipelineInfo
{
	GraphicsPipelineInfo(vk::RenderPass renderPass, vk::Extent2D viewportExtent);

	vk::PrimitiveTopology primitiveTopology = vk::PrimitiveTopology::eTriangleList;
	vk::SampleCountFlagBits sampleCount;
	vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eBack;
	vk::Extent2D viewportExtent;
	vk::RenderPass renderPass; // could be an internal RenderPassID
	bool blendEnable = false; // todo (hbedard): support mask
	bool depthTestEnable = true;
	bool depthWriteEnable = true;
};

using GraphicsPipelineID = uint32_t;

// Handles all graphics pipeline that share the same layout
class GraphicsPipelineCache
{
public:
	GraphicsPipelineCache(ShaderCache& shaderCache);

	ShaderCache& GetShaderCache() const { return *m_shaderCache; }

	void SetCommonLayout(
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> descriptorSetLayoutBindingOverrides,
		SetVector<vk::DescriptorSetLayout> descriptorSetLayoutOverrides,
		SetVector<vk::PipelineLayout> pipelineLayoutOverrides);

	GraphicsPipelineID CreateGraphicsPipeline(
		ShaderInstanceID vertexShaderID,
		ShaderInstanceID fragmentShaderID,
		const GraphicsPipelineInfo& info);

	void ResetGraphicsPipeline(
		GraphicsPipelineID graphicsPipelineID,
		const GraphicsPipelineInfo& info);

	// --- todo: reorganize calls to navigate the arrays instead --- //

	vk::Pipeline GetPipeline(GraphicsPipelineID id) const { return m_pipelines[id].get(); }

	vk::PipelineLayout GetPipelineLayout(GraphicsPipelineID id, uint8_t set) const { return m_pipelineLayouts[set]; }

	vk::PipelineLayout GetPipelineLayout(GraphicsPipelineID id);

private:
	gsl::not_null<ShaderCache*> m_shaderCache;

	struct GraphicsPipelineShaders
	{
		ShaderInstanceID vertexShader;
		ShaderInstanceID fragmentShader;
	};

	// todo (hbedard): convert to linear arrays
	// GrapicsPipelineID -> Array Index
	std::vector<GraphicsPipelineShaders> m_shaders; // [id]
	std::vector<vk::UniquePipeline> m_pipelines; // [id]

	// Skips shader reflection if set
	SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> m_descriptorSetLayoutBindings;
	SetVector<vk::DescriptorSetLayout> m_descriptorSetLayouts; 
	SetVector<vk::PipelineLayout> m_pipelineLayouts;

	GraphicsPipelineID m_nextID = 0;
};
