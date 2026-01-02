#include <Renderer/Grid.h>

#include <Renderer/RenderCommandEncoder.h>
#include <RHI/CommandRingBuffer.h>

Grid::Grid(vk::RenderPass renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineCache& graphicsPipelineCache,
	BindlessDrawParams& bindlessDrawParams)
	: m_graphicsPipelineCache(&graphicsPipelineCache)
	, m_bindlessDrawParams(&bindlessDrawParams)
{
	ShaderCache& shaderCache = m_graphicsPipelineCache->GetShaderCache();
	ShaderID vertexShaderID = shaderCache.CreateShader(AssetPath("/Engine/Generated/Shaders/grid_vert.spv").PathOnDisk(), "main");
	ShaderID fragmentShaderID = shaderCache.CreateShader(AssetPath("/Engine/Generated/Shaders/grid_frag.spv").PathOnDisk(), "main");
	vertexShader = shaderCache.CreateShaderInstance(vertexShaderID);
	fragmentShader = shaderCache.CreateShaderInstance(fragmentShaderID);
	Reset(renderPass, swapchainExtent);

	m_drawParamsHandle = m_bindlessDrawParams->DeclareParams<GridDrawParams>();
}

void Grid::SetViewBufferHandles(gsl::span<const BufferHandle> viewBufferHandles)
{
	m_viewBufferHandles.clear();
	m_viewBufferHandles.reserve(viewBufferHandles.size());
	std::copy(viewBufferHandles.begin(), viewBufferHandles.end(), std::back_inserter(m_viewBufferHandles));
}

void Grid::UploadToGPU(CommandRingBuffer& commandRingBuffer)
{
	assert(!m_viewBufferHandles.empty());
	for (uint32_t i = 0; i < m_viewBufferHandles.size(); ++i)
	{
		GridDrawParams drawParams;
		drawParams.view = m_viewBufferHandles[i];
		m_bindlessDrawParams->DefineParams(m_drawParamsHandle, drawParams, i);
	}
}

void Grid::Draw(RenderCommandEncoder& renderCommandEncoder)
{
	vk::CommandBuffer commandBuffer = renderCommandEncoder.GetCommandBuffer();
	renderCommandEncoder.BindDrawParams(m_drawParamsHandle);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipelineCache->GetPipeline(pipelineID));
	commandBuffer.draw(6, 1, 0, 0);
}

void Grid::Reset(vk::RenderPass renderPass, vk::Extent2D swapchainExtent)
{
	GraphicsPipelineInfo info(renderPass, swapchainExtent);
	info.blendEnable = true;
	info.depthWriteEnable = true;
	pipelineID = m_graphicsPipelineCache->CreateGraphicsPipeline(
		vertexShader, fragmentShader, info
	);
}
