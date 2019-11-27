#include <iostream>

#include "RenderLoop.h"

#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "CommandBuffers.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "GraphicsPipeline.h"
#include "Shader.h"
#include "Image.h"
#include "Texture.h"
#include "Camera.h"

#include "vk_utils.h"
#include <GLFW/glfw3.h>
#define _USE_MATH_DEFINES
#include <cmath>

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

struct UniformBufferObject {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	bool operator==(const Vertex& other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord;
	}
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}

class App : public RenderLoop
{
public:
	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
		: RenderLoop(surface, extent, window)
		, m_renderPass(std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format))
		, m_framebuffers(Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get()))
		, m_vertexShader(std::make_unique<Shader>("vert.spv", "main"))
		, m_fragmentShader(std::make_unique<Shader>("frag.spv", "main"))
		, m_graphicsPipeline(
			std::make_unique<GraphicsPipeline>(m_renderPass->Get(),
			m_swapchain->GetImageDescription().extent,
			*m_vertexShader,
			*m_fragmentShader))
		,camera(glm::vec3(4.0f, 4.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 45.0f)
	{
		window.SetMouseButtonCallback(reinterpret_cast<void*>(this), OnMouseButton);
		window.SetMouseScrollCallback(reinterpret_cast<void*>(this), OnMouseScroll);
		window.SetCursorPositionCallback(reinterpret_cast<void*>(this), OnCursorPosition);
		window.SetKeyCallback(reinterpret_cast<void*>(this), onKey);
	}

	using RenderLoop::Init;

protected:
	const std::string kModelPath = "cube.obj";
	const std::string kTexturePath = "cube.jpg";
	glm::vec2 m_mouseDownPos;
	bool m_mouseIsDown = false;
	std::map<int, bool> m_keyState;
	Camera camera;

	void Init(vk::CommandBuffer& commandBuffer) override
	{
		LoadModel();
		UploadGeometry(commandBuffer);
		CreateAndUploadTextureImage(commandBuffer);
		CreateUniformBuffers();
		CreateSampler();
		CreateDescriptorSets();

		RecordRenderPassCommands(m_renderCommandBuffers);
	}

	// Render pass commands are recorded once and executed every frame
	void OnSwapchainRecreated(CommandBufferPool& commandBuffers) override
	{
		// Reset resources that depend on the swapchain images
		m_graphicsPipeline.reset();
		m_framebuffers.clear();
		m_renderPass.reset();

		m_renderPass = std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format);
		m_framebuffers = Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get());
		m_graphicsPipeline = std::make_unique<GraphicsPipeline>(
			m_renderPass->Get(), m_swapchain->GetImageDescription().extent,
			*m_vertexShader, *m_fragmentShader
		);

		// Recreate everything that depends on the number of images
		CreateUniformBuffers();
		CreateDescriptorSets();

		RecordRenderPassCommands(commandBuffers);
	}

	void RecordRenderPassCommands(CommandBufferPool& commandBuffers)
	{
		std::array<vk::ClearValue, 2> clearValues = {
			vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f }),
			vk::ClearDepthStencilValue(1.0f, 0.0f)
		};

		for (size_t i = 0; i < m_swapchain->GetImageCount(); i++)
		for (size_t i = 0; i < commandBuffers.GetCount(); i++)
		{
			auto& commandBuffer = commandBuffers.GetCommandBuffer();
			commandBuffer.begin(vk::CommandBufferBeginInfo());
			{
				m_renderPass->Begin(commandBuffer, m_framebuffers[i], clearValues);
				{
					m_graphicsPipeline->Draw(
						commandBuffer,
						m_indices.size(),
						m_vertexBuffer->Get(),
						m_indexBuffer->Get(),
						m_vertexOffsets.data(),
						m_descriptors.descriptorSets[i].get()
					);
				}
				commandBuffer.endRenderPass();
			}
			commandBuffer.end();
			commandBuffers.MoveToNext();
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
		m_uniformBuffers.reserve(m_swapchain->GetImageCount());
		for (uint32_t i = 0; i < m_swapchain->GetImageCount(); ++i)
		{
			m_uniformBuffers.emplace_back(sizeof(UniformBufferObject));
		}
	}

	void CreateDescriptorSets()
	{
		m_descriptors = m_graphicsPipeline->CreateDescriptorSetPool(m_uniformBuffers.size());

		// Update
		for (uint32_t i = 0; i < m_uniformBuffers.size(); ++i)
		{
			vk::DescriptorBufferInfo descriptorBufferInfo(m_uniformBuffers[i].Get(), 0, sizeof(UniformBufferObject));
			vk::DescriptorImageInfo imageInfo(m_sampler.get(), m_texture->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
			std::array<vk::WriteDescriptorSet, 2> writeDescriptorSets = {
				vk::WriteDescriptorSet(m_descriptors.descriptorSets[i].get(), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo), // binding = 0
				vk::WriteDescriptorSet(m_descriptors.descriptorSets[i].get(), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr) // binding = 1
			};
			g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
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

	void UploadGeometry(vk::CommandBuffer& commandBuffer)
	{
		m_vertexBuffer = std::make_unique<VertexBuffer>(sizeof(m_vertices[0]) * m_vertices.size());
		m_vertexBuffer->Overwrite(commandBuffer, reinterpret_cast<const void*>(m_vertices.data()));

		m_indexBuffer = std::make_unique<IndexBuffer>(sizeof(m_indices[0]) * m_indices.size());
		m_indexBuffer->Overwrite(commandBuffer, reinterpret_cast<const void*>(m_indices.data()));
	}

	void UpdateUniformBuffer(uint32_t imageIndex)
	{
		using namespace std::chrono;

		vk::Extent2D extent = m_swapchain->GetImageDescription().extent;

		static auto startTime = high_resolution_clock::now();

		auto currentTime = high_resolution_clock::now();
		float time = duration<float, seconds::period>(currentTime - startTime).count();
		time = 0;

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = camera.GetViewMatrix();
		ubo.proj = glm::perspective(glm::radians(camera.GetFieldOfView()), extent.width / (float)extent.height, 0.1f, 10.0f);

		ubo.proj[1][1] *= -1; // inverse Y for OpenGL -> Vulkan (clip coordinates)

		// Upload to GPU (inefficient, use push-constants instead)
		m_uniformBuffers[imageIndex].Overwrite(reinterpret_cast<const void*>(&ubo));
	}

	void Update() override 
	{
		for (std::pair<int,bool> key : m_keyState)
		{
			if (key.second) 
			{
				glm::vec3 forward = glm::normalize(camera.GetLookAt() - camera.GetEye());
				glm::vec3 rightVector = glm::normalize(glm::cross(forward, camera.GetUpVector()));
				float speed = 0.01f;

				switch (key.first) {
					case GLFW_KEY_W:
						camera.MoveCamera(forward, speed, false);
						break;
					case GLFW_KEY_A:
						camera.MoveCamera(rightVector, -speed, true);
						break;
					case GLFW_KEY_S:
						camera.MoveCamera(forward, -speed, false);
						break;
					case GLFW_KEY_D:
						camera.MoveCamera(rightVector, speed, true);
						break;
					default:
						break;
				}
			}
		}
	}

	static void OnMouseButton(void* data, int button, int action, int mods)
	{
		App* app = reinterpret_cast<App*>(data);

		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
			app->m_mouseIsDown = true;
		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
			app->m_mouseIsDown = false;
	}

	static void OnMouseScroll(void* data, double xOffset, double yOffset)
	{
		App* app = reinterpret_cast<App*>(data);
		app->camera.SetFieldOfView(app->camera.GetFieldOfView() - yOffset);
	}

	static void OnCursorPosition(void* data, double xPos, double yPos)
	{
		App* app = reinterpret_cast<App*>(data);
		int width = 0;
		int height = 0;
		app->m_window.GetSize(&width, &height);
		float speed = 0.008;

		if (app->m_mouseIsDown)
		{
			float diffX = app->m_mouseDownPos.x - xPos;
			float diffY = app->m_mouseDownPos.y - yPos;

			auto lookat = app->camera.GetLookAt() - app->camera.GetUpVector() * speed * diffY;

			glm::vec3 forward = glm::normalize(lookat - app->camera.GetEye());
			glm::vec3 rightVector = glm::normalize(glm::cross(forward, app->camera.GetUpVector()));

			app->camera.LookAt(lookat + rightVector * speed * diffX);
		}

		app->m_mouseDownPos.x = xPos;
		app->m_mouseDownPos.y = yPos;
	}

	static void onKey(void* data, int key, int scancode, int action, int mods) {
		App* app = reinterpret_cast<App*>(data);
		app->m_keyState[key] = action == GLFW_PRESS ? true : action == GLFW_REPEAT ? true : false;
	}

private:
	std::unique_ptr<RenderPass> m_renderPass;
	std::vector<Framebuffer> m_framebuffers;
	std::unique_ptr<Shader> m_vertexShader;
	std::unique_ptr<Shader> m_fragmentShader;
	std::unique_ptr<GraphicsPipeline> m_graphicsPipeline;

	std::unique_ptr<VertexBuffer> m_vertexBuffer{ nullptr };
	std::unique_ptr<IndexBuffer> m_indexBuffer{ nullptr };
	std::vector<UniformBuffer> m_uniformBuffers; // one per image since these change every frame

	std::unique_ptr<Texture> m_texture{ nullptr };
	vk::UniqueSampler m_sampler;

	DescriptorSetPool m_descriptors;

	// Model data
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::vector<vk::DeviceSize> m_vertexOffsets; // 1 big vertex buffer for now
};

int main()
{
	vk::Extent2D extent(800, 600);
	Window window(extent, "Vulkan");
	window.SetInputMode(GLFW_STICKY_KEYS, GLFW_TRUE);

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
