#include <Renderer/Skybox.h>

#include <Renderer/Bindless.h>
#include <Renderer/RenderCommandEncoder.h>
#include <RHI/Device.h>
#include <RHI/CommandRingBuffer.h>
#include <RHI/Swapchain.h>
#include <AssetPath.h>

#include <iostream>

// todo (hbedard): expose this as a reusable cube
const std::vector<float> vertices = {
	// positions          
	-1.0f,  1.0f, -1.0f,
	-1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,
	 1.0f,  1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,

	-1.0f, -1.0f,  1.0f,
	-1.0f, -1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,
	-1.0f,  1.0f, -1.0f,
	-1.0f,  1.0f,  1.0f,
	-1.0f, -1.0f,  1.0f,

	 1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,

	-1.0f, -1.0f,  1.0f,
	-1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f, -1.0f,  1.0f,
	-1.0f, -1.0f,  1.0f,

	-1.0f,  1.0f, -1.0f,
	 1.0f,  1.0f, -1.0f,
	 1.0f,  1.0f,  1.0f,
	 1.0f,  1.0f,  1.0f,
	-1.0f,  1.0f,  1.0f,
	-1.0f,  1.0f, -1.0f,

	-1.0f, -1.0f, -1.0f,
	-1.0f, -1.0f,  1.0f,
	 1.0f, -1.0f, -1.0f,
	 1.0f, -1.0f, -1.0f,
	-1.0f, -1.0f,  1.0f,
	 1.0f, -1.0f,  1.0f
};

Skybox::Skybox(
	const Swapchain& swapchain,
	GraphicsPipelineCache& graphicsPipelineCache,
	BindlessDescriptors& bindlessDescriptors,
	BindlessDrawParams& bindlessDrawParams,
	TextureCache& textureCache
)
	: m_textureCache(&textureCache)
	, m_graphicsPipelineCache(&graphicsPipelineCache)
	, m_bindlessDescriptors(&bindlessDescriptors)
	, m_bindlessDrawParams(&bindlessDrawParams)
{
	// Load textures
	std::vector<AssetPath> cubeFacesFiles = {
		AssetPath("/Engine/Textures/skybox/right.jpg"),
		AssetPath("/Engine/Textures/skybox/left.jpg"),
		AssetPath("/Engine/Textures/skybox/top.jpg"),
		AssetPath("/Engine/Textures/skybox/bottom.jpg"),
		AssetPath("/Engine/Textures/skybox/front.jpg"),
		AssetPath("/Engine/Textures/skybox/back.jpg")
	};
	m_drawParams.skyboxTexture = m_textureCache->LoadCubeMapFaces(cubeFacesFiles);

	// Create graphics pipeline
	ShaderCache& shaderCache = m_graphicsPipelineCache->GetShaderCache();
	ShaderID vertexShaderID = shaderCache.CreateShader(AssetPath("/Engine/Generated/Shaders/skybox_vert.spv").GetPathOnDisk(), "main");
	ShaderID fragmentShaderID = shaderCache.CreateShader(AssetPath("/Engine/Generated/Shaders/skybox_frag.spv").GetPathOnDisk(), "main");
	m_vertexShader = shaderCache.CreateShaderInstance(vertexShaderID);
	m_fragmentShader = shaderCache.CreateShaderInstance(fragmentShaderID);
	GraphicsPipelineInfo info(swapchain.GetPipelineRenderingCreateInfo(), swapchain.GetImageExtent());
	m_graphicsPipelineID = m_graphicsPipelineCache->CreateGraphicsPipeline(
		m_vertexShader, m_fragmentShader, info
	);
	m_drawParamsHandle = m_bindlessDrawParams->DeclareParams<SkyboxDrawParams>();
}

void Skybox::Reset(const Swapchain& swapchain)
{
	GraphicsPipelineInfo info(swapchain.GetPipelineRenderingCreateInfo(), swapchain.GetImageExtent());
	m_graphicsPipelineCache->ResetGraphicsPipeline(m_graphicsPipelineID, info);
}

void Skybox::SetViewBufferHandles(gsl::span<const BufferHandle> viewBufferHandles)
{
	m_viewBufferHandles.clear();
	m_viewBufferHandles.reserve(viewBufferHandles.size());
	std::copy(viewBufferHandles.begin(), viewBufferHandles.end(), std::back_inserter(m_viewBufferHandles));
}

void Skybox::UploadToGPU(CommandRingBuffer& commandRingBuffer)
{
	// Create and upload vertex buffer
	vk::CommandBuffer commandBuffer = commandRingBuffer.GetCommandBuffer();
	vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
	m_vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
	memcpy(m_vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(vertices.data()), bufferSize);
	m_vertexBuffer->CopyStagingToGPU(commandBuffer);
	commandRingBuffer.DestroyAfterSubmit(m_vertexBuffer->ReleaseStagingBuffer());

	assert(!m_viewBufferHandles.empty());
	SkyboxDrawParams drawParams = m_drawParams;
	for (uint32_t i = 0; i < m_viewBufferHandles.size(); ++i)
	{
		drawParams.view = m_viewBufferHandles[i];
		m_bindlessDrawParams->DefineParams(m_drawParamsHandle, drawParams, i);
	}
}

void Skybox::Render(RenderCommandEncoder& renderCommandEncoder)
{
	// Expects the unlit view descriptors to be bound
	// Assumes owner already bound the graphics pipeline

	vk::CommandBuffer commandBuffer = renderCommandEncoder.GetCommandBuffer();
	renderCommandEncoder.BindDrawParams(m_drawParamsHandle);
	renderCommandEncoder.BindPipeline(m_graphicsPipelineID);

	// Bind vertex buffer
	vk::DeviceSize offsets[] = { 0 };
	vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
	commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
	commandBuffer.draw(vertices.size() / 3, 1, 0, 0);
}

vk::Buffer Skybox::GetVertexBuffer() const
{
	assert(m_vertexBuffer != nullptr);
	return m_vertexBuffer->Get();
}

uint32_t Skybox::GetVertexCount() const
{
	return vertices.size() / 3U;
}
