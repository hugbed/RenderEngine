#include <iostream>

#include "RenderLoop.h"
#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "CommandBufferPool.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "GraphicsPipeline.h"
#include "Shader.h"
#include "Image.h"
#include "Texture.h"
#include "vk_utils.h"

#include "Camera.h"

#include <GLFW/glfw3.h>
#define _USE_MATH_DEFINES
#include <cmath>

// For Uniform Buffer
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// For Texture
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Model
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <chrono>
#include <unordered_map>

struct GlobalUniforms {
	glm::mat4 view;
	glm::mat4 proj;
};

struct PerObjectUniforms {
	glm::mat4 model;
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

struct Material
{
	GraphicsPipeline* pipeline = nullptr;
	Texture* texture = nullptr;
	vk::Sampler sampler = nullptr;
};

struct Object
{
	vk::DeviceSize indexOffset;
	vk::DeviceSize nbIndices;
	Material* material;
};

class App : public RenderLoop
{
public:
	struct SurfaceShaderConstants
	{
		uint32_t lightingModel = 1;
	};

	enum class DescriptorSetIndices
	{
		Global = 0, // Scene
		PerMaterial = 1, // Material
		PerObject = 2 // Node
	};

	// todo: per material please
	SurfaceShaderConstants m_specializationConstants;

	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
		: RenderLoop(surface, extent, window)
		, m_renderPass(std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format))
		, m_framebuffers(Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get()))
		, m_vertexShader(std::make_unique<Shader>("mvp_vert.spv", "main"))
		, m_fragmentShader(std::make_unique<Shader>("surface_frag.spv", "main"))
		, camera(1.0f * glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 45.0f)
	{
		m_fragmentShader->SetSpecializationConstants(m_specializationConstants);

		m_graphicsPipelines.emplace_back(
			m_renderPass->Get(),
			m_swapchain->GetImageDescription().extent,
			*m_vertexShader,
			*m_fragmentShader
		);

		window.SetMouseButtonCallback(reinterpret_cast<void*>(this), OnMouseButton);
		window.SetMouseScrollCallback(reinterpret_cast<void*>(this), OnMouseScroll);
		window.SetCursorPositionCallback(reinterpret_cast<void*>(this), OnCursorPosition);
		window.SetKeyCallback(reinterpret_cast<void*>(this), onKey);
	}

	using RenderLoop::Init;

