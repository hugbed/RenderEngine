#pragma once

#include <gsl/pointers>

#include <memory>

class CameraViewSystem;
class Grid;
class LightSystem;
class MeshAllocator;
struct MeshDrawInfo;
class Renderer;
class RenderState;
class SceneTree;
class Skybox;
class ShadowSystem;
class SurfaceLitMaterialSystem;

namespace vk
{
	class CommandBuffer;
}

class RenderScene
{
public:
	RenderScene(Renderer& renderer);
	~RenderScene();

	void Init(vk::CommandBuffer commandBuffer);
	void Reset(vk::CommandBuffer commandBuffer);
	void UploadToGPU();
	// todo (hbedard): reconsider passing this and abstract this away
	void Update(uint32_t concurrentFrameIndex);
	void Render(RenderState& renderState, uint32_t concurrentFrameIndex);

	gsl::not_null<MeshAllocator*> GetMeshAllocator() const { return m_meshAllocator.get(); }
	gsl::not_null<SceneTree*> GetSceneTree() const { return m_sceneTree.get(); }
	gsl::not_null<LightSystem*> GetLightSystem() const { return m_lightSystem.get(); }
	gsl::not_null<ShadowSystem*> GetShadowSystem() const { return m_shadowSystem.get(); }
	gsl::not_null<CameraViewSystem*> GetCameraViewSystem() const { return m_cameraViewSystem.get(); }
	gsl::not_null<SurfaceLitMaterialSystem*> GetMaterialSystem() const { return m_materialSystem.get(); }
	gsl::not_null<Grid*> GetGrid() const { return m_grid.get(); }
	gsl::not_null<Skybox*> GetSkybox() const { return m_skybox.get(); }

private:
	gsl::not_null<Renderer*> m_renderer;
	std::unique_ptr<MeshAllocator> m_meshAllocator;
	std::unique_ptr<SceneTree> m_sceneTree;
	std::unique_ptr<LightSystem> m_lightSystem;
	std::unique_ptr<ShadowSystem> m_shadowSystem;
	std::unique_ptr<CameraViewSystem> m_cameraViewSystem;
	std::unique_ptr<SurfaceLitMaterialSystem> m_materialSystem;
	std::unique_ptr<Grid> m_grid;
	std::unique_ptr<Skybox> m_skybox;

	// todo (hbedard): rename drawCalls
	// todo (hbedard): handle this somewhere else
	std::vector<MeshDrawInfo> m_opaqueDrawCalls;
	std::vector<MeshDrawInfo> m_translucentDrawCalls;

	void PopulateMeshDrawCalls();
	void SortOpaqueMeshes();
	void SortTranslucentMeshes();

	void RenderMeshes(RenderState& renderState, const std::vector<MeshDrawInfo>& drawCalls) const;
	void RenderShadowMaps(RenderState& renderState, uint32_t concurrentFrameIndex);
};
