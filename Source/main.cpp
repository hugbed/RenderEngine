#include <iostream>

#include "RenderLoop.h"

#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "CommandBuffers.h"
#include "RenderPass.h"
#include "Image.h"

#include "vk_utils.h"

// For Uniform Buffer
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

// For Texture
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

const std::vector<Vertex> vertices = {
	{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
	{{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
	{{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
	{{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
};;

const std::vector<uint16_t> indices = {
	0, 1, 2, 2, 3, 0
};

class App : public RenderLoop
{
public:
	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
		: RenderLoop(surface, extent, window)
		, m_vertexBuffer(
			sizeof(Vertex) * vertices.size(),
			vk::BufferUsageFlagBits::eVertexBuffer)
		, m_indexBuffer(
			sizeof(uint16_t) * indices.size(),
			vk::BufferUsageFlagBits::eIndexBuffer)
	{
	}

	using RenderLoop::Init;

protected:
	void Init(vk::CommandBuffer& commandBuffer) override
	{
		UploadGeometry(commandBuffer);
		CreateAndUploadTextureImage(commandBuffer);
		CreateUniformBuffers();
		CreateSampler();
		CreateDescriptorSets();

		// Bind shader variables
		renderPass->BindIndexBuffer(m_indexBuffer.Get());
		renderPass->BindVertexBuffer(m_vertexBuffer.Get());
		renderPass->BindDescriptorSets(vk_utils::remove_unique(m_descriptorSets));
	}

	// Render pass commands are recorded once and executed every frame
	void RecordRenderPassCommands(CommandBuffers& commandBuffers) override
	{
		// Record commands
		for (size_t i = 0; i < swapchain->GetImageCount(); i++)
		{
			auto& commandBuffer = m_renderCommandBuffers.Get(i);
			commandBuffer.begin(vk::CommandBufferBeginInfo());
			renderPass->PopulateRenderCommands(commandBuffer, i);
			commandBuffer.end();
		}
	}

	// Called every frame to update frame resources
	void UpdateImageResources(uint32_t imageIndex) override
	{
		UpdateUniformBuffer(imageIndex);
	}

	void CreateUniformBuffers()
	{
		m_uniformBuffers.reserve(swapchain->GetImageCount());
		for (uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
		{
			m_uniformBuffers.emplace_back(
				sizeof(UniformBufferObject),
				vk::BufferUsageFlagBits::eUniformBuffer,
				vk::MemoryPropertyFlagBits::eHostCoherent |
					vk::MemoryPropertyFlagBits::eHostCoherent);
		}
	}

	void CreateDescriptorSets()
	{
		std::array<vk::DescriptorPoolSize, 2> poolSizes = {
			vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, swapchain->GetImageCount()),
			vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, swapchain->GetImageCount()),
		};
		m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			swapchain->GetImageCount(),
			static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
		));
	
		std::vector<vk::DescriptorSetLayout> layouts(swapchain->GetImageCount(), renderPass->GetDescriptorSetLayout());

		m_descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(m_descriptorPool.get(), swapchain->GetImageCount(), layouts.data()));

		for (uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
		{
			vk::DescriptorBufferInfo descriptorBufferInfo(m_uniformBuffers[i].Get(), 0, sizeof(UniformBufferObject));
			vk::DescriptorImageInfo imageInfo(m_sampler.get(), m_image->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
			std::array<vk::WriteDescriptorSet, 2> writeDescriptorSets = {
				vk::WriteDescriptorSet(m_descriptorSets[i].get(), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo), // binding = 0
				vk::WriteDescriptorSet(m_descriptorSets[i].get(), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr) // binding = 1
			};
			g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	// todo: extract reusable code from here and maybe move into a Image factory or something
	void CreateAndUploadTextureImage(vk::CommandBuffer& commandBuffer)
	{
		// Read image from file
		int texWidth = 0, texHeight = 0, texChannels = 0;
		stbi_uc* pixels = stbi_load("texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0) {
			throw std::runtime_error("failed to load texture image!");
		}

		// Texture image
		m_image = std::make_unique<Image>(
			texWidth, texHeight, 4UL, // R8G8B8A8, depth = 4
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		// Copy image data to staging buffer
		m_image->TransitionLayout(
			commandBuffer,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal
		);
		{
			m_image->Overwrite(commandBuffer, reinterpret_cast<const void*>(pixels));
		}
		m_image->TransitionLayout(
			commandBuffer,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal
		);

		stbi_image_free(pixels);
	}

	void CreateSampler()
	{
		m_sampler = g_device->Get().createSamplerUnique(vk::SamplerCreateInfo(
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
			{}, // minLod
			{}, // maxLod
			vk::BorderColor::eIntOpaqueBlack, // borderColor
			false // unnormalizedCoordinates
		));
	}

	void UploadGeometry(vk::CommandBuffer& commandBuffer)
	{
		m_vertexBuffer.Overwrite(commandBuffer, reinterpret_cast<const void*>(vertices.data()));
		m_indexBuffer.Overwrite(commandBuffer, reinterpret_cast<const void*>(indices.data()));
	}

	void UpdateUniformBuffer(uint32_t imageIndex)
	{
		using namespace std::chrono;

		static auto startTime = high_resolution_clock::now();

		auto currentTime = high_resolution_clock::now();
		float time = duration<float, seconds::period>(currentTime - startTime).count();

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.proj = glm::perspective(glm::radians(45.0f), extent.width / (float)extent.height, 0.1f, 10.0f);

		ubo.proj[1][1] *= -1; // inverse Y for OpenGL -> Vulkan (clip coordinates)

		// Upload to GPU (inefficient, use push-constants instead)
		m_uniformBuffers[imageIndex].Overwrite(reinterpret_cast<const void*>(&ubo));
	}

private:
	BufferWithStaging m_vertexBuffer;
	BufferWithStaging m_indexBuffer;
	std::vector<Buffer> m_uniformBuffers; // one per image since these change every frame

	vk::UniqueDescriptorPool m_descriptorPool;
	std::vector<vk::UniqueDescriptorSet> m_descriptorSets;

	std::unique_ptr<Image> m_image;
	vk::UniqueSampler m_sampler;
};

int main()
{
	vk::Extent2D extent(800, 600);
	Window window(extent, "Vulkan");
	Instance instance(window);
	vk::UniqueSurfaceKHR surface(window.CreateSurface(instance.Get()));

	PhysicalDevice::Init(instance.Get(), surface.get());
	Device::Init(*g_physicalDevice);
	{
		App app(surface.get(), extent, window);
		app.Init();
		app.Run();
	}
	Device::Term();
	PhysicalDevice::Term();

	return 0;
}
