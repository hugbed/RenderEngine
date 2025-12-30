#pragma once

#include <Renderer/SurfaceLitMaterialSystem.h>
#include <Renderer/MeshAllocator.h>
#include <RHI/GraphicsPipelineCache.h>
#include <Renderer/Bindless.h>

#include <vulkan/vulkan.hpp>
#include <gsl/pointers>

// todo (hbedard): that's only specific to the surface lit material
// technically this could be a single draw call
// that's a bad name, maybe call it drawInterface?
class RenderState // todo (hbedard): rename render context
{
public:
	RenderState(
		GraphicsPipelineCache& graphicsPipelineCache,
		SurfaceLitMaterialSystem& materialSystem,
		const BindlessDrawParams& bindlessDrawParams
	)
		: m_graphicsPipelineCache(&graphicsPipelineCache)
		, m_bindlessDrawParams(&bindlessDrawParams)
	{}

	vk::CommandBuffer GetCommandBuffer() const
	{
		assert(m_commandBuffer != nullptr);
		return *m_commandBuffer;
	}

	void BeginRender(vk::CommandBuffer& commandBuffer, uint32_t frameIndex)
	{
		m_commandBuffer = &commandBuffer;
		m_frameIndex = frameIndex;
	}

	void EndRender()
	{
		m_commandBuffer = nullptr;
	}

	void BindBindlessDescriptorSet(vk::PipelineLayout pipelineLayout, vk::DescriptorSet descriptorSet)
	{
		const uint32_t set = static_cast<uint32_t>(BindlessDescriptorSet::eBindlessDescriptors);
		m_commandBuffer->bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			pipelineLayout,
			set,
			1, &descriptorSet,
			0, nullptr);

		// Bind default push constants
		uint64_t zero = 0;
		m_commandBuffer->pushConstants(
			pipelineLayout,
			vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
			0, sizeof(uint64_t), &zero
		);
	}

	void BindDrawParams(BindlessDrawParamsHandle handle)
	{
		vk::PipelineLayout pipelineLayout = m_bindlessDrawParams->GetPipelineLayout();
		vk::DescriptorSet descriptorSet = m_bindlessDrawParams->GetDescriptorSet(m_frameIndex);

		const uint32_t set = static_cast<uint32_t>(BindlessDescriptorSet::eDrawParams);
		const uint32_t offset = static_cast<uint32_t>(handle);
		m_commandBuffer->bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			pipelineLayout,
			set,
			1, &descriptorSet,
			1, &offset);
	}

	void BindPipeline(GraphicsPipelineID newPipelineID)
	{
		if (newPipelineID != m_pipelineID)
		{
			m_commandBuffer->bindPipeline(
				vk::PipelineBindPoint::eGraphics,
				m_graphicsPipelineCache->GetPipeline(newPipelineID)
			);
			m_pipelineID = newPipelineID;
		}
	}

	void BindSceneNode(SceneNodeHandle newSceneNodeID)
	{
		if (newSceneNodeID != m_sceneNodeID)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineCache->GetPipelineLayout(m_pipelineID, 0);

			uint32_t sceneNodeIndex = static_cast<uint32_t>(newSceneNodeID);

			// Set scene node index push constant
			m_commandBuffer->pushConstants(
				pipelineLayout,
				vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				0, sizeof(uint32_t), &sceneNodeIndex
			);

			m_sceneNodeID = newSceneNodeID;
		}
	}

	void BindMaterial(MaterialHandle newMaterial)
	{
		if (newMaterial != m_material)
		{
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineCache->GetPipelineLayout(m_pipelineID, 0); // todo (hbedard): is that right?

			uint32_t materialIndex = newMaterial.GetIndex();

			// Set material index push constant
			m_commandBuffer->pushConstants(
				pipelineLayout,
				vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				4, sizeof(uint32_t), &materialIndex
			);

			m_material = newMaterial;
		}
	}

private:
	gsl::not_null<GraphicsPipelineCache*> m_graphicsPipelineCache;
	gsl::not_null<const BindlessDrawParams*> m_bindlessDrawParams;

	uint32_t m_frameIndex = 0;
	vk::CommandBuffer* m_commandBuffer = nullptr;
	SceneNodeHandle m_sceneNodeID = SceneNodeHandle::Invalid;
	GraphicsPipelineID m_pipelineID = ~0U;
	MaterialHandle m_material = MaterialHandle::Invalid();
};
