#pragma once

#include <Renderer/TextureSystem.h>
#include <Renderer/MaterialSystem.h>
#include <Renderer/LightSystem.h>
#include <Renderer/ShadowSystem.h>
#include <Renderer/RenderState.h>
#include <Renderer/Skybox.h>
#include <Renderer/Bindless.h>
#include <Renderer/Grid.h>
#include <Camera.h>

#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include "glm_includes.h"

#include <gsl/pointers>
#include <iostream>

class ShadowSystem;
class SceneTree;

// todo (hbedard): create an importer for assimp files, and add a Scene class in Renderer
class AssimpScene
{
public:
	AssimpScene(
		std::string basePath,
		std::string sceneFilename,
		CommandBufferPool& commandBufferPool,
		GraphicsPipelineSystem& graphicsPipelineSystem,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams,
		TextureSystem& textureSystem,
		MeshAllocator& meshAllocator,
		LightSystem& lightSystem,
		SurfaceLitMaterialSystem& materialSystem,
		ShadowSystem& shadowSystem,
		SceneTree& sceneTree,
		Grid& grid,
		const RenderPass& renderPass, vk::Extent2D imageExtent
	);

	void Load(vk::CommandBuffer commandBuffer);

	void Reset(vk::CommandBuffer& commandBuffer, const RenderPass& renderPass, vk::Extent2D imageExtent);

	void Update(uint32_t imageIndex);

	void BeginRender(RenderState& renderState, uint32_t frameIndex);
	void DrawOpaqueObjects(RenderState& renderState) const;
	void EndRender();

	bool HasTransparentObjects() const { return m_transparentDrawCache.empty() == false; }
	void SortTransparentObjects();
	void DrawTransparentObjects(RenderState& renderState) const;

	Camera& GetCamera() { return m_camera; } // todo: there should be a camera control or something

	const Camera& GetCamera() const { return m_camera; } // todo: there should be a camera control or something

	// todo: take the world bounding box from the scene tree
	BoundingBox GetBoundingBox() const { return m_boundingBox; }

	void ResetCamera();

	const std::vector<MeshDrawInfo>& GetOpaqueDrawCommands() const { return m_opaqueDrawCache; }
	const std::vector<MeshDrawInfo>& GetTransparentDrawCommands() const { return m_transparentDrawCache; }

	const BindlessDrawParams& GetBindlessDrawParams() const
	{
		assert(m_bindlessDrawParams != nullptr);
		return *m_bindlessDrawParams;
	}

private:
	// Uses scene materials
	void DrawSceneObjects(RenderState& state, const std::vector<MeshDrawInfo>& drawCalls) const;

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

	uint32_t m_concurrentFrameIndex = 0;

	// --- Camera --- //

	const glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);
	float kInitOrbitCameraRadius = 1.0f;
	Camera m_camera;
	
	// --- Lights --- //

	gsl::not_null<LightSystem*> m_lightSystem;

	// --- View --- //

	LitViewProperties m_viewUniforms;
	std::vector<UniqueBuffer> m_viewUniformBuffers; // one per in flight frame since these change every frame

	// --- Scene --- //
	
	BoundingBox m_boundingBox;

	float m_maxVertexDist = 0.0f;
	gsl::not_null<MeshAllocator*> m_meshAllocator;
	gsl::not_null<SceneTree*> m_sceneTree;
	gsl::not_null<Grid*> m_grid;

	// --- Materials --- ///

	gsl::not_null<TextureSystem*> m_textureSystem;
	gsl::not_null<SurfaceLitMaterialSystem*> m_materialSystem;
	std::vector<MaterialHandle> m_materials;

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

	std::vector<BufferHandle> m_viewBufferHandles;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
};
