#include "BaseMaterialCache.h"

#include "Shader.h"
#include "GraphicsPipeline.h"

BaseMaterialCache::BaseMaterialCache(vk::Extent2D swapchainExtent)
	: m_imageExtent(swapchainExtent)
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

std::unique_ptr<Material> BaseMaterialCache::CreateMaterial(const MaterialInfo& materialInfo)
{
	const auto& materialDescription = m_baseMaterialsInfo[(size_t)materialInfo.baseMaterial];

	auto material = std::make_unique<Material>();
	material->shadingModel = materialDescription.shadingModel;
	material->isTransparent = materialInfo.isTransparent;

	auto pipelineIt = m_graphicsPipelines.find(materialInfo.Hash());
	if (pipelineIt != m_graphicsPipelines.end())
	{
		material->pipeline = pipelineIt->second.get();
	}
	else
	{
		auto& vertexShader = LoadShader(materialDescription.vertexShader);
		auto& fragmentShader = LoadShader(materialDescription.fragmentShader);
		if (materialDescription.shadingModel == ShadingModel::Lit)
			fragmentShader.SetSpecializationConstants(materialInfo.constants);

		GraphicsPipelineInfo info;
		info.blendEnable = materialInfo.isTransparent;
		auto [it, wasAdded] = m_graphicsPipelines.emplace(materialInfo.Hash(),
			std::make_unique<GraphicsPipeline>(
				materialInfo.renderPass, m_imageExtent,
				vertexShader, fragmentShader,
				info
			));

		material->pipeline = it->second.get();
	}

	return material;
}

std::vector<const GraphicsPipeline*> BaseMaterialCache::GetGraphicsPipelines() const
{
	std::vector<const GraphicsPipeline*> pipelines;
	pipelines.reserve(m_graphicsPipelines.size());
	for (const auto& pipelineItem : m_graphicsPipelines)
		pipelines.push_back(pipelineItem.second.get());
	return pipelines;
}

Shader& BaseMaterialCache::LoadShader(const std::string& filename)
{
	auto shaderIt = m_shaders.find(filename);
	if (shaderIt != m_shaders.end())
		return *(shaderIt->second);

	auto shader = std::make_unique<Shader>(filename, "main");
	auto [it, wasAdded] = m_shaders.emplace(filename, std::move(shader));
	return *it->second;
}
