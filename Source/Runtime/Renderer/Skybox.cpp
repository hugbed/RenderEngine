#include <Renderer/Skybox.h>

#include <Renderer/Bindless.h>
#include <Renderer/RenderState.h>
#include <RHI/Device.h>
#include <RHI/CommandBufferPool.h>
#include <RHI/RenderPass.h>
#include <AssetPath.h>

#include <iostream>

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
	const RenderPass& renderPass,
	vk::Extent2D swapchainExtent,
	TextureSystem& textureSystem,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	BindlessDescriptors& bindlessDescriptors,
	BindlessDrawParams& bindlessDrawParams
)
	: m_textureSystem(&textureSystem)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
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
	m_drawParams.skyboxTexture = m_textureSystem->LoadCubeMapFaces(cubeFacesFiles);

	// Create graphics pipeline
	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	ShaderID vertexShaderID = shaderSystem.CreateShader(AssetPath("/Engine/Generated/Shaders/skybox_vert.spv").PathOnDisk(), "main");
	ShaderID fragmentShaderID = shaderSystem.CreateShader(AssetPath("/Engine/Generated/Shaders/skybox_frag.spv").PathOnDisk(), "main");
	m_vertexShader = shaderSystem.CreateShaderInstance(vertexShaderID);
	m_fragmentShader = shaderSystem.CreateShaderInstance(fragmentShaderID);
	GraphicsPipelineInfo info(renderPass.Get(), swapchainExtent);
	m_graphicsPipelineID = m_graphicsPipelineSystem->CreateGraphicsPipeline(
		m_vertexShader, m_fragmentShader, info
	);
	m_drawParamsHandle = m_bindlessDrawParams->DeclareParams<SkyboxDrawParams>();
}

void Skybox::Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent)
{
	GraphicsPipelineInfo info(renderPass.Get(), swapchainExtent);
	m_graphicsPipelineSystem->ResetGraphicsPipeline(m_graphicsPipelineID, info);
}

void Skybox::SetViewBufferHandles(gsl::span<BufferHandle> viewBufferHandles)
{
	m_viewBufferHandles.reserve(viewBufferHandles.size());
	std::copy(viewBufferHandles.begin(), viewBufferHandles.end(), std::back_inserter(m_viewBufferHandles));
}

// todo (hbedard): no need to pass the command buffer here no?
void Skybox::UploadToGPU(vk::CommandBuffer& commandBuffer, CommandBufferPool& commandBufferPool)
{
	// Create and upload vertex buffer
	vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
	m_vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
	memcpy(m_vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(vertices.data()), bufferSize);
	m_vertexBuffer->CopyStagingToGPU(commandBuffer);
	commandBufferPool.DestroyAfterSubmit(m_vertexBuffer->ReleaseStagingBuffer());

	assert(!m_viewBufferHandles.empty());
	SkyboxDrawParams drawParams = m_drawParams;
	for (uint32_t i = 0; i < m_viewBufferHandles.size(); ++i)
	{
		drawParams.view = m_viewBufferHandles[i];
		m_bindlessDrawParams->DefineParams(m_drawParamsHandle, drawParams, i);
	}
}

void Skybox::Draw(RenderState& renderState)
{
	// Expects the unlit view descriptors to be bound
	// Assumes owner already bound the graphics pipeline

	vk::CommandBuffer commandBuffer = renderState.GetCommandBuffer();
	renderState.BindDrawParams(m_drawParamsHandle);
	renderState.BindPipeline(GetGraphicsPipelineID());

	// Bind vertex buffer
	vk::DeviceSize offsets[] = { 0 };
	vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
	commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
	commandBuffer.draw(vertices.size() / 3, 1, 0, 0);
}
