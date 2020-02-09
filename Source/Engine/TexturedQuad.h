#pragma once

#include "Image.h"
#include "Shader.h"
#include "GraphicsPipeline.h"
#include "CommandBufferPool.h"
#include "RenderPass.h"
#include "TextureCache.h"

#include "glm_includes.h"

#include <memory>

// Utility to draw a texture on a small viewport on the screen
class TexturedQuad
{
public:
	struct Properties {
		glm::aligned_vec2 center = glm::vec2(-0.6f, -0.6f);
		glm::aligned_vec2 size = glm::vec2(0.35f, 0.35f);
	};

	TexturedQuad(
		CombinedImageSampler combinedImageSampler,
		const RenderPass& renderPass,
		vk::Extent2D swapchainExtent,
		vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
	);

	void Reset(
		CombinedImageSampler combinedImageSampler,
		const RenderPass& renderPass,
		vk::Extent2D swapchainExtent
	);

	void SetProperties(Properties properties) { m_properties = properties; }

	void Draw(vk::CommandBuffer& commandBuffer);

private:
	void CreateDescriptorPool();
	void CreateDescriptorSets();
	void UpdateDescriptorSets();

	Properties m_properties;

	vk::ImageLayout m_imageLayout;
	CombinedImageSampler m_combinedImageSampler;
	std::unique_ptr<Shader> m_vertexShader;
	std::unique_ptr<Shader> m_fragmentShader;
	std::unique_ptr<GraphicsPipeline> m_graphicsPipeline;

	vk::UniqueDescriptorPool m_descriptorPool; // todo: group descriptor pools
	vk::UniqueDescriptorSet m_descriptorSet;
};
