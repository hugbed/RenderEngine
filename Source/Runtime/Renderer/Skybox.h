#pragma once

#include "Renderer/TextureSystem.h"
#include "RHI/Texture.h"
#include "RHI/GraphicsPipelineSystem.h"
#include "RHI/ShaderSystem.h"

#include <vulkan/vulkan.hpp>
#include <gsl/pointers>
#include <vector>

class TextureSystem;
class CommandBufferPool;
class RenderPass;

class Skybox
{
public:
	Skybox(
		const RenderPass& renderPass,
		vk::Extent2D swapchainExtent,
		TextureSystem& textureSystem,
		GraphicsPipelineSystem& graphicsPipelineSystem
	);

	void UploadToGPU(vk::CommandBuffer& commandBuffer, CommandBufferPool& commandBufferPool);

	void Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent);

	void Draw(vk::CommandBuffer& commandBuffer, uint32_t frameIndex);

	TextureID GetCubeMap() const { return m_cubeMap; }

	GraphicsPipelineID GetGraphicsPipelineID() const { return m_graphicsPipelineID; }

	vk::PipelineLayout GetGraphicsPipelineLayout(uint8_t set) const
	{
		return m_graphicsPipelineSystem->GetPipelineLayout(m_graphicsPipelineID, set);
	}

	vk::DescriptorSetLayout GetDescriptorSetLayout(uint8_t set) const
	{
		return m_graphicsPipelineSystem->GetDescriptorSetLayout(m_graphicsPipelineID, set);
	}

private:
	void CreateDescriptors();
	void UpdateDescriptors();

	gsl::not_null<TextureSystem*> m_textureSystem;
	TextureID m_cubeMap = ~0;

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	ShaderInstanceID m_vertexShader;
	ShaderInstanceID m_fragmentShader;
	GraphicsPipelineID m_graphicsPipelineID;

	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer;
	vk::UniqueDescriptorPool m_descriptorPool; // consider merging with global pool
	
	std::vector<vk::UniqueDescriptorSet> m_cubeDescriptorSets;
};
