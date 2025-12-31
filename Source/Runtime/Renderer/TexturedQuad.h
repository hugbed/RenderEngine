#pragma once

#include <Renderer/TextureCache.h>
#include <Renderer/Bindless.h>
#include <RHI/Image.h>
#include <RHI/ShaderCache.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/CommandRingBuffer.h>
#include <RHI/RenderPass.h>
#include <glm_includes.h>
#include <gsl/pointers>

#include <memory>

class RenderCommandEncoder;

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
		GraphicsPipelineCache& graphicsPipelineCache,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams,
		vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
	);

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	void Reset(
		CombinedImageSampler combinedImageSampler,
		const RenderPass& renderPass,
		vk::Extent2D swapchainExtent
	);

	void SetProperties(Properties properties) { m_properties = properties; }

	void Draw(RenderCommandEncoder& renderCommandEncoder);

private:
	struct TexturedQuadDrawParams
	{
		TextureHandle texture;
		uint32_t padding[3];
	} m_drawParams;
	BindlessDrawParamsHandle m_drawParamsHandle = BindlessDrawParamsHandle::Invalid;
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;

	Properties m_properties;

	vk::ImageLayout m_imageLayout;
	CombinedImageSampler m_combinedImageSampler;

	ShaderInstanceID m_vertexShader;
	ShaderInstanceID m_fragmentShader;
	GraphicsPipelineID m_graphicsPipelineID;
	gsl::not_null<GraphicsPipelineCache*> m_graphicsPipelineCache; // todo: share pipeline system between systems
};
