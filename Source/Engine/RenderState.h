#pragma once

#include "GraphicsPipeline.h"
#include "Material.h"
#include "Model.h"
#include "DescriptorSetLayouts.h"

#include <vulkan/vulkan.hpp>

#include <gsl/pointers>

class RenderState
{
public:
	RenderState(MaterialSystem& materialSystem)
		: m_materialSystem(&materialSystem)
	{}

	void BindPipeline(vk::CommandBuffer& commandBuffer, GraphicsPipelineID newPipelineID)
	{
		// Bind Graphics Pipeline
		if (newPipelineID != pipelineID)
		{
			auto graphicsPipelineSystem = m_materialSystem->GetGraphicsPipelineSystem();
			if (pipelineID != ~0U)
			{
				for (int i = 0; i < isSetCompatible.size(); ++i)
				{
					isSetCompatible[i] = graphicsPipelineSystem.IsSetLayoutCompatible(pipelineID, newPipelineID, i);
				}
			}
			commandBuffer.bindPipeline(
				vk::PipelineBindPoint::eGraphics,
				graphicsPipelineSystem.GetPipeline(newPipelineID)
			);
			pipelineID = newPipelineID;
		}
	}

	void BindView(vk::CommandBuffer& commandBuffer, ShadingModel newShadingModel, vk::DescriptorSet viewDescriptorSet)
	{
		uint8_t set = (uint16_t)DescriptorSetIndices::View;
		if (newShadingModel != shadingModel || isSetCompatible[set] == false)
		{
			auto graphicsPipelineSystem = m_materialSystem->GetGraphicsPipelineSystem();
			vk::PipelineLayout pipelineLayout = graphicsPipelineSystem.GetPipelineLayout(pipelineID, set);
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
			auto graphicsPipelineSystem = m_materialSystem->GetGraphicsPipelineSystem();
			vk::PipelineLayout pipelineLayout = graphicsPipelineSystem.GetPipelineLayout(pipelineID, set);
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

	void BindMaterial(vk::CommandBuffer& commandBuffer, MaterialInstanceID newMaterial)
	{
		uint8_t set = (uint16_t)DescriptorSetIndices::Material;
		if (newMaterial != material || isSetCompatible[set] == false)
		{
			auto graphicsPipelineSystem = m_materialSystem->GetGraphicsPipelineSystem();
			vk::DescriptorSet descriptorSet = m_materialSystem->GetDescriptorSet(newMaterial);
			vk::PipelineLayout pipelineLayout = graphicsPipelineSystem.GetPipelineLayout(pipelineID, set);
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, set,
				1, &descriptorSet, 0, nullptr
			);
			material = newMaterial;
			isSetCompatible[set] = true;
		}
	}

private:
	std::array<bool, (size_t)DescriptorSetIndices::Count> isSetCompatible = {}; // all false by default

	gsl::not_null<MaterialSystem*> m_materialSystem;

	ShadingModel shadingModel = ShadingModel::Count;
	const Model* model = nullptr;
	GraphicsPipelineID pipelineID = ~0U;
	MaterialInstanceID material = ~0;
};
