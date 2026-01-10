#include <Renderer/RenderScene.h>

#include <Renderer/CameraViewSystem.h>
#include <Renderer/Bindless.h>
#include <Renderer/Grid.h>
#include <Renderer/ImageBasedLightSystem.h>
#include <Renderer/LightSystem.h>
#include <Renderer/MeshAllocator.h>
#include <Renderer/Renderer.h>
#include <Renderer/MaterialSystem.h>
#include <Renderer/SceneTree.h>
#include <Renderer/Skybox.h>
#include <Renderer/ShadowSystem.h>
#include <Renderer/RenderCommandEncoder.h>
#include <RHI/GraphicsPipelineCache.h>
#include <vulkan/vulkan.hpp>
#include <glm_includes.h>

// todo (hbedard): just pass the scene to these so they can bind to what they want
// or perhaps a struct with all buffer handles
RenderScene::RenderScene(Renderer& renderer)
	: m_renderer(&renderer)
	, m_meshAllocator(std::make_unique<MeshAllocator>())
	, m_sceneTree(std::make_unique<SceneTree>(*m_renderer->GetBindlessDescriptors()))
	, m_lightSystem(std::make_unique<LightSystem>(*m_renderer->GetBindlessDescriptors()))
	, m_shadowSystem(std::make_unique<ShadowSystem>(vk::Extent2D(4096, 4096), *m_renderer))
	, m_cameraViewSystem(std::make_unique<CameraViewSystem>(
		m_renderer->GetImageExtent()))
	, m_materialSystem(std::make_unique<MaterialSystem>(
		m_renderer->GetSwapchain(),
		*m_renderer->GetGraphicsPipelineCache(),
		*m_renderer->GetBindlessDescriptors(),
		*m_renderer->GetBindlessDrawParams(),
		*m_sceneTree,
		*m_lightSystem,
		*m_shadowSystem))
	, m_grid(std::make_unique<Grid>(
		m_renderer->GetSwapchain(),
		*m_renderer->GetGraphicsPipelineCache(),
		*m_renderer->GetBindlessDrawParams()))
	, m_skybox(std::make_unique<Skybox>(
		m_renderer->GetSwapchain(),
		*m_renderer->GetGraphicsPipelineCache(),
		*m_renderer->GetBindlessDescriptors(),
		*m_renderer->GetBindlessDrawParams(),
		*m_renderer->GetTextureCache()))
	, m_iblSystem(std::make_unique<ImageBasedLightSystem>(*m_renderer))
	, m_areShadowsDirty(true)
	, m_areEnvironmentMapsDirty(true)
{
}

RenderScene::~RenderScene() = default;

void RenderScene::Init()
{
	m_cameraViewSystem->Init(*m_renderer);

	m_materialSystem->SetViewBufferHandles(m_cameraViewSystem->GetViewBufferHandles());
	m_grid->SetViewBufferHandles(m_cameraViewSystem->GetViewBufferHandles());
	m_skybox->SetViewBufferHandles(m_cameraViewSystem->GetViewBufferHandles());
	
	m_iblSystem->Init();

	PopulateMeshDrawCalls();
	SortOpaqueMeshes();
	UploadToGPU();
}

void RenderScene::Reset()
{
	const Swapchain& swapchain = m_renderer->GetSwapchain();
	m_cameraViewSystem->Reset(swapchain);
	m_materialSystem->Reset(swapchain);
	m_grid->Reset(swapchain);
	m_skybox->Reset(swapchain);
	m_iblSystem->Reset(swapchain);
}

void RenderScene::UploadToGPU()
{
	CommandRingBuffer& commandRingBuffer = m_renderer->GetCommandRingBuffer();
	m_sceneTree->UploadToGPU(commandRingBuffer);
	m_meshAllocator->UploadToGPU(commandRingBuffer);
	m_lightSystem->UploadToGPU(commandRingBuffer);
	m_shadowSystem->UploadToGPU(commandRingBuffer);
	m_cameraViewSystem->UploadToGPU(commandRingBuffer);
	m_materialSystem->UploadToGPU(commandRingBuffer);
	m_grid->UploadToGPU(commandRingBuffer);
	m_skybox->UploadToGPU(commandRingBuffer);
	m_iblSystem->UploadToGPU(commandRingBuffer);
}

void RenderScene::PopulateMeshDrawCalls()
{
	m_meshAllocator->ForEachMesh([this](SceneNodeHandle sceneNodeID, Mesh mesh) {
		MeshDrawInfo info = { sceneNodeID, std::move(mesh) };
		if (m_materialSystem->IsTranslucent(mesh.materialHandle) == false)
			m_opaqueMeshes.push_back(std::move(info));
		else
			m_translucentMeshes.push_back(std::move(info));
		});
}

void RenderScene::SortOpaqueMeshes()
{
	// todo (hbedard): this comment probably needs to be updated
	// Sort opaque draw calls by material, then materialInstance, then mesh.
	// This minimizes the number of pipeline bindings (costly),
	// then descriptor set bindings (a bit less costly).
	//
	// For example (m = material, i = materialInstance, o = object mesh):
	//
	// | m0, i0, o0 | m0, i0, o1 | m0, i1, o2 | m1, i2, o3 |
	//
	std::sort(m_opaqueMeshes.begin(), m_opaqueMeshes.end(),
		[](const MeshDrawInfo& a, const MeshDrawInfo& b) {
			// Then material instance
			if (a.mesh.materialHandle != b.mesh.materialHandle)
				return a.mesh.materialHandle < b.mesh.materialHandle;
			// Then scene node
			else
				return a.sceneNodeID < b.sceneNodeID;
		});
}

