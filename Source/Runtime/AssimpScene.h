#pragma once

#include "TextureSystem.h"
#include "MaterialSystem.h"
#include "LightSystem.h"
#include "ShadowSystem.h"
#include "RenderState.h"
#include "Camera.h"
#include "Skybox.h"

#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include "glm_includes.h"

#include <gsl/pointers>

#include <iostream>

class ShadowSystem;
class SceneTree;

class AssimpScene
{
public:
	AssimpScene(
		std::string basePath,
		std::string sceneFilename,
		CommandBufferPool& commandBufferPool,
		GraphicsPipelineSystem& graphicsPipelineSystem,
		TextureSystem& textureSystem,
		MeshAllocator& meshAllocator,
		LightSystem& lightSystem,
		MaterialSystem& materialSystem,
		ShadowSystem& shadowSystem,
		SceneTree& sceneTree,
		const RenderPass& renderPass, vk::Extent2D imageExtent
	);

	void Load(vk::CommandBuffer commandBuffer);

	void Reset(vk::CommandBuffer& commandBuffer, const RenderPass& renderPass, vk::Extent2D imageExtent);

	void Update(uint32_t imageIndex);

	void DrawOpaqueObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const;
	
	bool HasTransparentObjects() const { return m_transparentDrawCache.empty() == false; }
	void SortTransparentObjects();
	void DrawTransparentObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const;

	Camera& GetCamera() { return m_camera; } // todo: there should be a camera control or something

	const Camera& GetCamera() const { return m_camera; } // todo: there should be a camera control or something

	// todo: take the world bounding box from the scene tree
	BoundingBox GetBoundingBox() const { return m_boundingBox; }

	void ResetCamera();

	void UpdateMaterialDescriptors();

	const std::vector<MeshDrawInfo>& GetOpaqueDrawCommands() const { return m_opaqueDrawCache; }
	const std::vector<MeshDrawInfo>& GetTransparentDrawCommands() const { return m_transparentDrawCache; }

private:
	// Uses scene materials
	void DrawSceneObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& state, const std::vector<MeshDrawInfo>& drawCalls) const;

	void LoadScene(vk::CommandBuffer commandBuffer);
	void LoadLights(vk::CommandBuffer buffer);
	void LoadCamera();
	void LoadSceneNodes(vk::CommandBuffer commandBuffer);
	void LoadNodeAndChildren(aiNode* node, glm::mat4 transform);
	SceneNodeID LoadSceneNode(const aiNode& fileNode, glm::mat4 transform);
	void LoadMaterials(vk::CommandBuffer commandBuffer);

	void CreateViewUniformBuffers();

	UniqueBuffer& GetViewUniformBuffer(uint32_t imageIndex);

private:
	CommandBufferPool* m_commandBufferPool{ nullptr };
	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;

	vk::UniqueDescriptorPool m_descriptorPool;

	std::string m_sceneFilename;
	std::string m_sceneDir;

	const RenderPass* m_renderPass;
	vk::Extent2D m_imageExtent;

	// --- Camera --- //

	const glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);
	float kInitOrbitCameraRadius = 1.0f;
	Camera m_camera;
	
	// --- Lights --- //

	gsl::not_null<LightSystem*> m_lightSystem;

	// --- View --- //

	LitViewProperties m_viewUniforms;
	std::vector<vk::UniqueDescriptorSet> m_unlitViewDescriptorSets;
	std::vector<UniqueBuffer> m_viewUniformBuffers; // one per in flight frame since these change every frame

	// --- Scene --- //
	
	BoundingBox m_boundingBox;

	float m_maxVertexDist = 0.0f;
	gsl::not_null<MeshAllocator*> m_meshAllocator;
	gsl::not_null<SceneTree*> m_sceneTree;

	// --- Materials --- ///

	gsl::not_null<TextureSystem*> m_textureSystem;
	gsl::not_null<MaterialSystem*> m_materialSystem;
	std::vector<MaterialInstanceID> m_materials; // todo: don't need that

	// ---

	gsl::not_null<ShadowSystem*> m_shadowSystem;

	struct AssimpData
	{
		std::unique_ptr<Assimp::Importer> importer = nullptr;
		const aiScene* scene = nullptr;
	} m_assimp;

	std::vector<MeshDrawInfo> m_opaqueDrawCache;
	std::vector<MeshDrawInfo> m_transparentDrawCache;

	std::unique_ptr<Skybox> m_skybox;
};
