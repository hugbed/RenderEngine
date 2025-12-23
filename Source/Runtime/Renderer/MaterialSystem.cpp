#include <Renderer/MaterialSystem.h>

#include <Renderer/DescriptorSetLayouts.h>
#include <Renderer/MeshAllocator.h>
#include <RHI/ShaderSystem.h>
#include <RHI/GraphicsPipelineSystem.h>
#include <RHI/CommandBufferPool.h>

namespace
{
	enum class ConstantIDs
	{
		// Fragment
		eNbLights = 0,
		eNbShadowMaps = 1,
		eNbMaterialSamplers2D = 2,
		eNbMaterialSamplersCube = 3
	};

	enum class ViewSetBindings
	{
		eViewUniforms = 0,
		eLightData = 1,
		eShadowMaps = 2,
		eShadowData = 3
	};

	enum class SceneSetBindings
	{
		eSceneData = 0,
	};

	enum class MaterialSetBindings
	{
		eProperties = 0,
		eSamplers2D = 1,
		eSamplersCube = 2
	};

	SmallVector<vk::SpecializationMapEntry> GetFragmentSpecializationMapEntries()
	{
		using FSConst = MaterialSystem::ShaderConstants;

		uint32_t nbEntries = 0;
		return SmallVector<vk::SpecializationMapEntry>{
			vk::SpecializationMapEntry((uint32_t)ConstantIDs::eNbLights, offsetof(FSConst, nbLights), sizeof(FSConst::nbLights)),
			vk::SpecializationMapEntry((uint32_t)ConstantIDs::eNbShadowMaps, offsetof(FSConst, nbShadowMaps), sizeof(FSConst::nbShadowMaps)),
			vk::SpecializationMapEntry((uint32_t)ConstantIDs::eNbMaterialSamplers2D, offsetof(FSConst, nbSamplers2D), sizeof(FSConst::nbSamplers2D)),
			vk::SpecializationMapEntry((uint32_t)ConstantIDs::eNbMaterialSamplersCube, offsetof(FSConst, nbSamplersCube), sizeof(FSConst::nbSamplersCube))
		};
	}
}

const AssetPath MaterialSystem::kVertexShader = AssetPath("/Engine/Generated/Shaders/primitive_vert.spv");
const AssetPath MaterialSystem::kFragmentShader = AssetPath("/Engine/Generated/Shaders/surface_frag.spv");

MaterialSystem::MaterialSystem(
	vk::RenderPass renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	TextureSystem& textureSystem,
	MeshAllocator& meshAllocator
)
	: m_renderPass(renderPass)
	, m_imageExtent(swapchainExtent)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_textureSystem(&textureSystem)
	, m_meshAllocator(&meshAllocator)
{
}

MaterialSystem::~MaterialSystem()
{
	// Reset descriptor sets before resetting pool
	for (auto& descriptorSets : m_descriptorSets)
	{
		for (auto& descriptorSet : descriptorSets)
		{
			descriptorSet.reset();
		}
	}

	m_descriptorPool.reset();
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

void MaterialSystem::UploadToGPU(CommandBufferPool& commandBufferPool, ShaderConstants shaderConstants)
{
	m_constants = std::move(shaderConstants);
	CreatePendingInstances();
	CreateAndUploadUniformBuffer(commandBufferPool);
	CreateDescriptorPool(commandBufferPool.GetNbConcurrentSubmits());
	CreateDescriptorSets(commandBufferPool.GetNbConcurrentSubmits());
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

	ShaderInstanceID vertexInstanceID = shaderSystem.CreateShaderInstance(
		vertexShaderID
	);
	ShaderInstanceID fragmentInstanceID = shaderSystem.CreateShaderInstance(
		fragmentShaderID, (const void*)&m_constants, ::GetFragmentSpecializationMapEntries()
	);

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
		
	const void* data = reinterpret_cast<const void*>(m_properties.data());
	size_t bufferSize = m_properties.size() * sizeof(LitMaterialProperties);
	m_uniformBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eStorageBuffer);
	memcpy(m_uniformBuffer->GetStagingMappedData(), data, bufferSize);
	m_uniformBuffer->CopyStagingToGPU(commandBuffer);
	commandBufferPool.DestroyAfterSubmit(m_uniformBuffer->ReleaseStagingBuffer());
}

