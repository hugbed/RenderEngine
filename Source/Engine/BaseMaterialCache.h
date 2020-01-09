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
	VkRenderPass renderPass;
	MaterialConstants constants;
	bool isTransparent = false;

	uint64_t Hash() const { return fnv_hash(this); }
};

/* Loads and creates resources for base materials so that
 * material instance creation reuses graphics pipelines 
 * and shaders whenever possible. */
class BaseMaterialCache
{
public:
	// We need a graphics pipeline for each MaterialInfo
	BaseMaterialCache(vk::Extent2D swapchainExtent);

	// Invalidates all materials
	void Reset(vk::Extent2D extent) { m_graphicsPipelines.clear(); }
	
	std::unique_ptr<Material> CreateMaterial(const MaterialInfo& materialInfo);

	const BaseMaterialInfo& GetBaseMaterialInfo(BaseMaterialID baseMaterial) const
	{
		return m_baseMaterialsInfo[(size_t)baseMaterial];
	}

	std::vector<const GraphicsPipeline*> GetGraphicsPipelines() const;

private:
	Shader& LoadShader(const std::string& filename);

	vk::Extent2D m_imageExtent;
	std::vector<BaseMaterialInfo> m_baseMaterialsInfo;
	std::map<std::string, std::unique_ptr<Shader>> m_shaders;
	std::map<uint64_t, std::unique_ptr<GraphicsPipeline>> m_graphicsPipelines;
};
