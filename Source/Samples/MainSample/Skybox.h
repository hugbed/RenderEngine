#pragma once

#include "Texture.h"
#include "GraphicsPipeline.h"
#include "Shader.h"

#include "TextureCache.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class TextureCache;
class CommandBufferPool;
class RenderPass;

class Skybox
{
public:
	Skybox(const RenderPass& renderPass, TextureCache* textureCache, vk::Extent2D swapchainExtent);

	void UploadToGPU(vk::CommandBuffer& commandBuffer, CommandBufferPool& commandBufferPool);

	void Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent);

	void Draw(vk::CommandBuffer& commandBuffer, uint32_t frameIndex);

	CombinedImageSampler GetCubeMap() const { return cubeMap; }

	const GraphicsPipeline& GetGraphicsPipeline() const { return *pipeline; }

private:
	void CreateDescriptors();
	void UpdateDescriptors();

	TextureCache* m_textureCache{ nullptr };
	CombinedImageSampler cubeMap;
	std::unique_ptr<GraphicsPipeline> pipeline;
	std::unique_ptr<Shader> vertexShader;
	std::unique_ptr<Shader> fragmentShader;
	std::unique_ptr<UniqueBufferWithStaging> vertexBuffer;
	vk::UniqueDescriptorPool descriptorPool; // consider merging with global pool
	
	std::vector<vk::UniqueDescriptorSet> cubeDescriptorSets;
};
