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
	RenderState(
		GraphicsPipelineSystem& graphicsPipelineSystem,
		MaterialSystem& materialSystem,
		ModelSystem& modelSystem
	)
		: m_graphicsPipelineSystem(&graphicsPipelineSystem)
		, m_materialSystem(&materialSystem)
		, m_modelSystem(&modelSystem)
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

	void BindView(vk::CommandBuffer& commandBuffer, Material::ShadingModel newShadingModel, vk::DescriptorSet viewDescriptorSet)
	{
		uint8_t set = (uint16_t)DescriptorSetIndex::View;
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

	void BindModel(vk::CommandBuffer& commandBuffer, ModelID newModel)
	{
		uint8_t set = (uint8_t)DescriptorSetIndex::Model;

		if (isSetCompatible[set] == false)
		{
			// Bind the model transforms buffer
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(pipelineID, (uint8_t)set);
			vk::DescriptorSet descriptorSet = m_materialSystem->GetDescriptorSet(DescriptorSetIndex::Model);

			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, (uint32_t)DescriptorSetIndex::Model,
				1, &descriptorSet,
				0, nullptr
			);

			isSetCompatible[set] = true;
		}

		if (newModel != model)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(pipelineID, (uint8_t)set);

			uint32_t modelIndex = newModel;

			// Set model index push constant
			commandBuffer.pushConstants(
				pipelineLayout,
				vk::ShaderStageFlagBits::eVertex,
				0, sizeof(uint32_t), &modelIndex
			);

			model = newModel;
		}
	}

	void BindMaterial(vk::CommandBuffer& commandBuffer, MaterialInstanceID newMaterial)
	{
		uint8_t set = (uint16_t)DescriptorSetIndex::Material;

		if (isSetCompatible[set] == false)
		{
			vk::DescriptorSet descriptorSet = m_materialSystem->GetDescriptorSet(DescriptorSetIndex::Material);
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(pipelineID, set);
			
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, set,
				1, &descriptorSet, 0, nullptr
			);

			isSetCompatible[set] = true;
		}

		if (newMaterial != material)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(pipelineID, (uint8_t)set);

			uint32_t materialIndex = newMaterial;

			// Set material index push constant
			commandBuffer.pushConstants(
				pipelineLayout,
				vk::ShaderStageFlagBits::eFragment,
				4, sizeof(uint32_t), &materialIndex
			);

			material = newMaterial;
		}
	}

private:
	std::array<bool, (size_t)DescriptorSetIndex::Count> isSetCompatible = {}; // all false by default

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	gsl::not_null<MaterialSystem*> m_materialSystem;
	gsl::not_null<ModelSystem*> m_modelSystem;

	Material::ShadingModel shadingModel = Material::ShadingModel::Count;
	ModelID model = ~0U;
	GraphicsPipelineID pipelineID = ~0U;
	MaterialInstanceID material = ~0U;
};
