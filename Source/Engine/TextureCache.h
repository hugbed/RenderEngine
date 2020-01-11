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

// todo: maybe keep references to texture that haven't been uploaded to GPU
//       and expose a function to upload them all at once instead
class TextureCache
{
public:
	TextureCache(std::string basePath)
		: m_basePath(std::move(basePath))
	{}

	// todo: support loading as sRGB vs linear for different texture types

	CombinedImageSampler LoadTexture(const std::string filename);

	CombinedImageSampler LoadCubeMapFaces(const std::vector<std::string>& filenames);

	Texture* CreateAndUploadTextureImage(const std::string& filename);

	vk::Sampler CreateSampler(uint32_t nbMipLevels);

	void UploadTextures(vk::CommandBuffer& commandBuffer, CommandBufferPool& commandBufferPool);

private:
	std::string m_basePath;
	std::map<uint64_t, std::unique_ptr<Texture>> m_textures;
	std::map<uint32_t, vk::UniqueSampler> m_samplers; // per mip level
	std::vector<Texture*> m_texturesToUpload;
};
