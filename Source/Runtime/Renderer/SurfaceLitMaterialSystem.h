#pragma once

#include <Renderer/LightSystem.h>
#include <Renderer/TextureCache.h>
#include <Renderer/MeshAllocator.h>
#include <Renderer/MaterialDefines.h>
#include <RHI/GraphicsPipelineCache.h>
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
class RenderCommandEncoder;

// todo: find better naming for those structures

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
	TextureHandle textures[(uint8_t)PhongMaterialTextures::eCount];
};

struct LitMaterialPipelineProperties
{
	bool isTransparent = false;
};

struct LitMaterialInstanceInfo
{
	LitMaterialProperties properties = {};
	LitMaterialPipelineProperties pipelineProperties = {};
};

/* Loads and creates resources for base materials so that
 * material instance creation reuses graphics pipelines 
 * and shaders whenever possible. Owns materials to update them
 * when resources are reset. */
class SurfaceLitMaterialSystem
{
public:
	static const AssetPath kVertexShader;
	static const AssetPath kFragmentShader;

	// todo (hbedard): actually just pass the renderer
	SurfaceLitMaterialSystem(
		vk::RenderPass renderPass,
		vk::Extent2D swapchainExtent,
		GraphicsPipelineCache& graphicsPipelineCache,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams,
		SceneTree& sceneTree,
		LightSystem& lightSystem,
		ShadowSystem& shadowSystem
	);

	IMPLEMENT_MOVABLE_ONLY(SurfaceLitMaterialSystem)

	void Reset(vk::RenderPass renderPass, vk::Extent2D extent);
	
	void Draw(RenderCommandEncoder& renderCommandEncoder, gsl::span<const MeshDrawInfo> drawCalls) const;

	void SetViewBufferHandles(gsl::span<const BufferHandle> viewBufferHandles);

	// Reserve a material ID for a given set of material properties
	// The graphics pipeline and GPU resources will not be created until UploadToGPU is called
	MaterialHandle CreateMaterialInstance(const LitMaterialInstanceInfo& materialInfo);

	// This can be called once the total number of resources is known (constants)
	// So that actual GPU resources are created and uploaded
	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	// -- Getters -- //

	size_t GetMaterialInstanceCount() const { return m_properties.size(); }

	const GraphicsPipelineCache& GetGraphicsPipelineSystem() const { return *m_graphicsPipelineCache; }

	const std::vector<GraphicsPipelineID>& GetGraphicsPipelinesIDs() const { return m_graphicsPipelineIDs; }
	
	GraphicsPipelineID GetGraphicsPipelineID(MaterialHandle materialHandle) const { return m_graphicsPipelineIDs[materialHandle.GetIndex()]; }

	bool IsTransparent(MaterialHandle materialHandle) const { return m_pipelineProperties[materialHandle.GetIndex()].isTransparent; }

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
		BufferHandle shadowTransforms;
		uint32_t padding[2];
	};
	SurfaceLitDrawParams m_drawParams;
	BindlessDrawParamsHandle m_drawParamsHandle;
	std::vector<BufferHandle> m_viewBufferHandles;

	GraphicsPipelineID LoadGraphicsPipeline(const LitMaterialInstanceInfo& materialInfo);

	void CreatePendingInstances();
	void CreateAndUploadUniformBuffer(CommandRingBuffer& commandRingBuffer);

	vk::RenderPass m_renderPass; // light/color pass, there could be others
	vk::Extent2D m_imageExtent;

	gsl::not_null<GraphicsPipelineCache*> m_graphicsPipelineCache;
	gsl::not_null<SceneTree*> m_sceneTree;
	gsl::not_null<LightSystem*> m_lightSystem;
	gsl::not_null<ShadowSystem*> m_shadowSystem;

	std::map<uint64_t, MaterialHandle> m_materialHashToHandle;

	// MaterialInstanceID -> Array Index
	std::vector<LitMaterialInstanceInfo> m_materialInstanceInfo;
	std::vector<GraphicsPipelineID> m_graphicsPipelineIDs;
	std::vector<LitMaterialProperties> m_properties;
	std::vector<LitMaterialPipelineProperties> m_pipelineProperties;

	std::vector<std::pair<MaterialHandle, LitMaterialInstanceInfo>> m_toInstantiate;

	// GPU resources
	std::unique_ptr<UniqueBufferWithStaging> m_uniformBuffer; // containing all LitMaterialProperties
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;
	BufferHandle m_uniformBufferHandle = BufferHandle::Invalid;
	vk::PipelineLayout m_pipelineLayout;

	MaterialHandle m_nextHandle;
};
