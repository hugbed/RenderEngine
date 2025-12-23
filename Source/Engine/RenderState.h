#pragma once

#include "MaterialSystem.h"
#include "MeshAllocator.h"
#include "GraphicsPipelineSystem.h"
#include "DescriptorSetLayouts.h"

#include <vulkan/vulkan.hpp>

#include <gsl/pointers>

class RenderState
{
public:
	RenderState(
		GraphicsPipelineSystem& graphicsPipelineSystem,
		MaterialSystem& materialSystem
	)
		: m_graphicsPipelineSystem(&graphicsPipelineSystem)
		, m_materialSystem(&materialSystem)
	{}

	void BindPipeline(vk::CommandBuffer& commandBuffer, GraphicsPipelineID newPipelineID)
	{
		// Bind Graphics Pipeline
		if (newPipelineID != m_pipelineID)
		{
			if (m_pipelineID != ~0U)
			{
				for (int i = 0; i < m_isSetCompatible.size(); ++i)
				{
					m_isSetCompatible[i] = m_graphicsPipelineSystem->IsSetLayoutCompatible(m_pipelineID, newPipelineID, i);
				}
			}
			commandBuffer.bindPipeline(
				vk::PipelineBindPoint::eGraphics,
				m_graphicsPipelineSystem->GetPipeline(newPipelineID)
			);
			m_pipelineID = newPipelineID;
		}
	}

	void BindView(vk::CommandBuffer& commandBuffer, Material::ShadingModel newShadingModel, vk::DescriptorSet viewDescriptorSet)
	{
		uint8_t set = (uint16_t)DescriptorSetIndex::View;
		if (newShadingModel != m_shadingModel || m_isSetCompatible[set] == false)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(m_pipelineID, set);
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, set,
				1, &viewDescriptorSet,
				0, nullptr
			);
			m_shadingModel = newShadingModel;
			m_isSetCompatible[set] = true;
		}
	}

	void BindSceneNode(vk::CommandBuffer& commandBuffer, SceneNodeID newSceneNodeID)
	{
		uint8_t set = (uint8_t)DescriptorSetIndex::Scene;

		if (m_isSetCompatible[set] == false)
		{
			// Bind the model transforms buffer
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(m_pipelineID, (uint8_t)set);
			vk::DescriptorSet descriptorSet = m_materialSystem->GetDescriptorSet(DescriptorSetIndex::Scene);

			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, (uint32_t)DescriptorSetIndex::Scene,
				1, &descriptorSet,
				0, nullptr
			);

			m_isSetCompatible[set] = true;
		}

		if (newSceneNodeID != m_sceneNodeID)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(m_pipelineID, (uint8_t)set);

			uint32_t sceneNodeIndex = static_cast<uint32_t>(newSceneNodeID);

			// Set scene node index push constant
			commandBuffer.pushConstants(
				pipelineLayout,
				vk::ShaderStageFlagBits::eVertex,
				0, sizeof(uint32_t), &sceneNodeIndex
			);

			m_sceneNodeID = newSceneNodeID;
		}
	}

	void BindMaterial(vk::CommandBuffer& commandBuffer, MaterialInstanceID newMaterial)
	{
		uint8_t set = (uint16_t)DescriptorSetIndex::Material;

		if (m_isSetCompatible[set] == false)
		{
			vk::DescriptorSet descriptorSet = m_materialSystem->GetDescriptorSet(DescriptorSetIndex::Material);
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(m_pipelineID, set);
			
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				pipelineLayout, set,
				1, &descriptorSet, 0, nullptr
			);

			m_isSetCompatible[set] = true;
		}

		if (newMaterial != m_material)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(m_pipelineID, (uint8_t)set);

			uint32_t materialIndex = newMaterial;

			// Set material index push constant
			commandBuffer.pushConstants(
				pipelineLayout,
				vk::ShaderStageFlagBits::eFragment,
				4, sizeof(uint32_t), &materialIndex
			);

			m_material = newMaterial;
		}
	}

private:
	std::array<bool, (size_t)DescriptorSetIndex::Count> m_isSetCompatible = {}; // all false by default

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	gsl::not_null<MaterialSystem*> m_materialSystem;

	Material::ShadingModel m_shadingModel = Material::ShadingModel::Count;
	SceneNodeID m_sceneNodeID = SceneNodeID::Invalid;
	GraphicsPipelineID m_pipelineID = ~0U;
	MaterialInstanceID m_material = ~0U;
};
