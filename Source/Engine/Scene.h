#pragma once

#include "TextureCache.h"
#include "MaterialCache.h"
#include "RenderState.h"
#include "Camera.h"
#include "Skybox.h"
#include "Light.h"

#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include "glm_includes.h"

#include <iostream>

class ShadowMap;

struct ViewUniforms
{
	glm::aligned_mat4 view;
	glm::aligned_mat4 proj;
	glm::aligned_vec3 pos;
	glm::aligned_mat4 lightTransform;
	glm::aligned_int32 shadowMapIndex = 0;
};

struct ShadowData
{
	glm::aligned_mat4 transform;
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

	void DrawOpaqueObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const;
	
	bool HasTransparentObjects() const { return m_transparentDrawCache.empty() == false; }
	void SortTransparentObjects();
	void DrawTransparentObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const;

	// Assumes external pipeline is already bound.
	// Binds vertices + view + model descriptors and calls draw for each mesh
	void DrawAllWithoutShading(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, RenderState& renderState) const;

	Camera& GetCamera() { return m_camera; } // todo: there should be a camera control or something

	const Camera& GetCamera() const { return m_camera; } // todo: there should be a camera control or something

	BoundingBox GetBoundingBox() const { return m_boundingBox; }

	void ResetCamera();

	const std::vector<Light>& GetLights() const { return m_lights; }

	void UpdateShadowMaps(const std::vector<const ShadowMap*>& shadowMaps);

	void SetShadowCaster(vk::CommandBuffer& commandBuffer, const glm::mat4& transform, uint32_t shadowMapIndex, uint32_t imageIndex);

private:
	// Uses scene materials
	void DrawSceneObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& state, const std::vector<MeshDrawInfo>& drawCalls) const;

	void DrawWithoutShading(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, RenderState& state, const std::vector<MeshDrawInfo>& drawCalls) const;

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

	std::unique_ptr<UniqueBuffer> m_shadowDataBuffer;

	// --- View --- //

	using ViewDescriptorSets = std::array<std::vector<vk::UniqueDescriptorSet>, (size_t)ShadingModel::Count>;

	ViewUniforms m_viewUniforms;
	ViewDescriptorSets m_viewDescriptorSets;
	std::vector<UniqueBuffer> m_viewUniformBuffers; // one per in flight frame since these change every frame
	std::unique_ptr<UniqueBufferWithStaging> m_lightsUniformBuffer;

	// --- Model --- //
	
	BoundingBox m_boundingBox;

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
