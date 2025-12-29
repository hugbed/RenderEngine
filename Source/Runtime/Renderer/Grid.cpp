#include <Renderer/Grid.h>

#include <Renderer/RenderState.h>

Grid::Grid(const RenderPass& renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	BindlessDrawParams& bindlessDrawParams)
	: m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_bindlessDrawParams(&bindlessDrawParams)
{
	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	ShaderID vertexShaderID = shaderSystem.CreateShader(AssetPath("/Engine/Generated/Shaders/grid_vert.spv").PathOnDisk(), "main");
	ShaderID fragmentShaderID = shaderSystem.CreateShader(AssetPath("/Engine/Generated/Shaders/grid_frag.spv").PathOnDisk(), "main");
	vertexShader = shaderSystem.CreateShaderInstance(vertexShaderID);
	fragmentShader = shaderSystem.CreateShaderInstance(fragmentShaderID);
	Reset(renderPass, swapchainExtent);

	m_drawParamsHandle = m_bindlessDrawParams->DeclareParams<GridDrawParams>();
}

void Grid::SetViewBufferHandles(gsl::span<BufferHandle> viewBufferHandles)
{
	m_viewBufferHandles.reserve(viewBufferHandles.size());
	std::copy(viewBufferHandles.begin(), viewBufferHandles.end(), std::back_inserter(m_viewBufferHandles));
}

void Grid::UploadToGPU(vk::CommandBuffer& commandBuffer)
{
	assert(!m_viewBufferHandles.empty());
	for (uint32_t i = 0; i < m_viewBufferHandles.size(); ++i)
	{
		GridDrawParams drawParams;
		drawParams.view = m_viewBufferHandles[i];
		m_bindlessDrawParams->DefineParams(m_drawParamsHandle, drawParams, i);
	}
}

void Grid::Draw(RenderState& renderState)
{
	vk::CommandBuffer commandBuffer = renderState.GetCommandBuffer();
	renderState.BindDrawParams(m_drawParamsHandle);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipelineSystem->GetPipeline(pipelineID));
	commandBuffer.draw(6, 1, 0, 0);
}

void Grid::Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent)
{
	GraphicsPipelineInfo info(renderPass.Get(), swapchainExtent);
	info.blendEnable = true;
	info.depthWriteEnable = true;
	pipelineID = m_graphicsPipelineSystem->CreateGraphicsPipeline(
		vertexShader, fragmentShader, info
	);
}
