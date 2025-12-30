#pragma once

#include <Renderer/TextureCache.h>
#include <Renderer/SurfaceLitMaterialSystem.h>
#include <Renderer/LightSystem.h>
#include <Renderer/ShadowSystem.h>
#include <Renderer/RenderState.h>
#include <Renderer/Skybox.h>
#include <Renderer/Bindless.h>
#include <Renderer/Grid.h>
#include <Renderer/Camera.h>
#include <Renderer/ViewProperties.h>

#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <glm_includes.h>
#include <gsl/pointers>

#include <iostream>

class Renderer;
class RenderScene;
class ShadowSystem;
class SceneTree;
class Grid;

// todo (hbedard): create an importer for assimp files, and add a Scene class in Renderer
class AssimpScene
{
public:
	AssimpScene(
		std::string basePath,
		std::string sceneFilename,
		Renderer& renderer
	);

	void Load(vk::CommandBuffer commandBuffer);

	void Reset(vk::CommandBuffer& commandBuffer, const RenderPass& renderPass, vk::Extent2D imageExtent);

	// todo: take the world bounding box from the scene tree
	BoundingBox GetBoundingBox() const { return m_boundingBox; }

	void ResetCamera();

private:
	RenderScene& GetRenderScene();

	// Uses scene materials
	void LoadScene(vk::CommandBuffer commandBuffer);
	void LoadLights(vk::CommandBuffer buffer);
	void LoadCamera();
	void LoadSceneNodes(vk::CommandBuffer commandBuffer);
	void LoadNodeAndChildren(aiNode* node, glm::mat4 transform);
	SceneNodeHandle LoadSceneNode(const aiNode& fileNode, glm::mat4 transform);
	void LoadMaterials(vk::CommandBuffer commandBuffer);

private:
	gsl::not_null<Renderer*> m_renderer;
	std::string m_sceneFilename;
	std::string m_sceneDir;
	uint32_t m_concurrentFrameIndex = 0;

	const glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);
	float kInitOrbitCameraRadius = 1.0f;
	BoundingBox m_boundingBox;
	float m_maxVertexDist = 0.0f;
	std::vector<MaterialHandle> m_materials;

	struct AssimpData
	{
		std::unique_ptr<Assimp::Importer> importer = nullptr;
		const aiScene* scene = nullptr;
	} m_assimp;
};
