#pragma once

#include "Texture.h"
#include "GraphicsPipeline.h"
#include "Shader.h"
#include "RenderPass.h"
#include "CommandBufferPool.h"
#include "Device.h"

#include <stb_image.h>

#include <memory>
#include <vector>

class Grid
{
public:

	Grid(const RenderPass& renderPass, vk::Extent2D swapchainExtent)
	{
		vertexShader = std::make_unique<Shader>("grid_vert.spv", "main");
		fragmentShader = std::make_unique<Shader>("grid_frag.spv", "main");
		GraphicsPipelineInfo info;
		info.blendEnable = true;
		info.depthWriteEnable = true;
		pipeline = std::make_unique<GraphicsPipeline>(
			renderPass.Get(),
			swapchainExtent,
			*vertexShader, *fragmentShader,
			info
		);
	}

	void Draw(vk::CommandBuffer& commandBuffer)
	{
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Get());
		commandBuffer.draw(6, 1, 0, 0);
	}

	void Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent) 
	{
		GraphicsPipelineInfo info;
		info.blendEnable = true;
		pipeline = std::make_unique<GraphicsPipeline>(
			renderPass.Get(),
			swapchainExtent,
			*vertexShader, *fragmentShader,
			info
		);
	}

private:
	std::unique_ptr<GraphicsPipeline> pipeline;
	std::unique_ptr<Shader> vertexShader;
	std::unique_ptr<Shader> fragmentShader;
};
