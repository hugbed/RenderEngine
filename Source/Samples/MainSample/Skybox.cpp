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

Skybox::Skybox(const RenderPass& renderPass, vk::Extent2D swapchainExtent)
{
	// Create graphics pipeline
	vertexShader = std::make_unique<Shader>("skybox_vert.spv", "main");
	fragmentShader = std::make_unique<Shader>("skybox_frag.spv", "main");
	pipeline = std::make_unique<GraphicsPipeline>(
		renderPass.Get(),
		swapchainExtent,
		*vertexShader, *fragmentShader
	);

	CreateDescriptors();
}

void Skybox::Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent)
{
	pipeline = std::make_unique<GraphicsPipeline>(
		renderPass.Get(),
		swapchainExtent,
		*vertexShader, *fragmentShader
	);

	CreateDescriptors();
	UpdateDescriptors();
}

void Skybox::CreateDescriptors()
{
	cubeDescriptorSets.clear();
	descriptorPool.reset();

	// Descriptor pool
	uint32_t nbSamplers = 1;
	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.push_back(vk::DescriptorPoolSize(
		vk::DescriptorType::eCombinedImageSampler,
		nbSamplers
	));
	descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		nbSamplers,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));

	// Descriptor and Pipeline Layouts
	vk::DescriptorSetLayout cubeSetLayout(pipeline->GetDescriptorSetLayout(1));
	cubeDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		descriptorPool.get(), 1, &cubeSetLayout
	));
}

void Skybox::UpdateDescriptors()
{
	uint32_t binding = 0;
	vk::DescriptorImageInfo imageInfo(cubeMap.sampler, cubeMap.texture->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet(
			cubeDescriptorSets[0].get(), binding++, {},
			1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr
		) // binding = 0
	};
	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void Skybox::UploadToGPU(TextureManager* textureManager, vk::CommandBuffer& commandBuffer)
{
	std::vector<std::string> cubeFacesFiles = {
		"skybox/right.jpg",
		"skybox/left.jpg",
		"skybox/top.jpg",
		"skybox/bottom.jpg",
		"skybox/front.jpg",
		"skybox/back.jpg"
	};
	cubeMap = textureManager->LoadCubeMapFaces(commandBuffer, cubeFacesFiles);

	// Create and upload vertex buffer
	vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
	vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
	memcpy(vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(vertices.data()), bufferSize);
	vertexBuffer->CopyStagingToGPU(commandBuffer);
	textureManager->GetCommandBufferPool()->DestroyAfterSubmit(vertexBuffer->ReleaseStagingBuffer());

	UpdateDescriptors();
}

void Skybox::Draw(vk::CommandBuffer& commandBuffer, uint32_t frameIndex)
{
	// Expects the unlit view descriptors to be bound

	// Bind vertex buffer
	vk::DeviceSize offsets[] = { 0 };
	vk::Buffer vertexBuffers[] = { vertexBuffer->Get() };
	commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

	// Bind Pipeline
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Get());

	// Bind cube sampler
	commandBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics, pipeline->GetPipelineLayout(),
		1, // set
		1, &cubeDescriptorSets[0].get(), 0, nullptr
	);

	commandBuffer.draw(vertices.size(), 1, 0, 0);
}
