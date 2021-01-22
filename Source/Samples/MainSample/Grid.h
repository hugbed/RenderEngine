#pragma once

#include "Texture.h"
#include "ShaderSystem.h"
#include "GraphicsPipelineSystem.h"
#include "RenderPass.h"

#include <gsl/gsl>

#include <memory>
#include <vector>

class Grid
{
public:

	Grid(const RenderPass& renderPass, vk::Extent2D swapchainExtent, GraphicsPipelineSystem& graphicsPipelineSystem)
		: m_graphicsPipelineSystem(&graphicsPipelineSystem)
	{
		ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
		ShaderID vertexShaderID = shaderSystem.CreateShader("grid_vert.spv", "main");
		ShaderID fragmentShaderID = shaderSystem.CreateShader("grid_frag.spv", "main");
		vertexShader = shaderSystem.CreateShaderInstance(vertexShaderID);
		fragmentShader = shaderSystem.CreateShaderInstance(fragmentShaderID);
		Reset(renderPass, swapchainExtent);
	}

	void Draw(vk::CommandBuffer& commandBuffer)
	{
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipelineSystem->GetPipeline(pipelineID));
		commandBuffer.draw(6, 1, 0, 0);
	}

	void Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent) 
	{
		GraphicsPipelineInfo info(renderPass.Get(), swapchainExtent);
		info.blendEnable = true;
		info.depthWriteEnable = true;
		pipelineID = m_graphicsPipelineSystem->CreateGraphicsPipeline(
			vertexShader, fragmentShader, info
		);
	}

private:
	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	GraphicsPipelineID pipelineID;
	ShaderInstanceID vertexShader;
	ShaderInstanceID fragmentShader;
};
