#pragma once

#include "GraphicsPipeline.h"
#include "RenderPass.h"
#include "TextureCache.h"
#include "DescriptorSetLayouts.h"
#include "hash.h"

#include <vulkan/vulkan.hpp>

#include <gsl/pointers>

#include <vector>
#include <map>
#include <memory>

class Shader;
class GraphicsPipeline;
class RenderPass;

struct Material
{
	// Each shading model can have different view descriptors
	enum class ShadingModel
	{
		Unlit = 0,
		Lit = 1,
		Count
	};

	enum class ID
	{
		Textured = 0,
		Phong = 1,
		Count
	};

	static constexpr ShadingModel kShadingModel[] = {
		ShadingModel::Unlit,
		ShadingModel::Lit
	};

	static constexpr const char* kVertexShader[] = {
		"primitive_vert.spv",
		"primitive_vert.spv"
	};

	static constexpr const char* kFragmentShader[] = {
		"surface_unlit_frag.spv",
		"surface_frag.spv"
	};
};

struct MaterialConstants
{
	uint32_t nbLights = 1;
};

struct MaterialInstanceInfo
{
	Material::ID materialID;
	MaterialConstants constants;
	bool isTransparent = false;

	uint64_t Hash() const { return fnv_hash(this); }
};

using MaterialInstanceID = uint32_t;

/* Loads and creates resources for base materials so that
 * material instance creation reuses graphics pipelines 
 * and shaders whenever possible. Owns materials to update them
 * when resources are reset. */
class MaterialSystem
{
public:
	MaterialSystem(
		vk::RenderPass renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineSystem& graphicsPipelineSystem
	);

	void Reset(vk::RenderPass renderPass, vk::Extent2D extent);
	
	MaterialInstanceID CreateMaterialInstance(const MaterialInstanceInfo& materialInfo);

	const std::vector<GraphicsPipelineID>& GetGraphicsPipelinesIDs() const { return m_graphicsPipelineIDs; }

	const GraphicsPipelineSystem& GetGraphicsPipelineSystem() const { return *m_graphicsPipelineSystem; }

	// todo: reorganize to navigate arrays instead of fetching these values one by one

	GraphicsPipelineID GetGraphicsPipelineID(MaterialInstanceID materialInstanceID) const { return m_graphicsPipelineIDs[materialInstanceID]; }

	vk::DescriptorSet GetDescriptorSet(MaterialInstanceID materialInstanceID) const { return m_descriptorSets[materialInstanceID].get(); }

	bool IsMaterialInstanceTransparent(MaterialInstanceID materialInstanceID) const { return m_isTransparent[materialInstanceID]; }

private:
	GraphicsPipelineID LoadGraphicsPipeline(const MaterialInstanceInfo& materialInfo);

	vk::RenderPass m_renderPass; // light/color pass, there could be others
	vk::Extent2D m_imageExtent;

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	std::map<uint64_t, MaterialInstanceID> m_materialHashToInstanceID;

	// MaterialInstanceID -> Array Index
	std::vector<MaterialConstants> m_materialConstants;
	std::vector<Material::ShadingModel> m_shadingModels;
	std::vector<GraphicsPipelineID> m_graphicsPipelineIDs;
	std::vector<bool> m_isTransparent;

	std::vector<std::vector<CombinedImageSampler>> m_textures;
	std::vector<std::vector<CombinedImageSampler>> m_cubeMaps;
	std::vector<UniqueBufferWithStaging> m_uniformBuffers;
	std::vector<vk::UniqueDescriptorSet> m_descriptorSets;

	MaterialInstanceID m_nextID = 0;
};
