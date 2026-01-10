#pragma once

#include <Renderer/Bindless.h>
#include <RHI/Texture.h>
#include <RHI/SmallVector.h>
#include <AssetPath.h>
#include <vulkan/vulkan.hpp>

#include <gsl/span>
#include <gsl/pointers>
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>

class CommandRingBuffer;

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

struct TextureKey
{
	ImageViewType type;
	uint32_t index;
};

class TextureCache
{
public:
	TextureCache(BindlessDescriptors& bindlessDescriptors)
		: m_bindlessDescriptors(&bindlessDescriptors)
	{}

	// todo: support loading as sRGB vs linear for different texture types

	// ImageViewType::e2D
	TextureHandle LoadTexture(const AssetPath& assetPath);

	// ImageViewType::eCube (6 separate .jpg or .png)
	TextureHandle LoadCubeMapFaces(gsl::span<AssetPath> filenames); // todo, this is the same as LoadTexture but with vk::ImageViewType::eCube

	// ImageViewType::eCube (.exr)
	TextureHandle LoadHdri(const AssetPath& exrPath);

	vk::Format GetHdriFormat() const { return vk::Format::eR32G32B32A32Sfloat; }

	vk::Format GetTextureFormat() const { return vk::Format::eR16G16B16A16Unorm; }

	vk::Sampler CreateSampler(uint32_t nbMipLevels);

	void UploadTextures(CommandRingBuffer& commandRingBuffer);

	SmallVector<vk::DescriptorImageInfo> GetDescriptorImageInfos(ImageViewType imageViewType) const;

	vk::DescriptorImageInfo GetDescriptorImageInfo(ImageViewType imageViewType, TextureHandle id) const;

	size_t GetTextureCount() const { return m_textures.size(); }

	size_t GetTextureCount(ImageViewType imageViewType) const { return m_imageTypeCount[(size_t)imageViewType]; }

private:
	TextureHandle CreateAndUploadTextureImage(const AssetPath& assetPath);

	// Internal ID for samplers
	using SamplerID = uint32_t;

	std::map<uint64_t, TextureHandle> m_fileHashToTextureHandle;
	std::map<uint64_t, std::string> m_fileHashToFileName; // todo (hbedard): only in debug
	std::map<uint32_t, SamplerID> m_mipLevelToSamplerID;
	std::vector<TextureKey> m_texturesToUpload;

	template <class T>
	using ImageViewTypeArray = std::array<T, (size_t)ImageViewType::eCount>;

	// ImageViewType, TextureHandle -> Array Index
	std::unordered_map<TextureHandle, TextureKey> m_textureHandleToKey;
	ImageViewTypeArray<std::vector<std::unique_ptr<Texture>>> m_textures; // todo: this can be std::vector<Texture> if we return a TextureID, also we only need the image view here
	ImageViewTypeArray<std::vector<uint32_t>> m_mipLevels;
	ImageViewTypeArray<std::vector<std::string>> m_names; // debugging only

	// SamplerID -> Array Index
	std::vector<vk::UniqueSampler> m_samplers;
	std::array<uint32_t, (size_t)ImageViewType::eCount> m_imageTypeCount = {};

	// Textures are bound to a single array of textures
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
};
