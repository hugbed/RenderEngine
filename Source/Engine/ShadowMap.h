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
	ShadowMap(
		vk::Extent2D extent,
		const Light& light, 
		GraphicsPipelineSystem& graphicsPipelineSystem,
		const Scene& scene
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

	void UpdateViewUniforms();

private:
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
	const char* vertexShaderFile = "shadow_map_vert.spv";
	const char* fragmentShaderFile = "shadow_map_frag.spv";

	vk::Extent2D m_extent;
	const Scene* m_scene{ nullptr };
	Light m_light;

	std::unique_ptr<Image> m_depthImage;
	vk::UniqueSampler m_sampler;
	vk::UniqueRenderPass m_renderPass;
	vk::UniqueFramebuffer m_framebuffer;

	gsl::not_null<GraphicsPipelineSystem*> m_graphicsPipelineSystem;
	GraphicsPipelineID m_graphicsPipelineID;

	ViewUniforms m_viewUniforms;
	vk::UniqueDescriptorPool m_descriptorPool; // todo: group descriptor pools
	vk::UniqueDescriptorSet m_viewDescriptorSet;
	std::unique_ptr<UniqueBuffer> m_viewUniformBuffer;
};
