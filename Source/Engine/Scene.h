#pragma once

#include "TextureCache.h"
#include "MaterialCache.h"
#include "RenderState.h"
#include "Camera.h"
#include "Skybox.h"

#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include "glm_includes.h"

#include <iostream>

struct ViewUniforms
{
	glm::aligned_mat4 view;
	glm::aligned_mat4 proj;
	glm::aligned_vec3 pos;
};

enum class CameraMode { OrbitCamera, FreeCamera };

// todo: these should be somewhere else
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

// todo: this is pretty much phong material properties?
struct LitMaterialProperties
{
	PhongMaterialProperties phong;
	EnvironmentMaterialProperties env;
};

// todo: this belongs somewhere else
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

struct MeshDrawInfo
{
	Model* model;
	Mesh* mesh;
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

class Scene
{
public:
	Scene(
		std::string basePath,
		std::string sceneFilename,
		CommandBufferPool& commandBufferPool,
		const RenderPass& renderPass, vk::Extent2D imageExtent
	);

	void Load(vk::CommandBuffer commandBuffer);

	void Reset(vk::CommandBuffer& commandBuffer, const RenderPass& renderPass, vk::Extent2D imageExtent);

	void Update(uint32_t imageIndex);

	void DrawOpaqueObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState);
	
	bool HasTransparentObjects() const { return m_transparentDrawCache.empty() == false; }
	void SortTransparentObjects();
	void DrawTransparentObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState);

	Camera& GetCamera() { return m_camera; } // todo: there should be a camera control or something

	void ResetCamera();

private:
	void DrawSceneObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& state, const std::vector<MeshDrawInfo>& drawCalls);

	void LoadScene(vk::CommandBuffer commandBuffer);
	void LoadLights(vk::CommandBuffer buffer);
	void LoadCamera();
	void LoadSceneNodes(vk::CommandBuffer commandBuffer);
	void LoadNodeAndChildren(aiNode* node, glm::mat4 transform);
	void LoadMeshes(const aiNode& fileNode, Model& model);
	void LoadMaterials(vk::CommandBuffer commandBuffer);

	void CreateLightsUniformBuffers(vk::CommandBuffer commandBuffer);
	void CreateViewUniformBuffers();

	void CreateDescriptorPool();
	void CreateDescriptorSets();
	void CreateDescriptorLayouts();
	void UpdateMaterialDescriptors();

	void UploadToGPU(vk::CommandBuffer& commandBuffer);

	UniqueBuffer& GetViewUniformBuffer(uint32_t imageIndex);

private:
	CommandBufferPool* m_commandBufferPool{ nullptr };

	vk::UniqueDescriptorPool m_descriptorPool;

	std::string m_sceneFilename;
	std::string m_basePath;

	const RenderPass* m_renderPass;
	vk::Extent2D m_imageExtent;

	// --- Camera --- //

	const glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);
	float kInitOrbitCameraRadius = 1.0f;
	Camera m_camera;
	
	// --- Lights --- //

	std::vector<Light> m_lights;

	// --- View --- //

	using ViewDescriptorSets = std::array<std::vector<vk::UniqueDescriptorSet>, (size_t)ShadingModel::Count>;

	ViewDescriptorSets m_viewDescriptorSets;
	std::vector<UniqueBuffer> m_viewUniformBuffers; // one per in flight frame since these change every frame
	std::unique_ptr<UniqueBufferWithStaging> m_lightsUniformBuffer;

	// --- Model --- //
	
	float m_maxVertexDist = 0.0f;
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<UniqueBufferWithStaging> m_indexBuffer{ nullptr };

	std::vector<Model> m_models;

	// --- Materials --- ///

	std::unique_ptr<TextureCache> m_textureCache{ nullptr };
	std::unique_ptr<MaterialCache> m_materialCache{ nullptr };
	std::vector<Material*> m_materials;

	// ---

	struct AssimpData
	{
		std::unique_ptr<Assimp::Importer> importer = nullptr;
		const aiScene* scene = nullptr;
	} m_assimp;

	std::vector<MeshDrawInfo> m_opaqueDrawCache;
	std::vector<MeshDrawInfo> m_transparentDrawCache;

	std::unique_ptr<Skybox> m_skybox;
};
