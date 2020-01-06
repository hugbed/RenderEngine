#pragma once

#include "Texture.h"

#include <vulkan/vulkan.hpp>

#include <map>
#include <string>
#include <memory>

class CommandBufferPool;

struct CombinedImageSampler
{
	Texture* texture = nullptr;
	vk::Sampler sampler = nullptr;
};

// todo: maybe rename to texture cache
// todo: possibly support fetching textures from different threads
// todo: maybe keep references to texture that haven't been uploaded to GPU
//       and expose a function to upload them all at once instead
class TextureManager
{
public:
	TextureManager(std::string basePath, CommandBufferPool* commandBufferPool)
		: m_basePath(std::move(basePath))
		, m_commandBufferPool(commandBufferPool)
	{}

	// todo: support loading as sRGB vs linear for different texture types

	// todo: allow being able to load the texture and upload to GPU later (if it hasn't been done already)
	// to allow loading the texture from file when we don't necessarily have a command buffer

	CombinedImageSampler LoadTexture(vk::CommandBuffer& commandBuffer, const std::string filename);

	CombinedImageSampler LoadCubeMapFaces(vk::CommandBuffer& commandBuffer, const std::vector<std::string>& filenames);

	Texture* CreateAndUploadTextureImage(vk::CommandBuffer& commandBuffer, const std::string& filename);

	vk::Sampler CreateSampler(uint32_t nbMipLevels);

	CommandBufferPool* GetCommandBufferPool() { return m_commandBufferPool; }

private:
	std::string m_basePath;
	CommandBufferPool* m_commandBufferPool{ nullptr };
	std::map<std::string, std::unique_ptr<Texture>> m_textures; // todo: have a better texture ID than the filename
	std::map<uint32_t, vk::UniqueSampler> m_samplers; // per mip level
};
