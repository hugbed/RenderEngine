#if defined(_WIN32)
#include <Windows.h>
#define _USE_MATH_DEFINES
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
#include "Skybox.h"
#include "vk_utils.h"
#include "file_utils.h"

#include "Camera.h"
#include "TextureManager.h"

#include "Grid.h"

#include <GLFW/glfw3.h>

// For Uniform Buffer
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/type_aligned.hpp>

// Scene loading
#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <iostream>
#include <cmath>

struct ViewUniforms
{
	glm::aligned_mat4 view;
	glm::aligned_mat4 proj;
	glm::aligned_vec3 pos;
};

// Array of point lights

struct ModelUniforms
{
	glm::aligned_mat4 transform;
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
	glm::vec2 texCoord;
	glm::vec3 normal;

	bool operator==(const Vertex& other) const
	{
		return pos == other.pos && texCoord == other.texCoord && normal == other.normal;
	}
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec2>()(vertex.texCoord) << 1)));
		}
	};
}

struct LitShadingConstants
{
	uint32_t nbPointLights = 1;
};

struct PhongMaterialProperties
{
	glm::aligned_vec4 diffuse;
	glm::aligned_vec4 specular;
	glm::aligned_float32 shininess;
};

struct EnvironmentMaterialProperties
{
	glm::aligned_float32 ior;
	glm::aligned_float32 metallic; // reflection {0, 1}
	glm::aligned_float32 transmission; // refraction [0..1]
};

struct LitMaterialProperties
{
	PhongMaterialProperties phong;
	EnvironmentMaterialProperties env;
};

// Each shading model can have different view descriptors
enum class ShadingModel
{
	Unlit = 0,
	Lit = 1,
	Count
};

enum class MaterialIndex
{
	TexturedUnlit = 0,
	Phong = 1,
	PhongTransparent = 2,
	Count
};

enum class FragmentShaders
{
	TexturedUnlit = (int)MaterialIndex::TexturedUnlit,
	Phong = (int)MaterialIndex::Phong,
	PhongTransparent = (int)MaterialIndex::Phong,
	Count
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

	ShadingModel shadingModel;
	bool isTransparent = false;
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

	std::string name;
	const Material* material;

	// Per-material descriptors
	std::vector<CombinedImageSampler> textures;
	std::vector<CombinedImageSampler> cubeMaps;
	std::unique_ptr<UniqueBufferWithStaging> uniformBuffer;
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

struct MeshDrawInfo
{
	Model* model;
	Mesh* mesh;
};

enum class CameraMode { OrbitCamera, FreeCamera };

class App : public RenderLoop
{
public:
	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window, std::string basePath, std::string sceneFile)
		: RenderLoop(surface, extent, window)
		, m_basePath(std::move(basePath))
		, m_sceneFilename(std::move(sceneFile))
		, m_renderPass(std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format))
		, m_framebuffers(Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get()))
		, m_vertexShader(std::make_unique<Shader>("primitive_vert.spv", "main"))
		, m_camera(1.0f * glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 45.0f, 0.01f, 1000.0f)
		, m_textureManager(std::make_unique<TextureManager>(m_basePath, &m_commandBufferPool))
	{
		m_fragmentShaders.resize((size_t)FragmentShaders::Count);
		m_fragmentShaders[(size_t)FragmentShaders::TexturedUnlit] = std::make_unique<Shader>("surface_unlit_frag.spv", "main");
		m_fragmentShaders[(size_t)FragmentShaders::Phong] = std::make_unique<Shader>("surface_frag.spv", "main");

		window.SetMouseButtonCallback(reinterpret_cast<void*>(this), OnMouseButton);
		window.SetMouseScrollCallback(reinterpret_cast<void*>(this), OnMouseScroll);
		window.SetCursorPositionCallback(reinterpret_cast<void*>(this), OnCursorPosition);
		window.SetKeyCallback(reinterpret_cast<void*>(this), OnKey);
	}

	std::string m_sceneFilename;
	std::string m_basePath;

	using RenderLoop::Init;

