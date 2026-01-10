#pragma once

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
class LightSystem;
class Swapchain;

// todo: find better naming for those structures

enum class MaterialTextureType : uint8_t
{
	eBaseColor = 0,
	eEmissive,
	eOcclusionMetallicRoughness,
	eNormals,
	eAmbientOcclusion,
	eCount
};

struct MaterialProperties
{
	glm::vec4 baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // linear RGB [0..1]
	glm::vec4 emissive = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // linear RGB [0..1] + exposure compensation
	float f0 = 0.04f; // [0..1]
	float metallic = 1.0f; // [0..1]
	float perceptualRoughness = 1.0f; // [0..1]
	float ambientOcclusion = 1.0f; // [0..1]
	TextureHandle textures[static_cast<uint8_t>(MaterialTextureType::eCount)];
	uint32_t padding[3];
};

enum class AlphaMode
{
	eOpaque,
	eMask,
	eBlend,
};

struct MaterialPipelineProperties
{
	AlphaMode alphaMode;
};

struct MaterialInstanceInfo
{
	MaterialProperties properties = {};
	MaterialPipelineProperties pipelineProperties = {};
};

/* Loads and creates resources for base materials so that
 * material instance creation reuses graphics pipelines 
 * and shaders whenever possible. Owns materials to update them
 * when resources are reset. */
class MaterialSystem
{
public:
	static const AssetPath kVertexShader;
	static const AssetPath kFragmentShader;

	// todo (hbedard): actually just pass the render scene
	MaterialSystem(
		const Swapchain& swapchain,
		GraphicsPipelineCache& graphicsPipelineCache,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams,
		SceneTree& sceneTree,
		LightSystem& lightSystem,
		ShadowSystem& shadowSystem
	);

	IMPLEMENT_MOVABLE_ONLY(MaterialSystem)

	void Reset(const Swapchain& swapchain);
	
	void Draw(RenderCommandEncoder& renderCommandEncoder, gsl::span<const MeshDrawInfo> drawCalls) const;

	void SetViewBufferHandles(gsl::span<const BufferHandle> viewBufferHandles);

	// Reserve a material ID for a given set of material properties
	// The graphics pipeline and GPU resources will not be created until UploadToGPU is called
	MaterialHandle CreateMaterialInstance(const MaterialInstanceInfo& materialInfo);

	// This can be called once the total number of resources is known (constants)
	// So that actual GPU resources are created and uploaded
	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	// -- Getters -- //

	size_t GetMaterialInstanceCount() const { return m_properties.size(); }

	const GraphicsPipelineCache& GetGraphicsPipelineSystem() const { return *m_graphicsPipelineCache; }

	const std::vector<GraphicsPipelineID>& GetGraphicsPipelinesIDs() const { return m_graphicsPipelineIDs; }
	
	GraphicsPipelineID GetGraphicsPipelineID(MaterialHandle materialHandle) const { return m_graphicsPipelineIDs[materialHandle.GetIndex()]; }

	bool IsTranslucent(MaterialHandle materialHandle) const { return m_pipelineProperties[materialHandle.GetIndex()].alphaMode == AlphaMode::eBlend; }

	BufferHandle GetUniformBufferHandle() const { return m_uniformBufferHandle; }

	vk::PipelineLayout GetPipelineLayout() const;

private:
	struct MaterialDrawParams
	{
		BufferHandle view = BufferHandle::Invalid;
		BufferHandle transforms = BufferHandle::Invalid;
		BufferHandle lights = BufferHandle::Invalid;
		uint32_t lightCount = 0;
		BufferHandle materials = BufferHandle::Invalid;
		BufferHandle shadowTransforms = BufferHandle::Invalid;
		uint32_t padding[2] = { 0, 0 };
	};
	MaterialDrawParams m_drawParams;
	BindlessDrawParamsHandle m_drawParamsHandle;
	std::vector<BufferHandle> m_viewBufferHandles;

	GraphicsPipelineID LoadGraphicsPipeline(const MaterialInstanceInfo& materialInfo);

	void CreatePendingInstances();
	void CreateAndUploadStorageBuffer(CommandRingBuffer& commandRingBuffer);

	gsl::not_null<const Swapchain*> m_swapchain;
	gsl::not_null<GraphicsPipelineCache*> m_graphicsPipelineCache;
	gsl::not_null<SceneTree*> m_sceneTree;
	gsl::not_null<LightSystem*> m_lightSystem;
	gsl::not_null<ShadowSystem*> m_shadowSystem;

	std::map<uint64_t, MaterialHandle> m_materialHashToHandle;

	// MaterialInstanceID -> Array Index
	std::vector<MaterialInstanceInfo> m_materialInstanceInfo;
	std::vector<GraphicsPipelineID> m_graphicsPipelineIDs;
	std::vector<MaterialProperties> m_properties;
	std::vector<MaterialPipelineProperties> m_pipelineProperties;

	std::vector<std::pair<MaterialHandle, MaterialInstanceInfo>> m_toInstantiate;

	// GPU resources
	std::unique_ptr<UniqueBufferWithStaging> m_storageBuffer; // containing all MaterialProperties
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;
	BufferHandle m_uniformBufferHandle = BufferHandle::Invalid;
	vk::PipelineLayout m_pipelineLayout;

	MaterialHandle m_nextHandle;
};
