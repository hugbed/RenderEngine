#include "Skybox.h"

#include "Device.h"
#include "CommandBufferPool.h"
#include "RenderPass.h"

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
	TextureCache& textureCache,
	GraphicsPipelineSystem& graphicsPipelineSystem
)
	: m_textureCache(&textureCache)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
{
	// Load textures
	std::vector<std::string> cubeFacesFiles = {
		"skybox/right.jpg",
		"skybox/left.jpg",
		"skybox/top.jpg",
		"skybox/bottom.jpg",
		"skybox/front.jpg",
		"skybox/back.jpg"
	};
	m_cubeMap = m_textureCache->LoadCubeMapFaces(cubeFacesFiles);

	// Create graphics pipeline
	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	ShaderID vertexShaderID = shaderSystem.CreateShader("skybox_vert.spv", "main");
	ShaderID fragmentShaderID = shaderSystem.CreateShader("skybox_frag.spv", "main");
	m_vertexShader = shaderSystem.CreateShaderInstance(vertexShaderID);
	m_fragmentShader = shaderSystem.CreateShaderInstance(fragmentShaderID);

	GraphicsPipelineInfo info(renderPass.Get(), swapchainExtent);
	m_graphicsPipelineID = m_graphicsPipelineSystem->CreateGraphicsPipeline(
		m_vertexShader, m_fragmentShader, info
	);

	CreateDescriptors();
}

void Skybox::Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent)
{
	GraphicsPipelineInfo info(renderPass.Get(), swapchainExtent);
	m_graphicsPipelineSystem->ResetGraphicsPipeline(m_graphicsPipelineID, info);
	CreateDescriptors();
	UpdateDescriptors();
}

void Skybox::CreateDescriptors()
{
	m_cubeDescriptorSets.clear();
	m_descriptorPool.reset();

	// Descriptor pool
	uint32_t nbSamplers = 1;
	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.push_back(vk::DescriptorPoolSize(
		vk::DescriptorType::eCombinedImageSampler,
		nbSamplers
	));
	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		nbSamplers,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));

	// Descriptor and Pipeline Layouts
	vk::DescriptorSetLayout cubeSetLayout(m_graphicsPipelineSystem->GetDescriptorSetLayout(m_graphicsPipelineID, 1));
	m_cubeDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		m_descriptorPool.get(), 1, &cubeSetLayout
	));
}

void Skybox::UpdateDescriptors()
{
	uint32_t binding = 0;
	vk::DescriptorImageInfo imageInfo(m_cubeMap.sampler, m_cubeMap.texture->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet(
			m_cubeDescriptorSets[0].get(), binding++, {},
			1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr
		) // binding = 0
	};
	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void Skybox::UploadToGPU(vk::CommandBuffer& commandBuffer, CommandBufferPool& commandBufferPool)
{
	// Create and upload vertex buffer
	vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
	m_vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
	memcpy(m_vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(vertices.data()), bufferSize);
	m_vertexBuffer->CopyStagingToGPU(commandBuffer);
	commandBufferPool.DestroyAfterSubmit(m_vertexBuffer->ReleaseStagingBuffer());

	m_textureCache->UploadTextures(commandBuffer, commandBufferPool);

	UpdateDescriptors();
}

void Skybox::Draw(vk::CommandBuffer& commandBuffer, uint32_t frameIndex)
{
	// Expects the unlit view descriptors to be bound
	// Assumes owner already bound the graphics pipeline

	// Bind vertex buffer
	vk::DeviceSize offsets[] = { 0 };
	vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
	commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

	// Bind cube sampler
	uint32_t set = 1;
	commandBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		m_graphicsPipelineSystem->GetPipelineLayout(m_graphicsPipelineID, set), set,
		1, &m_cubeDescriptorSets[0].get(), 0, nullptr
	);

	commandBuffer.draw(vertices.size() / 3, 1, 0, 0);
}
