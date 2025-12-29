#include <Renderer/TexturedQuad.h>

#include <AssetPath.h>
#include <Renderer/RenderState.h>

TexturedQuad::TexturedQuad(
	CombinedImageSampler combinedImageSampler,
	const RenderPass& renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	BindlessDescriptors& bindlessDescriptors,
	BindlessDrawParams& bindlessDrawParams,
	vk::ImageLayout imageLayout
)
	: m_combinedImageSampler(combinedImageSampler)
	, m_imageLayout(imageLayout)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_bindlessDescriptors(&bindlessDescriptors)
	, m_bindlessDrawParams(&bindlessDrawParams)
{
	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	ShaderID vertexShaderID = shaderSystem.CreateShader(AssetPath("/Engine/Generated/Shaders/textured_quad_vert.spv").PathOnDisk(), "main");
	ShaderID fragmentShaderID = shaderSystem.CreateShader(AssetPath("/Engine/Generated/Shaders/textured_quad_frag.spv").PathOnDisk(), "main");

	m_vertexShader = shaderSystem.CreateShaderInstance(vertexShaderID);

	if (imageLayout == vk::ImageLayout::eDepthStencilReadOnlyOptimal)
	{
		uint32_t isGrayscale = 1;
		SmallVector<vk::SpecializationMapEntry> entries = { 
			vk::SpecializationMapEntry(0, 0, sizeof(uint32_t)) // constant_id, offset, size
		};
		m_fragmentShader = shaderSystem.CreateShaderInstance(fragmentShaderID, (const void*)&isGrayscale, entries);
	}
	else
	{
		m_fragmentShader = shaderSystem.CreateShaderInstance(fragmentShaderID);
	}

	Reset(combinedImageSampler, renderPass, swapchainExtent);

	m_drawParamsHandle = m_bindlessDrawParams->DeclareParams<TexturedQuadDrawParams>();
	m_drawParams.texture = m_bindlessDescriptors->StoreTexture(combinedImageSampler.texture->GetImageView(), combinedImageSampler.sampler);
}

void TexturedQuad::UploadToGPU(CommandBufferPool& commandBufferPool)
{
	m_bindlessDrawParams->DefineParams(m_drawParamsHandle, m_drawParams);
}

void TexturedQuad::Reset(CombinedImageSampler combinedImageSampler, const RenderPass& renderPass, vk::Extent2D swapchainExtent)
{
	m_combinedImageSampler = combinedImageSampler;

	GraphicsPipelineInfo info(renderPass.Get(), swapchainExtent);
	info.primitiveTopology = vk::PrimitiveTopology::eTriangleStrip;
	m_graphicsPipelineID = m_graphicsPipelineSystem->CreateGraphicsPipeline(
		m_vertexShader,
		m_fragmentShader,
		info
	);
}

void TexturedQuad::Draw(RenderState& renderState)
{
	vk::CommandBuffer commandBuffer = renderState.GetCommandBuffer();
	renderState.BindDrawParams(m_drawParamsHandle);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipelineSystem->GetPipeline(m_graphicsPipelineID));
	commandBuffer.draw(4, 1, 0, 0);
}
