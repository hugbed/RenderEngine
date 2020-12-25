#pragma once

#include "GraphicsPipeline.h"
#include "MaterialCache.h"
#include "Model.h"
#include "DescriptorSetLayouts.h"

#include <vulkan/vulkan.hpp>

#include <gsl/pointers>

class RenderState
{
public:
	RenderState(GraphicsPipelineSystem& pipelineSystem)
		: m_graphicsPipelineSystem(&pipelineSystem)
	{}

	void BindPipeline(vk::CommandBuffer& commandBuffer, GraphicsPipelineID newPipelineID)
	{
		// Bind Graphics Pipeline
		if (newPipelineID != pipelineID)
		{
			if (pipelineID != ~0U)
			{
				for (int i = 0; i < isSetCompatible.size(); ++i)
				{
					isSetCompatible[i] = m_graphicsPipelineSystem->IsSetLayoutCompatible(pipelineID, newPipelineID, i);
				}
			}
			commandBuffer.bindPipeline(
				vk::PipelineBindPoint::eGraphics,
				m_graphicsPipelineSystem->GetPipeline(newPipelineID)
			);
			pipelineID = newPipelineID;
		}
	}

	void BindView(vk::CommandBuffer& commandBuffer, ShadingModel newShadingModel, vk::DescriptorSet viewDescriptorSet)
	{
		uint8_t set = (uint16_t)DescriptorSetIndices::View;
		if (newShadingModel != shadingModel || isSetCompatible[set] == false)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(pipelineID, set);
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, set,
				1, &viewDescriptorSet,
				0, nullptr
			);
			shadingModel = newShadingModel;
			isSetCompatible[set] = true;
		}
	}

	void BindModel(vk::CommandBuffer& commandBuffer, const Model* newModel)
	{
		uint8_t set = (uint16_t)DescriptorSetIndices::Model;
		if (newModel != model || isSetCompatible[set] == false)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(pipelineID, set);
			vk::DescriptorSet descriptorSet = newModel->descriptorSet.get();
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, set,
				1, &descriptorSet,
				0, nullptr
			);
			model = newModel;
			isSetCompatible[set] = true;
		}
	}

	void BindMaterial(vk::CommandBuffer& commandBuffer, const Material* newMaterial)
	{
		uint8_t set = (uint16_t)DescriptorSetIndices::Material;
		if (newMaterial != material || isSetCompatible[set] == false)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(pipelineID, set);
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, set,
				1, &newMaterial->descriptorSet.get(), 0, nullptr
			);
			material = newMaterial;
			isSetCompatible[set] = true;
		}
	}

private:
	std::array<bool, (size_t)DescriptorSetIndices::Count> isSetCompatible = {}; // all false by default

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;

	ShadingModel shadingModel = ShadingModel::Count;
	const Model* model = nullptr;
	GraphicsPipelineID pipelineID = ~0U;
	const Material* material = nullptr;
};