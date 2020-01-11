#pragma once

#include "TextureCache.h"
#include "DescriptorSetLayouts.h"

#include "GraphicsPipeline.h"
#include "RenderPass.h"

#include <vulkan/vulkan.hpp>

// Each shading model can have different view descriptors
enum class ShadingModel
{
	Unlit = 0,
	Lit = 1,
	Count
};

struct Material
{
	void BindDescriptors(vk::CommandBuffer& commandBuffer) const
	{
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			pipeline->GetPipelineLayout(),
			(uint32_t)DescriptorSetIndices::Material,
			1, &descriptorSet.get(), 0, nullptr
		);
	}

	const vk::DescriptorSetLayout& GetDescriptorSetLayout() const
	{
		return pipeline->GetDescriptorSetLayout((size_t)DescriptorSetIndices::Material);
	}

	void BindPipeline(vk::CommandBuffer commandBuffer) const
	{
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Get());
	}

#ifdef DEBUG_MODE
	std::string name;
#endif

	ShadingModel shadingModel;
	bool isTransparent = false;
	const GraphicsPipeline* pipeline = nullptr;

	// Per-material descriptors
	std::vector<CombinedImageSampler> textures;
	std::vector<CombinedImageSampler> cubeMaps;
	std::unique_ptr<UniqueBufferWithStaging> uniformBuffer;
	vk::UniqueDescriptorSet descriptorSet;
};
