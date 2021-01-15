#include "TextureCache.h"

#include "Texture.h"
#include "CommandBufferPool.h"

#include "hash.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <iostream>

TextureID TextureCache::LoadTexture(std::string_view filename)
{
	TextureID id = CreateAndUploadTextureImage(filename);
	vk::Sampler sampler = CreateSampler(m_textures[(size_t)ImageViewType::e2D][id]->GetMipLevels());
	return id;
}

TextureID TextureCache::CreateAndUploadTextureImage(std::string_view filename)
{
	// Check if we already loaded this texture
	uint64_t fileHash = fnv_hash((uint8_t*)filename.data(), filename.size());
	auto& cachedTexture = m_fileHashToTextureKey.find(fileHash);
	if (cachedTexture != m_fileHashToTextureKey.end())
		return cachedTexture->second.id;

	// Read image from file
	int texWidth = 0, texHeight = 0, texChannels = 0;
	stbi_uc* pixels = stbi_load((m_basePath + "/" + filename.data()).c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0) {
		throw std::runtime_error("failed to load texture image!");
	}

	uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2((std::max)(texWidth, texHeight)))) + 1;

	// Texture image
	size_t imageViewTypeIndex = (size_t)ImageViewType::e2D;

	TextureID id = m_textures[imageViewTypeIndex].size();
	m_textures[imageViewTypeIndex].push_back(
		std::make_unique<Texture>(
			texWidth, texHeight, 4UL, // R8G8B8A8, depth = 4
			vk::Format::eR8G8B8A8Unorm, // figure out gamma correction if we need to do sRGB or something
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferSrc |
			vk::ImageUsageFlagBits::eTransferDst | // src and dst for mipmaps blit
			vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageViewType::e2D,
			mipLevels
		)
	);
	TextureKey key = { ImageViewType::e2D, id };
	auto& texture = m_textures[imageViewTypeIndex][id];
	m_texturesToUpload.push_back(key);
	m_mipLevels[imageViewTypeIndex].push_back(texture->GetMipLevels());
	m_fileHashToTextureKey.emplace(fileHash, std::move(key));
	m_names[imageViewTypeIndex].push_back(filename.data());
	m_imageTypeCount[(size_t)ImageViewType::e2D]++;

	memcpy(texture->GetStagingMappedData(), reinterpret_cast<const void*>(pixels), (size_t)texWidth* texHeight * 4);
	stbi_image_free(pixels);

	return id;
}

vk::Sampler TextureCache::CreateSampler(uint32_t nbMipLevels)
{
	// Check if we already have a sampler
	auto it = m_mipLevelToSamplerID.find(nbMipLevels);
	if (it != m_mipLevelToSamplerID.end())
		return m_samplers[it->second].get();

	SamplerID samplerID = m_samplers.size();
	m_samplers.push_back(
		g_device->Get().createSamplerUnique(vk::SamplerCreateInfo(
			{}, // flags
			vk::Filter::eLinear, // magFilter
			vk::Filter::eLinear, // minFilter
			vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eRepeat, // addressModeU
			vk::SamplerAddressMode::eRepeat, // addressModeV
			vk::SamplerAddressMode::eRepeat, // addressModeW
			{}, // mipLodBias
			true, // anisotropyEnable
			16, // maxAnisotropy
			false, // compareEnable
			vk::CompareOp::eAlways, // compareOp
			0.0f, // minLod
			static_cast<float>(nbMipLevels), // maxLod
			vk::BorderColor::eIntOpaqueBlack, // borderColor
			false // unnormalizedCoordinates
		))
	);
	m_mipLevelToSamplerID[nbMipLevels] = samplerID;
	return m_samplers[samplerID].get();
}

