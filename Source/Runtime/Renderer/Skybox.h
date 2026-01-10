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
class RenderCommandEncoder;
class Swapchain;

class Skybox
{
public:
	Skybox(
		const Swapchain& swapchain,
		GraphicsPipelineCache& graphicsPipelineCache,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams,
		TextureCache& textureCache
	);

	void SetViewBufferHandles(gsl::span<const BufferHandle> viewBufferHandles);

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	void Reset(const Swapchain& swapchain);

	void Render(RenderCommandEncoder& renderCommandEncoder);

	TextureHandle GetTextureHandle() const { return m_drawParams.skyboxTexture; }

	vk::Buffer GetVertexBuffer() const;
	uint32_t GetVertexCount() const;

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
