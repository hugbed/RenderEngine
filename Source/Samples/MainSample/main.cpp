#if defined(_WIN32)
#include <Windows.h>
#endif

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

struct ViewUniforms
{
	glm::mat4 view;
	glm::mat4 proj;
};

struct ModelUniforms
{
	glm::mat4 transform;
};

struct MaterialUniforms
{
	vk::ImageView imageView = nullptr;
	vk::Sampler sampler = nullptr;
};

enum class DescriptorSetIndices
{
	View = 0,
	Model = 1,
	Material = 2,
};

struct Vertex
{
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
	const vk::DescriptorSetLayout& GetDescriptorSetLayout() const
	{
		return pipeline->GetDescriptorSetLayout((size_t)DescriptorSetIndices::Material);
	}

	void Bind(vk::CommandBuffer commandBuffer) const
	{
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Get());
	}

	const GraphicsPipeline* pipeline = nullptr;
};

struct MaterialInstance
{
	void Bind(vk::CommandBuffer& commandBuffer) const
	{
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			material->pipeline->GetPipelineLayout(),
			(uint32_t)DescriptorSetIndices::Material,
			1, &descriptorSet.get(), 0, nullptr
		);
	}

	const Material* material;
	MaterialUniforms uniforms; // todo: this can vary per material
	vk::UniqueDescriptorSet descriptorSet; // per-material descriptors
};

struct Mesh
{
	vk::DeviceSize indexOffset;
	vk::DeviceSize nbIndices;
	const MaterialInstance* materialInstance;
};

struct Model
{
	void Bind(vk::CommandBuffer& commandBuffer, vk::PipelineLayout modelPipelineLayout) const
	{
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			modelPipelineLayout,
			(uint32_t)DescriptorSetIndices::Model,
			1, &descriptorSet.get(), 0, nullptr
		);
	}

	// All parts share the same model transform.
	ModelUniforms uniforms;

	// A model as multiple parts (meshes)
	std::vector<Mesh> meshes;

	// Per-object descriptors
	vk::UniqueDescriptorSet descriptorSet;
};

class App : public RenderLoop
{
public:
	struct SurfaceShaderConstants
	{
		uint32_t lightingModel = 1;
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
		LoadMaterials();
		LoadTextures(commandBuffer);
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

		// Recreate everything that depends on the number of images
		LoadMaterials();
		CreateUniformBuffers();
		CreateDescriptorSets();

		CreateSecondaryCommandBuffers();
		RecordRenderPassCommands();
	}