TextureID TextureCache::LoadCubeMapFaces(gsl::span<std::string> filenames)
{
	if (filenames.size() != 6)
		return {};

	// Check if we already loaded this texture
	std::string filename;
	for (const auto& file : filenames)
		filename += std::string(file) + ";"; // use hash of ";".join(filenames) as id
	uint64_t fileHash = fnv_hash((uint8_t*)filename.data(), filename.size());
	auto cachedTextureIt = m_fileHashToTextureKey.find(fileHash); // todo: use better ID
	if (cachedTextureIt != m_fileHashToTextureKey.end())
	{
		size_t samplerTypeIndex = (size_t)cachedTextureIt->second.type;
		TextureID id = cachedTextureIt->second.id;
		auto& cachedTexture = m_textures[samplerTypeIndex][id];
		Texture* texture = cachedTexture.get();
		return cachedTextureIt->second.id;
	}
	std::vector<stbi_uc*> faces;
	faces.reserve(filenames.size());

	bool success = true;
	int width = 0, height = 0, channels = 0;

	for (size_t i = 0; i < filenames.size(); ++i)
	{
		const auto& faceFile = filenames[i];
		int texWidth = 0, texHeight = 0, texChannels = 0;

		stbi_uc* pixels = stbi_load(faceFile.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0)
		{
#ifdef _DEBUG
			std::cout << "failed to load cubemape face: " << faceFile.data() << std::endl;
#endif
			success = false;
		}

		if (i > 0 && (texWidth != width || texHeight != height || texChannels != channels))
		{
#ifdef _DEBUG
			std::cout << "cube map textures have different formats" << std::endl;
#endif
			success = false;
		}

		faces.push_back(pixels);
		width = texWidth;
		height = texHeight;
		channels = texChannels;
	}

	// Create cubemap
	size_t samplerTypeIndex = (size_t)ImageViewType::eCube;

	TextureID id = m_textures[samplerTypeIndex].size();
	m_textures[samplerTypeIndex].push_back(
		std::make_unique<Texture>(
			width, height, 4UL, // R8G8B8A8, depth = 4
			vk::Format::eR8G8B8A8Unorm, // figure out gamma correction if we need to do sRGB or something
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageViewType::eCube,
			1, // mipLevels (todo: support mip-maps)
			6 // layerCount (6 for cube)
		)
	);
	auto& texture = m_textures[samplerTypeIndex][id];

	size_t bufferSize = (size_t)width * height * 4ULL;
	char* data = reinterpret_cast<char*>(texture->GetStagingMappedData());
	for (auto* face : faces)
	{
		if (success)
			memcpy(data, reinterpret_cast<const void*>(face), bufferSize);
		else
			memset(data, 0, bufferSize); // upload black textures on error
		stbi_image_free(face);
		data += bufferSize;
	}
	TextureKey key = { ImageViewType::eCube, id };
	m_texturesToUpload.push_back(key);
	m_mipLevels[samplerTypeIndex].push_back(texture->GetMipLevels());
	m_fileHashToTextureKey.emplace(fileHash, std::move(key));
	m_names[samplerTypeIndex].push_back(filename);
	m_imageTypeCount[(size_t)ImageViewType::eCube]++;
	CreateSampler(texture->GetMipLevels());

	return id;
}

void TextureCache::UploadTextures(CommandBufferPool& commandBufferPool)
{
	vk::CommandBuffer& commandBuffer = commandBufferPool.GetCommandBuffer();

	for (const TextureKey& key : m_texturesToUpload)
	{
		const auto& texture = m_textures[(size_t)key.type][key.id];
		texture->UploadStagingToGPU(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal);
		auto* stagingBuffer = texture->ReleaseStagingBuffer();
		commandBufferPool.DestroyAfterSubmit(stagingBuffer);
	}
	m_texturesToUpload.clear();
}

SmallVector<vk::DescriptorImageInfo> TextureCache::GetDescriptorImageInfos(ImageViewType samplerType) const
{
	const size_t samplerTypeIndex = (size_t)samplerType;

	SmallVector<vk::DescriptorImageInfo> imageInfos;
	for (int id = 0; id < m_textures[samplerTypeIndex].size(); ++id)
	{
		uint32_t mipLevel = m_mipLevels[samplerTypeIndex][id];
		imageInfos.emplace_back(
			m_samplers[m_mipLevelToSamplerID.at(mipLevel)].get(),
			m_textures[samplerTypeIndex][id]->GetImageView(),
			vk::ImageLayout::eShaderReadOnlyOptimal
		);
	}
	
	return imageInfos;
}

vk::DescriptorImageInfo TextureCache::GetDescriptorImageInfo(ImageViewType imageViewType, TextureID id) const
{
	return vk::DescriptorImageInfo(
		m_samplers[m_mipLevelToSamplerID.at(m_mipLevels[(size_t)imageViewType][id])].get(),
		m_textures[(size_t)imageViewType][id]->GetImageView(),
		vk::ImageLayout::eShaderReadOnlyOptimal
	);
}
