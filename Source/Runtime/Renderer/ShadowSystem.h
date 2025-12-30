#pragma once

#include <BoundingBox.h>
#include <Renderer/Camera.h>
#include <Renderer/LightSystem.h>
#include <Renderer/MeshAllocator.h>
#include <RHI/RenderPass.h>
#include <RHI/Swapchain.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/ShaderCache.h>
#include <RHI/Framebuffer.h>
#include <RHI/Texture.h>
#include <RHI/Image.h>
#include <RHI/Device.h>
#include <RHI/PhysicalDevice.h>
#include <AssetPath.h>
#include <hash.h>
#include <glm_includes.h>
#include <vulkan/vulkan.hpp>
#include <gsl/pointers>

class Scene;
struct ViewProperties;
struct CombinedImageSampler;
class CommandRingBuffer;
class RenderState;

struct ShadowData
{
	glm::mat4 transform;
};

using ShadowID = uint32_t;

class ShadowSystem
{
public:
	ShadowSystem(
		vk::Extent2D extent,
		GraphicsPipelineCache& graphicsPipelineCache,
		BindlessDescriptors& bindlessDescriptors,
		BindlessDrawParams& bindlessDrawParams,
		MeshAllocator& meshAllocator,
		SceneTree& sceneTree,
		LightSystem& lightSystem
	);

	IMPLEMENT_MOVABLE_ONLY(ShadowSystem)

	void Reset(vk::Extent2D extent);

	ShadowID CreateShadowMap(LightID lightID);

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);
	
	void Update(const Camera& camera, BoundingBox sceneBoundingBox);

	void Render(RenderState& renderState, const std::vector<MeshDrawInfo> drawCommands) const;

	size_t GetShadowCount() const { return m_lights.size(); }

	glm::mat4 GetLightTransform(ShadowID id) const;

	SmallVector<vk::DescriptorImageInfo, 16> GetTexturesInfo() const;

	BufferHandle GetMaterialShadowsBufferHandle() const { return m_materialShadowsBufferHandle; }

	CombinedImageSampler GetCombinedImageSampler(ShadowID id) const;

private:
	// Only created when we know the VertexShaderConstants
	void CreateGraphicsPipeline();

private:
	struct MaterialShadow
	{
		glm::aligned_mat4 transform;
		TextureHandle shadowMapTextureHandle = TextureHandle::Invalid;
		uint32_t padding[3];
	};

	struct ShadowMapDrawParams
	{
		BufferHandle meshTransforms;
		BufferHandle shadowViews;
		uint32_t padding[2];
	};
	ShadowMapDrawParams m_drawParams;
	BindlessDrawParamsHandle m_drawParamsHandle = BindlessDrawParamsHandle::Invalid;

	static const AssetPath kVertexShaderFile;
	static const AssetPath kFragmentShaderFile;

	vk::Extent2D m_extent;
	vk::UniqueRenderPass m_renderPass;

	gsl::not_null<GraphicsPipelineCache*> m_graphicsPipelineCache;
	gsl::not_null<MeshAllocator*> m_meshAllocator;
	gsl::not_null<SceneTree*> m_sceneTree;
	gsl::not_null<LightSystem*> m_lightSystem;
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;

	// ShadowID -> Array index
	std::vector<LightID> m_lights; // casting shadows
	std::vector<ViewProperties> m_shadowViews;
	std::vector<MaterialShadow> m_materialShadows;
	std::vector<std::unique_ptr<Image>> m_depthImages; // todo: replace with Image (remove nullptr)
	std::vector<vk::UniqueFramebuffer> m_framebuffers;

	// Use these resources for all shadow map rendering
	vk::UniqueSampler m_sampler; // use the same sampler for all images
	GraphicsPipelineID m_graphicsPipelineID; // all shadows use the same shaders
	std::unique_ptr<UniqueBuffer> m_shadowViewsBuffer; // for rendering shadow maps
	std::unique_ptr<UniqueBuffer> m_materialShadowsBuffer; // for using shadows in material rendering
	BufferHandle m_materialShadowsBufferHandle = BufferHandle::Invalid;
};
