#include <Renderer/RenderScene.h>

#include <Renderer/CameraViewSystem.h>
#include <Renderer/Bindless.h>
#include <Renderer/Grid.h>
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
	, m_shadowSystem(std::make_unique<ShadowSystem>(
		vk::Extent2D(4096, 4096),
		*m_renderer->GetGraphicsPipelineCache(),
		*m_renderer->GetBindlessDescriptors(),
		*m_renderer->GetBindlessDrawParams(),
		*m_meshAllocator,
		*m_sceneTree,
		*m_lightSystem))
	, m_cameraViewSystem(std::make_unique<CameraViewSystem>(
		m_renderer->GetImageExtent()))
	, m_materialSystem(std::make_unique<MaterialSystem>(
		m_renderer->GetRenderPass(),
		m_renderer->GetImageExtent(),
		*m_renderer->GetGraphicsPipelineCache(),
		*m_renderer->GetBindlessDescriptors(),
		*m_renderer->GetBindlessDrawParams(),
		*m_sceneTree,
		*m_lightSystem,
		*m_shadowSystem))
	, m_grid(std::make_unique<Grid>(
		m_renderer->GetRenderPass(),
		m_renderer->GetImageExtent(),
		*m_renderer->GetGraphicsPipelineCache(),
		*m_renderer->GetBindlessDrawParams()))
	, m_skybox(std::make_unique<Skybox>(
		m_renderer->GetRenderPass(),
		m_renderer->GetImageExtent(),
		*m_renderer->GetGraphicsPipelineCache(),
		*m_renderer->GetBindlessDescriptors(),
		*m_renderer->GetBindlessDrawParams(),
		*m_renderer->GetTextureCache()))
{
}

RenderScene::~RenderScene() = default;


vk::CommandBuffer RenderScene::GetBasePassCommandBuffer() const
{
	return m_basePassCommandBuffers[m_renderer->GetImageIndex()].get();
}

void RenderScene::Init()
{
	CreateBasePassCommandBuffers();

	m_cameraViewSystem->Init(*m_renderer);

	m_materialSystem->SetViewBufferHandles(m_cameraViewSystem->GetViewBufferHandles());
	m_grid->SetViewBufferHandles(m_cameraViewSystem->GetViewBufferHandles());
	m_skybox->SetViewBufferHandles(m_cameraViewSystem->GetViewBufferHandles());

	PopulateMeshDrawCalls();
	SortOpaqueMeshes();
	UploadToGPU();
}

void RenderScene::Reset()
{
	vk::Extent2D newExtent = m_renderer->GetImageExtent();
	vk::RenderPass newRenderPass = m_renderer->GetRenderPass();
	m_cameraViewSystem->Reset(newExtent);
	//m_phongMaterialSystem->Reset(newRenderPass, newExtent);
	m_materialSystem->Reset(newRenderPass, newExtent);
	m_grid->Reset(newRenderPass, newExtent);
	m_skybox->Reset(newRenderPass, newExtent);

	CreateBasePassCommandBuffers();
}

void RenderScene::UploadToGPU()
{
	m_sceneTree->UploadToGPU(m_renderer->GetCommandRingBuffer());
	m_meshAllocator->UploadToGPU(m_renderer->GetCommandRingBuffer());
	m_lightSystem->UploadToGPU(m_renderer->GetCommandRingBuffer());
	m_shadowSystem->UploadToGPU(m_renderer->GetCommandRingBuffer());
	m_cameraViewSystem->UploadToGPU(m_renderer->GetCommandRingBuffer());
	m_materialSystem->UploadToGPU(m_renderer->GetCommandRingBuffer());
	m_grid->UploadToGPU(m_renderer->GetCommandRingBuffer());
	m_skybox->UploadToGPU(m_renderer->GetCommandRingBuffer());
}

void RenderScene::CreateBasePassCommandBuffers()
{
	m_basePassCommandBuffers.clear();
	m_commandPool.reset();
	m_commandPool = g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer, g_physicalDevice->GetQueueFamilies().graphicsFamily.value()
	));
	m_basePassCommandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
		m_commandPool.get(), vk::CommandBufferLevel::eSecondary, m_renderer->GetImageCount()
	));
}

void RenderScene::PopulateMeshDrawCalls()
{
	m_meshAllocator->ForEachMesh([this](SceneNodeHandle sceneNodeID, Mesh mesh) {
		MeshDrawInfo info = { sceneNodeID, std::move(mesh) };
		if (m_materialSystem->IsTransparent(mesh.materialHandle) == false)
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

	RenderBasePass();
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

	gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
	gsl::not_null<BindlessDescriptors*> bindlessDescriptors = m_renderer->GetBindlessDescriptors();
	gsl::not_null<BindlessDrawParams*> bindlessDrawParams = m_renderer->GetBindlessDrawParams();
	vk::RenderPass renderPass = m_renderer->GetRenderPass();
	uint32_t imageIndex = m_renderer->GetImageIndex();
	uint32_t frameIndex = m_renderer->GetFrameIndex();

	// Render into shadow depth maps
	RenderCommandEncoder renderCommandEncoder(*graphicsPipelineCache, *m_materialSystem, *bindlessDrawParams);
	renderCommandEncoder.BeginRender(commandBuffer, m_renderer->GetFrameIndex());
	renderCommandEncoder.BindBindlessDescriptorSet(bindlessDescriptors->GetPipelineLayout(), bindlessDescriptors->GetDescriptorSet());
	GetShadowSystem()->Render(renderCommandEncoder, drawCalls);
	renderCommandEncoder.EndRender();
}

void RenderScene::RenderBasePass() const
{
	gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
	gsl::not_null<BindlessDescriptors*> bindlessDescriptors = m_renderer->GetBindlessDescriptors();
	gsl::not_null<BindlessDrawParams*> bindlessDrawParams = m_renderer->GetBindlessDrawParams();
	vk::CommandBuffer mainCommandBuffer = m_renderer->GetCommandRingBuffer().GetCommandBuffer();
	vk::RenderPass renderPass = m_renderer->GetRenderPass();
	uint32_t imageIndex = m_renderer->GetImageIndex();
	uint32_t frameIndex = m_renderer->GetFrameIndex();

	// Base pass (into a secondary command buffer // todo (hbedard): why?
	vk::CommandBuffer basePassCommandBuffer = m_basePassCommandBuffers[imageIndex].get();
	vk::CommandBufferInheritanceInfo info(renderPass, 0, m_renderer->GetFramebuffer().Get());
	basePassCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
	{
		RenderCommandEncoder renderCommandEncoder(*graphicsPipelineCache, *m_materialSystem, *bindlessDrawParams);
		renderCommandEncoder.BeginRender(basePassCommandBuffer, m_renderer->GetFrameIndex());
		renderCommandEncoder.BindBindlessDescriptorSet(bindlessDescriptors->GetPipelineLayout(), bindlessDescriptors->GetDescriptorSet());
		RenderBasePassMeshes(renderCommandEncoder, m_opaqueMeshes);
		RenderBasePassMeshes(renderCommandEncoder, m_translucentMeshes);
		m_skybox->Draw(renderCommandEncoder);
		renderCommandEncoder.EndRender();
	}
	basePassCommandBuffer.end();
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
