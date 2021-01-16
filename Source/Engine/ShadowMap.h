#pragma once

#include "Light.h"
#include "Scene.h"

#include "RenderPass.h"
#include "Swapchain.h"
#include "GraphicsPipeline.h"
#include "Framebuffer.h"
#include "Texture.h"
#include "Image.h"
#include "Shader.h"

#include "Device.h"
#include "PhysicalDevice.h"
#include "hash.h"

#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <gsl/pointers>

class ShadowMap
{
public:
	struct VertexShaderConstants
	{
		uint32_t nbModels = 64;
	};

	ShadowMap(
		vk::Extent2D extent,
		const PhongLight& light,
		GraphicsPipelineSystem& graphicsPipelineSystem,
		ModelSystem& modelSystem,
		const Scene& scene,
		VertexShaderConstants constants
	);

	void Reset(vk::Extent2D extent);

	void Render(vk::CommandBuffer& commandBuffer, uint32_t frameIndex) const;

	CombinedImageSampler GetCombinedImageSampler() const
	{
		return CombinedImageSampler{ m_depthImage.get(), m_sampler.get() };
	}

	glm::mat4 GetLightTransform() const
	{
		return m_viewUniforms.proj * m_viewUniforms.view;
	}

	vk::DescriptorSet GetDescriptorSet(DescriptorSetIndex setIndex) const
	{
		return m_descriptorSets[(size_t)setIndex].get();
	}

	// Set 0
	void UpdateViewUniforms();

private:
	void Draw(vk::CommandBuffer& commandBuffer, const std::vector<MeshDrawInfo>& drawCalls) const;

	void CreateDepthImage();

	void CreateSampler();

	void CreateRenderPass();

	void CreateFramebuffer();

	void CreateGraphicsPipeline();

	void CreateDescriptorPool();

	void CreateDescriptorSets();

	void CreateViewUniformBuffers();

	void UpdateDescriptorSets();

private:
	static constexpr char* vertexShaderFile = "shadow_map_vert.spv";
	static constexpr char* fragmentShaderFile = "shadow_map_frag.spv";

	vk::Extent2D m_extent;
	const Scene* m_scene{ nullptr };
	PhongLight m_light;

	std::unique_ptr<Image> m_depthImage;
	vk::UniqueSampler m_sampler;
	vk::UniqueRenderPass m_renderPass;
	vk::UniqueFramebuffer m_framebuffer;

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	gsl::not_null<ModelSystem*> m_modelSystem;
	GraphicsPipelineID m_graphicsPipelineID;

	VertexShaderConstants m_constants;
	LitViewProperties m_viewUniforms;
	vk::UniqueDescriptorPool m_descriptorPool; // todo: group descriptor pools
	std::array<vk::UniqueDescriptorSet, 2> m_descriptorSets; // View, Model
	std::unique_ptr<UniqueBuffer> m_viewUniformBuffer;
};
