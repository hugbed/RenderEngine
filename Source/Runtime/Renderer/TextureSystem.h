#pragma once

#include <RHI/Texture.h>
#include <RHI/SmallVector.h>
#include <AssetPath.h>
#include <vulkan/vulkan.hpp>

#include <gsl/span>
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>

class CommandBufferPool;

struct CombinedImageSampler
{
	const Image* texture = nullptr;
	vk::Sampler sampler = nullptr;
};

enum class ImageViewType
{
	e2D,   // vk::ImageViewType::e2D
	eCube, // vk::ImageViewType::eCube
	eCount
};

using TextureID = uint32_t;

struct TextureKey
{
	ImageViewType type;
	TextureID id;
};

class TextureSystem
{
public:
	TextureSystem(std::string basePath)
		: m_basePath(std::move(basePath))
	{}

	// todo: support loading as sRGB vs linear for different texture types

	// ImageViewType::e2D
	TextureID LoadTexture(const AssetPath& assetPath);

	// ImageViewType::eCube
	TextureID LoadCubeMapFaces(gsl::span<AssetPath> filenames); // todo, this is the same as LoadTexture but with vk::ImageViewType::eCube

	vk::Sampler CreateSampler(uint32_t nbMipLevels);

	void UploadTextures(CommandBufferPool& commandBufferPool);

	SmallVector<vk::DescriptorImageInfo> GetDescriptorImageInfos(ImageViewType imageViewType) const;

	vk::DescriptorImageInfo GetDescriptorImageInfo(ImageViewType imageViewType, TextureID id) const;

	size_t GetTextureCount() const { return m_textures.size(); }

	size_t GetTextureCount(ImageViewType imageViewType) const { return m_imageTypeCount[(size_t)imageViewType]; }

private:
	TextureID CreateAndUploadTextureImage(const AssetPath& assetPath);

	// Internal ID for samplers
	using SamplerID = uint32_t;

	std::string m_basePath;
	std::map<uint64_t, TextureKey> m_fileHashToTextureKey;
	std::map<uint32_t, SamplerID> m_mipLevelToSamplerID;
	std::vector<TextureKey> m_texturesToUpload;

	template <class T>
	using ImageViewTypeArray = std::array<T, (size_t)ImageViewType::eCount>;

	// ImageViewType, TextureID -> Array Index
	ImageViewTypeArray<std::vector<std::unique_ptr<Texture>>> m_textures; // todo: this can be std::vector<Texture> if we return a TextureID, also we only need the image view here
	ImageViewTypeArray<std::vector<uint32_t>> m_mipLevels;
	ImageViewTypeArray<std::vector<std::string>> m_names; // debugging only

	// SamplerID -> Array Index
	std::vector<vk::UniqueSampler> m_samplers;
	std::array<uint32_t, (size_t)ImageViewType::eCount> m_imageTypeCount = {};
};
