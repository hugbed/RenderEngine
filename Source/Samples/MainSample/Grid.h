#pragma once

#include "Texture.h"
#include "Shader.h"
#include "GraphicsPipeline.h"
#include "RenderPass.h"

#include <memory>
#include <vector>

class Grid
{
public:

	Grid(const RenderPass& renderPass, vk::Extent2D swapchainExtent)
	{
		ShaderID vertexShaderID = shaderSystem.CreateShader("grid_vert.spv", "main");
		ShaderID fragmentShaderID = shaderSystem.CreateShader("grid_frag.spv", "main");
		vertexShader = shaderSystem.CreateShaderInstance(vertexShaderID);
		fragmentShader = shaderSystem.CreateShaderInstance(fragmentShaderID);

		GraphicsPipelineInfo info;
		info.blendEnable = true;
		info.depthWriteEnable = true;
		pipeline = std::make_unique<GraphicsPipeline>(
			renderPass.Get(),
			swapchainExtent,
			shaderSystem, vertexShader, fragmentShader,
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
			shaderSystem, vertexShader, fragmentShader,
			info
		);
	}

private:
	std::unique_ptr<GraphicsPipeline> pipeline;

	ShaderSystem shaderSystem; // todo: share shader system between systems
	ShaderInstanceID vertexShader;
	ShaderInstanceID fragmentShader;
};