	// Record commands that don't change each frame in secondary command buffers
	// todo: add bind methods to Model, MaterialInstance
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
				// Bind view uniforms
				commandBuffer.get().bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics, m_viewPipelineLayout.get(),
					(uint32_t)DescriptorSetIndices::View,
					1, &m_viewDescriptorSets[i % m_commandBufferPool.GetNbConcurrentSubmits()].get(), 0, nullptr
				);

				// Bind the one big vertex + index buffers
				vk::DeviceSize offsets[] = { 0 };
				vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
				commandBuffer.get().bindVertexBuffers(0, 1, vertexBuffers, offsets);
				commandBuffer.get().bindIndexBuffer(m_indexBuffer->Get(), 0, vk::IndexType::eUint32);

				const Model* model = nullptr;
				const MaterialInstance* materialInstance = nullptr;
				const GraphicsPipeline* pipeline = nullptr;
				for (const auto& drawItem : m_drawCache)
				{
					// Bind model uniforms
					if (model != drawItem.model)
					{
						model = drawItem.model;
						model->Bind(commandBuffer.get(), m_modelPipelineLayout.get());
					}

					// Bind material uniforms
					if (drawItem.mesh->materialInstance != materialInstance)
					{
						materialInstance = drawItem.mesh->materialInstance;
						materialInstance->Bind(commandBuffer.get());
					}

					// Bind Graphics Pipeline
					if (materialInstance->material->pipeline != pipeline)
					{
						pipeline = drawItem.mesh->materialInstance->material->pipeline;
						drawItem.mesh->materialInstance->material->Bind(commandBuffer.get());
					}

					// Draw
					commandBuffer.get().drawIndexed(drawItem.mesh->nbIndices, 1, drawItem.mesh->indexOffset, 0, 0);
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

	void LoadMaterials()
	{
		m_graphicsPipelines.clear();
		while (m_materials.size() < 2)
			m_materials.push_back(std::make_unique<Material>());

		std::vector<GraphicsPipeline> pipelines;

		// Textured
		{
			m_specializationConstants.lightingModel = 1;
			m_fragmentShader->SetSpecializationConstants(m_specializationConstants);
			m_graphicsPipelines.push_back(std::make_unique<GraphicsPipeline>(
				m_renderPass->Get(),
				m_swapchain->GetImageDescription().extent,
				*m_vertexShader,
				*m_fragmentShader
			));
			m_materials[0]->pipeline = m_graphicsPipelines[m_graphicsPipelines.size() - 1].get();
		}

		// Unlit albedo
		{
			m_specializationConstants.lightingModel = 0;
			m_fragmentShader->SetSpecializationConstants(m_specializationConstants);
			m_graphicsPipelines.push_back(std::make_unique<GraphicsPipeline>(
				m_renderPass->Get(),
				m_swapchain->GetImageDescription().extent,
				*m_vertexShader,
				*m_fragmentShader
			));
			m_materials[1]->pipeline = m_graphicsPipelines[m_graphicsPipelines.size() - 1].get();
		}
	}

	void LoadTextures(vk::CommandBuffer& commandBuffer)
	{
		CreateAndUploadTextureImage(commandBuffer);
		CreateSamplerForLastTexture();
	}

	void CreateUniformBuffers()
	{
		// Per view
		m_viewUniformBuffers.clear();
		m_viewUniformBuffers.reserve(m_commandBufferPool.GetNbConcurrentSubmits());
		for (uint32_t i = 0; i < m_commandBufferPool.GetNbConcurrentSubmits(); ++i)
		{
			m_viewUniformBuffers.emplace_back(
				vk::BufferCreateInfo(
					{},
					sizeof(ViewUniforms),
					vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc // needs TransferSrc?
				), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
			);
		}

		// Upload identity matrix for model transform for each object
		ModelUniforms ubo = {};
		ubo.transform = glm::mat4(1.0f);
		m_modelUniformBuffers.clear();
		m_modelUniformBuffers.reserve(m_models.size());
		for (int i = 0; i < m_models.size(); ++i)
		{
			m_modelUniformBuffers.emplace_back(
				vk::BufferCreateInfo(
					{},
					m_models.size() * sizeof(ModelUniforms),
					vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc // needs TransferSrc?
				), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
			);
			void* data = m_modelUniformBuffers[m_modelUniformBuffers.size() - 1].GetMappedData();
			memcpy(data, &ubo, sizeof(ModelUniforms));
		}
	}

	// This assumes that there's at least one graphics pipeline
	void CreateDescriptorSets()
	{
		m_viewDescriptorSets.clear();

		for (size_t i = 0; i < m_models.size(); ++i)
			m_models[i].descriptorSet.reset();

		for (auto& materialInstance : m_materialInstances)
			materialInstance->descriptorSet.reset();

		m_descriptorPool.reset();

		// We have 1 global descriptor set (set = 0)
		uint32_t nbViews = 1 * m_commandBufferPool.GetNbConcurrentSubmits();

		// 1 descriptor set per material instance (set = 1)
		uint32_t nbMaterials = m_materialInstances.size();

		// 1 descriptor set per mesh (set = 2) // todo: have push constant instead
		uint32_t nbModels = m_models.size();

		// Dynamic uniform data (updated each frame) gets one set per swapchain image
		uint32_t totalSets = nbViews + nbMaterials + nbModels;
		
		// Create descriptor pool
		std::vector<vk::DescriptorPoolSize> poolSizes;

		// 1 sampler per texture
		poolSizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eSampler,
			static_cast<uint32_t>(m_samplers.size())
		));

		poolSizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eUniformBuffer,
			nbViews + nbModels // view and model sets need a uniform buffer
		));

		// If the number of required descriptors were to change at run-time
		// we could have a descriptorPool per concurrent frame and reset the pool
		// to increase its size while it's not in use.
		m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			totalSets,
			static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
		));

		// Create view descriptor set.
		// Ask any graphics pipeline to provide the view layout
		// since all surface materials should share this layout
		{
			// todo: regroup these fields into some kind of structure, they seem to go together
			m_viewSetLayout = m_graphicsPipelines[0]->GetDescriptorSetLayout((size_t)DescriptorSetIndices::View);
			std::vector<vk::DescriptorSetLayout> layouts(nbViews, m_viewSetLayout);
			m_viewDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
				m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
			));
			m_viewPipelineLayout = g_device->Get().createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
				{}, 1, &m_viewSetLayout
			));

			// Update view descriptor sets
			for (size_t i = 0; i < m_viewDescriptorSets.size(); ++i)
			{
				uint32_t binding = 0;
				vk::DescriptorBufferInfo descriptorBufferInfo(m_viewUniformBuffers[i].Get(), 0, sizeof(ViewUniforms));
				std::array<vk::WriteDescriptorSet, 1> writeDescriptorSets = {
					vk::WriteDescriptorSet(
						m_viewDescriptorSets[i].get(), binding, {},
						1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
					) // binding = 0
				};
				g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			}
		}

		// Create model descriptor sets
		// Ask any graphics pipeline to provide the model layout
		// since all surface materials should share this layout
		{
			// Create layouts first
			m_modelSetLayout = m_graphicsPipelines[0]->GetDescriptorSetLayout((size_t)DescriptorSetIndices::Model);
			std::vector<vk::DescriptorSetLayout> layouts = {
				m_viewSetLayout, m_modelSetLayout // sets 0, 1
			};
			m_modelPipelineLayout = g_device->Get().createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
				{}, static_cast<uint32_t>(layouts.size()), layouts.data()
			));
		}
		{
			// Then allocate one descriptor set per model
			std::vector<vk::DescriptorSetLayout> layouts(m_models.size(), m_modelSetLayout);
			auto modelDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
				m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
			));

			// and update each one
			for (size_t i = 0; i < m_models.size(); ++i)
			{

				m_models[i].descriptorSet = std::move(modelDescriptorSets[i]);

				uint32_t binding = 0;
				vk::DescriptorBufferInfo descriptorBufferInfo(m_modelUniformBuffers[i].Get(), 0, sizeof(ModelUniforms));
				std::array<vk::WriteDescriptorSet, 1> writeDescriptorSets = {
					vk::WriteDescriptorSet(
						m_models[i].descriptorSet.get(), binding, {},
						1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
					) // binding = 0
				};
				g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			}
		}

		// Create material instance descriptor sets.
		{
			for (auto& materialInstance : m_materialInstances)
			{
				auto descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
					m_descriptorPool.get(), 1, &materialInstance->material->GetDescriptorSetLayout()
				));
				materialInstance->descriptorSet = std::move(descriptorSets[0]);

				// Use the material's sampler and texture
				auto& sampler = materialInstance->uniforms.sampler;
				auto& imageView = materialInstance->uniforms.imageView;
				vk::DescriptorImageInfo imageInfo(sampler, imageView, vk::ImageLayout::eShaderReadOnlyOptimal);

				uint32_t binding = 0;
				std::array<vk::WriteDescriptorSet, 1> writeDescriptorSets = {
					vk::WriteDescriptorSet(
						materialInstance->descriptorSet.get(), binding, {},
						1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr
					)
				};
				g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			}
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

		Model model;
		model.uniforms.transform = glm::mat4(1.0f);
		//model.descriptorSet // todo: set later
		model.meshes.reserve(shapes.size());

		// At the moment assign one material per mesh to test multiple materials
		size_t materialIndex = 0; // Figure this out by name 
		for (const auto& shape : shapes)
		{
			// todo: if model needs a new material instance, create it here
			auto materialInstance = std::make_unique<MaterialInstance>();
			materialInstance->material = m_materials[materialIndex].get();
			// materialInstance.descriptorSet // set in CreateDescriptors()

			// todo: load texture referenced by model here, as we discover them
			// or take not and load later asynchroneously
			MaterialUniforms bindings;
			bindings.imageView = m_textures[0].GetImageView(); // use 0 for now
			bindings.sampler = m_samplers[0].get();
			materialInstance->uniforms = std::move(bindings);

			Mesh mesh;
			mesh.indexOffset = m_indices.size();
			mesh.nbIndices = shape.mesh.indices.size();
			mesh.materialInstance = materialInstance.get();
			model.meshes.push_back(std::move(mesh));

			m_materialInstances.push_back(std::move(materialInstance));
			m_models.push_back(std::move(model));

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

			materialIndex = (materialIndex + 1) % m_materials.size();
		}
		
		// --- Ordered draw cache --- //

		for (auto& model : m_models)
			for (auto& mesh : model.meshes)
				m_drawCache.push_back(MeshDrawInfo{ &model, &mesh });

		// Sort draw calls by material, then materialInstance, then mesh.
		// This minimizes the number of pipeline bindings (costly),
		// then descriptor set bindings (a bit less costly).
		//
		// For example (m = material, i = materialInstance, o = object mesh):
		//
		// | m0, i0, o0 | m0, i0, o1 | m0, i1, o2 | m1, i2, o3 |
		//
		std::sort(m_drawCache.begin(), m_drawCache.end(),
			[](const MeshDrawInfo& a, const MeshDrawInfo& b) {
				// Sort by material first
				if (a.mesh->materialInstance->material == b.mesh->materialInstance->material)
					return a.mesh->materialInstance->material == b.mesh->materialInstance->material;
				// Then material instance
				else if (a.mesh->materialInstance == b.mesh->materialInstance)
					return a.mesh->materialInstance < b.mesh->materialInstance;
				// Then object
				else
					return a.model < b.model;
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
			m_vertices.clear();
		}
		{
			vk::DeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();
			m_indexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer);
			memcpy(m_indexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_indices.data()), bufferSize);
			m_indexBuffer->CopyStagingToGPU(commandBuffer);

			// We won't need the staging buffer after the initial upload
			m_commandBufferPool.DestroyAfterSubmit(m_indexBuffer->ReleaseStagingBuffer());
			m_indices.clear();
		}
	}

	void UpdateUniformBuffer(uint32_t imageIndex)
	{
		using namespace std::chrono;

		vk::Extent2D extent = m_swapchain->GetImageDescription().extent;

		ViewUniforms ubo = {};
		ubo.view = camera.GetViewMatrix();
		ubo.proj = glm::perspective(glm::radians(camera.GetFieldOfView()), extent.width / (float)extent.height, 0.1f, 10.0f);

		ubo.proj[1][1] *= -1; // inverse Y for OpenGL -> Vulkan (clip coordinates)

		// Upload to GPU
		auto& uniformBuffer = m_viewUniformBuffers[imageIndex % m_commandBufferPool.GetNbConcurrentSubmits()];
		memcpy(uniformBuffer.GetMappedData(), reinterpret_cast<const void*>(&ubo), sizeof(ViewUniforms));
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
	std::vector<std::unique_ptr<GraphicsPipeline>> m_graphicsPipelines;

	// Secondary command buffers
	vk::UniqueCommandPool m_secondaryCommandPool;
	std::vector<vk::UniqueCommandBuffer> m_renderPassCommandBuffers;

	// Geometry
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<UniqueBufferWithStaging> m_indexBuffer{ nullptr };

	vk::UniqueDescriptorPool m_descriptorPool;

	// --- View --- //

	// For uniforms that change every frame
	std::vector<UniqueBuffer> m_viewUniformBuffers; // one per in flight frame since these change every frame
	
	// View descriptor sets
	vk::DescriptorSetLayout m_viewSetLayout;
	vk::UniquePipelineLayout m_viewPipelineLayout;
	std::vector<vk::UniqueDescriptorSet> m_viewDescriptorSets;

	// --- Model --- //

	// One per object (could be one big, in this case, make sure to align to minUniformBufferOffsetAlignment)
	std::vector<UniqueBuffer> m_modelUniformBuffers;

	// Model descriptor sets
	vk::DescriptorSetLayout m_modelSetLayout;
	vk::UniquePipelineLayout m_modelPipelineLayout;

	std::vector<Model> m_models;

	// --- Material --- //

	std::vector<Texture> m_textures;
	std::vector<vk::UniqueSampler> m_samplers;
	std::vector<std::unique_ptr<Material>> m_materials;
	std::vector<std::unique_ptr<MaterialInstance>> m_materialInstances;

	// Sort items to draw to minimize the number of bindings
	// Less pipeline bindings, then descriptor set bindings.
	struct MeshDrawInfo
	{
		Model* model;
		Mesh* mesh;
	};
	std::vector<MeshDrawInfo> m_drawCache;
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
