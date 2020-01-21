#include "MaterialCache.h"

#include "Shader.h"
#include "GraphicsPipeline.h"

MaterialCache::MaterialCache(vk::RenderPass renderPass, vk::Extent2D swapchainExtent)
	: m_renderPass(renderPass)
	, m_imageExtent(swapchainExtent)
	, m_shaderCache()
{
	// All materials are based on the same "base materials"
	// Create a material description for each one of them.

	GraphicsPipelineInfo pipelineInfo;
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

	m_graphicsPipelines.clear();

	ASSERT(m_materials.size() == m_materialsInfo.size());
	for (size_t i = 0; i < m_materials.size(); ++i)
	{
		m_materials[i]->pipeline = LoadGraphicsPipeline(m_materialsInfo[i]);
	}
}

Material* MaterialCache::CreateMaterial(const MaterialInfo& materialInfo)
{
	const auto& materialDescription = m_baseMaterialsInfo[(size_t)materialInfo.baseMaterial];

	auto material = std::make_unique<Material>();
	material->shadingModel = materialDescription.shadingModel;
	material->isTransparent = materialInfo.isTransparent;
	material->pipeline = LoadGraphicsPipeline(materialInfo);

	m_materials.push_back(std::move(material));
	m_materialsInfo.push_back(materialInfo);
	return m_materials.back().get();
}

GraphicsPipeline* MaterialCache::LoadGraphicsPipeline(const MaterialInfo& materialInfo)
{
	auto pipelineIt = m_graphicsPipelines.find(materialInfo.Hash());
	if (pipelineIt != m_graphicsPipelines.end())
		return pipelineIt->second.get();

	const auto& materialDescription = m_baseMaterialsInfo[(size_t)materialInfo.baseMaterial];

	auto& vertexShader = m_shaderCache.Load(materialDescription.vertexShader);
	auto& fragmentShader = m_shaderCache.Load(materialDescription.fragmentShader);
	if (materialDescription.shadingModel == ShadingModel::Lit)
	{
		fragmentShader.SetSpecializationConstants(materialInfo.constants);
	}

	GraphicsPipelineInfo info;
	info.blendEnable = materialInfo.isTransparent;
	auto [it, wasAdded] = m_graphicsPipelines.emplace(materialInfo.Hash(),
		std::make_unique<GraphicsPipeline>(
			m_renderPass, m_imageExtent,
			vertexShader, fragmentShader,
			info
		));

	return it->second.get();
}

std::vector<const GraphicsPipeline*> MaterialCache::GetGraphicsPipelines() const
{
	std::vector<const GraphicsPipeline*> pipelines;
	pipelines.reserve(m_graphicsPipelines.size());
	for (const auto& pipelineItem : m_graphicsPipelines)
		pipelines.push_back(pipelineItem.second.get());
	return pipelines;
}
