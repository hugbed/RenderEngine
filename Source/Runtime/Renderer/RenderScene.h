#pragma once

#include <vulkan/vulkan.hpp>
#include <gsl/pointers>

#include <memory>

class CameraViewSystem;
class Grid;
class LightSystem;
class MeshAllocator;
struct MeshDrawInfo;
class Renderer;
class RenderCommandEncoder;
class SceneTree;
class Skybox;
class ShadowSystem;
class MaterialSystem;

class RenderScene
{
public:
	RenderScene(Renderer& renderer);
	~RenderScene();

	void Init();
	void Reset();
	void UploadToGPU();
	void Update();
	void Render();
	
	gsl::not_null<MeshAllocator*> GetMeshAllocator() const { return m_meshAllocator.get(); }
	gsl::not_null<SceneTree*> GetSceneTree() const { return m_sceneTree.get(); }
	gsl::not_null<LightSystem*> GetLightSystem() const { return m_lightSystem.get(); }
	gsl::not_null<ShadowSystem*> GetShadowSystem() const { return m_shadowSystem.get(); }
	gsl::not_null<CameraViewSystem*> GetCameraViewSystem() const { return m_cameraViewSystem.get(); }
	gsl::not_null<MaterialSystem*> GetMaterialSystem() const { return m_materialSystem.get(); }
	gsl::not_null<Grid*> GetGrid() const { return m_grid.get(); }
	gsl::not_null<Skybox*> GetSkybox() const { return m_skybox.get(); }

	vk::CommandBuffer GetBasePassCommandBuffer() const;

private:
	gsl::not_null<Renderer*> m_renderer;

	std::unique_ptr<MeshAllocator> m_meshAllocator;
	std::unique_ptr<SceneTree> m_sceneTree;
	std::unique_ptr<LightSystem> m_lightSystem;
	std::unique_ptr<ShadowSystem> m_shadowSystem;
	std::unique_ptr<CameraViewSystem> m_cameraViewSystem;
	std::unique_ptr<MaterialSystem> m_materialSystem;
	std::unique_ptr<Grid> m_grid;
	std::unique_ptr<Skybox> m_skybox;

	// This secondary command buffer is not really necessary but is a good example so let's keep it for now
	vk::UniqueCommandPool m_commandPool;
	std::vector<vk::UniqueCommandBuffer> m_basePassCommandBuffers; // contains render commands

	std::vector<MeshDrawInfo> m_opaqueMeshes;
	std::vector<MeshDrawInfo> m_translucentMeshes;
	bool m_areShadowsDirty = true; // render shadows once on start

	void CreateBasePassCommandBuffers();

	void PopulateMeshDrawCalls();
	void SortOpaqueMeshes();
	void SortTranslucentMeshes();

	void RenderShadowDepthPass() const;
	void RenderBasePass() const;
	void RenderBasePassMeshes(RenderCommandEncoder& renderCommandEncoder, const std::vector<MeshDrawInfo>& drawCalls) const;
};
