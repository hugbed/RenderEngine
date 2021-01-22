#pragma once

#include "LightSystem.h"
#include "ModelSystem.h"
#include "Camera.h"
#include "BoundingBox.h"

#include "RenderPass.h"
#include "Swapchain.h"
#include "GraphicsPipelineSystem.h"
#include "ShaderSystem.h"
#include "Framebuffer.h"
#include "Texture.h"
#include "Image.h"

#include "Device.h"
#include "PhysicalDevice.h"
#include "hash.h"

#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <gsl/pointers>

class Scene;

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
		GraphicsPipelineSystem& graphicsPipelineSystem,
		ModelSystem& modelSystem,
		LightSystem& lightSystem
	);

	~ShadowSystem();

	IMPLEMENT_MOVABLE_ONLY(ShadowSystem)

	void Reset(vk::Extent2D extent);

	ShadowID CreateShadowMap(LightID lightID);

	void UploadToGPU();
	
	void Update(const Camera& camera, BoundingBox sceneBoundingBox);

	void Render(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, const std::vector<MeshDrawInfo> drawCommands) const;

	size_t GetShadowCount() const { return m_lights.size(); }

	CombinedImageSampler GetCombinedImageSampler(ShadowID id) const
	{
		return CombinedImageSampler{ m_depthImages[id].get(), m_sampler.get() };
	}

	glm::mat4 GetLightTransform(ShadowID id) const
	{
		return m_properties[id].proj * m_properties[id].view;
	}

	vk::DescriptorSet GetDescriptorSet(DescriptorSetIndex setIndex) const
	{
		return m_descriptorSets[(size_t)setIndex].get();
	}

	SmallVector<vk::DescriptorImageInfo, 16> GetTexturesInfo() const;

	const UniqueBuffer& GetShadowTransformsBuffer() const
	{
		return *m_transformsBuffer;
	}

private:
	// Common resources for all shadow maps
	void CreateDescriptorPool();
	
	// Only created when we know the VertexShaderConstants
	void CreateGraphicsPipeline();
	void CreateDescriptorSets();

private:
	static constexpr char* vertexShaderFile = "shadow_map_vert.spv";
	static constexpr char* fragmentShaderFile = "shadow_map_frag.spv";

	vk::Extent2D m_extent;
	vk::UniqueRenderPass m_renderPass;

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	gsl::not_null<ModelSystem*> m_modelSystem;
	gsl::not_null<LightSystem*> m_lightSystem;

	// ShadowID -> Array index
	std::vector<LightID> m_lights; // casting shadows
	std::vector<LitViewProperties> m_properties;
	std::vector<std::unique_ptr<Image>> m_depthImages; // todo: replace with Image (remove nullptr)
	std::vector<vk::UniqueFramebuffer> m_framebuffers;
	std::vector<glm::aligned_mat4> m_transforms;

	// Use these resources for all shadow map rendering
	vk::UniqueSampler m_sampler; // use the same sampler for all images
	GraphicsPipelineID m_graphicsPipelineID; // all shadows use the same shaders
	std::array<vk::UniqueDescriptorSet, 2> m_descriptorSets; // View, Model
	VkDescriptorPool m_descriptorPool;
	std::unique_ptr<UniqueBuffer> m_viewPropertiesBuffer; // for rendering shadow maps
	std::unique_ptr<UniqueBuffer> m_transformsBuffer; // for rendering shadows on materials
};
