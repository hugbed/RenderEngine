#pragma once

#include <Renderer/Bindless.h>
#include <RHI/Texture.h>
#include <RHI/ShaderCache.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/RenderPass.h>
#include <AssetPath.h>

#include <gsl/gsl>
#include <memory>
#include <vector>

class CommandRingBuffer;
class RenderState;

class Grid
{
public:
	Grid(vk::RenderPass renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineCache& graphicsPipelineCache,
		BindlessDrawParams& bindlessDrawParams);

	void SetViewBufferHandles(gsl::span<const BufferHandle> viewBufferHandles);

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	void Draw(RenderState& renderState);

	void Reset(vk::RenderPass renderPass, vk::Extent2D swapchainExtent);

private:
	struct GridDrawParams
	{
		BufferHandle view;
		uint32_t padding[3];
	};
	BindlessDrawParamsHandle m_drawParamsHandle = BindlessDrawParamsHandle::Invalid;
	std::vector<BufferHandle> m_viewBufferHandles;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;

	gsl::not_null<GraphicsPipelineCache*> m_graphicsPipelineCache;
	GraphicsPipelineID pipelineID;
	ShaderInstanceID vertexShader;
	ShaderInstanceID fragmentShader;
};
