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
#include "file_utils.h"

#include "Camera.h"

#include <GLFW/glfw3.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>

// For Uniform Buffer
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define UNIFORM_ALIGNED alignas(16)

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/gtx/norm.hpp>

// For Texture
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Scene loading
#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include <chrono>
#include <unordered_map>
#include <iostream>

struct ViewUniforms
{
	UNIFORM_ALIGNED glm::mat4 view;
	UNIFORM_ALIGNED glm::mat4 proj;
	UNIFORM_ALIGNED glm::vec3 dir;
};

// Array of point lights

struct ModelUniforms
{
	UNIFORM_ALIGNED glm::mat4 transform;
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
	glm::vec3 normal;

	bool operator==(const Vertex& other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord && normal == other.normal;
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

struct MaterialProperties
{
	UNIFORM_ALIGNED glm::vec3 ambient;
	UNIFORM_ALIGNED glm::vec3 diffuse;
	UNIFORM_ALIGNED glm::vec3 specular;
	float opacity;
	float shininess;
};

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

struct MaterialTexture
{
	uint32_t binding = 0;
	Texture* texture = nullptr;
	vk::Sampler sampler = nullptr;
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
	
	std::string name;
	MaterialProperties properties;
	std::vector<MaterialTexture> textures;

	// Per-material descriptors
	std::unique_ptr<UniqueBufferWithStaging> uniformBuffer; // for properties
	vk::UniqueDescriptorSet descriptorSet; 
};

struct Mesh
{
	vk::DeviceSize indexOffset;
	vk::DeviceSize nbIndices;
	const MaterialInstance* materialInstance;
};

struct Model
{
	Model()
		: uniformBuffer(std::make_unique<UniqueBuffer>(
			vk::BufferCreateInfo(
				{}, sizeof(ModelUniforms), vk::BufferUsageFlagBits::eUniformBuffer
			), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
		))
	{
		memcpy(uniformBuffer->GetMappedData(), &transform, sizeof(glm::mat4));
	}

	void SetTransform(glm::mat4 transform)
	{
		this->transform = std::move(transform);
		memcpy(uniformBuffer->GetMappedData(), &this->transform, sizeof(glm::mat4));
	}

	glm::mat4& GetTransform() { return transform; }

	void Bind(vk::CommandBuffer& commandBuffer, vk::PipelineLayout modelPipelineLayout) const
	{
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			modelPipelineLayout,
			(uint32_t)DescriptorSetIndices::Model,
			1, &descriptorSet.get(), 0, nullptr
		);
	}

	std::unique_ptr<UniqueBuffer> uniformBuffer;

	// A model as multiple parts (meshes)
	std::vector<Mesh> meshes;

	// Per-object descriptors
	vk::UniqueDescriptorSet descriptorSet;

private:
	glm::mat4 transform = glm::mat4(1.0f);
};

enum CameraMode { OrbitCamera, FreeCamera };

class App : public RenderLoop
{
public:
	enum class FragmentShaders
	{
		Unlit = 0,
		Lit,
		Count
	};
	struct LitShaderConstants
	{
		uint32_t nbPointLights = 1;
	};

	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
		: RenderLoop(surface, extent, window)
		, m_renderPass(std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format))
		, m_framebuffers(Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get()))
		, m_vertexShader(std::make_unique<Shader>("mvp_vert.spv", "main"))
		, m_camera(1.0f * glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 45.0f)
	{
		m_fragmentShaders.resize((size_t)FragmentShaders::Count);
		m_fragmentShaders[(size_t)FragmentShaders::Unlit] = std::make_unique<Shader>("surface_unlit_frag.spv", "main");
		m_fragmentShaders[(size_t)FragmentShaders::Lit] = std::make_unique<Shader>("surface_frag.spv", "main");

		window.SetMouseButtonCallback(reinterpret_cast<void*>(this), OnMouseButton);
		window.SetMouseScrollCallback(reinterpret_cast<void*>(this), OnMouseScroll);
		window.SetCursorPositionCallback(reinterpret_cast<void*>(this), OnCursorPosition);
		window.SetKeyCallback(reinterpret_cast<void*>(this), OnKey);
	}

	std::string m_sceneFilename;
	std::string m_basePath;

	using RenderLoop::Init;

