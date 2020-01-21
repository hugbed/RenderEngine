#include "TextureCache.h"

#include "Texture.h"
#include "CommandBufferPool.h"

#include "hash.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <iostream>

CombinedImageSampler TextureCache::LoadTexture(const std::string filename)
{
	Texture* texture = CreateAndUploadTextureImage(filename);
	vk::Sampler sampler = CreateSampler(texture->GetMipLevels());
	return CombinedImageSampler{ texture, sampler };
}

Texture* TextureCache::CreateAndUploadTextureImage(const std::string& filename)
{
	// Check if we already loaded this texture
	uint64_t id = fnv_hash((uint8_t*)filename.data(), filename.size());
	auto& cachedTexture = m_textures.find(id);
	if (cachedTexture != m_textures.end())
		return cachedTexture->second.get();

	// Read image from file
	int texWidth = 0, texHeight = 0, texChannels = 0;
	stbi_uc* pixels = stbi_load((m_basePath + "/" + filename).c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0) {
		throw std::runtime_error("failed to load texture image!");
	}

	uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2((std::max)(texWidth, texHeight)))) + 1;

	// Texture image
	auto [pair, res] = m_textures.emplace(id, std::make_unique<Texture>(
		texWidth, texHeight, 4UL, // R8G8B8A8, depth = 4
		vk::Format::eR8G8B8A8Unorm, // figure out gamma correction if we need to do sRGB or something
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferSrc |
			vk::ImageUsageFlagBits::eTransferDst | // src and dst for mipmaps blit
			vk::ImageUsageFlagBits::eSampled,
		vk::ImageAspectFlagBits::eColor,
		vk::ImageViewType::e2D,
		mipLevels
	));
	auto& texture = pair->second;

	memcpy(texture->GetStagingMappedData(), reinterpret_cast<const void*>(pixels), (size_t)texWidth* texHeight * 4);
	stbi_image_free(pixels);

	m_texturesToUpload.push_back(texture.get());

	return texture.get();
}

vk::Sampler TextureCache::CreateSampler(uint32_t nbMipLevels)
{
	// Check if we already have a sampler
	auto samplerIt = m_samplers.find(nbMipLevels);
	if (samplerIt != m_samplers.end())
		return samplerIt->second.get();

	auto [pair, wasInserted] = m_samplers.emplace(nbMipLevels,
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
		)
	));
	return pair->second.get();
}

CombinedImageSampler TextureCache::LoadCubeMapFaces(const std::vector<std::string>& filenames)
{
	if (filenames.size() != 6)
		return {};

	// Check if we already loaded this texture
	std::string filename;
	for (const auto& file : filenames)
		filename += file + ";"; // use hash of ";".join(filenames) as id
	uint64_t id = fnv_hash((uint8_t*)filename.data(), filename.size());
	auto& cachedTexture = m_textures.find(id); // todo: use better ID
	if (cachedTexture != m_textures.end())
	{
		Texture* texture = cachedTexture->second.get();
		vk::Sampler sampler = CreateSampler(texture->GetMipLevels());
		return { texture, sampler };
	}
	std::vector<stbi_uc*> faces;
	faces.reserve(filenames.size());

	bool success = true;
	int width = 0, height = 0, channels = 0;

	for (size_t i = 0; i < filenames.size(); ++i)
	{
		const auto& faceFile = filenames[i];
		int texWidth = 0, texHeight = 0, texChannels = 0;

		stbi_uc* pixels = stbi_load(faceFile.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (pixels == nullptr || texWidth == 0 || texHeight == 0 || texChannels == 0)
		{
#ifdef _DEBUG
			std::cout << "failed to load cubemape face: " << faceFile.c_str() << std::endl;
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

	// todo: filenames[0] is bad, use something else than the filename of the first face
	auto [pair, res] = m_textures.emplace(id, std::make_unique<Texture>(
		width, height, 4UL, // R8G8B8A8, depth = 4
		vk::Format::eR8G8B8A8Unorm, // figure out gamma correction if we need to do sRGB or something
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		vk::ImageAspectFlagBits::eColor,
		vk::ImageViewType::eCube,
		1, // mipLevels (todo: support mip-maps)
		6 // layerCount (6 for cube)
	));
	auto& texture = pair->second;

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

	m_texturesToUpload.push_back(texture.get());

	vk::Sampler sampler = CreateSampler(1);
	return CombinedImageSampler{ texture.get(), sampler };
}

void TextureCache::UploadTextures(vk::CommandBuffer& commandBuffer, CommandBufferPool& commandBufferPool)
{
	for (const auto& texture : m_texturesToUpload)
	{
		texture->UploadStagingToGPU(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal);
		auto* stagingBuffer = texture->ReleaseStagingBuffer();
		commandBufferPool.DestroyAfterSubmit(stagingBuffer);
	}
	m_texturesToUpload.clear();
}
