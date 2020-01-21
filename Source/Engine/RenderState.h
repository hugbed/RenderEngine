#pragma once

#include "GraphicsPipeline.h"
#include "MaterialCache.h"
#include "Model.h"
#include "DescriptorSetLayouts.h"

#include <vulkan/vulkan.hpp>

class RenderState
{
public:
	void BindPipeline(vk::CommandBuffer& commandBuffer, const GraphicsPipeline* newPipeline)
	{
		// Bind Graphics Pipeline
		if (newPipeline != pipeline)
		{
			if (pipeline != nullptr)
			{
				for (int i = 0; i < isSetCompatible.size(); ++i)
					isSetCompatible[i] = newPipeline->IsLayoutCompatible(*pipeline, i);
			}
			pipeline = newPipeline;
			commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Get());
		}
	}

	void BindView(vk::CommandBuffer& commandBuffer, ShadingModel newShadingModel, vk::DescriptorSet viewDescriptorSet)
	{
		uint32_t set = (uint32_t)DescriptorSetIndices::View;
		if (newShadingModel != shadingModel || isSetCompatible[set] == false)
		{
			shadingModel = newShadingModel;
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipeline->GetPipelineLayout(set), set,
				1, &viewDescriptorSet, 0, nullptr
			);
			isSetCompatible[set] = true;
		}
	}

	void BindModel(vk::CommandBuffer& commandBuffer, const Model* newModel)
	{
		uint32_t set = (uint32_t)DescriptorSetIndices::Model;
		if (newModel != model || isSetCompatible[set] == false)
		{
			model = newModel;
			model->Bind(commandBuffer, *pipeline);
			isSetCompatible[set] = true;
		}
	}

	void BindMaterial(vk::CommandBuffer& commandBuffer, const Material* newMaterial)
	{
		uint32_t set = (uint32_t)DescriptorSetIndices::Material;
		if (newMaterial != material || isSetCompatible[set] == false)
		{
			material = newMaterial;
			material->BindDescriptors(commandBuffer);
			isSetCompatible[set] = true;
		}
	}

private:
	std::array<bool, (size_t)DescriptorSetIndices::Count> isSetCompatible = {}; // all false by default

	ShadingModel shadingModel = ShadingModel::Count;
	const Model* model = nullptr;
	const GraphicsPipeline* pipeline = nullptr;
	const Material* material = nullptr;
};