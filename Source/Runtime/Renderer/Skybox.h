#pragma once

#include <Renderer/TextureSystem.h>
#include <RHI/Texture.h>
#include <RHI/GraphicsPipelineSystem.h>
#include <RHI/ShaderSystem.h>

#include <vulkan/vulkan.hpp>
#include <gsl/pointers>
#include <vector>

class TextureSystem;
class CommandBufferPool;
class RenderPass;
class RenderState;

class Skybox
{
public:
	Skybox(
		const RenderPass& renderPass,
		vk::Extent2D swapchainExtent,
		TextureSystem& textureSystem,
		GraphicsPipelineSystem& graphicsPipelineSystem,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams
	);

	void SetViewBufferHandles(gsl::span<BufferHandle> viewBufferHandles);

	// todo (hbedard): actually call this
	void UploadToGPU(vk::CommandBuffer& commandBuffer, CommandBufferPool& commandBufferPool);

	void Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent);

	void Draw(RenderState& renderState);

	GraphicsPipelineID GetGraphicsPipelineID() const { return m_graphicsPipelineID; }

	vk::PipelineLayout GetGraphicsPipelineLayout(uint8_t set) const
	{
		return m_graphicsPipelineSystem->GetPipelineLayout(m_graphicsPipelineID, set);
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

	gsl::not_null<TextureSystem*> m_textureSystem;
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	ShaderInstanceID m_vertexShader;
	ShaderInstanceID m_fragmentShader;
	GraphicsPipelineID m_graphicsPipelineID;

	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer;
};
