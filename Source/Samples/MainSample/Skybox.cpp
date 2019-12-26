#include "Skybox.h"

#include "Device.h"
#include "CommandBufferPool.h"
#include "RenderPass.h"

#include <stb_image.h>

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
	LoadCubeMap();

	// Create graphics pipeline
	fragmentShader = std::make_unique<Shader>("skybox_vert.spv", "main");
	vertexShader = std::make_unique<Shader>("skybox_frag.spv", "main");
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
}

void Skybox::LoadCubeMap()
{
	std::vector<std::string> cubeFacesFiles = {
		"skybox/right.jpg",
		"skybox/left.jpg",
		"skybox/top.jpg",
		"skybox/bottom.jpg",
		"skybox/front.jpg",
		"skybox/back.jpg"
	};

	std::vector<stbi_uc*> faces;
	faces.reserve(cubeFacesFiles.size());

	bool success = true;
	int width = 0, height = 0, channels = 0;

	for (size_t i = 0; i < cubeFacesFiles.size(); ++i)
	{
		const auto& faceFile = cubeFacesFiles[i];
		int texWidth = 0, texHeight = 0, texChannels = 0;

		stbi_uc* pixels = stbi_load(faceFile.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0)
		{
			std::cout << "failed to load cubemape face: " << faceFile.c_str() << std::endl;
			success = false;
		}

		if (i > 0 && (texWidth != width || texHeight != height || texChannels != channels))
		{
			std::cout << "cube map textures have different formats" << std::endl;
			success = false;
		}

		faces.push_back(pixels);
		width = texWidth;
		height = texHeight;
		channels = texChannels;
	}

	// Create cubemap
	if (success)
	{
		cubeMap = std::make_unique<Texture>(
			width, height, 4UL,
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageViewType::eCube,
			1, // mipLevels
			6 // layerCount (6 for cube)
		);

		size_t bufferSize = (size_t)width * height * 4ULL;
		char* data = reinterpret_cast<char*>(cubeMap->GetStagingMappedData());
		for (auto* face : faces)
		{
			memcpy(data, reinterpret_cast<const void*>(face), bufferSize);
			stbi_image_free(face);
			data += bufferSize;
		}
	}
	else
	{
		for (auto* face : faces)
			stbi_image_free(face);
	}
}

void Skybox::CreateDescriptors()
{
	if (cubeMap == nullptr)
		return;

	cubeDescriptorSets.clear();
	descriptorPool.reset();

	// Sampler
	samplerCube = g_device->Get().createSamplerUnique(vk::SamplerCreateInfo(
		{}, // flags
		vk::Filter::eLinear, // magFilter
		vk::Filter::eLinear, // minFilter
		vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, // addressModeU
		vk::SamplerAddressMode::eRepeat, // addressModeV
		vk::SamplerAddressMode::eRepeat, // addressModeW
		{}, // mipLodBias
		true, // anisotropyEnable
		16, // maxAnisotropy
		false, // compareEnable
		vk::CompareOp::eAlways, // compareOp
		0.0f, // minLod
		static_cast<float>(1), // maxLod
		vk::BorderColor::eIntOpaqueBlack, // borderColor
		false // unnormalizedCoordinates
	));

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
	uint32_t binding = 0;
	vk::DescriptorImageInfo imageInfo(samplerCube.get(), cubeMap->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet(
			cubeDescriptorSets[0].get(), binding++, {},
			1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr
		) // binding = 0
	};
	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void Skybox::UploadToGPU(CommandBufferPool& commandBufferPool, vk::CommandBuffer& commandBuffer)
{
	if (cubeMap == nullptr)
		return;

	// Upload textures
	cubeMap->UploadStagingToGPU(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal);

	// Create and upload vertex buffer
	vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
	vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
	memcpy(vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(vertices.data()), bufferSize);
	vertexBuffer->CopyStagingToGPU(commandBuffer);

	// Don't need this anymore
	commandBufferPool.DestroyAfterSubmit(vertexBuffer->ReleaseStagingBuffer());
}

void Skybox::Draw(vk::CommandBuffer& commandBuffer, uint32_t frameIndex)
{
	if (cubeMap == nullptr)
		return;

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
