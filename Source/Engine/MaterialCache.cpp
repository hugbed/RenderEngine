#include "MaterialCache.h"

#include "Shader.h"
#include "GraphicsPipeline.h"

MaterialCache::MaterialCache(
	vk::RenderPass renderPass,
	vk::Extent2D swapchainExtent,
	GraphicsPipelineSystem& graphicsPipelineSystem
)
	: m_renderPass(renderPass)
	, m_imageExtent(swapchainExtent)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
{
	// All materials are based on the same "base materials"
	// Create a material description for each one of them.

	m_baseMaterialsInfo.reserve((size_t)BaseMaterialID::Count);

	// MaterialIndex::Textured
	m_baseMaterialsInfo.push_back({
		ShadingModel::Unlit, std::string("primitive_vert.spv"), std::string("surface_unlit_frag.spv")
	});

	// MaterialIndex::Phong
	m_baseMaterialsInfo.push_back({
		ShadingModel::Lit, std::string("primitive_vert.spv"), std::string("surface_frag.spv")
	});
}

void MaterialCache::Reset(vk::RenderPass renderPass, vk::Extent2D extent)
{
	m_renderPass = renderPass;
	m_imageExtent = extent;

	for (size_t i = 0; i < m_materials.size(); ++i)
	{
		// Assume that each material uses a different pipeline
		GraphicsPipelineInfo info(m_renderPass, m_imageExtent);
		info.blendEnable = m_materials[i]->isTransparent;
		m_graphicsPipelineSystem->ResetGraphicsPipeline(
			m_materials[i]->pipelineID, info
		);
	}
}

Material* MaterialCache::CreateMaterial(const MaterialInfo& materialInfo)
{
	const auto& materialDescription = m_baseMaterialsInfo[(size_t)materialInfo.baseMaterial];

	auto material = std::make_unique<Material>();
	material->shadingModel = materialDescription.shadingModel;
	material->isTransparent = materialInfo.isTransparent;
	material->pipelineID = LoadGraphicsPipeline(materialInfo);

	m_materials.push_back(std::move(material));
	m_materialsInfo.push_back(materialInfo);
	return m_materials.back().get();
}

GraphicsPipelineID MaterialCache::LoadGraphicsPipeline(const MaterialInfo& materialInfo)
{
	auto pipelineIndexIt = m_materialHashToPipelineIndex.find(materialInfo.Hash());
	if (pipelineIndexIt != m_materialHashToPipelineIndex.end())
		return m_graphicsPipelineIDs[pipelineIndexIt->second];

	const auto& materialDescription = m_baseMaterialsInfo[(size_t)materialInfo.baseMaterial];

	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	ShaderID vertexShaderID = shaderSystem.CreateShader(materialDescription.vertexShader, "main");
	ShaderID fragmentShaderID = shaderSystem.CreateShader(materialDescription.fragmentShader, "main");
	ShaderInstanceID vertexInstanceID = shaderSystem.CreateShaderInstance(vertexShaderID);
	ShaderInstanceID fragmentInstanceID = 0;
	if (materialDescription.shadingModel == ShadingModel::Lit)
		fragmentInstanceID = shaderSystem.CreateShaderInstance(fragmentShaderID, SpecializationConstant::Create(materialInfo.constants));
	else
		fragmentInstanceID = shaderSystem.CreateShaderInstance(fragmentShaderID);

	uint32_t pipelineIndex = m_graphicsPipelineIDs.size();
	GraphicsPipelineInfo info(m_renderPass, m_imageExtent);
	info.blendEnable = materialInfo.isTransparent;
	m_graphicsPipelineIDs.push_back(m_graphicsPipelineSystem->CreateGraphicsPipeline(
		vertexInstanceID, fragmentInstanceID, info
	));
	auto [it, wasAdded] = m_materialHashToPipelineIndex.emplace(materialInfo.Hash(), pipelineIndex);
	return m_graphicsPipelineIDs[it->second];
}