void MaterialSystem::CreateDescriptorSets(size_t nbConcurrentSubmits)
{
	// View (1 set per concurent frame)
	vk::DescriptorSetLayout viewSetLayout = GetDescriptorSetLayout(DescriptorSetIndex::View);
	std::vector<vk::DescriptorSetLayout> layouts(nbConcurrentSubmits, viewSetLayout);
	m_descriptorSets[(size_t)DescriptorSetIndex::View] = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
	));

	// Scene
	vk::DescriptorSetLayout sceneSetLayout = GetDescriptorSetLayout(DescriptorSetIndex::Scene);
	m_descriptorSets[(size_t)DescriptorSetIndex::Scene] = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		m_descriptorPool.get(), 1U, &sceneSetLayout
	));

	// Material
	vk::DescriptorSetLayout materialSetLayout = GetDescriptorSetLayout(DescriptorSetIndex::Material);
	m_descriptorSets[(size_t)DescriptorSetIndex::Material] = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		m_descriptorPool.get(), 1U, &materialSetLayout
	));
}

void MaterialSystem::CreateDescriptorPool(uint8_t numConcurrentFrames)
{
	// Reset descriptor sets before reseting pool
	for (auto& descriptorSets : m_descriptorSets)
	{
		for (auto& descriptorSet : descriptorSets)
		{
			descriptorSet.reset();
		}
	}

	// Then reset pool
	if (m_descriptorPool)
		g_device->Get().resetDescriptorPool(m_descriptorPool.get());

	uint16_t nbSamplers =
		m_constants.nbSamplers2D +
		m_constants.nbSamplersCube +
		m_constants.nbShadowMaps;

	// Set 0 (view):     - 1 uniform buffer for view uniforms per concurrentFrames +
	//                   - 1 uniform buffer containing all lights +
	//					 - 1 image sampler per shadow map texture
	//					 - 1 storage buffer with all shadow map transforms
	// Set 1 (scene):    - 1 storage buffer containing scene nodes (transforms, etc.)
	// Set 2 (material): - 1 storage buffer containing all material properties +
	//                   - 1 image sampler per texture in the texture system
	std::array<std::pair<vk::DescriptorType, uint16_t>, 3ULL> descriptorCount = {
		std::make_pair(vk::DescriptorType::eUniformBuffer, (uint16_t)2 * numConcurrentFrames),
		std::make_pair(vk::DescriptorType::eStorageBuffer, (uint16_t)3),
		std::make_pair(vk::DescriptorType::eCombinedImageSampler, (uint16_t)nbSamplers)
	};

	// Create descriptor pool
	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.reserve(descriptorCount.size());
	for (const auto& descriptor : descriptorCount)
		poolSizes.emplace_back(descriptor.first, descriptor.second);

	// If the number of required descriptors were to change at run-time
	// we could have 1 descriptorPool per concurrent frame and reset the pool
	// to increase its size while it's not in use.
	vk::DescriptorPoolCreateInfo info(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		(uint32_t)DescriptorSetIndex::Count * numConcurrentFrames, // maxSets
		(uint32_t)poolSizes.size(),
		poolSizes.data()
	);

	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(info);
}