void RenderScene::SortTranslucentMeshes()
{
	if (m_translucentMeshes.empty())
	{
		return;
	}

	// Meshes using translucent materials need to be sorted by distance every time the camera moves
	const Camera& camera = m_cameraViewSystem->GetCamera();
	glm::mat4 viewInverse = glm::inverse(camera.GetViewMatrix());
	glm::vec3 cameraPosition = viewInverse[3]; // m_camera.GetPosition();
	glm::vec3 front = viewInverse[2]; // todo: m_camera.GetForwardVector();

	// todo: assign 64 bit number to each MeshDrawInfo for sorting and
	// use this here also instead of copy pasting the sorting logic here.
	std::sort(m_translucentMeshes.begin(), m_translucentMeshes.end(),
		[&cameraPosition, &front, this](const MeshDrawInfo& a, const MeshDrawInfo& b) {
			glm::vec3 dx_a = cameraPosition - glm::vec3(m_sceneTree->GetTransform(a.sceneNodeID)[3]);
			glm::vec3 dx_b = cameraPosition - glm::vec3(m_sceneTree->GetTransform(b.sceneNodeID)[3]);
			float distA = glm::dot(front, dx_a);
			float distB = glm::dot(front, dx_b);

			// Sort by distance first
			if (distA != distB)
				return distA > distB; // back to front
			// Then material
			if (a.mesh.materialHandle != b.mesh.materialHandle)
				return a.mesh.materialHandle < b.mesh.materialHandle;
			// Then model
			else
				return a.sceneNodeID < b.sceneNodeID;
		});
}

void RenderScene::Update()
{
	m_cameraViewSystem->Update(m_renderer->GetFrameIndex());
	GetShadowSystem()->Update(m_cameraViewSystem->GetCamera(), m_sceneTree->GetSceneBoundingBox());
	SortTranslucentMeshes();
}

void RenderScene::Render()
{
	// Only render shadow depth maps once at the start since everything is static at the moment
	if (m_areShadowsDirty)
	{
		RenderShadowDepthPass();
		m_areShadowsDirty = false;
	}

	if (m_areEnvironmentMapsDirty)
	{
		RenderEnvironmentMaps();
		m_areEnvironmentMapsDirty = false;
	}

	RenderBasePass();
}

void RenderScene::RenderEnvironmentMaps() const
{
	m_iblSystem->Render(); // todo (hbedard): this needs another pass
}

void RenderScene::RenderShadowDepthPass() const
{
	// todo (hbedard): I have a feeling this is supposed to be in another pass?
	if (m_shadowSystem->GetShadowCount() == 0 || (m_opaqueMeshes.empty() && m_translucentMeshes.empty()))
	{
		return;
	}

	// Prepare draw commands
	vk::CommandBuffer commandBuffer = m_renderer->GetCommandRingBuffer().GetCommandBuffer();
	std::vector<MeshDrawInfo> drawCalls;
	drawCalls.resize(m_opaqueMeshes.size() + m_translucentMeshes.size());
	std::copy(m_opaqueMeshes.begin(), m_opaqueMeshes.end(), drawCalls.begin());
	std::copy(m_translucentMeshes.begin(), m_translucentMeshes.end(), drawCalls.begin() + m_opaqueMeshes.size());

	// Render into shadow depth maps
	m_shadowSystem->Render(drawCalls);
}

void RenderScene::RenderBasePass() const
{
	gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
	gsl::not_null<BindlessDescriptors*> bindlessDescriptors = m_renderer->GetBindlessDescriptors();
	gsl::not_null<BindlessDrawParams*> bindlessDrawParams = m_renderer->GetBindlessDrawParams();
	vk::CommandBuffer commandBuffer = m_renderer->GetCommandRingBuffer().GetCommandBuffer();

	RenderingInfo renderingInfo = m_renderer->GetRenderingInfo(
		vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f }),
		vk::ClearDepthStencilValue(1.0f, 0.0f)
	);
	commandBuffer.beginRendering(renderingInfo.info);
	{
		RenderCommandEncoder renderCommandEncoder(*graphicsPipelineCache, *bindlessDrawParams);
		renderCommandEncoder.BeginRender(commandBuffer, m_renderer->GetFrameIndex());
		renderCommandEncoder.BindBindlessDescriptorSet(bindlessDescriptors->GetPipelineLayout(), bindlessDescriptors->GetDescriptorSet());
		RenderBasePassMeshes(renderCommandEncoder, m_opaqueMeshes);
		RenderBasePassMeshes(renderCommandEncoder, m_translucentMeshes);
		m_skybox->Render(renderCommandEncoder);
		renderCommandEncoder.EndRender();
	}
	commandBuffer.endRendering();
}

void RenderScene::RenderBasePassMeshes(RenderCommandEncoder& renderCommandEncoder, const std::vector<MeshDrawInfo>& drawCalls) const
{
	if (!drawCalls.empty())
	{
		vk::CommandBuffer commandBuffer = renderCommandEncoder.GetCommandBuffer();
		m_meshAllocator->BindGeometry(commandBuffer);
		m_materialSystem->Draw(renderCommandEncoder, gsl::span(drawCalls.data(), drawCalls.size()));
	}
}
