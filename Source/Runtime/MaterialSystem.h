#pragma once

#include "LightSystem.h"
#include "GraphicsPipelineSystem.h"
#include "TextureSystem.h"
#include "RenderPass.h"
#include "DescriptorSetLayouts.h"
#include "AssetPath.h"
#include "hash.h"
#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <gsl/pointers>
#include <gsl/span>

#include <vector>
#include <map>
#include <memory>
#include <string_view>
#include <filesystem>

class MeshAllocator;

// todo: find better naming for those structures

// todo: this could be shared across different material types
struct LitViewProperties
{
	glm::aligned_mat4 view;
	glm::aligned_mat4 proj;
	glm::aligned_vec3 pos;
};

enum class PhongLightType
{
	Directional = 1,
	Point = 2,
	Spot = 3,
	Count
};

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

struct LitMaterialPipelineProperties
{
	bool isTransparent = false;
};

struct LitMaterialInstanceInfo
{
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
	static const AssetPath kVertexShader;
	static const AssetPath kFragmentShader;

	struct ShaderConstants
	{
		uint32_t nbLights = 1;
		uint32_t nbShadowMaps = 12;
		uint32_t nbSamplers2D = 64;
		uint32_t nbSamplersCube = 64;
	};

	MaterialSystem(
		vk::RenderPass renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineSystem& graphicsPipelineSystem,
		TextureSystem& textureSystem,
		MeshAllocator& meshAllocator
	);

	~MaterialSystem();

	IMPLEMENT_MOVABLE_ONLY(MaterialSystem)

	void Reset(vk::RenderPass renderPass, vk::Extent2D extent);
	
	// Reserve a material ID for a given set of material properties
	// The graphics pipeline and GPU resources will not be created until UploadToGPU is called
	MaterialInstanceID CreateMaterialInstance(const LitMaterialInstanceInfo& materialInfo);

	// This can be called once the total number of resources is known (constants)
	// So that actual GPU resources are created and uploaded
	void UploadToGPU(CommandBufferPool& commandBufferPool, ShaderConstants shaderConstants);

	// Set 0
	void UpdateViewDescriptorSets(
		const VectorView<vk::Buffer>& viewUniformBuffers, size_t viewBufferSize,
		vk::Buffer lightsUniformBuffer, size_t lightsBufferSize
	) const;

	void UpdateShadowDescriptorSets(
		const SmallVector<vk::DescriptorImageInfo, 16>& shadowMaps,
		vk::Buffer shadowDataBuffer, size_t shadowDataBufferSize
	) const;

	// Set 1
	void UpdateSceneDescriptorSet(
		vk::Buffer sceneBuffer, size_t sceneBufferSize
	) const;

	// Set 2
	void UpdateMaterialDescriptorSet() const;

	// -- Getters -- //

	size_t GetMaterialInstanceCount() const { return m_properties.size(); }

	const GraphicsPipelineSystem& GetGraphicsPipelineSystem() const { return *m_graphicsPipelineSystem; }

	const std::vector<GraphicsPipelineID>& GetGraphicsPipelinesIDs() const { return m_graphicsPipelineIDs; }
	
	GraphicsPipelineID GetGraphicsPipelineID(MaterialInstanceID materialInstanceID) const { return m_graphicsPipelineIDs[materialInstanceID]; }

	vk::DescriptorSet GetDescriptorSet(DescriptorSetIndex setIndex, uint8_t imageIndex = 0) const { return m_descriptorSets[(size_t)setIndex][imageIndex].get(); }

	bool IsTransparent(MaterialInstanceID materialInstanceID) const { return m_pipelineProperties[materialInstanceID].isTransparent; }

private:
	GraphicsPipelineID LoadGraphicsPipeline(const LitMaterialInstanceInfo& materialInfo);

	void CreatePendingInstances();
	void CreateAndUploadUniformBuffer(CommandBufferPool& commandBufferPool);
	void CreateDescriptorSets(size_t nbConcurrentSubmits);
	void CreateDescriptorPool(uint8_t numConcurrentFrames);

	const UniqueBufferWithStaging& GetUniformBuffer() const { return *m_uniformBuffer; }

	vk::DescriptorSetLayout GetDescriptorSetLayout(DescriptorSetIndex setIndex) const;

	vk::RenderPass m_renderPass; // light/color pass, there could be others
	vk::Extent2D m_imageExtent;
	ShaderConstants m_constants;

	gsl::not_null<TextureSystem*> m_textureSystem;
	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	gsl::not_null<MeshAllocator*> m_meshAllocator;

	std::map<uint64_t, MaterialInstanceID> m_materialHashToInstanceID;

	// MaterialInstanceID -> Array Index
	std::vector<LitMaterialInstanceInfo> m_materialInstanceInfo;
	std::vector<GraphicsPipelineID> m_graphicsPipelineIDs;
	std::vector<LitMaterialProperties> m_properties;
	std::vector<LitMaterialPipelineProperties> m_pipelineProperties;

	std::vector<std::pair<MaterialInstanceID, LitMaterialInstanceInfo>> m_toInstantiate;

	// GPU resources
	std::unique_ptr<UniqueBufferWithStaging> m_uniformBuffer; // containing all LitMaterialProperties
	std::array<std::vector<vk::UniqueDescriptorSet>, (size_t)DescriptorSetIndex::Count> m_descriptorSets; // [descriptorSetIndex][frame]
	vk::UniqueDescriptorPool m_descriptorPool;

	MaterialInstanceID m_nextID = 0;
};
