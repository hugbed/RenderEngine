#include <Renderer/MaterialSystem.h>

#include <Renderer/DescriptorSetLayouts.h>
#include <Renderer/Bindless.h>
#include <Renderer/RenderState.h>
#include <Renderer/SceneTree.h>
#include <Renderer/ShadowSystem.h>
#include <RHI/ShaderSystem.h>
#include <RHI/GraphicsPipelineSystem.h>
#include <RHI/CommandBufferPool.h>

const AssetPath MaterialSystem::kVertexShader = AssetPath("/Engine/Generated/Shaders/primitive_vert.spv");
const AssetPath MaterialSystem::kFragmentShader = AssetPath("/Engine/Generated/Shaders/surface_frag.spv");

MaterialSystem::MaterialSystem(
	vk::RenderPass renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	TextureSystem& textureSystem,
	MeshAllocator& meshAllocator,
	BindlessDescriptors& bindlessDescriptors,
	BindlessDrawParams& bindlessDrawParams
)
	: m_renderPass(renderPass)
	, m_imageExtent(swapchainExtent)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_textureSystem(&textureSystem)
	, m_meshAllocator(&meshAllocator)
	, m_bindlessDescriptors(&bindlessDescriptors)
	, m_bindlessDrawParams(&bindlessDrawParams)
{
	m_drawParamsHandle = m_bindlessDrawParams->DeclareParams<SurfaceLitDrawParams>();
}

void MaterialSystem::Reset(vk::RenderPass renderPass, vk::Extent2D extent)
{
	m_renderPass = renderPass;
	m_imageExtent = extent;

	for (size_t i = 0; i < m_graphicsPipelineIDs.size(); ++i)
	{
		// Assume that each material uses a different pipeline
		GraphicsPipelineInfo info(m_renderPass, m_imageExtent);
		info.blendEnable = m_pipelineProperties[i].isTransparent;
		m_graphicsPipelineSystem->ResetGraphicsPipeline(
			m_graphicsPipelineIDs[i], info
		);
	}
}

MaterialInstanceID MaterialSystem::CreateMaterialInstance(const LitMaterialInstanceInfo& materialInfo)
{
	MaterialInstanceID id = m_nextID;

	m_toInstantiate.emplace_back(std::make_pair(id, materialInfo));
	m_pipelineProperties.push_back(materialInfo.pipelineProperties);
	m_properties.push_back(materialInfo.properties);

	m_nextID++;
	return id;
}

void MaterialSystem::SetViewBufferHandles(gsl::span<BufferHandle> viewBufferHandles)
{
	m_viewBufferHandles.reserve(viewBufferHandles.size());
	std::copy(viewBufferHandles.begin(), viewBufferHandles.end(), std::back_inserter(m_viewBufferHandles));
}

// todo (hbedard): that's a weird place to pass dependencies
void MaterialSystem::UploadToGPU(
	CommandBufferPool& commandBufferPool,
	gsl::not_null<SceneTree*> sceneTree,
	gsl::not_null<LightSystem*> lightSystem,
	gsl::not_null<ShadowSystem*> shadowSystem)
{
	CreatePendingInstances();
	CreateAndUploadUniformBuffer(commandBufferPool);

	assert(!m_viewBufferHandles.empty());
	SurfaceLitDrawParams drawParams = m_drawParams;
	drawParams.lights = lightSystem->GetLightsBufferHandle();
	drawParams.lightCount = lightSystem->GetLightCount();
	drawParams.materials = m_uniformBufferHandle;
	drawParams.transforms = sceneTree->GetTransformsBufferHandle();
	drawParams.shadowTransforms = shadowSystem->GetMaterialShadowsBufferHandle();

	for (uint32_t i = 0; i < m_viewBufferHandles.size(); ++i)
	{
		drawParams.view = m_viewBufferHandles[i];
		m_bindlessDrawParams->DefineParams(m_drawParamsHandle, drawParams, i);
	}
}