protected:
	glm::vec2 m_lastMousePos = glm::vec2(0.0f);
	bool m_isMouseDown = false;
	std::map<int, bool> m_keyState;

	// assimp uses +Y as the up vector
	glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);
	Camera m_camera;
	float kInitOrbitCameraRadius = 1.0f;
	CameraMode m_cameraMode = CameraMode::OrbitCamera;
	bool m_showGrid = true;

	void Init(vk::CommandBuffer& commandBuffer) override
	{
		CreateSkybox(commandBuffer);
		m_grid = std::make_unique<Grid>(*m_renderPass, m_swapchain->GetImageDescription().extent);
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
			m_skybox->Reset(*m_renderPass, m_swapchain->GetImageDescription().extent);
			m_grid->Reset(*m_renderPass, m_swapchain->GetImageDescription().extent);
			CreateBaseMaterials();
			CreateViewUniformBuffers();
			CreateDescriptorLayouts();
			UpdateMaterialDescriptors();
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
			RecordFrameRenderPassCommands(i);
		}
	}

	// To bind only when necessary
	struct RenderState
	{
		ShadingModel materialType = ShadingModel::Count;
		vk::PipelineLayout modelPipelineLayout;
		const Model* model = nullptr;
		const MaterialInstance* materialInstance = nullptr;
		const Material* material = nullptr;
	};

	void RecordFrameRenderPassCommands(uint32_t frameIndex)
	{
		auto& commandBuffer = m_renderPassCommandBuffers[frameIndex];
		vk::CommandBufferInheritanceInfo info(
			m_renderPass->Get(), 0, m_framebuffers[frameIndex].Get()
		);
		commandBuffer->begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
		{
			// --- Draw all scene opaque objects --- //
			RenderState state;

			// Draw opaque materials first
			DrawSceneObjects(commandBuffer.get(), frameIndex, m_opaqueDrawCache, state);

			//// With Skybox last (to prevent processing fragments for nothing)
			if (state.materialType != ShadingModel::Unlit)
			{
				state.materialType = ShadingModel::Unlit;
				const auto& layouts = m_layouts[(size_t)state.materialType];
				state.modelPipelineLayout = layouts.m_modelPipelineLayout.get();
				auto& viewDescriptorSet = layouts.m_viewDescriptorSets[frameIndex % m_commandBufferPool.GetNbConcurrentSubmits()].get();
				commandBuffer.get().bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics, layouts.m_viewPipelineLayout.get(),
					(uint32_t)DescriptorSetIndices::View,
					1, &viewDescriptorSet, 0, nullptr
				);
			}
			
			m_skybox->Draw(commandBuffer.get(), frameIndex);

			// Then the grid
			if (m_showGrid)
				m_grid->Draw(commandBuffer.get());

			// Draw transparent objects last (sorted by distance to camera)
			DrawSceneObjects(commandBuffer.get(), frameIndex, m_transparentDrawCache, state);
		}
		commandBuffer->end();
	}

	void DrawSceneObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, const std::vector<MeshDrawInfo>& drawCalls, RenderState& state)
	{
		// Bind the one big vertex + index buffers
		if (drawCalls.empty() == false)
		{
			vk::DeviceSize offsets[] = { 0 };
			vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
			commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
			commandBuffer.bindIndexBuffer(m_indexBuffer->Get(), 0, vk::IndexType::eUint32);
		}

		for (const auto& drawItem : drawCalls)
		{
			// Bind descriptors using material type's { view + model } layouts
			if (state.materialType != drawItem.mesh->materialInstance->material->shadingModel)
			{
				state.materialType = drawItem.mesh->materialInstance->material->shadingModel;

				const auto& layouts = m_layouts[(size_t)state.materialType];
				const auto& viewPipelineLayout = layouts.m_viewPipelineLayout.get();
				state.modelPipelineLayout = layouts.m_modelPipelineLayout.get();
				auto& viewDescriptorSet = layouts.m_viewDescriptorSets[frameIndex % m_commandBufferPool.GetNbConcurrentSubmits()].get();

				commandBuffer.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics, viewPipelineLayout,
					(uint32_t)DescriptorSetIndices::View,
					1, &viewDescriptorSet, 0, nullptr
				);
			}

			// Bind model uniforms
			if (state.model != drawItem.model)
			{
				state.model = drawItem.model;
				state.model->Bind(commandBuffer, state.modelPipelineLayout);
			}

			// Bind Graphics Pipeline
			if (drawItem.mesh->materialInstance->material != state.material)
			{
				state.material = drawItem.mesh->materialInstance->material;
				state.material->Bind(commandBuffer);
			}

			// Bind material uniforms
			if (drawItem.mesh->materialInstance != state.materialInstance)
			{
				state.materialInstance = drawItem.mesh->materialInstance;
				state.materialInstance->Bind(commandBuffer);
			}

			// Draw
			commandBuffer.drawIndexed(drawItem.mesh->nbIndices, 1, drawItem.mesh->indexOffset, 0, 0);
		}
	}

	static constexpr uint8_t kAllFrameDirty = std::numeric_limits<uint8_t>::max();
	uint8_t m_frameDirty = kAllFrameDirty;

	void RenderFrame(uint32_t imageIndex, vk::CommandBuffer commandBuffer) override
	{
		auto& framebuffer = m_framebuffers[imageIndex];

		UpdateUniformBuffer(imageIndex);

		// Record commands again if something changed
		if ((m_frameDirty & (1 << (uint8_t)imageIndex)) > 0)
		{
			RecordFrameRenderPassCommands(imageIndex);
			m_frameDirty &= ~(1 << (uint8_t)imageIndex);
		}

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
		m_secondaryCommandPool.reset();

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

	void CreateBaseMaterials()
	{
		m_graphicsPipelines.clear();
		m_graphicsPipelines.resize((size_t)MaterialIndex::Count);
		m_materials.resize((size_t)MaterialIndex::Count);
		for (auto& material : m_materials)
			if (material == nullptr)
				material = std::make_unique<Material>();

		// Create materials for opaque shading
		{
			// Textured Unlit
			{
				size_t materialIndex = (size_t)MaterialIndex::TexturedUnlit;
				Shader* fragmentShader = m_fragmentShaders[(size_t)FragmentShaders::TexturedUnlit].get();
				m_graphicsPipelines[materialIndex] = std::make_unique<GraphicsPipeline>(
					m_renderPass->Get(),
					m_swapchain->GetImageDescription().extent,
					*m_vertexShader, *fragmentShader
				);
				m_materials[materialIndex]->shadingModel = ShadingModel::Unlit;
				m_materials[materialIndex]->pipeline = m_graphicsPipelines[materialIndex].get();
			}

			// Phong
			{
				LitShadingConstants constants;
				constants.nbPointLights = m_lights.size();
				Shader* fragmentShader = m_fragmentShaders[(size_t)FragmentShaders::Phong].get();
				fragmentShader->SetSpecializationConstants(constants.nbPointLights);

				size_t materialIndex = (size_t)MaterialIndex::Phong;
				m_graphicsPipelines[materialIndex] = std::make_unique<GraphicsPipeline>(
					m_renderPass->Get(),
					m_swapchain->GetImageDescription().extent,
					*m_vertexShader, *fragmentShader
				);
				m_materials[materialIndex]->shadingModel = ShadingModel::Lit;
				m_materials[materialIndex]->pipeline = m_graphicsPipelines[materialIndex].get();
			}

			// Phong Transparent
			{
				LitShadingConstants constants;
				constants.nbPointLights = m_lights.size();
				Shader* fragmentShader = m_fragmentShaders[(size_t)FragmentShaders::PhongTransparent].get();
				fragmentShader->SetSpecializationConstants(constants.nbPointLights);

				size_t materialIndex = (size_t)MaterialIndex::PhongTransparent;
				GraphicsPipelineInfo info;
				info.blendEnable = true;
				m_graphicsPipelines[materialIndex] = std::make_unique<GraphicsPipeline>(
					m_renderPass->Get(),
					m_swapchain->GetImageDescription().extent,
					*m_vertexShader, *fragmentShader, info
				);
				m_materials[materialIndex]->isTransparent = true;
				m_materials[materialIndex]->shadingModel = ShadingModel::Lit;
				m_materials[materialIndex]->pipeline = m_graphicsPipelines[materialIndex].get();
			}
		}
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

	struct Light
	{
		glm::aligned_int32 type;
		glm::aligned_vec3 pos;
		glm::aligned_vec3 direction;
		glm::aligned_vec4 ambient;
		glm::aligned_vec4 diffuse;
		glm::aligned_vec4 specular;
		glm::aligned_float32 innerCutoff; // (cos of the inner angle)
		glm::aligned_float32 outerCutoff; // (cos of the outer angle)
	};
	std::vector<Light> m_lights;

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

	glm::vec4 ClampColor(glm::vec4 color)
	{
		float maxComponent = std::max(color.x, std::max(color.y, color.z));

		if (maxComponent > 1.0f)
			return color / maxComponent;
	
		return color;
	}

	void LoadLights(vk::CommandBuffer buffer)
	{
		for (int i = 0; i < m_assimp.scene->mNumLights; ++i)
		{
			aiLight* aLight = m_assimp.scene->mLights[i];

			aiNode* node = m_assimp.scene->mRootNode->FindNode(aLight->mName);
			glm::mat4 transform = ComputeAiNodeGlobalTransform(node);

			Light light;
			light.type = (int)aLight->mType;
			light.ambient = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // add a little until we have global illumination
			light.diffuse = ClampColor(glm::make_vec4(&aLight->mColorDiffuse.r));
			light.specular = ClampColor(glm::make_vec4(&aLight->mColorSpecular.r));
			light.pos = transform[3];

			if (aLight->mType != aiLightSource_POINT)
			{
				light.direction = glm::make_vec3(&aLight->mDirection.x);
				light.direction = glm::vec4(transform * glm::vec4(light.direction, 0.0f));
			}

			if (aLight->mType == aiLightSource_SPOT)
			{
				// falloff exponent is not correctly read from collada
				// so set outer angle to 120% of inner angle for now
				light.innerCutoff = std::cos(aLight->mAngleInnerCone);
				light.outerCutoff = std::cos(aLight->mAngleInnerCone * 1.20f);
			}

			m_lights.push_back(std::move(light));
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
		{
			for (auto& mesh : model.meshes)
			{
				if (mesh.materialInstance->material->isTransparent == false)
					m_opaqueDrawCache.push_back(MeshDrawInfo{ &model, &mesh });
				else
					m_transparentDrawCache.push_back(MeshDrawInfo{ &model, &mesh });
			}
		}

		// Sort opaqe draw calls by material, then materialInstance, then mesh.
		// This minimizes the number of pipeline bindings (costly),
		// then descriptor set bindings (a bit less costly).
		//
		// For example (m = material, i = materialInstance, o = object mesh):
		//
		// | m0, i0, o0 | m0, i0, o1 | m0, i1, o2 | m1, i2, o3 |
		//
		std::sort(m_opaqueDrawCache.begin(), m_opaqueDrawCache.end(),
			[](const MeshDrawInfo& a, const MeshDrawInfo& b) {
				// Sort by material type
				if (a.mesh->materialInstance->material->shadingModel != b.mesh->materialInstance->material->shadingModel)
					return a.mesh->materialInstance->material->shadingModel < b.mesh->materialInstance->material->shadingModel;
				// Then by material
				else if (a.mesh->materialInstance->material != b.mesh->materialInstance->material)
					return a.mesh->materialInstance->material < b.mesh->materialInstance->material;
				// Then material instance
				else if (a.mesh->materialInstance != b.mesh->materialInstance)
					return a.mesh->materialInstance < b.mesh->materialInstance;
				// Then model
				else
					return a.model < b.model;
			});

		// Transparent materials need to be sorted by distance every time the camera moves
	}

	void SortTransparentObjects()
	{
		glm::mat4 viewInverse = glm::inverse(m_camera.GetViewMatrix());
		glm::vec3 cameraPosition = viewInverse[3]; // m_camera.GetPosition();
		glm::vec3 front = viewInverse[2]; // todo: m_camera.GetForwardVector();

		// todo: assign 64 bit number to each MeshDrawInfo for sorting and
		// use this here also instead of copy pasting the sorting logic here.
		std::sort(m_transparentDrawCache.begin(), m_transparentDrawCache.end(),
			[&cameraPosition, &front](const MeshDrawInfo& a, const MeshDrawInfo& b) {
				glm::vec3 dx_a = cameraPosition - glm::vec3(a.model->GetTransform()[3]);
				glm::vec3 dx_b = cameraPosition - glm::vec3(b.model->GetTransform()[3]);
				float distA = glm::dot(front, dx_a);
				float distB = glm::dot(front, dx_b);

				// Sort by distance first
				if (distA != distB)
					return distA > distB; // back to front
				// Then by material type
				if (a.mesh->materialInstance->material->shadingModel != b.mesh->materialInstance->material->shadingModel)
					return a.mesh->materialInstance->material->shadingModel < b.mesh->materialInstance->material->shadingModel;
				// Then by material
				else if (a.mesh->materialInstance->material != b.mesh->materialInstance->material)
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
		CreateBaseMaterials();

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

			auto& assimpMaterial = m_assimp.scene->mMaterials[i];

			aiString name;
			assimpMaterial->Get(AI_MATKEY_NAME, name);
			material->name = std::string(name.C_Str());

			// Properties
			LitMaterialProperties properties;

			aiColor4D color;
			assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color);
			properties.phong.diffuse = glm::make_vec4(&color.r);

			assimpMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color);
			properties.phong.specular = glm::make_vec4(&color.r);

			assimpMaterial->Get(AI_MATKEY_SHININESS, properties.phong.shininess);

			// If a material is transparent, opacity will be fetched
			// from the diffuse alpha channel (material property * diffuse texture)
			float opacity = 1.0f;
			assimpMaterial->Get(AI_MATKEY_OPACITY, opacity);

			auto materialIndex = opacity == 1.0f ? MaterialIndex::Phong : MaterialIndex::PhongTransparent;
			material->material = m_materials[(size_t)materialIndex].get();

			// Create default textures
			const auto& bindings = material->material->pipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::Material);
			for (const auto& binding : bindings)
			{
				if (binding.descriptorType == vk::DescriptorType::eCombinedImageSampler)
				{
					for (int i = 0; i < binding.descriptorCount; ++i)
					{
						CombinedImageSampler texture = m_textureManager->LoadTexture(commandBuffer, "dummy_texture.png");
						material->textures.push_back(std::move(texture));
					}
				}
			}

			// Load textures
			auto loadTexture = [this, &assimpMaterial, &material, &commandBuffer](aiTextureType type, uint32_t binding) { // todo: move this to a function
				int textureCount = assimpMaterial->GetTextureCount(type);
				if (textureCount > 0 && binding < material->textures.size())
				{
					aiString textureFile;
					assimpMaterial->GetTexture(type, 0, &textureFile);
					auto texture = m_textureManager->LoadTexture(commandBuffer, textureFile.C_Str());
					material->textures[binding] = std::move(texture); // replace dummy with real texture
				}
			};
			// todo: use sRGB format for color textures if necessary
			// it looks like gamma correction is OK for now but it might
			// not be the case for all textures
			loadTexture(aiTextureType_DIFFUSE, 0);
			loadTexture(aiTextureType_SPECULAR, 1);

			// Environment mapping
			assimpMaterial->Get(AI_MATKEY_REFRACTI, properties.env.ior);
			properties.env.metallic = 1.0f;
			properties.env.transmission = 0.0f;

			CombinedImageSampler skyboxCubeMap = m_skybox->GetCubeMap();
			if (skyboxCubeMap.texture != nullptr)
				material->cubeMaps.push_back(std::move(skyboxCubeMap));

			// Upload properties to uniform buffer
			material->uniformBuffer = std::make_unique<UniqueBufferWithStaging>(sizeof(LitMaterialProperties), vk::BufferUsageFlagBits::eUniformBuffer);
			memcpy(material->uniformBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(&properties), sizeof(LitMaterialProperties));
			material->uniformBuffer->CopyStagingToGPU(commandBuffer);
			m_commandBufferPool.DestroyAfterSubmit(material->uniformBuffer->ReleaseStagingBuffer());
		}
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
		if (m_lights.empty() == false)
		{
			vk::DeviceSize bufferSize = m_lights.size() * sizeof(Light);
			m_lightsUniformBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer);
			memcpy(m_lightsUniformBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_lights.data()), bufferSize);
			m_lightsUniformBuffer->CopyStagingToGPU(commandBuffer);

			// We won't need the staging buffer after the initial upload
			m_commandBufferPool.DestroyAfterSubmit(m_lightsUniformBuffer->ReleaseStagingBuffer());
		}
	}

	void CreateDescriptorPool()
	{
		// Reset used descriptors while they're still valid
		for (size_t i = 0; i < m_models.size(); ++i)
			m_models[i].descriptorSet.reset();

		for (auto& materialInstance : m_materialInstances)
			materialInstance->descriptorSet.reset();

		// Then reset pool
		m_descriptorPool.reset();

		// Sum view descriptor needs for all material types.
		// Each material can need different view parameters (e.g. Unlit doesn't need lights).
		// Need a set of descriptors per concurrent frame
		std::map<vk::DescriptorType, uint32_t> descriptorCount;
		for (const auto& material : m_materials)
		{
			const auto& bindings = material->pipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::View);
			for (const auto& binding : bindings)
			{
				descriptorCount[binding.descriptorType] += binding.descriptorCount * m_commandBufferPool.GetNbConcurrentSubmits();
			}
		}

		// Sum model descriptors
		for (const auto& model : m_models)
		{
			// Pick model layout from any material, they should be compatible
			const auto* pipeline = model.meshes[0].materialInstance->material->pipeline;
			const auto& bindings = pipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::Model);
			for (const auto& binding : bindings)
			{
				descriptorCount[binding.descriptorType] += binding.descriptorCount;
			}
		}

		// Sum material instance descriptors
		for (const auto& materialInstance : m_materialInstances)
		{
			// Each material can have different descriptor set layout
			// Each material instance has its own descriptor sets
			const auto* pipeline = materialInstance->material->pipeline;
			const auto& bindings = pipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::Material);
			for (const auto& binding : bindings)
			{
				descriptorCount[binding.descriptorType] += binding.descriptorCount;
			}
		}

		uint32_t maxNbSets = 0;
		std::vector<vk::DescriptorPoolSize> poolSizes;
		poolSizes.reserve(descriptorCount.size());
		for (const auto& descriptor : descriptorCount)
		{
			poolSizes.emplace_back(descriptor.first, descriptor.second);
			maxNbSets += descriptor.second;
		}

		// If the number of required descriptors were to change at run-time
		// we could have a descriptorPool per concurrent frame and reset the pool
		// to increase its size while it's not in use.
		m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			maxNbSets,
			static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
		));
	}

	// todo: There must be a better way to keep track of the number of sets to allocate.
	// we can probably get descriptor sets requirements for each material from reflection
	// and multiply that by the number of instances of this material.
	void CreateDescriptorSets()
	{
		CreateDescriptorPool();
		CreateDescriptorLayouts();
		UpdateMaterialDescriptors();
	}

	void CreateDescriptorLayouts()
	{
		// View layout and descriptor sets
		for (size_t materialType = 0; materialType < (size_t)ShadingModel::Count; ++materialType)
		{
			auto& layout = m_layouts[materialType];

			layout.m_viewSetLayout = m_graphicsPipelines[materialType]->GetDescriptorSetLayout((size_t)DescriptorSetIndices::View);

			std::vector<vk::DescriptorSetLayout> layouts(m_commandBufferPool.GetNbConcurrentSubmits(), layout.m_viewSetLayout);
			layout.m_viewDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
				m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
			));

			layout.m_viewPipelineLayout = g_device->Get().createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
				{}, 1, &layout.m_viewSetLayout
			));
		}

		// Model layout
		if (m_graphicsPipelines.empty() == false)
		{
			// All materials share the same descriptor set for model
			m_modelSetLayout = m_graphicsPipelines[0]->GetDescriptorSetLayout((size_t)DescriptorSetIndices::Model);
		}

		for (size_t materialType = 0; materialType < (size_t)ShadingModel::Count; ++materialType)
		{
			auto& layout = m_layouts[materialType];

			std::vector<vk::DescriptorSetLayout> layouts = {
				layout.m_viewSetLayout, m_modelSetLayout // sets 0, 1
			};

			layout.m_modelPipelineLayout = g_device->Get().createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
				{}, static_cast<uint32_t>(layouts.size()), layouts.data()
			));
		}
	}

	void UpdateMaterialDescriptors()
	{
		// Create view descriptor set.
		// Ask any graphics pipeline to provide the view layout
		// since all surface materials should share this layout
		for (size_t materialType = 0; materialType < (size_t)ShadingModel::Count; ++materialType)
		{
			auto& viewDescriptorSets = m_layouts[materialType].m_viewDescriptorSets;

			// Update view descriptor sets
			for (size_t i = 0; i < viewDescriptorSets.size(); ++i)
			{
				uint32_t binding = 0;
				vk::DescriptorBufferInfo descriptorBufferInfoView(m_viewUniformBuffers[i].Get(), 0, sizeof(ViewUniforms));
				std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
					vk::WriteDescriptorSet(
						viewDescriptorSets[i].get(), binding++, {},
						1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoView
					) // binding = 0
				};

				if ((ShadingModel)materialType == ShadingModel::Lit)
				{
					vk::DescriptorBufferInfo descriptorBufferInfoLights(m_lightsUniformBuffer->Get(), 0, sizeof(Light) * m_lights.size());
					writeDescriptorSets.push_back(
						vk::WriteDescriptorSet(
							viewDescriptorSets[i].get(), binding++, {},
							1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoLights
						) // binding = 1
					);
				}

				g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			}
		}

		// Create model descriptor sets
		// Ask any graphics pipeline to provide the model layout
		// since all material of this type should share this layout
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
		for (auto& materialInstance : m_materialInstances)
		{
			auto descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
				m_descriptorPool.get(), 1, &materialInstance->material->GetDescriptorSetLayout()
			));
			materialInstance->descriptorSet = std::move(descriptorSets[0]);

			uint32_t binding = 0;
			std::vector<vk::WriteDescriptorSet> writeDescriptorSets;

			// Material properties in uniform buffer
			vk::DescriptorBufferInfo descriptorBufferInfo(materialInstance->uniformBuffer->Get(), 0, materialInstance->uniformBuffer->Size());
			writeDescriptorSets.push_back(
				vk::WriteDescriptorSet(
					materialInstance->descriptorSet.get(), binding++, {},
					1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
				) // binding = 0
			);

			// Material's textures
			std::vector<vk::DescriptorImageInfo> imageInfos;
			if (materialInstance->textures.empty() == false)
			{
				for (const auto& materialTexture : materialInstance->textures)
				{
					imageInfos.emplace_back(
						materialTexture.sampler,
						materialTexture.texture->GetImageView(),
						vk::ImageLayout::eShaderReadOnlyOptimal
					);
				}
				writeDescriptorSets.push_back(
					vk::WriteDescriptorSet(
						materialInstance->descriptorSet.get(), binding++, {},
						static_cast<uint32_t>(imageInfos.size()), vk::DescriptorType::eCombinedImageSampler, imageInfos.data(), nullptr
					) // binding = 1
				);
			}

			// Material's cubemaps on a separate binding
			std::vector<vk::DescriptorImageInfo> cubemapsInfo;
			if (materialInstance->cubeMaps.empty() == false)
			{
				for (const auto& materialTexture : materialInstance->cubeMaps)
				{
					cubemapsInfo.emplace_back(
						materialTexture.sampler,
						materialTexture.texture->GetImageView(),
						vk::ImageLayout::eShaderReadOnlyOptimal
					);
				}
				writeDescriptorSets.push_back(
					vk::WriteDescriptorSet(
						materialInstance->descriptorSet.get(), binding++, {},
						static_cast<uint32_t>(cubemapsInfo.size()), vk::DescriptorType::eCombinedImageSampler, cubemapsInfo.data(), nullptr
					) // binding = 2
				);
			}

			g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
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
		ubo.proj = glm::perspective(glm::radians(m_camera.GetFieldOfView()), extent.width / (float)extent.height, m_camera.GetNearPlane(), m_camera.GetFarPlane());
		ubo.pos = m_camera.GetEye();

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

	void CreateSkybox(vk::CommandBuffer commandBuffer)
	{
		m_skybox.reset();
		m_skybox = std::make_unique<Skybox>(*m_renderPass, m_swapchain->GetImageDescription().extent);
		m_skybox->UploadToGPU(m_textureManager.get(), commandBuffer);
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

	template <typename T>
	static T sgn(T val) 
	{
		return (T(0) < val) - (val < T(0));
	}

	static void OnCursorPosition(void* data, double xPos, double yPos)
	{
		App* app = reinterpret_cast<App*>(data);
		int width = 0;
		int height = 0;
		app->m_window.GetSize(&width, &height);

		if ((app->m_isMouseDown) && app->m_cameraMode == CameraMode::OrbitCamera) 
		{
			glm::vec4 position(app->m_camera.GetEye().x, app->m_camera.GetEye().y, app->m_camera.GetEye().z, 1);
			glm::vec4 target(app->m_camera.GetLookAt().x, app->m_camera.GetLookAt().y, app->m_camera.GetLookAt().z, 1);

			float deltaAngle = (M_PI / 300.0f);
			float xDeltaAngle = (app->m_lastMousePos.x - xPos) * deltaAngle;
			float yDeltaAngle = (app->m_lastMousePos.y - yPos) * deltaAngle;

			float cosAngle = dot(app->m_camera.GetForwardVector(), app->m_upVector);
			if (cosAngle * sgn(yDeltaAngle) > 0.99f)
				yDeltaAngle = 0;

			// Rotate in X
			glm::mat4x4 rotationMatrixX(1.0f);
			rotationMatrixX = glm::rotate(rotationMatrixX, xDeltaAngle, app->m_upVector);
			position = (rotationMatrixX * (position - target)) + target;

			// Rotate in Y
			glm::mat4x4 rotationMatrixY(1.0f);
			rotationMatrixY = glm::rotate(rotationMatrixY, yDeltaAngle, app->m_camera.GetRightVector());
			glm::vec3 finalPositionV3 = (rotationMatrixY * (position - target)) + target;

			app->m_camera.SetCameraView(finalPositionV3, app->m_camera.GetLookAt(), app->m_upVector);

			// We need to recompute transparent object order if camera changes
			if (app->m_transparentDrawCache.size() > 0)
			{
				app->SortTransparentObjects();
				app->m_frameDirty = kAllFrameDirty;
			}
		}
		else if (app->m_isMouseDown && app->m_cameraMode == CameraMode::FreeCamera) 
		{
			float speed = 0.0000001;

			auto dt_s = app->GetDeltaTime();
			float dx = speed * dt_s.count();

			float diffX = app->m_lastMousePos.x - xPos;
			float diffY = app->m_lastMousePos.y - yPos;

			float m_fovV = app->m_camera.GetFieldOfView() / width * height;

			float angleX = diffX / width * app->m_camera.GetFieldOfView();
			float angleY = diffY / height * m_fovV;

			auto lookat = app->m_camera.GetLookAt() - app->m_camera.GetUpVector() * dx * angleY;

			glm::vec3 forward = glm::normalize(lookat - app->m_camera.GetEye());
			glm::vec3 rightVector = glm::normalize(glm::cross(forward, app->m_camera.GetUpVector()));

			app->m_camera.LookAt(lookat + rightVector * dx * angleX);
		}
		app->m_lastMousePos.x = xPos; 
		app->m_lastMousePos.y = yPos;
	}

	static void OnKey(void* data, int key, int scancode, int action, int mods) {
		App* app = reinterpret_cast<App*>(data);
		app->m_keyState[key] = action == GLFW_PRESS ? true : action == GLFW_REPEAT ? true : false;

		if (key == GLFW_KEY_F && action == GLFW_PRESS) 
		{
			if (app->m_cameraMode == CameraMode::FreeCamera) 
			{
				app->LoadCamera();
			}
			app->m_cameraMode = app->m_cameraMode == CameraMode::FreeCamera ? CameraMode::OrbitCamera : CameraMode::FreeCamera;
		}
		if (key == GLFW_KEY_G && action == GLFW_PRESS)
		{
			app->m_showGrid = !app->m_showGrid;
			app->m_frameDirty = kAllFrameDirty;
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
	std::vector<vk::UniqueCommandBuffer> m_helpersCommandBuffers;

	// Geometry
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<UniqueBufferWithStaging> m_indexBuffer{ nullptr };

	vk::UniqueDescriptorPool m_descriptorPool;

	// --- Layouts --- //

	// Each material type may need different scene
	// uniforms (e.g. unlit shading does not need lights).
	struct DescriptorLayouts
	{
		// set 0
		vk::DescriptorSetLayout m_viewSetLayout;
		vk::UniquePipelineLayout m_viewPipelineLayout;
		std::vector<vk::UniqueDescriptorSet> m_viewDescriptorSets;

		// set 1
		vk::UniquePipelineLayout m_modelPipelineLayout;
	};
	std::array<DescriptorLayouts, (size_t)ShadingModel::Count> m_layouts;

	// --- View --- //

	std::vector<UniqueBuffer> m_viewUniformBuffers; // one per in flight frame since these change every frame
	std::unique_ptr<UniqueBufferWithStaging> m_lightsUniformBuffer;

	// --- Model --- //

	// All material types use the same descriptor set layout for models
	vk::DescriptorSetLayout m_modelSetLayout;
	std::vector<Model> m_models;

	// --- Material --- //

	std::vector<std::unique_ptr<Material>> m_materials;
	std::vector<std::unique_ptr<MaterialInstance>> m_materialInstances;

	std::unique_ptr<Skybox> m_skybox;
	std::unique_ptr<Grid> m_grid;

	// Sort items to draw to minimize the number of bindings
	// Less pipeline bindings, then descriptor set bindings.
	std::vector<MeshDrawInfo> m_opaqueDrawCache;
	std::vector<MeshDrawInfo> m_transparentDrawCache;

	// Texture cache and image loading utility
	std::unique_ptr<TextureManager> m_textureManager;
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
		App app(surface.get(), extent, window, std::move(basePath), std::move(sceneFile));
		app.Init();
		app.Run();
	}
	Device::Term();
	PhysicalDevice::Term();

	return 0;
}
