#pragma once

#include "Light.h"
#include "ShaderCache.h"
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

class ShadowMap
{
public:
	ShadowMap(vk::Extent2D extent, const Light& light, ShaderCache& shaderCache, const Scene& scene);

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

private:
	void CreateDepthImage();

	void CreateSampler();

	void CreateRenderPass();

	void CreateFramebuffer();

	void CreateGraphicsPipeline();

	void CreateDescriptorPool();

	void CreateDescriptorSets();

	void CreateViewUniformBuffers();

	void UpdateViewUniforms();

private:
	const char* vertexShaderFile = "shadow_map_vert.spv";
	const char* fragmentShaderFile = "shadow_map_frag.spv";

	vk::Extent2D m_extent;
	ShaderCache* m_shaderCache{ nullptr }; // usually shared by all shadow maps
	const Scene* m_scene{ nullptr };
	Light m_light;

	std::unique_ptr<Image> m_depthImage;
	vk::UniqueSampler m_sampler;
	vk::UniqueRenderPass m_renderPass;
	vk::UniqueFramebuffer m_framebuffer;
	std::unique_ptr<GraphicsPipeline> m_graphicsPipeline;

	ViewUniforms m_viewUniforms;
	vk::UniqueDescriptorPool m_descriptorPool;
	vk::UniqueDescriptorSet m_viewDescriptorSet;
	std::unique_ptr<UniqueBuffer> m_viewUniformBuffer;
};
