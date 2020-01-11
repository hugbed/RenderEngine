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
		uint32_t set = (uint32_t)DescriptorSetIndices::Material;
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			pipeline->GetPipelineLayout(set), set,
			1, &descriptorSet.get(), 0, nullptr
		);
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
