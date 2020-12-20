#pragma once

#include "Material.h"
#include "hash.h"

#include <vulkan/vulkan.hpp>

#include <vector>
#include <map>
#include <memory>

class Shader;
class GraphicsPipeline;
class RenderPass;

enum class BaseMaterialID
{
	Textured = 0,
	Phong = 1,
	Count
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
	MaterialCache(vk::RenderPass renderPass, vk::Extent2D swapchainExtent);

	void Reset(vk::RenderPass renderPass, vk::Extent2D extent);
	
	Material* CreateMaterial(const MaterialInfo& materialInfo);

	const BaseMaterialInfo& GetBaseMaterialInfo(BaseMaterialID baseMaterial) const
	{
		return m_baseMaterialsInfo[(size_t)baseMaterial];
	}

	std::vector<const GraphicsPipeline*> GetGraphicsPipelines() const;

private:
	GraphicsPipeline* LoadGraphicsPipeline(const MaterialInfo& materialInfo);

	vk::RenderPass m_renderPass; // light/color pass, there could be others
	vk::Extent2D m_imageExtent;
	std::vector<BaseMaterialInfo> m_baseMaterialsInfo;
	ShaderSystem m_shaderSystem; // todo: share shader system between systems
	std::map<uint64_t, std::unique_ptr<GraphicsPipeline>> m_graphicsPipelines;

	// Keep ownership of materials since we need to be able to update their
	// graphics pipeline when it's invalidated
	std::vector<MaterialInfo> m_materialsInfo;
	std::vector<std::unique_ptr<Material>> m_materials;
};
