#include "Material.h"

#include "Shader.h"
#include "GraphicsPipeline.h"
#include "CommandBufferPool.h"

MaterialSystem::MaterialSystem(
	vk::RenderPass renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	TextureCache& textureCache
)
	: m_renderPass(renderPass)
	, m_imageExtent(swapchainExtent)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_textureCache(&textureCache)
{
}

void MaterialSystem::Reset(vk::RenderPass renderPass, vk::Extent2D extent)
{
	m_renderPass = renderPass;
	m_imageExtent = extent;

	assert(m_pipelineProperties.size() == m_pipelineProperties.size());
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
	MaterialInstanceID id = (MaterialInstanceID)m_graphicsPipelineIDs.size();

	m_graphicsPipelineIDs.push_back(LoadGraphicsPipeline(materialInfo));
	m_constants.push_back(materialInfo.constants);
	m_pipelineProperties.push_back(materialInfo.pipelineProperties);
	m_properties.push_back(materialInfo.properties);

	return id;
}

void MaterialSystem::UploadToGPU(CommandBufferPool& commandBufferPool)
{
	UploadUniformBuffer(commandBufferPool);
	CreateDescriptorSets(commandBufferPool);
}

void MaterialSystem::UploadUniformBuffer(CommandBufferPool& commandBufferPool)
{
	vk::CommandBuffer& commandBuffer = commandBufferPool.GetCommandBuffer();
	
	if (m_uniformBuffer == nullptr)
	{
		// todo: probably an alignment issue here
		const void* data = reinterpret_cast<const void*>(m_properties.data());
		size_t bufferSize = m_properties.size() * sizeof(LitMaterialProperties);
		m_uniformBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer);
		memcpy(m_uniformBuffer->GetStagingMappedData(), data, bufferSize);
		m_uniformBuffer->CopyStagingToGPU(commandBuffer);
		commandBufferPool.DestroyAfterSubmit(m_uniformBuffer->ReleaseStagingBuffer());
	}
	else
	{
		assert(!"Material System uniform buffer has already been uploaded");
	}
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
	m_descriptorPool.reset();

	// Sum descriptor needs for all materials
	std::unordered_map<vk::DescriptorType, uint16_t> descriptorCount;

	for (int i = 0; i < m_graphicsPipelineIDs.size(); ++i)
	{
		GraphicsPipelineID pipelineID = m_graphicsPipelineIDs[i];

		const auto& modelBindings = m_graphicsPipelineSystem->GetDescriptorSetLayoutBindings(pipelineID, (uint8_t)DescriptorSetIndex::Model);
		for (const auto& binding : modelBindings)
		{
			descriptorCount[binding.descriptorType] += binding.descriptorCount;
		}

		const auto& materialBindings = m_graphicsPipelineSystem->GetDescriptorSetLayoutBindings(pipelineID, (uint8_t)DescriptorSetIndex::Material);
		for (const auto& binding : materialBindings)
		{
			descriptorCount[binding.descriptorType] += binding.descriptorCount;
		}
	}

	// Create descriptor pool
	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.reserve(descriptorCount.size());
	for (const auto& descriptor : descriptorCount)
	{
		poolSizes.emplace_back(descriptor.first, descriptor.second);
	}

	// If the number of required descriptors were to change at run-time
	// we could have 1 descriptorPool per concurrent frame and reset the pool
	// to increase its size while it's not in use.
	uint32_t maxNbSets = (uint32_t)DescriptorSetIndex::Count * numConcurrentFrames;
	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		maxNbSets,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));
}

void MaterialSystem::CreateDescriptorSets(CommandBufferPool& commandBufferPool)
{
	// ohno: what if the same shader with different specialization constants give different descriptor set layouts...

	CreateDescriptorPool(commandBufferPool.GetNbConcurrentSubmits());

	// View (1 set per concurent frame)
	{
		vk::DescriptorSetLayout viewSetLayout = GetDescriptorSetLayout(DescriptorSetIndex::View);
		std::vector<vk::DescriptorSetLayout> layouts(commandBufferPool.GetNbConcurrentSubmits(), viewSetLayout);
		m_descriptorSets[(size_t)DescriptorSetIndex::View] = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
			m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
		));
	}

	// Model
	{
		vk::DescriptorSetLayout& modelSetLayout = GetDescriptorSetLayout(DescriptorSetIndex::Model);
		std::vector<vk::DescriptorSetLayout> layouts(1, modelSetLayout);
		m_descriptorSets[(size_t)DescriptorSetIndex::Model] = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
			m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
		));
	}

	// Material
	{
		vk::DescriptorSetLayout& materialSetLayout = GetDescriptorSetLayout(DescriptorSetIndex::Material);
		std::vector<vk::DescriptorSetLayout> layouts(1, materialSetLayout);
		m_descriptorSets[(size_t)DescriptorSetIndex::Material] = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
			m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
		));
	}

	UpdateDescriptorSets();
}

void MaterialSystem::UpdateDescriptorSets()
{
	vk::DescriptorSet descriptorSet = GetDescriptorSet(DescriptorSetIndex::Material);
	const UniqueBufferWithStaging& uniformBuffer = GetUniformBuffer();

	uint32_t binding = 0;
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets;

	// Material properties in uniform buffer
	vk::DescriptorBufferInfo descriptorBufferInfo(uniformBuffer.Get(), 0, uniformBuffer.Size());
	writeDescriptorSets.push_back(
		vk::WriteDescriptorSet(
			descriptorSet, binding++, {},
			1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
		) // binding = 0
	);

	// Material's textures
	SmallVector<vk::DescriptorImageInfo> descriptorImageInfoTwo2D = m_textureCache->GetDescriptorImageInfos(ImageViewType::e2D);
	writeDescriptorSets.push_back(
		vk::WriteDescriptorSet(
			descriptorSet, binding++, {},
			static_cast<uint32_t>(descriptorImageInfoTwo2D.size()), vk::DescriptorType::eCombinedImageSampler, descriptorImageInfoTwo2D.data(), nullptr
		) // binding = 1
	);

	// Material's cube maps
	SmallVector<vk::DescriptorImageInfo> descriptorImageInfoCube = m_textureCache->GetDescriptorImageInfos(ImageViewType::eCube);
	writeDescriptorSets.push_back(
		vk::WriteDescriptorSet(
			descriptorSet, binding++, {},
			static_cast<uint32_t>(descriptorImageInfoCube.size()), vk::DescriptorType::eCombinedImageSampler, descriptorImageInfoCube.data(), nullptr
		) // binding = 2
	);

	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

GraphicsPipelineID MaterialSystem::LoadGraphicsPipeline(const LitMaterialInstanceInfo& materialInfo)
{
	auto instanceIDIt = m_materialHashToInstanceID.find(fnv_hash(&materialInfo));
	if (instanceIDIt != m_materialHashToInstanceID.end())
		return m_graphicsPipelineIDs[instanceIDIt->second];
	
	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	ShaderID vertexShaderID = shaderSystem.CreateShader(kVertexShader);
	ShaderID fragmentShaderID = shaderSystem.CreateShader(kFragmentShader);
	ShaderInstanceID vertexInstanceID = shaderSystem.CreateShaderInstance(vertexShaderID);

	ShaderInstanceID fragmentInstanceID = 0;
	fragmentInstanceID = shaderSystem.CreateShaderInstance(
		fragmentShaderID,
		SpecializationConstant::Create(materialInfo.constants)
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
