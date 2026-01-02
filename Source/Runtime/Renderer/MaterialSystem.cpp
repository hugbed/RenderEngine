#include <Renderer/MaterialSystem.h>

#include <Renderer/Bindless.h>
#include <Renderer/LightSystem.h>
#include <Renderer/RenderCommandEncoder.h>
#include <Renderer/SceneTree.h>
#include <Renderer/ShadowSystem.h>
#include <RHI/ShaderCache.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/CommandRingBuffer.h>

const AssetPath MaterialSystem::kVertexShader = AssetPath("/Engine/Generated/Shaders/primitive_vert.spv");
const AssetPath MaterialSystem::kFragmentShader = AssetPath("/Engine/Generated/Shaders/surface_pbr_frag.spv");

MaterialSystem::MaterialSystem(
	vk::RenderPass renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineCache& graphicsPipelineCache,
	BindlessDescriptors& bindlessDescriptors,
	BindlessDrawParams& bindlessDrawParams,
	SceneTree& sceneTree,
	LightSystem& lightSystem,
	ShadowSystem& shadowSystem
)
	: m_renderPass(renderPass)
	, m_imageExtent(swapchainExtent)
	, m_graphicsPipelineCache(&graphicsPipelineCache)
	, m_bindlessDescriptors(&bindlessDescriptors)
	, m_bindlessDrawParams(&bindlessDrawParams)
	, m_sceneTree(&sceneTree)
	, m_lightSystem(&lightSystem)
	, m_shadowSystem(&shadowSystem)
	, m_nextHandle(MaterialShadingDomain::Surface, MaterialShadingModel::Lit, 0)
{
	m_drawParamsHandle = m_bindlessDrawParams->DeclareParams<MaterialDrawParams>();
}

void MaterialSystem::Reset(vk::RenderPass renderPass, vk::Extent2D extent)
{
	m_renderPass = renderPass;
	m_imageExtent = extent;

	for (size_t i = 0; i < m_graphicsPipelineIDs.size(); ++i)
	{
		// Assume that each material uses a different pipeline
		GraphicsPipelineInfo info(m_renderPass, m_imageExtent);
		info.blendEnable = m_pipelineProperties[i].isTranslucent;
		m_graphicsPipelineCache->ResetGraphicsPipeline(
			m_graphicsPipelineIDs[i], info
		);
	}
}

MaterialHandle MaterialSystem::CreateMaterialInstance(const MaterialInstanceInfo& materialInfo)
{
	MaterialHandle id = m_nextHandle;

	m_toInstantiate.emplace_back(std::make_pair(id, materialInfo));
	m_pipelineProperties.push_back(materialInfo.pipelineProperties);
	m_properties.push_back(materialInfo.properties);

	m_nextHandle.IncrementIndex();
	return id;
}

void MaterialSystem::SetViewBufferHandles(gsl::span<const BufferHandle> viewBufferHandles)
{
	m_viewBufferHandles.clear();
	m_viewBufferHandles.reserve(viewBufferHandles.size());
	std::copy(viewBufferHandles.begin(), viewBufferHandles.end(), std::back_inserter(m_viewBufferHandles));
}

void MaterialSystem::UploadToGPU(CommandRingBuffer& commandRingBuffer)
{
	CreatePendingInstances();
	CreateAndUploadStorageBuffer(commandRingBuffer);

	assert(!m_viewBufferHandles.empty());
	MaterialDrawParams drawParams = m_drawParams;
	drawParams.lights = m_lightSystem->GetLightsBufferHandle();
	drawParams.lightCount = m_lightSystem->GetLightCount();
	drawParams.materials = m_uniformBufferHandle;
	drawParams.transforms = m_sceneTree->GetTransformsBufferHandle();
	drawParams.shadowTransforms = m_shadowSystem->GetMaterialShadowsBufferHandle();

	for (uint32_t i = 0; i < m_viewBufferHandles.size(); ++i)
	{
		drawParams.view = m_viewBufferHandles[i];
		m_bindlessDrawParams->DefineParams(m_drawParamsHandle, drawParams, i);
	}
}

