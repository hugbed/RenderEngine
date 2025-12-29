#pragma once

#include <Renderer/LightSystem.h>
#include <Renderer/TextureSystem.h>
#include <Renderer/DescriptorSetLayouts.h>
#include <Renderer/MeshAllocator.h>
#include <RHI/GraphicsPipelineSystem.h>
#include <RHI/RenderPass.h>
#include <AssetPath.h>
#include <hash.h>
#include <glm_includes.h>
#include <vulkan/vulkan.hpp>

#include <gsl/pointers>
#include <gsl/span>
#include <vector>
#include <map>
#include <memory>
#include <string_view>
#include <filesystem>

class MeshAllocator;
class BindlessDescriptors;
class SceneTree;
class ShadowSystem;
class RenderState;

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
	TextureHandle textures[(uint8_t)PhongMaterialTextures::eCount]; // todo (hbedard): should I used aligned everywhere?
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
	static constexpr Material::ShadingModel kShadingModel = Material::ShadingModel::Lit; // todo (hbedard): also shading model = surface
	static const AssetPath kVertexShader;
	static const AssetPath kFragmentShader;

	MaterialSystem(
		vk::RenderPass renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineSystem& graphicsPipelineSystem,
		TextureSystem& textureSystem,
		MeshAllocator& meshAllocator,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams
	);

	IMPLEMENT_MOVABLE_ONLY(MaterialSystem)

	void Reset(vk::RenderPass renderPass, vk::Extent2D extent);
	
	void Draw(RenderState& renderState, gsl::span<const MeshDrawInfo> drawCalls) const;

	void SetViewBufferHandles(gsl::span<BufferHandle> viewBufferHandles);

	// Reserve a material ID for a given set of material properties
	// The graphics pipeline and GPU resources will not be created until UploadToGPU is called
	MaterialInstanceID CreateMaterialInstance(const LitMaterialInstanceInfo& materialInfo);

	// This can be called once the total number of resources is known (constants)
	// So that actual GPU resources are created and uploaded
	void UploadToGPU(
		CommandBufferPool& commandBufferPool,
		gsl::not_null<SceneTree*> sceneTree,
		gsl::not_null<LightSystem*> lightSystem,
		gsl::not_null<ShadowSystem*> shadowSystem);

	// -- Getters -- //

	size_t GetMaterialInstanceCount() const { return m_properties.size(); }

	const GraphicsPipelineSystem& GetGraphicsPipelineSystem() const { return *m_graphicsPipelineSystem; }

	const std::vector<GraphicsPipelineID>& GetGraphicsPipelinesIDs() const { return m_graphicsPipelineIDs; }
	
	GraphicsPipelineID GetGraphicsPipelineID(MaterialInstanceID materialInstanceID) const { return m_graphicsPipelineIDs[materialInstanceID]; }

	bool IsTransparent(MaterialInstanceID materialInstanceID) const { return m_pipelineProperties[materialInstanceID].isTransparent; }

	BufferHandle GetUniformBufferHandle() const { return m_uniformBufferHandle; }

	vk::PipelineLayout GetPipelineLayout() const;

private:
	struct SurfaceLitDrawParams
	{
		BufferHandle view;
		BufferHandle transforms;
		BufferHandle lights;
		uint32_t lightCount;
		BufferHandle materials;
		BufferHandle shadowTransforms; // todo (hbedard): light transforms? what's this?
		// todo (hbedard): also handle textures through this!
		//TextureHandle textures2D; // material textures & shadow maps
		//TextureHandle texturesCube;
		uint32_t padding[2];
	};
	SurfaceLitDrawParams m_drawParams;
	BindlessDrawParamsHandle m_drawParamsHandle;
	std::vector<BufferHandle> m_viewBufferHandles;

	GraphicsPipelineID LoadGraphicsPipeline(const LitMaterialInstanceInfo& materialInfo);

	void CreatePendingInstances();
	void CreateAndUploadUniformBuffer(CommandBufferPool& commandBufferPool);

	vk::DescriptorSetLayout GetDescriptorSetLayout(DescriptorSetIndex setIndex) const;

	vk::RenderPass m_renderPass; // light/color pass, there could be others
	vk::Extent2D m_imageExtent;

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
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;
	BufferHandle m_uniformBufferHandle = BufferHandle::Invalid;
	vk::PipelineLayout m_pipelineLayout;

	MaterialInstanceID m_nextID = 0;
};