protected:
	glm::vec2 m_mouseDownPos = glm::vec2(0.0f);
	bool m_isMouseDown = false;
	std::map<int, bool> m_keyState;

	// assimp uses +Y as the up vector
	glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);
	Camera m_camera;
	float kInitOrbitCameraRadius = 1.0f;
	CameraMode m_cameraMode = CameraMode::OrbitCamera;

	void Init(vk::CommandBuffer& commandBuffer) override
	{
		LoadScene(commandBuffer, m_basePath + "/" + m_sceneFilename);
		UploadGeometry(commandBuffer);

		CreateViewUniformBuffers();
		CreateLightsUniformBuffers(commandBuffer);
		CreateDescriptorSets();

		CreateSecondaryCommandBuffers();
		RecordRenderPassCommands();
	}

	// Render pass commands are recorded once and executed every frame
	void OnSwapchainRecreated() override
	{
		// Reset resources that depend on the swapchain images
		m_graphicsPipelines.clear();
		m_framebuffers.clear();
		m_renderPass.reset();

		m_renderPass = std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format);
		m_framebuffers = Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get());

		// --- Recreate everything that depends on the number of images ---

		// Use any command buffer for init
		auto commandBuffer = m_commandBufferPool.ResetAndGetCommandBuffer();
		commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		{
			LoadMaterials(commandBuffer);
			CreateViewUniformBuffers();
			CreateDescriptorSets();
		}
		commandBuffer.end();

		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		m_commandBufferPool.Submit(submitInfo);
		m_commandBufferPool.WaitUntilSubmitComplete();

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
				const Material* material = nullptr;
				for (const auto& drawItem : m_drawCache)
				{
					// Bind model uniforms
					if (model != drawItem.model)
					{
						model = drawItem.model;
						model->Bind(commandBuffer.get(), m_modelPipelineLayout.get());
					}

					// Bind Graphics Pipeline
					if (drawItem.mesh->materialInstance->material != material)
					{
						material = drawItem.mesh->materialInstance->material;
						material->Bind(commandBuffer.get());
					}

					// Bind material uniforms
					if (drawItem.mesh->materialInstance != materialInstance)
					{
						materialInstance = drawItem.mesh->materialInstance;
						materialInstance->Bind(commandBuffer.get());
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

	// Keep this data available in case of Swapchain reset
	struct AssimpData
	{
		std::unique_ptr<Assimp::Importer> importer = nullptr;
		const aiScene* scene = nullptr;
	}
	m_assimp;

	void LoadScene(vk::CommandBuffer commandBuffer, const std::string filename)
	{
		int flags = aiProcess_Triangulate
			| aiProcess_GenNormals
			| aiProcess_JoinIdenticalVertices;

		m_assimp.importer = std::make_unique<Assimp::Importer>();
		m_assimp.scene = m_assimp.importer->ReadFile(filename.c_str(), 0);
		if (m_assimp.scene == nullptr)
		{
			std::cout << m_assimp.importer->GetErrorString() << std::endl;
			throw std::runtime_error("Cannot load scene, file not found or parsing failed");
		}

		LoadLights(commandBuffer);
		LoadMaterials(commandBuffer);
		LoadSceneNodes(commandBuffer);
		LoadCamera();
	}

	struct PointLight
	{
		UNIFORM_ALIGNED glm::vec3 pos;
		UNIFORM_ALIGNED glm::vec3 colorDiffuse;
		UNIFORM_ALIGNED glm::vec3 colorSpecular;
		UNIFORM_ALIGNED glm::vec3 attenuation; // const, linear, quadratic
	};
	std::vector<PointLight> m_pointLights;

	glm::mat4 ComputeAiNodeGlobalTransform(const aiNode* node)
	{
		// Convert from row-major (aiMatrix4x4) to column-major (glm::mat4)
		glm::mat4 transform = glm::transpose(glm::make_mat4(&node->mTransformation.a1));

		// Apply all parents in inverse order
		while (node->mParent != nullptr)
		{
			node = node->mParent;
			glm::mat4 parentTransform = glm::transpose(glm::make_mat4(&node->mTransformation.a1));
			transform = parentTransform * transform;
		}

		return transform;
	}

	void LoadCamera()
	{
		if (m_assimp.scene->mNumCameras > 0)
		{
			aiCamera* camera = m_assimp.scene->mCameras[0];

			aiNode* node = m_assimp.scene->mRootNode->FindNode(camera->mName);
			glm::mat4 transform = ComputeAiNodeGlobalTransform(node);
			glm::vec3 pos = transform[3];
			glm::vec3 lookat = glm::vec3(0.0f);
			glm::vec3 up = transform[1];
			m_camera.SetCameraView(pos, lookat, up);

			m_camera.SetFieldOfView(camera->mHorizontalFOV * 180 / M_PI * 2);
		}
		else
		{
			// Init camera to see the model
			kInitOrbitCameraRadius = m_maxVertexDist * 15.0f;
			m_camera.SetCameraView(glm::vec3(kInitOrbitCameraRadius, kInitOrbitCameraRadius, kInitOrbitCameraRadius), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
		}
	}

	glm::vec3 ClampColor(glm::vec3 color)
	{
		return color / std::max(color.x, std::max(color.y, color.z));
	}

	void LoadLights(vk::CommandBuffer buffer)
	{
		for (int i = 0; i < m_assimp.scene->mNumLights; ++i)
		{
			aiLight* light = m_assimp.scene->mLights[i];

			aiNode* node = m_assimp.scene->mRootNode->FindNode(light->mName);
			glm::mat4 transform = ComputeAiNodeGlobalTransform(node);

			PointLight pointLight;
			pointLight.pos = transform[3];
			pointLight.colorDiffuse = ClampColor(glm::make_vec3(&light->mColorDiffuse.r));
			pointLight.colorSpecular = ClampColor(glm::make_vec3(&light->mColorSpecular.r));
			pointLight.attenuation = glm::vec3(1.0f, 0.0f, 0.0001f); // todo: use gltf2 range

			m_pointLights.push_back(std::move(pointLight));
		}
	}

	float m_maxVertexDist = 0.0f;

	void LoadSceneNodes(vk::CommandBuffer commandBuffer)
	{
		m_vertices.clear();
		m_indices.clear();
		m_maxVertexDist = 0.0f;

		LoadNodeAndChildren(m_assimp.scene->mRootNode, glm::mat4(1.0f));

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
				if (a.mesh->materialInstance->material != b.mesh->materialInstance->material)
					return a.mesh->materialInstance->material < b.mesh->materialInstance->material;
				// Then material instance
				else if (a.mesh->materialInstance != b.mesh->materialInstance)
					return a.mesh->materialInstance < b.mesh->materialInstance;
				// Then model
				else
					return a.model < b.model;
			});
	}

	void LoadNodeAndChildren(aiNode* node, glm::mat4 transform)
	{
		// Convert from row-major (aiMatrix4x4) to column-major (glm::mat4)
		// Note: don't know if all formats supported by assimp are row-major but Collada is.
		glm::mat4 nodeTransform = glm::transpose(glm::make_mat4(&node->mTransformation.a1));

		glm::mat4 newTransform = transform * nodeTransform;

		if (node->mNumMeshes > 0)
		{
			// Create a new model if it has mesh(es)
			Model model;
			LoadMeshes(*node, model);
			model.SetTransform(newTransform);
			m_models.push_back(std::move(model));
		}

		for (int i = 0; i < node->mNumChildren; ++i)
			LoadNodeAndChildren(node->mChildren[i], newTransform);
	}

	void LoadMeshes(const aiNode& fileNode, Model& model)
	{
		model.meshes.reserve(m_assimp.scene->mNumMeshes);
		for (size_t i = 0; i < fileNode.mNumMeshes; ++i)
		{
			aiMesh* aMesh = m_assimp.scene->mMeshes[fileNode.mMeshes[i]];

			Mesh mesh;
			mesh.indexOffset = m_indices.size();
			mesh.nbIndices = (vk::DeviceSize)aMesh->mNumFaces * aMesh->mFaces->mNumIndices;
			mesh.materialInstance = m_materialInstances[aMesh->mMaterialIndex].get();
			size_t vertexIndexOffset = m_vertices.size();

			bool hasUV = aMesh->HasTextureCoords(0);
			bool hasColor = aMesh->HasVertexColors(0);
			bool hasNormals = aMesh->HasNormals();

			for (size_t v = 0; v < aMesh->mNumVertices; ++v)
			{
				Vertex vertex;
				vertex.pos = glm::make_vec3(&aMesh->mVertices[v].x);
				vertex.texCoord = hasUV ? glm::make_vec2(&aMesh->mTextureCoords[0][v].x) : glm::vec2(0.0f);
				vertex.texCoord.y = -vertex.texCoord.y;
				vertex.normal = hasNormals ? glm::make_vec3(&aMesh->mNormals[v].x) : glm::vec3(0.0f);
				vertex.color = hasColor ? glm::make_vec3(&aMesh->mColors[0][v].r) : glm::vec3(1.0f);

				m_maxVertexDist = (std::max)(m_maxVertexDist, glm::length(vertex.pos - glm::vec3(0.0f)));

				m_vertices.push_back(vertex);
			}

			for (size_t f = 0; f < aMesh->mNumFaces; ++f)
			{
				for (size_t fi = 0; fi < aMesh->mFaces->mNumIndices; ++fi)
					m_indices.push_back(aMesh->mFaces[f].mIndices[fi] + vertexIndexOffset);
			}

			model.meshes.push_back(std::move(mesh));
		}
	}

	void LoadMaterials(vk::CommandBuffer commandBuffer)
	{
		m_graphicsPipelines.clear();

		// Create one material for all solid shading
		{
			while (m_materials.size() < 1)
				m_materials.push_back(std::make_unique<Material>());

			// Choose between Lit/Unlit fragment shader depending if there are lights or not
			if (m_pointLights.empty() == false)
			{
				Shader* fragmentShader = m_fragmentShaders[(size_t)FragmentShaders::Lit].get();
				LitShaderConstants constants = { (uint32_t)m_pointLights.size() };
				fragmentShader->SetSpecializationConstants(constants);
				m_graphicsPipelines.push_back(std::make_unique<GraphicsPipeline>(
					m_renderPass->Get(),
					m_swapchain->GetImageDescription().extent,
					*m_vertexShader,
					*fragmentShader
				));
			}
			else
			{
				m_graphicsPipelines.push_back(std::make_unique<GraphicsPipeline>(
					m_renderPass->Get(),
					m_swapchain->GetImageDescription().extent,
					*m_vertexShader,
					*m_fragmentShaders[(size_t)FragmentShaders::Unlit]
				));
			}

			m_materials.back()->pipeline = m_graphicsPipelines.back().get();
		}

		// Check if we already have all materials set-up
		if (m_materialInstances.size() == m_assimp.scene->mNumMaterials)
			return;

		// Create a material instance per material description in the scene
		// todo: eventually create materials according to the needs of materials in the scene
		// to support different types of materials
		m_materialInstances.resize(m_assimp.scene->mNumMaterials);
		for (size_t i = 0; i < m_materialInstances.size(); ++i)
		{
			m_materialInstances[i].reset();
			m_materialInstances[i] = std::make_unique<MaterialInstance>();
			auto& material = m_materialInstances[i];

			// todo: choose or create material/permutation here if we need to
			material->material = m_materials.back().get();

			auto& assimpMaterial = m_assimp.scene->mMaterials[i];

			aiString name;
			assimpMaterial->Get(AI_MATKEY_NAME, name);
			material->name = std::string(name.C_Str());
			
			// Properties
			aiColor4D color;
			assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color);
			material->properties.diffuse = glm::make_vec4(&color.r);

			// Use darker diffuse as ambient to prevent the scene from being too dark
			material->properties.ambient = material->properties.diffuse * 0.1f;

			assimpMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color);
			material->properties.specular = glm::make_vec4(&color.r);

			assimpMaterial->Get(AI_MATKEY_SHININESS, material->properties.shininess);
			assimpMaterial->Get(AI_MATKEY_OPACITY, material->properties.opacity);

			//if ((material->properties.opacity) > 0.0f)
			//	material->properties.specular = glm::vec4(0.0f);

			// Upload properties to uniform buffer
			material->uniformBuffer = std::make_unique<UniqueBufferWithStaging>(sizeof(MaterialProperties), vk::BufferUsageFlagBits::eUniformBuffer);
			memcpy(material->uniformBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(&material->properties), sizeof(MaterialProperties));
			material->uniformBuffer->CopyStagingToGPU(commandBuffer);
			m_commandBufferPool.DestroyAfterSubmit(material->uniformBuffer->ReleaseStagingBuffer());

			// Textures

			if (assimpMaterial->GetTextureCount(aiTextureType_DIFFUSE) > 0)
			{
				aiString textureFile;
				assimpMaterial->GetTexture(aiTextureType_DIFFUSE, 0, &textureFile);
				auto texture = LoadMaterialTexture(commandBuffer, m_basePath + "/" + std::string(textureFile.C_Str()));
				texture.binding = 0; // diffuse
				material->textures.push_back(std::move(texture));
			}
			else
			{
				// Load dummy texture
				auto texture = LoadMaterialTexture(commandBuffer, "dummy_texture.png");
				texture.binding = 0; // diffuse
				material->textures.push_back(std::move(texture));
			}

			// todo: load other texture types
		}
	}

	MaterialTexture LoadMaterialTexture(vk::CommandBuffer& commandBuffer, const std::string filename)
	{
		// todo: set base path to load textures from
		// or load automatically next to the model
		Texture* texture = CreateAndUploadTextureImage(commandBuffer, filename);
		vk::Sampler sampler = CreateSampler(texture->GetMipLevels());
		return MaterialTexture{ 0, texture, sampler };
	}

	void CreateViewUniformBuffers()
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
	}

	void CreateLightsUniformBuffers(vk::CommandBuffer commandBuffer)
	{
		if (m_pointLights.empty() == false)
		{
			vk::DeviceSize bufferSize = m_pointLights.size() * sizeof(PointLight);
			m_lightsUniformBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer);
			memcpy(m_lightsUniformBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_pointLights.data()), bufferSize);
			m_lightsUniformBuffer->CopyStagingToGPU(commandBuffer);

			// We won't need the staging buffer after the initial upload
			m_commandBufferPool.DestroyAfterSubmit(m_lightsUniformBuffer->ReleaseStagingBuffer());
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
			nbViews + nbModels + nbMaterials
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
				vk::DescriptorBufferInfo descriptorBufferInfoView(m_viewUniformBuffers[i].Get(), 0, sizeof(ViewUniforms));
				std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
					vk::WriteDescriptorSet(
						m_viewDescriptorSets[i].get(), binding++, {},
						1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoView
					) // binding = 0
				};

				if (m_pointLights.empty() == false)
				{
					vk::DescriptorBufferInfo descriptorBufferInfoLights(m_lightsUniformBuffer->Get(), 0, sizeof(PointLight) * m_pointLights.size());
					writeDescriptorSets.push_back(
						vk::WriteDescriptorSet(
							m_viewDescriptorSets[i].get(), binding++, {},
							1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoLights
						) // binding = 1
					);
				}

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
				vk::DescriptorBufferInfo descriptorBufferInfo(m_models[i].uniformBuffer->Get(), 0, sizeof(ModelUniforms));
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

				// Material properties in uniform buffer
				vk::DescriptorBufferInfo descriptorBufferInfo(materialInstance->uniformBuffer->Get(), 0, sizeof(MaterialProperties));

				// Use the material's sampler and texture
				auto& sampler = materialInstance->textures[0].sampler;
				auto& imageView = materialInstance->textures[0].texture->GetImageView();
				vk::DescriptorImageInfo imageInfo(sampler, imageView, vk::ImageLayout::eShaderReadOnlyOptimal);

				uint32_t binding = 0;
				std::array<vk::WriteDescriptorSet, 2> writeDescriptorSets = {
					vk::WriteDescriptorSet(
						materialInstance->descriptorSet.get(), binding++, {},
						1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
					), // binding = 0
					vk::WriteDescriptorSet(
						materialInstance->descriptorSet.get(), binding++, {},
						1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr
					) // binding = 1
				};
				g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			}
		}
	}

	Texture* CreateAndUploadTextureImage(vk::CommandBuffer& commandBuffer, const std::string filename)
	{
		// Check if we already loaded this texture
		auto& cachedTexture = m_textures.find(filename);
		if (cachedTexture != m_textures.end())
			return cachedTexture->second.get();

		// Read image from file
		int texWidth = 0, texHeight = 0, texChannels = 0;
		stbi_uc* pixels = stbi_load(filename.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0) {
			throw std::runtime_error("failed to load texture image!");
		}

		uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

		// Texture image
		auto [pair, res] = m_textures.emplace(filename, std::make_unique<Texture>(
			texWidth, texHeight, 4UL, // R8G8B8A8, depth = 4
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | // src and dst for mipmaps blit
				vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eColor,
			mipLevels
		));
		auto& texture = pair->second;

		memcpy(texture->GetStagingMappedData(), reinterpret_cast<const void*>(pixels), texWidth * texHeight * 4UL);
		texture->UploadStagingToGPU(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal);

		// We won't need the staging buffer after the initial upload
		auto* stagingBuffer = texture->ReleaseStagingBuffer();
		m_commandBufferPool.DestroyAfterSubmit(stagingBuffer);

		stbi_image_free(pixels);

		return texture.get();
	}

	vk::Sampler CreateSampler(uint32_t nbMipLevels)
	{
		// Check if we already have a sampler
		auto samplerIt = m_samplers.find(nbMipLevels);
		if (samplerIt != m_samplers.end())
			return samplerIt->second.get();

		auto [pair, wasInserted] = m_samplers.emplace(nbMipLevels,
			g_device->Get().createSamplerUnique(vk::SamplerCreateInfo(
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
				static_cast<float>(nbMipLevels), // maxLod
				vk::BorderColor::eIntOpaqueBlack, // borderColor
				false // unnormalizedCoordinates
			)
		));
		return pair->second.get();
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
		ubo.view = m_camera.GetViewMatrix();
		ubo.proj = glm::perspective(glm::radians(m_camera.GetFieldOfView()), extent.width / (float)extent.height, 0.1f, 30000.0f);
		ubo.dir = m_camera.GetLookAt() - m_camera.GetEye();

		// OpenGL -> Vulkan invert y, half z
		auto clip = glm::mat4(
			1.0f,  0.0f, 0.0f, 0.0f,
			0.0f, -1.0f, 0.0f, 0.0f,
			0.0f,  0.0f, 0.5f, 0.0f,
			0.0f,  0.0f, 0.0f, 1.0f
		);
		ubo.proj *= clip;

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
			if (key.second && m_cameraMode == CameraMode::FreeCamera) 
			{
				glm::vec3 forward = glm::normalize(m_camera.GetLookAt() - m_camera.GetEye());
				glm::vec3 rightVector = glm::normalize(glm::cross(forward, m_camera.GetUpVector()));
				float dx = speed * dt_s.count(); // in m / s

				switch (key.first) {
					case GLFW_KEY_W:
						m_camera.MoveCamera(forward, dx, false);
						break;
					case GLFW_KEY_A:
						m_camera.MoveCamera(rightVector, -dx, true);
						break;
					case GLFW_KEY_S:
						m_camera.MoveCamera(forward, -dx, false);
						break;
					case GLFW_KEY_D:
						m_camera.MoveCamera(rightVector, dx, true);
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
			app->m_isMouseDown = true;
		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
			app->m_isMouseDown = false;
	}

	static void OnMouseScroll(void* data, double xOffset, double yOffset)
	{
		App* app = reinterpret_cast<App*>(data);
		float fov = std::clamp(app->m_camera.GetFieldOfView() - yOffset, 30.0, 130.0);
		app->m_camera.SetFieldOfView(fov);
	}

	static void OnCursorPosition(void* data, double xPos, double yPos)
	{
		App* app = reinterpret_cast<App*>(data);
		int width = 0;
		int height = 0;
		app->m_window.GetSize(&width, &height);

		if ((app->m_isMouseDown) && app->m_cameraMode == CameraMode::OrbitCamera) 
		{
			glm::vec3 rightVector = app->m_camera.GetRightVector();
			glm::vec4 position(app->m_camera.GetEye().x, app->m_camera.GetEye().y, app->m_camera.GetEye().z, 1);
			glm::vec4 target(app->m_camera.GetLookAt().x, app->m_camera.GetLookAt().y, app->m_camera.GetLookAt().z, 1);

			float dist = glm::distance2(app->m_upVector, glm::normalize(app->m_camera.GetEye() - app->m_camera.GetLookAt()));

			float xAngle = (app->m_mouseDownPos.x - xPos) * (M_PI/300);
			float yAngle = (app->m_mouseDownPos.y - yPos) * (M_PI/300);

			if (dist < 0.01 && yAngle < 0 || 4.0 - dist < 0.01 && yAngle > 0)
				yAngle = 0;

			glm::mat4x4 rotationMatrixY(1.0f);
			rotationMatrixY = glm::rotate(rotationMatrixY, yAngle, rightVector);

			glm::mat4x4 rotationMatrixX(1.0f);
			rotationMatrixX = glm::rotate(rotationMatrixX, xAngle, app->m_upVector);

			position = (rotationMatrixX * (position - target)) + target;

			glm::vec3 finalPositionV3 = (rotationMatrixY * (position - target)) + target;

			app->m_camera.SetCameraView(finalPositionV3, app->m_camera.GetLookAt(), app->m_upVector);
		}
		else if (app->m_isMouseDown && app->m_cameraMode == CameraMode::FreeCamera) 
		{
			float speed = 0.0000001;

			auto dt_s = app->GetDeltaTime();
			float dx = speed * dt_s.count();

			float diffX = app->m_mouseDownPos.x - xPos;
			float diffY = app->m_mouseDownPos.y - yPos;

			float m_fovV = app->m_camera.GetFieldOfView() / width * height;

			float angleX = diffX / width * app->m_camera.GetFieldOfView();
			float angleY = diffY / height * m_fovV;

			auto lookat = app->m_camera.GetLookAt() - app->m_camera.GetUpVector() * dx * angleY;

			glm::vec3 forward = glm::normalize(lookat - app->m_camera.GetEye());
			glm::vec3 rightVector = glm::normalize(glm::cross(forward, app->m_camera.GetUpVector()));

			app->m_camera.LookAt(lookat + rightVector * dx * angleX);
		}
		app->m_mouseDownPos.x = xPos; 
		app->m_mouseDownPos.y = yPos;
	}

	static void OnKey(void* data, int key, int scancode, int action, int mods) {
		App* app = reinterpret_cast<App*>(data);
		app->m_keyState[key] = action == GLFW_PRESS ? true : action == GLFW_REPEAT ? true : false;

		if (key == GLFW_KEY_F && action == GLFW_PRESS) {
			if (app->m_cameraMode == CameraMode::FreeCamera) 
			{
				app->LoadCamera();
			}
			app->m_cameraMode = app->m_cameraMode == CameraMode::FreeCamera ? CameraMode::OrbitCamera : CameraMode::FreeCamera;
		}
	}

private:
	std::unique_ptr<RenderPass> m_renderPass;
	std::vector<Framebuffer> m_framebuffers;
	std::unique_ptr<Shader> m_vertexShader;
	std::vector<std::unique_ptr<Shader>> m_fragmentShaders;
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
	std::unique_ptr<UniqueBufferWithStaging> m_lightsUniformBuffer;
	
	// View descriptor sets
	vk::DescriptorSetLayout m_viewSetLayout;
	vk::UniquePipelineLayout m_viewPipelineLayout;
	std::vector<vk::UniqueDescriptorSet> m_viewDescriptorSets;

	// --- Model --- //

	// Model descriptor sets
	vk::DescriptorSetLayout m_modelSetLayout;
	vk::UniquePipelineLayout m_modelPipelineLayout;
	std::vector<Model> m_models;

	// --- Material --- //

	std::map<std::string, std::unique_ptr<Texture>> m_textures;
	std::map<uint32_t, vk::UniqueSampler> m_samplers; // per mip level
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

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		std::cout << "Missing argument(s), expecting '\"path/to/assets/\" \"scene_file.dae\"'" << std::endl;
		return 1;
	}
	
	std::string basePath = argv[1];
	std::string sceneFile = argv[2];

	vk::Extent2D extent(800, 600);
	Window window(extent, "Vulkan");
	window.SetInputMode(GLFW_STICKY_KEYS, GLFW_TRUE);

	Instance instance(window);
	vk::UniqueSurfaceKHR surface(window.CreateSurface(instance.Get()));

	PhysicalDevice::Init(instance.Get(), surface.get());
	Device::Init(*g_physicalDevice);
	{
		App app(surface.get(), extent, window);
		app.m_basePath = std::move(basePath);
		app.m_sceneFilename = std::move(sceneFile);
		app.Init();
		app.Run();
	}
	Device::Term();
	PhysicalDevice::Term();

	return 0;
}