protected:
	const std::string kModelPath = "cubes.obj";
	const std::string kTexturePath = "donut.png";
	glm::vec2 m_mouseDownPos;
	bool m_mouseIsDown = false;
	std::map<int, bool> m_keyState;
	Camera camera;

	void Init(vk::CommandBuffer& commandBuffer) override
	{
		LoadModel(commandBuffer);
		UploadGeometry(commandBuffer);
		CreateUniformBuffers();
		CreateDescriptorSets();

		CreateSecondaryCommandBuffers();
		RecordRenderPassCommands();
	}

	// Render pass commands are recorded once and executed every frame
	void OnSwapchainRecreated(CommandBufferPool& commandBuffers) override
	{
		// Reset resources that depend on the swapchain images
		m_graphicsPipelines.clear();
		m_framebuffers.clear();
		m_renderPass.reset();

		m_renderPass = std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format);
		m_framebuffers = Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get());
		m_graphicsPipelines.emplace_back(
			m_renderPass->Get(), m_swapchain->GetImageDescription().extent,
			*m_vertexShader, *m_fragmentShader
		);

		// Recreate everything that depends on the number of images
		CreateUniformBuffers();
		CreateDescriptorSets();

		CreateSecondaryCommandBuffers();
		RecordRenderPassCommands();
	}

	void CreateSecondaryCommandBuffers()
	{
		m_renderPassCommandBuffers.clear();

		// We don't need to repopulate draw commands every frame
		// so keep them in a secondary command buffer
		m_secondaryCommandPool = g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
			vk::CommandPoolCreateFlagBits::eResetCommandBuffer, g_physicalDevice->GetQueueFamilies().graphicsFamily.value()
		));

		// Command Buffers
		m_renderPassCommandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
			m_secondaryCommandPool.get(), vk::CommandBufferLevel::eSecondary, m_framebuffers.size()
		));
	}

	// Record commands that don't change each frame in secondary command buffers
	void RecordRenderPassCommands()
	{
		for (size_t i = 0; i < m_framebuffers.size(); ++i)
		{
			auto& commandBuffer = m_renderPassCommandBuffers[i];
			vk::CommandBufferInheritanceInfo info(
				m_renderPass->Get(), 0, m_framebuffers[i].Get()
			);
			commandBuffer->begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
			{
				// Bind global descriptor set
				commandBuffer.get().bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics, m_globalPipelineLayout.get(),
					(uint32_t)DescriptorSetIndices::Global,
					1, &m_globalDescriptorSets[i % m_commandBufferPool.GetNbConcurrentSubmits()].get(), 0, nullptr
				);

				// Bind the one big vertex + index buffers
				vk::DeviceSize offsets[] = { 0 };
				vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
				commandBuffer.get().bindVertexBuffers(0, 1, vertexBuffers, offsets);
				commandBuffer.get().bindIndexBuffer(m_indexBuffer->Get(), 0, vk::IndexType::eUint32);

				// For each object
				uint32_t materialIndex = ~0UL;
				for (const auto& obj : m_objects)
				{
					// If we changed material (todo: this makes me sad)
					if (obj.first != materialIndex)
					{
						// Bind material's graphics pipeline
						commandBuffer.get().bindPipeline(vk::PipelineBindPoint::eGraphics, obj.second.material->pipeline->Get());
						materialIndex = obj.first;
					}

					// Bind object descriptor set
					auto& objectDescriptorSet = m_objectDescriptorSets[i % m_commandBufferPool.GetNbConcurrentSubmits()];
					commandBuffer.get().bindDescriptorSets(
						vk::PipelineBindPoint::eGraphics,
						obj.second.material->pipeline->GetPipelineLayout(),
						(uint32_t)DescriptorSetIndices::PerObject,
						1, &objectDescriptorSet.get(), 0, nullptr);

					// Draw with the correct index offset
					commandBuffer.get().drawIndexed(obj.second.nbIndices, 1, obj.second.indexOffset, 0, 0);
				}
			}
			commandBuffer->end();
		}
	}

	void RenderFrame(uint32_t imageIndex, vk::CommandBuffer commandBuffer) override
	{
		auto& framebuffer = m_framebuffers[imageIndex];

		UpdateUniformBuffer(imageIndex);

		std::array<vk::ClearValue, 2> clearValues = {
			vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f }),
			vk::ClearDepthStencilValue(1.0f, 0.0f)
		};

		auto renderPassInfo = vk::RenderPassBeginInfo(
			m_renderPass->Get(), framebuffer.Get(),
			vk::Rect2D(vk::Offset2D(0, 0), framebuffer.GetExtent()),
			static_cast<uint32_t>(clearValues.size()), clearValues.data()
		);
		commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);
		{
			commandBuffer.executeCommands(m_renderPassCommandBuffers[imageIndex].get());
		}
		commandBuffer.endRenderPass();
	}

	void CreateUniformBuffers()
	{
		m_dynamicUniformBuffers.clear();
		m_dynamicUniformBuffers.reserve(m_commandBufferPool.GetNbConcurrentSubmits());
		for (uint32_t i = 0; i < m_commandBufferPool.GetNbConcurrentSubmits(); ++i)
		{
			m_dynamicUniformBuffers.emplace_back(
				vk::BufferCreateInfo(
					{},
					sizeof(GlobalUniforms),
					vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc // needs TransferSrc?
				), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
			);
		}

		// Upload identity matrix for model transform for each object
		PerObjectUniforms ubo = {};
		ubo.model = glm::mat4(1.0f);
		m_staticUniformBuffers.clear();
		m_staticUniformBuffers.reserve(m_objects.size());
		for (int i = 0; i < m_objects.size(); ++i)
		{
			m_staticUniformBuffers.emplace_back(
				vk::BufferCreateInfo(
					{},
					m_objects.size() * sizeof(PerObjectUniforms),
					vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc // needs TransferSrc?
				), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
			);
			void* data = m_staticUniformBuffers[m_staticUniformBuffers.size() - 1].GetMappedData();
			memcpy(data, &ubo, sizeof(PerObjectUniforms));
		}
	}

	void CreateDescriptorSets()
	{
		m_globalDescriptorSets.clear();
		m_objectDescriptorSets.clear();
		m_descriptorPool.reset();

		// We have 1 global descriptor set (set = 0)
		uint32_t nbCameras = 1;

		// 1 descriptor set per material (set = 1)
		uint32_t nbMaterials = m_materials.size();

		// 1 descriptor set per object (set = 2)
		uint32_t nbObjects = m_objects.size();

		// Dynamic uniform data (updated each frame) gets one set per swapchain image
		uint32_t nbSets = nbCameras * m_commandBufferPool.GetNbConcurrentSubmits() + nbMaterials + nbObjects;
		
		// Create descriptor pool
		std::vector<vk::DescriptorPoolSize> poolSizes;

		// 1 sampler per texture
		poolSizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eSampler,
			static_cast<uint32_t>(m_samplers.size())
		));

		// Each set possibly needs a uniform buffer
		poolSizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eUniformBuffer,
			nbSets
		));

		m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			nbSets,
			static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
		));

		// Create global descriptor set (ask any graphics pipeline to provide the global layout)
		{
			m_globalSetLayout = m_graphicsPipelines[0].GetDescriptorSetLayout((size_t)DescriptorSetIndices::Global);
			std::vector<vk::DescriptorSetLayout> layouts(nbCameras * m_commandBufferPool.GetNbConcurrentSubmits(), m_globalSetLayout);
			m_globalDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
				m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
			));
			m_globalPipelineLayout = g_device->Get().createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
				{}, 1, &m_globalSetLayout
			));
		}

		// Update global descriptor sets
		for (size_t i = 0; i < m_globalDescriptorSets.size(); ++i)
		{
			vk::DescriptorBufferInfo descriptorBufferInfo(m_dynamicUniformBuffers[i].Get(), 0, sizeof(GlobalUniforms));
			std::array<vk::WriteDescriptorSet, 1> writeDescriptorSets = {
				vk::WriteDescriptorSet(
					m_globalDescriptorSets[i].get(), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
				) // binding = 0
			};
			g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}

		// No per material descriptor layouts yet
		//for (uint32_t i = 0; i < m_materials.size(); ++i)
		//{
		//	// Create per material set layout (ask material pipeline to get layout for set = 1)
		//}

		// Create per-object descriptor sets
		{
			m_objectSetLayout = m_graphicsPipelines[0].GetDescriptorSetLayout((size_t)DescriptorSetIndices::PerObject);
			std::vector<vk::DescriptorSetLayout> layouts(nbObjects, m_objectSetLayout);
			m_objectDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
				m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
			));
		}
		// Update per object descriptor sets
		for (size_t i = 0; i < m_objectDescriptorSets.size(); ++i)
		{
			auto& set = m_objectDescriptorSets[i];

			vk::DescriptorBufferInfo descriptorBufferInfo(m_staticUniformBuffers[i].Get(), 0, sizeof(PerObjectUniforms));

			// Use the material's sampler and texture
			auto& sampler = m_objects[i].second.material->sampler;
			auto& imageView = m_objects[i].second.material->texture->GetImageView();
			vk::DescriptorImageInfo imageInfo(sampler, imageView, vk::ImageLayout::eShaderReadOnlyOptimal);

			std::array<vk::WriteDescriptorSet, 2> writeDescriptorSets = {
				vk::WriteDescriptorSet(
					set.get(), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
				), // binding = 0
				vk::WriteDescriptorSet(
					set.get(), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr
				) // binding = 1
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
		m_textures.emplace_back(
			texWidth, texHeight, 4UL, // R8G8B8A8, depth = 4
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | // src and dst for mipmaps blit
				vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eColor,
			mipLevels
		);
		auto& texture = m_textures[m_textures.size() - 1];

		memcpy(texture.GetStagingMappedData(), reinterpret_cast<const void*>(pixels), texWidth * texHeight * 4UL);
		texture.UploadStagingToGPU(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal);

		// We won't need the staging buffer after the initial upload
		auto* stagingBuffer = texture.ReleaseStagingBuffer();
		m_commandBufferPool.DestroyAfterSubmit(stagingBuffer);

		stbi_image_free(pixels);
	}

	void CreateSamplerForLastTexture()
	{
		// Add sampler for the last texture
		auto& texture = m_textures[m_textures.size() - 1];
		m_samplers.push_back(g_device->Get().createSamplerUnique(vk::SamplerCreateInfo(
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
			static_cast<float>(texture.GetMipLevels()), // maxLod
			vk::BorderColor::eIntOpaqueBlack, // borderColor
			false // unnormalizedCoordinates
		)));
	}

	void LoadModel(vk::CommandBuffer& commandBuffer)
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

		// Create default material and assign it to all objects
		// Todo: create material for each material
		// Also material creation should handle creating
		// loading shaders and creating pipelines
		CreateAndUploadTextureImage(commandBuffer);
		CreateSamplerForLastTexture();
		{
			Material material;
			material.pipeline = &m_graphicsPipelines[0];
			material.texture = &m_textures[0];
			material.sampler = m_samplers[0].get();
			m_materials.push_back(std::move(material));
		}

		m_objects.reserve(shapes.size());
		for (const auto& shape : shapes)
		{
			Object object;
			object.indexOffset = m_indices.size();
			object.nbIndices = shape.mesh.indices.size();
			object.material = &m_materials[0];
			m_objects.push_back(std::make_pair(0, std::move(object))); // this pair structure makes me sad

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
		
		// Sort objects by material id (this makes me sad)
		std::sort(m_objects.begin(), m_objects.end(), [](const auto& a, const auto& b) {
			return a.first < b.first;
		});
	}

	void UploadGeometry(vk::CommandBuffer& commandBuffer)
	{
		{
			vk::DeviceSize bufferSize = sizeof(m_vertices[0]) * m_vertices.size();
			m_vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
			memcpy(m_vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_vertices.data()), bufferSize);
			m_vertexBuffer->CopyStagingToGPU(commandBuffer);

			// We won't need the staging buffer after the initial upload
			m_commandBufferPool.DestroyAfterSubmit(m_vertexBuffer->ReleaseStagingBuffer());
		}
		{
			vk::DeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();
			m_indexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer);
			memcpy(m_indexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_indices.data()), bufferSize);
			m_indexBuffer->CopyStagingToGPU(commandBuffer);

			// We won't need the staging buffer after the initial upload
			m_commandBufferPool.DestroyAfterSubmit(m_indexBuffer->ReleaseStagingBuffer());
		}
	}

	void UpdateUniformBuffer(uint32_t imageIndex)
	{
		using namespace std::chrono;

		vk::Extent2D extent = m_swapchain->GetImageDescription().extent;

		GlobalUniforms ubo = {};
		//ubo.model = glm::rotate(glm::mat4(1.0f), 0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = camera.GetViewMatrix();
		ubo.proj = glm::perspective(glm::radians(camera.GetFieldOfView()), extent.width / (float)extent.height, 0.1f, 10.0f);

		ubo.proj[1][1] *= -1; // inverse Y for OpenGL -> Vulkan (clip coordinates)

		// Upload to GPU
		auto& uniformBuffer = m_dynamicUniformBuffers[imageIndex % m_commandBufferPool.GetNbConcurrentSubmits()];
		memcpy(uniformBuffer.GetMappedData(), reinterpret_cast<const void*>(&ubo), sizeof(GlobalUniforms));
	}

	void Update() override 
	{
		std::chrono::duration<float> dt_s = GetDeltaTime();

		const float speed = 1.0f; // in m/s

		for (const std::pair<int,bool>& key : m_keyState)
		{
			if (key.second) 
			{
				glm::vec3 forward = glm::normalize(camera.GetLookAt() - camera.GetEye());
				glm::vec3 rightVector = glm::normalize(glm::cross(forward, camera.GetUpVector()));
				float dx = speed * dt_s.count(); // in m / s

				switch (key.first) {
					case GLFW_KEY_W:
						camera.MoveCamera(forward, dx, false);
						break;
					case GLFW_KEY_A:
						camera.MoveCamera(rightVector, -dx, true);
						break;
					case GLFW_KEY_S:
						camera.MoveCamera(forward, -dx, false);
						break;
					case GLFW_KEY_D:
						camera.MoveCamera(rightVector, dx, true);
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
		float speed = 0.0000001;

		auto dt_s = app->GetDeltaTime();

		float dx = speed * dt_s.count();

		if (app->m_mouseIsDown)
		{
			float diffX = app->m_mouseDownPos.x - xPos;
			float diffY = app->m_mouseDownPos.y - yPos;

			float m_fovV = app->camera.GetFieldOfView() / width * height;

			float angleX = diffX / width * app->camera.GetFieldOfView();
			float angleY = diffY / height * m_fovV;

			auto lookat = app->camera.GetLookAt() - app->camera.GetUpVector() * dx * angleY;

			glm::vec3 forward = glm::normalize(lookat - app->camera.GetEye());
			glm::vec3 rightVector = glm::normalize(glm::cross(forward, app->camera.GetUpVector()));

			app->camera.LookAt(lookat + rightVector * dx * angleX);
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
	std::vector<GraphicsPipeline> m_graphicsPipelines;

	// Secondary command buffers
	vk::UniqueCommandPool m_secondaryCommandPool;
	std::vector<vk::UniqueCommandBuffer> m_renderPassCommandBuffers;

	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<UniqueBufferWithStaging> m_indexBuffer{ nullptr };

	// For uniforms that change every frame
	std::vector<UniqueBuffer> m_dynamicUniformBuffers; // one per in flight frame since these change every frame
	
	// One per object (could be one big, in this case, make sure to align to minUniformBufferOffsetAlignment)
	std::vector<UniqueBuffer> m_staticUniformBuffers;

	std::vector<Texture> m_textures;
	std::vector<vk::UniqueSampler> m_samplers;

	vk::UniqueDescriptorPool m_descriptorPool;
	vk::DescriptorSetLayout m_globalSetLayout;
	std::vector<vk::UniqueDescriptorSet> m_globalDescriptorSets;
	vk::UniquePipelineLayout m_globalPipelineLayout;
	//vk::DescriptorSetLayout m_materialSetLayout;
	vk::DescriptorSetLayout m_objectSetLayout;
	//std::vector<vk::UniqueDescriptorSet> m_materialDescriptorSets;
	std::vector<vk::UniqueDescriptorSet> m_objectDescriptorSets;

	// Model data
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::vector<vk::DeviceSize> m_vertexOffsets; // 1 big vertex buffer for now

	using material_index = size_t;
	std::vector<Material> m_materials;
	std::vector<std::pair<material_index, Object>> m_objects;
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
