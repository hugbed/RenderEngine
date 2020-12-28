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

// Each shading model can have different view descriptors
enum class ShadingModel
{
	Unlit = 0,
	Lit = 1,
	Count
};

enum class BaseMaterialID
{
	Textured = 0,
	Phong = 1,
	Count
};

struct Material
{
#ifdef DEBUG_MODE
	std::string name;
#endif

	ShadingModel shadingModel;
	bool isTransparent = false;
	GraphicsPipelineID pipelineID;

	// Per-material descriptors
	std::unique_ptr<UniqueBufferWithStaging> uniformBuffer;
	vk::UniqueDescriptorSet descriptorSet;
};

struct BaseMaterialInfo
{
	ShadingModel shadingModel;
	std::string vertexShader;
	std::string fragmentShader;
};

struct MaterialConstants
{
	uint32_t nbLights = 1;
	uint32_t nbTextures2D = 64;
	uint32_t nbTexturesCube = 64;
};

struct MaterialInfo
{
	BaseMaterialID baseMaterial;
	MaterialConstants constants;
	bool isTransparent = false;

	uint64_t Hash() const { return fnv_hash(this); }
};

/* Loads and creates resources for base materials so that
 * material instance creation reuses graphics pipelines 
 * and shaders whenever possible. Owns materials to update them
 * when resources are reset. */
class MaterialCache
{
public:
	// We need a graphics pipeline for each MaterialInfo
	MaterialCache(
		vk::RenderPass renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineSystem& graphicsPipelineSystem
	);

	void Reset(vk::RenderPass renderPass, vk::Extent2D extent);
	
	Material* CreateMaterial(const MaterialInfo& materialInfo);

	const BaseMaterialInfo& GetBaseMaterialInfo(BaseMaterialID baseMaterial) const
	{
		return m_baseMaterialsInfo[(size_t)baseMaterial];
	}

	const std::vector<GraphicsPipelineID>& GetGraphicsPipelinesIDs() const { return m_graphicsPipelineIDs; }

	const GraphicsPipelineSystem& GetGraphicsPipelineSystem() const { return *m_graphicsPipelineSystem; }

private:
	GraphicsPipelineID LoadGraphicsPipeline(const MaterialInfo& materialInfo);

	vk::RenderPass m_renderPass; // light/color pass, there could be others
	vk::Extent2D m_imageExtent;
	std::vector<BaseMaterialInfo> m_baseMaterialsInfo;

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	std::vector<GraphicsPipelineID> m_graphicsPipelineIDs; // [materialInstance] -> pipelineID
	std::map<uint64_t, uint32_t> m_materialHashToPipelineIndex; // [materialHash] -> index of m_pipelineIDs

	// Keep ownership of materials since we need to be able to update their
	// graphics pipeline when it's invalidated
	std::vector<MaterialInfo> m_materialsInfo;
	std::vector<std::unique_ptr<Material>> m_materials;
};