void MaterialSystem::Draw(RenderCommandEncoder& renderCommandEncoder, gsl::span<const MeshDrawInfo> drawCalls) const
{
	vk::CommandBuffer commandBuffer = renderCommandEncoder.GetCommandBuffer();

	renderCommandEncoder.BindDrawParams(m_drawParamsHandle);

	for (const auto& drawItem : drawCalls)
	{
		renderCommandEncoder.BindPipeline(GetGraphicsPipelineID(drawItem.mesh.materialHandle));
		renderCommandEncoder.BindSceneNode(drawItem.sceneNodeID);
		renderCommandEncoder.BindMaterial(drawItem.mesh.materialHandle);
		commandBuffer.drawIndexed(drawItem.mesh.nbIndices, 1, drawItem.mesh.indexOffset, 0, 0);
	}
}

void MaterialSystem::CreatePendingInstances()
{
	for (const auto& instanceInfo : m_toInstantiate)
	{
		const MaterialHandle handle = instanceInfo.first;
		const MaterialInstanceInfo& materialInfo = instanceInfo.second;
		uint32_t materialIndex = handle.GetIndex();
		m_graphicsPipelineIDs.resize(materialIndex + 1ULL);
		m_graphicsPipelineIDs[materialIndex] = LoadGraphicsPipeline(materialInfo);
	}

	m_toInstantiate.clear();
}

GraphicsPipelineID MaterialSystem::LoadGraphicsPipeline(const MaterialInstanceInfo& materialInfo)
{
	auto instanceIDIt = m_materialHashToHandle.find(fnv_hash(&materialInfo));
	if (instanceIDIt != m_materialHashToHandle.end())
		return m_graphicsPipelineIDs[instanceIDIt->second.GetIndex()];

	ShaderCache& shaderCache = m_graphicsPipelineCache->GetShaderCache();

	ShaderID vertexShaderID = shaderCache.CreateShader(kVertexShader.PathOnDisk());
	ShaderID fragmentShaderID = shaderCache.CreateShader(kFragmentShader.PathOnDisk());

	ShaderInstanceID vertexInstanceID = shaderCache.CreateShaderInstance(vertexShaderID);
	ShaderInstanceID fragmentInstanceID = shaderCache.CreateShaderInstance(fragmentShaderID);

	uint32_t pipelineIndex = m_graphicsPipelineIDs.size();
	GraphicsPipelineInfo info(m_renderPass, m_imageExtent);
	info.blendEnable = materialInfo.pipelineProperties.isTranslucent;
	GraphicsPipelineID id = m_graphicsPipelineCache->CreateGraphicsPipeline(
		vertexInstanceID, fragmentInstanceID, info
	);

	MaterialHandle handle;
	handle.SetIndex(pipelineIndex);
	auto [it, wasAdded] = m_materialHashToHandle.emplace(fnv_hash(&materialInfo), handle);
	return id;
}

void MaterialSystem::CreateAndUploadStorageBuffer(CommandRingBuffer& commandRingBuffer)
{
	if (m_storageBuffer != nullptr)
		return; // nothing to do

	vk::CommandBuffer commandBuffer = commandRingBuffer.GetCommandBuffer();
	
	const vk::BufferUsageFlagBits bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer;
	const void* data = reinterpret_cast<const void*>(m_properties.data());
	size_t bufferSize = m_properties.size() * sizeof(m_properties[0]);
	m_storageBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, bufferUsage);
	memcpy(m_storageBuffer->GetStagingMappedData(), data, bufferSize);
	m_storageBuffer->CopyStagingToGPU(commandBuffer);
	commandRingBuffer.DestroyAfterSubmit(m_storageBuffer->ReleaseStagingBuffer());

	m_uniformBufferHandle = m_bindlessDescriptors->StoreBuffer(m_storageBuffer->Get(), bufferUsage);
}

vk::PipelineLayout MaterialSystem::GetPipelineLayout() const
{
	// all materials should have the same pipeline layout
	return m_graphicsPipelineCache->GetPipelineLayout(m_graphicsPipelineIDs.back());
}