void MaterialSystem::UpdateViewDescriptorSets(
	const VectorView<vk::Buffer>& viewUniformBuffers, size_t viewBufferSize,
	vk::Buffer lightsUniformBuffer, size_t lightsBufferSize) const
{
	assert(m_descriptorSets[0].size() == viewUniformBuffers.size());

	SmallVector<vk::WriteDescriptorSet, 16> writeDescriptorSets;
	SmallVector<vk::DescriptorBufferInfo, 4> descriptorBufferInfoView;
	vk::DescriptorBufferInfo descriptorBufferInfoLights(lightsUniformBuffer, 0, lightsBufferSize);

	for (int frameIndex = 0; frameIndex < (int)viewUniformBuffers.size(); ++frameIndex)
	{
		vk::DescriptorSet viewDescriptorSet = GetDescriptorSet(DescriptorSetIndex::View, frameIndex);

		descriptorBufferInfoView.emplace_back(viewUniformBuffers[frameIndex], 0, viewBufferSize);
		writeDescriptorSets.emplace_back(
			viewDescriptorSet, (uint32_t)ViewSetBindings::eViewUniforms, 0,
			1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoView[frameIndex]
		);

		writeDescriptorSets.emplace_back(
			viewDescriptorSet, (uint32_t)ViewSetBindings::eLightData, 0,
			1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoLights
		);
	}

	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void MaterialSystem::UpdateShadowDescriptorSets(
	const SmallVector<vk::DescriptorImageInfo, 16>& shadowMaps,
	vk::Buffer shadowDataBuffer, size_t shadowDataBufferSize) const
{
	SmallVector<vk::WriteDescriptorSet, 6> writeDescriptorSets;
	vk::DescriptorBufferInfo descriptorBufferInfo(shadowDataBuffer, 0, shadowDataBufferSize);

	for (int frameIndex = 0; frameIndex < (int)m_descriptorSets[0].size(); ++frameIndex)
	{
		vk::DescriptorSet viewDescriptorSet = GetDescriptorSet(DescriptorSetIndex::View, frameIndex);

		writeDescriptorSets.emplace_back(
			viewDescriptorSet, (uint32_t)ViewSetBindings::eShadowMaps, 0,
			static_cast<uint32_t>(shadowMaps.size()), vk::DescriptorType::eCombinedImageSampler, shadowMaps.data(), nullptr
		);

		writeDescriptorSets.emplace_back(
			viewDescriptorSet, (uint32_t)ViewSetBindings::eShadowData, 0,
			1, vk::DescriptorType::eStorageBuffer, nullptr, &descriptorBufferInfo
		);
	}

	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void MaterialSystem::UpdateSceneDescriptorSet(vk::Buffer transformsBuffer, size_t transformsBufferSize) const
{
	vk::DescriptorSet descriptorSet = GetDescriptorSet(DescriptorSetIndex::Scene);

	vk::DescriptorBufferInfo descriptorBufferInfo(transformsBuffer, 0, transformsBufferSize);
	vk::WriteDescriptorSet writeDescriptorSet(
		descriptorSet, (uint32_t)SceneSetBindings::eSceneData, {},
		1, vk::DescriptorType::eStorageBuffer, nullptr, &descriptorBufferInfo
	);
	g_device->Get().updateDescriptorSets(1, &writeDescriptorSet, 0, nullptr); // kind of sad that these updates are not batched together (todo: descriptor set system)
}

void MaterialSystem::UpdateMaterialDescriptorSet() const
{
	vk::DescriptorSet descriptorSet = GetDescriptorSet(DescriptorSetIndex::Material);
	const UniqueBufferWithStaging& uniformBuffer = GetUniformBuffer();

	uint32_t binding = 0;
	SmallVector<vk::WriteDescriptorSet> writeDescriptorSets;

	// Material properties in uniform buffer
	vk::DescriptorBufferInfo descriptorBufferInfo(uniformBuffer.Get(), 0, uniformBuffer.Size());
	writeDescriptorSets.push_back(
		vk::WriteDescriptorSet(
			descriptorSet, (uint32_t)MaterialSetBindings::eProperties, {},
			1, vk::DescriptorType::eStorageBuffer, nullptr, &descriptorBufferInfo
		)
	);

	// Material's textures
	SmallVector<vk::DescriptorImageInfo> descriptorImageInfoTwo2D = m_textureSystem->GetDescriptorImageInfos(ImageViewType::e2D);
	writeDescriptorSets.push_back(
		vk::WriteDescriptorSet(
			descriptorSet, (uint32_t)MaterialSetBindings::eSamplers2D, {},
			static_cast<uint32_t>(descriptorImageInfoTwo2D.size()), vk::DescriptorType::eCombinedImageSampler, descriptorImageInfoTwo2D.data(), nullptr
		)
	);

	// Material's cube maps
	SmallVector<vk::DescriptorImageInfo> descriptorImageInfoCube = m_textureSystem->GetDescriptorImageInfos(ImageViewType::eCube);
	writeDescriptorSets.push_back(
		vk::WriteDescriptorSet(
			descriptorSet, (uint32_t)MaterialSetBindings::eSamplersCube, {},
			static_cast<uint32_t>(descriptorImageInfoCube.size()), vk::DescriptorType::eCombinedImageSampler, descriptorImageInfoCube.data(), nullptr
		)
	);

	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}
