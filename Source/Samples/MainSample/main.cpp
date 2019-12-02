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
#include <algorithm>

// For Uniform Buffer
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/gtx/norm.hpp>

// For Texture
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Model
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <chrono>
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

enum CameraMode { OrbitCamera, FreeCamera };

class App : public RenderLoop
{
public:
	// todo: per material please
	struct Constants
	{
		uint32_t lightingModel = 1;
	} m_specializationConstants;

	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
		: RenderLoop(surface, extent, window)
		, m_renderPass(std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format))
		, m_framebuffers(Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get()))
		, m_vertexShader(std::make_unique<Shader>("mvp_vert.spv", "main"))
		, m_fragmentShader(std::make_unique<Shader>("surface_frag.spv", "main"))
		, camera(0.25f * glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 45.0f)
		, activeCameraMode(CameraMode::OrbitCamera)
	{
		m_fragmentShader->SetSpecializationConstants(m_specializationConstants);

		m_graphicsPipeline= std::make_unique<GraphicsPipeline>(
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
	const std::string kModelPath = "donut.obj";
	const std::string kTexturePath = "donut.png";
	glm::vec2 m_mouseDownPos;
	bool m_mouseIsDown = false;
	std::map<int, bool> m_keyState;
	Camera camera;
	CameraMode activeCameraMode;
	float InitOrbitCameraRadius;

	void Init(vk::CommandBuffer& commandBuffer) override
	{
		LoadModel();
		UploadGeometry(commandBuffer);
		CreateAndUploadTextureImage(commandBuffer);
		CreateUniformBuffers();
		CreateSampler();
		CreateDescriptorSets();

		CreateSecondaryCommandBuffers();
		RecordRenderPassCommands();
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
			vk::CommandBufferInheritanceInfo info(
				m_renderPass->Get(), 0, m_framebuffers[i].Get()
			);
			m_renderPassCommandBuffers[i]->begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
			{
				m_graphicsPipeline->Draw(
					m_renderPassCommandBuffers[i].get(),
					m_indices.size(),
					m_vertexBuffer->Get(),
					m_indexBuffer->Get(),
					m_vertexOffsets.data(),
					m_descriptors.descriptorSets[i].get()
				);
			}
			m_renderPassCommandBuffers[i]->end();
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
		m_uniformBuffers.clear();
		m_uniformBuffers.reserve(m_swapchain->GetImageCount());
		for (uint32_t i = 0; i < m_swapchain->GetImageCount(); ++i)
		{
			m_uniformBuffers.emplace_back(
				vk::BufferCreateInfo(
					{},
					sizeof(UniformBufferObject),
					vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc
				), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
			);
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
			vk::ImageAspectFlagBits::eColor,
			mipLevels
		);
		memcpy(m_texture->GetStagingMappedData(), reinterpret_cast<const void*>(pixels), texWidth * texHeight * 4UL);
		m_texture->UploadStagingToGPU(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal);

		// We won't need the staging buffer after the initial upload
		auto* stagingBuffer = m_texture->ReleaseStagingBuffer();
		m_commandBufferPool.DestroyAfterSubmit(stagingBuffer);

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

		float maxDist = 0;
		glm::vec3 zeroVect(0,0,0);

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

				float dist = glm::distance(vertex.pos, zeroVect);
				if(dist > maxDist) {
					maxDist = dist;
				}
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

		// Init camera to see the model
		InitOrbitCameraRadius = maxDist * 2;
		this->camera.SetCameraView(glm::vec3(InitOrbitCameraRadius, InitOrbitCameraRadius, InitOrbitCameraRadius), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
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

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(1.0f), 0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = camera.GetViewMatrix();
		ubo.proj = glm::perspective(glm::radians(camera.GetFieldOfView()), extent.width / (float)extent.height, 0.1f, 10.0f);

		ubo.proj[1][1] *= -1; // inverse Y for OpenGL -> Vulkan (clip coordinates)

		// Upload to GPU
		void* data = m_uniformBuffers[imageIndex].GetMappedData();
		memcpy(data, reinterpret_cast<const void*>(&ubo), sizeof(UniformBufferObject));
	}

	void Update() override 
	{
		std::chrono::duration<float> dt_s = GetDeltaTime();

		const float speed = 1.0f; // in m/s

		for (const std::pair<int,bool>& key : m_keyState)
		{
			if (key.second && activeCameraMode == CameraMode::FreeCamera) 
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
		float fov = std::clamp(app->camera.GetFieldOfView() - yOffset, 30.0, 130.0);
		app->camera.SetFieldOfView(fov);
	}

	static void OnCursorPosition(void* data, double xPos, double yPos)
	{
		App* app = reinterpret_cast<App*>(data);
		int width = 0;
		int height = 0;
		app->m_window.GetSize(&width, &height);

		if ((app->m_mouseIsDown) && app->activeCameraMode == CameraMode::OrbitCamera) 
		{
			glm::vec3 rightVector = app->camera.GetRightVector();
			glm::vec3 zVector(0, 0, 1);
			glm::vec4 position(app->camera.GetEye().x, app->camera.GetEye().y, app->camera.GetEye().z, 1);
			glm::vec4 target(app->camera.GetLookAt().x, app->camera.GetLookAt().y, app->camera.GetLookAt().z, 1);

			float dist = glm::distance2(zVector, glm::normalize(app->camera.GetEye() - app->camera.GetLookAt()));

			float xAngle = (app->m_mouseDownPos.x - xPos) * (M_PI/300);
			float yAngle = (app->m_mouseDownPos.y - yPos) * (M_PI/300);

			if (dist < 0.01 && yAngle < 0 || 4.0 - dist < 0.01 && yAngle > 0) {
				yAngle = 0;
			}

			glm::mat4x4 rotationMatrixY(1.0f);
			rotationMatrixY = glm::rotate(rotationMatrixY, yAngle, rightVector);

			glm::mat4x4 rotationMatrixX(1.0f);
			rotationMatrixX = glm::rotate(rotationMatrixX, xAngle, zVector);

			position = (rotationMatrixX * (position - target)) + target;

			glm::vec3 finalPositionV3 = (rotationMatrixY * (position - target)) + target;

			app->camera.SetCameraView(finalPositionV3, app->camera.GetLookAt(), zVector);
		}
		else if (app->m_mouseIsDown && app->activeCameraMode == CameraMode::FreeCamera) 
		{
			float speed = 0.0000001;

			auto dt_s = app->GetDeltaTime();
			float dx = speed * dt_s.count();

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

		if (key == GLFW_KEY_F && action == GLFW_PRESS) {
			if (app->activeCameraMode == CameraMode::FreeCamera) 
			{
				// Reset camera position
				app->camera.SetCameraView(glm::vec3(app->InitOrbitCameraRadius, app->InitOrbitCameraRadius, app->InitOrbitCameraRadius), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
			}
			app->activeCameraMode = app->activeCameraMode == CameraMode::FreeCamera ? CameraMode::OrbitCamera : CameraMode::FreeCamera;
		}
	}

private:
	std::unique_ptr<RenderPass> m_renderPass;
	std::vector<Framebuffer> m_framebuffers;
	std::unique_ptr<Shader> m_vertexShader;
	std::unique_ptr<Shader> m_fragmentShader;
	std::unique_ptr<GraphicsPipeline> m_graphicsPipeline;

	// Secondary command buffers
	vk::UniqueCommandPool m_secondaryCommandPool;
	std::vector<vk::UniqueCommandBuffer> m_renderPassCommandBuffers;

	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<UniqueBufferWithStaging> m_indexBuffer{ nullptr };

	std::vector<UniqueBuffer> m_uniformBuffers; // one per image since these change every frame

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
