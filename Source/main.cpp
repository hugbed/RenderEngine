#include <iostream>

#include "RenderLoop.h"

#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "CommandBuffers.h"
#include "RenderPass.h"
#include "Image.h"
#include "Texture.h"

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
	const std::string kModelPath = "donut.obj";
	const std::string kTexturePath = "donut.png";

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
		m_renderPass->BindVertexOffsets(m_vertexOffsets.data());
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

		m_renderPass->BindVertexBuffer(m_vertexBuffer->Get());
		m_renderPass->BindVertexOffsets(m_vertexOffsets.data());
		m_renderPass->BindIndexBuffer(m_indexBuffer->Get(), m_indices.size());

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
		m_descriptors = m_renderPass->CreateDescriptorSets(vk_utils::get_all(m_uniformBuffers), m_texture->GetImageView(), m_sampler.get());
		
		m_renderPass->BindDescriptorSets(vk_utils::remove_unique(m_descriptors.descriptorSets));
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
		m_texture = std::make_unique<Texture>(
			texWidth, texHeight, 4UL, // R8G8B8A8, depth = 4
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | // src and dst for mipmaps blit
				vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			vk::ImageAspectFlagBits::eColor,
			mipLevels
		);

		// Copy image data to staging buffer
		m_texture->Overwrite(
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
			static_cast<float>(m_texture->GetMipLevels()), // maxLod
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

		m_vertexOffsets.reserve(shapes.size());

		for (const auto& shape : shapes)
		{
			m_vertexOffsets.push_back(m_indices.size());

			for (const auto& index : shape.mesh.indices)
			{
				Vertex vertex = {};
				vertex.pos = {
					attrib.vertices[3UL * index.vertex_index + 0],
					attrib.vertices[3UL * index.vertex_index + 1],
					attrib.vertices[3UL * index.vertex_index + 2]
				};
				vertex.texCoord = {
					attrib.texcoords[2UL * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2UL * index.texcoord_index + 1]
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
		ubo.view = glm::lookAt(0.25f * glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
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

	std::unique_ptr<Texture> m_texture{ nullptr };
	vk::UniqueSampler m_sampler;

	RenderPass::Descriptors m_descriptors;

	// Model data
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::vector<uint64_t> m_vertexOffsets; // 1 big vertex buffer for now
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
