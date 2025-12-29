#pragma once

#include <Renderer/Bindless.h>
#include <RHI/Texture.h>
#include <RHI/ShaderSystem.h>
#include <RHI/GraphicsPipelineSystem.h>
#include <RHI/RenderPass.h>
#include <AssetPath.h>

#include <gsl/gsl>
#include <memory>
#include <vector>

class RenderState;

class Grid
{
public:
	Grid(const RenderPass& renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineSystem& graphicsPipelineSystem,
		BindlessDrawParams& bindlessDrawParams);

	void SetViewBufferHandles(gsl::span<BufferHandle> viewBufferHandles);

	void UploadToGPU(vk::CommandBuffer& commandBuffer);

	void Draw(RenderState& renderState);

	void Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent);

private:
	struct GridDrawParams
	{
		BufferHandle view;
		uint32_t padding[3];
	};
	BindlessDrawParamsHandle m_drawParamsHandle = BindlessDrawParamsHandle::Invalid;
	std::vector<BufferHandle> m_viewBufferHandles;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	GraphicsPipelineID pipelineID;
	ShaderInstanceID vertexShader;
	ShaderInstanceID fragmentShader;
};
