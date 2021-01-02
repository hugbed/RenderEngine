#pragma once

#include "GraphicsPipeline.h"
#include "RenderPass.h"
#include "TextureCache.h"
#include "DescriptorSetLayouts.h"
#include "hash.h"
#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <gsl/pointers>
#include <gsl/span>

#include <vector>
#include <map>
#include <memory>
#include <string_view>

class RenderPass;

struct PhongMaterialProperties
{
	glm::aligned_vec4 diffuse;
	glm::aligned_vec4 specular;
	glm::aligned_float32 shininess;
};

struct EnvironmentMaterialProperties
{
	glm::aligned_float32 ior;
	glm::aligned_float32 metallic; // reflection {0, 1}
	glm::aligned_float32 transmission; // refraction [0..1]
	glm::aligned_int32 cubeMapTexture;
};

enum class PhongMaterialTextures : uint8_t
{
	eDiffuse = 0,
	eSpecular,
	eCount
};

struct LitMaterialProperties
{
	PhongMaterialProperties phong;
	EnvironmentMaterialProperties env;
	glm::aligned_int32 textures[(uint8_t)PhongMaterialTextures::eCount];
};

struct Material
{
	// Each shading model can have different view descriptors
	enum class ShadingModel
	{
		Unlit = 0,
		Lit = 1,
		Count
	};

	enum class Type
	{
		Textured = 0,
		Phong = 1,
		Count
	};

	static constexpr ShadingModel kShadingModel[] = {
		ShadingModel::Unlit,
		ShadingModel::Lit
	};
};

struct LitMaterialConstants
{
	uint32_t nbLights = 1;
	uint32_t nbSamplers2D = 64;
	uint32_t nbSamplersCube = 64;
};

struct LitMaterialPipelineProperties
{
	bool isTransparent = false;
};

struct LitMaterialInstanceInfo
{
	LitMaterialConstants constants;
	LitMaterialProperties properties;
	LitMaterialPipelineProperties pipelineProperties;
};

using MaterialInstanceID = uint32_t;

/* Loads and creates resources for base materials so that
 * material instance creation reuses graphics pipelines 
 * and shaders whenever possible. Owns materials to update them
 * when resources are reset. */
class MaterialSystem
{
public:
	static constexpr Material::ShadingModel kShadingModel = Material::ShadingModel::Lit; // todo: template by ConstantType, PropertiesType
	static constexpr char* kVertexShader = "primitive_vert.spv";
	static constexpr char* kFragmentShader = "surface_frag.spv";

	using TextureNamesView = gsl::span<std::string, (uint8_t)PhongMaterialTextures::eCount>;

	MaterialSystem(
		vk::RenderPass renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineSystem& graphicsPipelineSystem,
		TextureCache& textureCache
	);

	void Reset(vk::RenderPass renderPass, vk::Extent2D extent);
	
	MaterialInstanceID CreateMaterialInstance(const LitMaterialInstanceInfo& materialInfo);

	void UploadToGPU(CommandBufferPool& commandBufferPool);

	// -- Getters -- //

	const GraphicsPipelineSystem& GetGraphicsPipelineSystem() const { return *m_graphicsPipelineSystem; }

	const std::vector<GraphicsPipelineID>& GetGraphicsPipelinesIDs() const { return m_graphicsPipelineIDs; }
	
	GraphicsPipelineID GetGraphicsPipelineID(MaterialInstanceID materialInstanceID) const { return m_graphicsPipelineIDs[materialInstanceID]; }
	
	vk::DescriptorSetLayout GetDescriptorSetLayout(DescriptorSetIndex setIndex) const;

	vk::DescriptorSet GetDescriptorSet(DescriptorSetIndex setIndex, uint8_t imageIndex = 0) const { return m_descriptorSets[(size_t)setIndex][imageIndex].get(); }
	
	const UniqueBufferWithStaging& GetUniformBuffer() { return *m_uniformBuffer; }

	bool IsTransparent(MaterialInstanceID materialInstanceID) const { return m_pipelineProperties[materialInstanceID].isTransparent; }

private:
	GraphicsPipelineID LoadGraphicsPipeline(const LitMaterialInstanceInfo& materialInfo);

	void UploadUniformBuffer(CommandBufferPool& commandBufferPool);
	
	void CreateDescriptorSets(CommandBufferPool& commandBufferPool);
	void CreateDescriptorPool(uint8_t numConcurrentFrames);
	void UpdateDescriptorSets();

	vk::RenderPass m_renderPass; // light/color pass, there could be others
	vk::Extent2D m_imageExtent;

	gsl::not_null<TextureCache*> m_textureCache;
	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	std::map<uint64_t, MaterialInstanceID> m_materialHashToInstanceID;

	// MaterialInstanceID -> Array Index
	std::vector<GraphicsPipelineID> m_graphicsPipelineIDs;
	std::vector<LitMaterialConstants> m_constants;
	std::vector<LitMaterialProperties> m_properties;
	std::vector<LitMaterialPipelineProperties> m_pipelineProperties;

	// GPU resources
	std::unique_ptr<UniqueBufferWithStaging> m_uniformBuffer; // containing all LitMaterialProperties
	std::array<std::vector<vk::UniqueDescriptorSet>, (size_t)DescriptorSetIndex::Count> m_descriptorSets; // [descriptorSetIndex][frame]
	vk::UniqueDescriptorPool m_descriptorPool;
};
