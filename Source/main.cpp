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
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

// For Texture
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Model
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <unordered_map>

class App : public RenderLoop
{
public:
	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
		: RenderLoop(surface, extent, window)
		, m_renderPass(std::make_unique<RenderPass>(*swapchain))
	{
	}

	using RenderLoop::Init;

protected:
	const std::string kModelPath = "chalet.obj";
	const std::string kTexturePath = "chalet.jpg";

	void Init(vk::CommandBuffer& commandBuffer) override
	{
		LoadModel();
		UploadAndBindGeometry(commandBuffer);
		CreateAndUploadTextureImage(commandBuffer);
		CreateUniformBuffers();
		CreateSampler();
		CreateDescriptorSets();

		// Bind other shader variables
		m_renderPass->BindIndexBuffer(m_indexBuffer->Get(), m_indices.size());
		m_renderPass->BindVertexBuffer(m_vertexBuffer->Get());

		RecordRenderPassCommands(m_renderCommandBuffers);
	}

	// Render pass commands are recorded once and executed every frame
	void OnSwapchainRecreated(CommandBuffers& commandBuffers) override
	{
		// Reset render pass that depends on the swapchain
		m_renderPass.reset();
		m_renderPass = std::make_unique<RenderPass>(*swapchain);

		// Recreate everything that depends on the number of images
		CreateUniformBuffers();
		CreateDescriptorSets();

		RecordRenderPassCommands(commandBuffers);
	}

	void RecordRenderPassCommands(CommandBuffers& commandBuffers)
	{
		for (size_t i = 0; i < swapchain->GetImageCount(); i++)
		{
			auto& commandBuffer = commandBuffers.Get(i);
			commandBuffer.begin(vk::CommandBufferBeginInfo());
			m_renderPass->PopulateRenderCommands(commandBuffer, i);
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
		m_uniformBuffers.clear();
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
		m_descriptorSets.clear();
		m_descriptorPool.reset();

		std::array<vk::DescriptorPoolSize, 2> poolSizes = {
			vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, swapchain->GetImageCount()),
			vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, swapchain->GetImageCount()),
		};
		m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			swapchain->GetImageCount(),
			static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
		));
	
		std::vector<vk::DescriptorSetLayout> layouts(swapchain->GetImageCount(), m_renderPass->GetDescriptorSetLayout());
		m_descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
			m_descriptorPool.get(), swapchain->GetImageCount(), layouts.data())
		);
		for (uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
		{
			vk::DescriptorBufferInfo descriptorBufferInfo(m_uniformBuffers[i].Get(), 0, sizeof(UniformBufferObject));
			vk::DescriptorImageInfo imageInfo(m_sampler.get(), m_textureImage->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
			std::array<vk::WriteDescriptorSet, 2> writeDescriptorSets = {
				vk::WriteDescriptorSet(m_descriptorSets[i].get(), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo), // binding = 0
				vk::WriteDescriptorSet(m_descriptorSets[i].get(), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr) // binding = 1
			};
			g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}

		m_renderPass->BindDescriptorSets(vk_utils::remove_unique(m_descriptorSets));
	}

	// todo: extract reusable code from here and maybe move into a Image factory or something
	void CreateAndUploadTextureImage(vk::CommandBuffer& commandBuffer)
	{
		// Read image from file
		int texWidth = 0, texHeight = 0, texChannels = 0;
		stbi_uc* pixels = stbi_load(kTexturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0) {
			throw std::runtime_error("failed to load texture image!");
		}

		uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

		// Texture image
		m_textureImage = std::make_unique<Image>(
			texWidth, texHeight, 4UL, // R8G8B8A8, depth = 4
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferSrc |
				vk::ImageUsageFlagBits::eTransferDst | // src and dst for mipmaps blit
				vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			vk::ImageAspectFlagBits::eColor,
			mipLevels
		);
		m_textureImage->CreateStagingBuffer();

		// Copy image data to staging buffer
		m_textureImage->Overwrite(
			commandBuffer,
			reinterpret_cast<const void*>(pixels),
			vk::ImageLayout::eShaderReadOnlyOptimal // dstImageLayout
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
			0.0f, // minLod
			static_cast<float>(m_textureImage->GetMipLevels()), // maxLod
			vk::BorderColor::eIntOpaqueBlack, // borderColor
			false // unnormalizedCoordinates
		));
	}

	void LoadModel()
	{
		m_vertices.clear();
		m_indices.clear();

		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, kModelPath.c_str()))
			throw std::runtime_error(warn + err);
		
		std::unordered_map<Vertex, uint32_t> uniqueVertices;

		for (const auto& shape : shapes)
		{
			for (const auto& index : shape.mesh.indices)
			{
				Vertex vertex = {};
				vertex.pos = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]
				};
				vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
				};
				vertex.color = { 1.0f, 1.0f, 1.0f };

				if (uniqueVertices.count(vertex) == 0)
				{
					uniqueVertices[vertex] = static_cast<uint32_t>(m_vertices.size());
					m_vertices.push_back(vertex);
				}
				m_indices.push_back(uniqueVertices[vertex]);
			}
		}
	}

	void UploadAndBindGeometry(vk::CommandBuffer& commandBuffer)
	{
		m_vertexBuffer = std::make_unique<BufferWithStaging>(
			sizeof(m_vertices[0]) * m_vertices.size(),
			vk::BufferUsageFlagBits::eVertexBuffer
		);
		m_indexBuffer = std::make_unique<BufferWithStaging>(
			sizeof(m_indices[0]) * m_indices.size(),
			vk::BufferUsageFlagBits::eIndexBuffer
		);

		m_indexBuffer->Overwrite(commandBuffer, reinterpret_cast<const void*>(m_indices.data()));
		m_vertexBuffer->Overwrite(commandBuffer, reinterpret_cast<const void*>(m_vertices.data()));
		
		m_renderPass->BindVertexBuffer(m_vertexBuffer->Get());
		m_renderPass->BindIndexBuffer(m_indexBuffer->Get(), m_indices.size());
	}

	void UpdateUniformBuffer(uint32_t imageIndex)
	{
		using namespace std::chrono;

		static auto startTime = high_resolution_clock::now();

		auto currentTime = high_resolution_clock::now();
		float time = duration<float, seconds::period>(currentTime - startTime).count();

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.proj = glm::perspective(glm::radians(45.0f), extent.width / (float)extent.height, 0.1f, 10.0f);

		ubo.proj[1][1] *= -1; // inverse Y for OpenGL -> Vulkan (clip coordinates)

		// Upload to GPU (inefficient, use push-constants instead)
		m_uniformBuffers[imageIndex].Overwrite(reinterpret_cast<const void*>(&ubo));
	}

private:
	std::unique_ptr<RenderPass> m_renderPass;

	std::unique_ptr<BufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<BufferWithStaging> m_indexBuffer{ nullptr };
	std::vector<Buffer> m_uniformBuffers; // one per image since these change every frame

	std::unique_ptr<Image> m_textureImage{ nullptr };
	vk::UniqueSampler m_sampler;

	vk::UniqueDescriptorPool m_descriptorPool;
	std::vector<vk::UniqueDescriptorSet> m_descriptorSets;

	// Model data
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
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
