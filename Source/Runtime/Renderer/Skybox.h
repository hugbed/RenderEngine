#pragma once

#include <Renderer/TextureCache.h>
#include <RHI/Texture.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/ShaderCache.h>

#include <vulkan/vulkan.hpp>
#include <gsl/pointers>
#include <vector>

class TextureCache;
class CommandRingBuffer;
class RenderPass;
class RenderState;

class Skybox
{
public:
	Skybox(
		vk::RenderPass renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineCache& graphicsPipelineCache,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams,
		TextureCache& textureCache
	);

	void SetViewBufferHandles(gsl::span<const BufferHandle> viewBufferHandles);

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	void Reset(vk::RenderPass renderPass, vk::Extent2D swapchainExtent);

	void Draw(RenderState& renderState);

	GraphicsPipelineID GetGraphicsPipelineID() const { return m_graphicsPipelineID; }

	vk::PipelineLayout GetGraphicsPipelineLayout(uint8_t set) const
	{
		return m_graphicsPipelineCache->GetPipelineLayout(m_graphicsPipelineID, set);
	}

	TextureHandle GetTextureHandle() const { return m_drawParams.skyboxTexture; }

private:
	struct SkyboxDrawParams
	{
		BufferHandle view;
		TextureHandle skyboxTexture;
		uint32_t padding[2];
	} m_drawParams;
	BindlessDrawParamsHandle m_drawParamsHandle = BindlessDrawParamsHandle::Invalid;
	std::vector<BufferHandle> m_viewBufferHandles;

	gsl::not_null<TextureCache*> m_textureCache;
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;

	gsl::not_null<GraphicsPipelineCache*> m_graphicsPipelineCache;
	ShaderInstanceID m_vertexShader;
	ShaderInstanceID m_fragmentShader;
	GraphicsPipelineID m_graphicsPipelineID;

	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer;
};
