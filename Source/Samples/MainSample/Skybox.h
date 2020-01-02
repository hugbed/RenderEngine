#pragma once

#include "Texture.h"
#include "GraphicsPipeline.h"
#include "Shader.h"

#include "TextureManager.h"

#include <vulkan/vulkan.hpp>

#include <vector>

class TextureManager;
class CommandBufferPool;
class RenderPass;

class Skybox
{
public:
	Skybox(const RenderPass& renderPass, vk::Extent2D swapchainExtent);

	void UploadToGPU(TextureManager* textureManager, vk::CommandBuffer& commandBuffer);

	void Reset(const RenderPass& renderPass, vk::Extent2D swapchainExtent);

	void Draw(vk::CommandBuffer& commandBuffer, uint32_t frameIndex);

private:
	void CreateDescriptors();
	void UpdateDescriptors();

	CombinedImageSampler cubeMap;
	std::unique_ptr<GraphicsPipeline> pipeline;
	std::unique_ptr<Shader> vertexShader;
	std::unique_ptr<Shader> fragmentShader;
	std::unique_ptr<UniqueBufferWithStaging> vertexBuffer;
	vk::UniqueDescriptorPool descriptorPool; // consider merging with global pool
	
	std::vector<vk::UniqueDescriptorSet> cubeDescriptorSets;
};