void MaterialSystem::Draw(RenderState& renderState, gsl::span<const MeshDrawInfo> drawCalls) const
{
	vk::CommandBuffer commandBuffer = renderState.GetCommandBuffer();

	renderState.BindDrawParams(m_drawParamsHandle);

	for (const auto& drawItem : drawCalls)
	{
		auto shadingModel = drawItem.mesh.shadingModel;
		renderState.BindPipeline(GetGraphicsPipelineID(drawItem.mesh.materialInstanceID));
		renderState.BindSceneNode(drawItem.sceneNodeID);
		renderState.BindMaterial(drawItem.mesh.materialInstanceID);
		commandBuffer.drawIndexed(drawItem.mesh.nbIndices, 1, drawItem.mesh.indexOffset, 0, 0);
	}
}

void MaterialSystem::CreatePendingInstances()
{
	for (const auto& instanceInfo : m_toInstantiate)
	{
		const MaterialInstanceID id = instanceInfo.first;
		const LitMaterialInstanceInfo& materialInfo = instanceInfo.second;
		m_graphicsPipelineIDs.resize(id + 1ULL);
		m_graphicsPipelineIDs[id] = LoadGraphicsPipeline(materialInfo);
	}

	m_toInstantiate.clear();
}

vk::DescriptorSetLayout MaterialSystem::GetDescriptorSetLayout(DescriptorSetIndex setIndex) const
{
	if (m_graphicsPipelineIDs.empty())
	{
		assert(!"Fetching empty descriptor set layout");
		return {};
	}

	return m_graphicsPipelineSystem->GetDescriptorSetLayout(m_graphicsPipelineIDs.front(), (uint8_t)setIndex);
}

GraphicsPipelineID MaterialSystem::LoadGraphicsPipeline(const LitMaterialInstanceInfo& materialInfo)
{
	auto instanceIDIt = m_materialHashToInstanceID.find(fnv_hash(&materialInfo));
	if (instanceIDIt != m_materialHashToInstanceID.end())
		return m_graphicsPipelineIDs[instanceIDIt->second];

	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();

	ShaderID vertexShaderID = shaderSystem.CreateShader(kVertexShader.PathOnDisk());
	ShaderID fragmentShaderID = shaderSystem.CreateShader(kFragmentShader.PathOnDisk());

	ShaderInstanceID vertexInstanceID = shaderSystem.CreateShaderInstance(vertexShaderID);
	ShaderInstanceID fragmentInstanceID = shaderSystem.CreateShaderInstance(fragmentShaderID);

	uint32_t pipelineIndex = m_graphicsPipelineIDs.size();
	GraphicsPipelineInfo info(m_renderPass, m_imageExtent);
	info.blendEnable = materialInfo.pipelineProperties.isTransparent;
	GraphicsPipelineID id = m_graphicsPipelineSystem->CreateGraphicsPipeline(
		vertexInstanceID, fragmentInstanceID, info
	);

	auto [it, wasAdded] = m_materialHashToInstanceID.emplace(fnv_hash(&materialInfo), pipelineIndex);
	return id;
}

void MaterialSystem::CreateAndUploadUniformBuffer(CommandBufferPool& commandBufferPool)
{
	if (m_uniformBuffer != nullptr)
		return; // nothing to do

	vk::CommandBuffer& commandBuffer = commandBufferPool.GetCommandBuffer();
	
	const vk::BufferUsageFlagBits bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer;
	const void* data = reinterpret_cast<const void*>(m_properties.data());
	size_t bufferSize = m_properties.size() * sizeof(LitMaterialProperties);
	m_uniformBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, bufferUsage);
	memcpy(m_uniformBuffer->GetStagingMappedData(), data, bufferSize);
	m_uniformBuffer->CopyStagingToGPU(commandBuffer);
	commandBufferPool.DestroyAfterSubmit(m_uniformBuffer->ReleaseStagingBuffer());

	m_uniformBufferHandle = m_bindlessDescriptors->StoreBuffer(m_uniformBuffer->Get(), bufferUsage);
}

vk::PipelineLayout MaterialSystem::GetPipelineLayout() const
{
	// all materials should have the same pipeline layout
	return m_graphicsPipelineSystem->GetPipelineLayout(m_graphicsPipelineIDs.back());
}
