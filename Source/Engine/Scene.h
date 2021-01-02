#pragma once

#include "TextureCache.h"
#include "Material.h"
#include "RenderState.h"
#include "Camera.h"
#include "Skybox.h"
#include "Light.h"

#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include "glm_includes.h"

#include <gsl/pointers>

#include <iostream>

class ShadowMap;

struct ViewUniforms
{
	glm::aligned_mat4 view;
	glm::aligned_mat4 proj;
	glm::aligned_vec3 pos;
};

struct ShadowData
{
	glm::aligned_mat4 transform;
};

enum class CameraMode { OrbitCamera, FreeCamera };

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
		GraphicsPipelineSystem& graphicsPipelineSystem,
		TextureCache& textureCache,
		ModelSystem& modelSystem,
		MaterialSystem& materialSystem,
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
	void DrawAllWithoutShading(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, vk::PipelineLayout pipelineLayout) const;

	Camera& GetCamera() { return m_camera; } // todo: there should be a camera control or something

	const Camera& GetCamera() const { return m_camera; } // todo: there should be a camera control or something

	BoundingBox GetBoundingBox() const { return m_boundingBox; }

	void ResetCamera();

	const std::vector<Light>& GetLights() const { return m_lights; }

	void InitShadowMaps(const std::vector<const ShadowMap*>& shadowMaps);

	void UpdateShadowMapsTransforms(const std::vector<glm::mat4>& shadowMaps);

	void UpdateMaterialDescriptors();

private:
	// Uses scene materials
	void DrawSceneObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& state, const std::vector<MeshDrawInfo>& drawCalls) const;

	void DrawWithoutShading(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, vk::PipelineLayout pipelineLayout, const std::vector<MeshDrawInfo>& drawCalls) const;

	void LoadScene(vk::CommandBuffer commandBuffer);
	void LoadLights(vk::CommandBuffer buffer);
	void LoadCamera();
	void LoadSceneNodes(vk::CommandBuffer commandBuffer);
	void LoadNodeAndChildren(aiNode* node, glm::mat4 transform);
	ModelID LoadModel(const aiNode& fileNode, glm::mat4 transform);
	void LoadMaterials(vk::CommandBuffer commandBuffer);

	void CreateLightsUniformBuffers(vk::CommandBuffer commandBuffer);
	void CreateViewUniformBuffers();

	void UploadToGPU(vk::CommandBuffer& commandBuffer);

	UniqueBuffer& GetViewUniformBuffer(uint32_t imageIndex);

private:
	CommandBufferPool* m_commandBufferPool{ nullptr };
	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;

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

	ViewUniforms m_viewUniforms;
	std::vector<vk::UniqueDescriptorSet> m_unlitViewDescriptorSets;
	std::vector<UniqueBuffer> m_viewUniformBuffers; // one per in flight frame since these change every frame
	std::unique_ptr<UniqueBufferWithStaging> m_lightsUniformBuffer;

	// --- Model --- //
	
	BoundingBox m_boundingBox;

	// Should be in model system?
	float m_maxVertexDist = 0.0f;
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<UniqueBufferWithStaging> m_indexBuffer{ nullptr };

	gsl::not_null<ModelSystem*> m_modelSystem;

	// --- Materials --- ///

	gsl::not_null<TextureCache*> m_textureCache;
	gsl::not_null<MaterialSystem*> m_materialSystem;
	std::vector<MaterialInstanceID> m_materials; // todo: don't need that

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
