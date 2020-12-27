#include "Material.h"

#include "Shader.h"
#include "GraphicsPipeline.h"

MaterialSystem::MaterialSystem(
	vk::RenderPass renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineSystem& graphicsPipelineSystem
)
	: m_renderPass(renderPass)
	, m_imageExtent(swapchainExtent)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
{
	// All materials are based on the same "base materials"
}

void MaterialSystem::Reset(vk::RenderPass renderPass, vk::Extent2D extent)
{
	m_renderPass = renderPass;
	m_imageExtent = extent;

	assert(m_isTransparent.size() == m_graphicsPipelineIDs.size());
	for (size_t i = 0; i < m_graphicsPipelineIDs.size(); ++i)
	{
		// Assume that each material uses a different pipeline
		GraphicsPipelineInfo info(m_renderPass, m_imageExtent);
		info.blendEnable = m_isTransparent[i];
		m_graphicsPipelineSystem->ResetGraphicsPipeline(
			m_graphicsPipelineIDs[i], info
		);
	}
}

MaterialInstanceID MaterialSystem::CreateMaterialInstance(const MaterialInstanceInfo& materialInfo)
{
	MaterialInstanceID id = m_nextID;

	m_shadingModels.push_back(Material::kShadingModel[(size_t)materialInfo.materialID]);
	m_isTransparent.push_back(materialInfo.isTransparent);
	m_graphicsPipelineIDs.push_back(LoadGraphicsPipeline(materialInfo));
	m_materialConstants.push_back(materialInfo.constants);

	m_nextID++;
	return id;
}

GraphicsPipelineID MaterialSystem::LoadGraphicsPipeline(const MaterialInstanceInfo& materialInfo)
{
	auto instanceIDIt = m_materialHashToInstanceID.find(materialInfo.Hash());
	if (instanceIDIt != m_materialHashToInstanceID.end())
		return m_graphicsPipelineIDs[instanceIDIt->second];

	size_t materialID = (size_t)materialInfo.materialID;
	
	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	ShaderID vertexShaderID = shaderSystem.CreateShader(Material::kVertexShader[materialID]);
	ShaderID fragmentShaderID = shaderSystem.CreateShader(Material::kFragmentShader[materialID]);
	ShaderInstanceID vertexInstanceID = shaderSystem.CreateShaderInstance(vertexShaderID);

	ShaderInstanceID fragmentInstanceID = 0;
	if (Material::kShadingModel[materialID] == Material::ShadingModel::Lit)
		fragmentInstanceID = shaderSystem.CreateShaderInstance(fragmentShaderID, SpecializationConstant::Create(materialInfo.constants));
	else
		fragmentInstanceID = shaderSystem.CreateShaderInstance(fragmentShaderID);

	uint32_t pipelineIndex = m_graphicsPipelineIDs.size();
	GraphicsPipelineInfo info(m_renderPass, m_imageExtent);
	info.blendEnable = materialInfo.isTransparent;
	m_graphicsPipelineIDs.push_back(m_graphicsPipelineSystem->CreateGraphicsPipeline(
		vertexInstanceID, fragmentInstanceID, info
	));

	auto [it, wasAdded] = m_materialHashToInstanceID.emplace(materialInfo.Hash(), pipelineIndex);
	return m_graphicsPipelineIDs[it->second];
}
