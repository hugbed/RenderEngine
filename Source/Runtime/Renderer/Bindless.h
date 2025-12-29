#pragma once

#include <RHI/Image.h>
#include <RHI/Buffers.h>
#include <RHI/constants.h>
#include <RHI/SmallVector.h> // todo (hbedard): have an impl in core

#include <cstdint>
#include <limits>
#include <unordered_map>

enum class TextureHandle : uint32_t { Invalid = (std::numeric_limits<uint32_t>::max)() };
enum class BufferHandle : uint32_t { Invalid = (std::numeric_limits<uint32_t>::max)() };
enum class BindlessDrawParamsHandle : uint32_t { Invalid = (std::numeric_limits<uint32_t>::max)() };

enum class BindlessDescriptorSet
{
	eBindlessDescriptors = 0,
	eDrawParams = 1,
};

namespace vk
{
	class CommandBuffer;
}

class GraphicsPipelineSystem;

class BindlessDrawParams
{
public:
	BindlessDrawParams(uint32_t minAlignment, vk::DescriptorSetLayout bindlessDescriptorsSetLayout);

	template <class T>
	BindlessDrawParamsHandle DeclareParams()
	{
		return DeclareParams(sizeof(T));
	}

	template <class T>
	void DefineParams(BindlessDrawParamsHandle handle, T&& data, uint32_t frameIndex = (std::numeric_limits<uint32_t>::max)())
	{
		DefineParams(handle, &data, sizeof(T), frameIndex);
	}

	void Build(vk::CommandBuffer& commandBuffer);

	vk::DescriptorSet GetDescriptorSet(uint32_t frameIndex) const;
	vk::DescriptorSetLayout GetDescriptorSetLayout() const;
	const SmallVector<vk::DescriptorSetLayoutBinding>& GetDescriptorSetLayoutBindings() const { return m_descriptorSetLayoutBindings; }
	vk::PipelineLayout GetPipelineLayout() const { return m_pipelineLayout.get(); }

private:
	struct Range
	{
		uint32_t offset = 0;
		std::vector<char> data;
	};
	
	std::unordered_map<BindlessDrawParamsHandle, uint32_t> m_handleToIndex;
	std::array<std::vector<Range>, RHIConstants::kMaxFramesInFlight> m_ranges;
	std::array<std::unique_ptr<UniqueBufferWithStaging>, RHIConstants::kMaxFramesInFlight> m_buffers;
	SmallVector<vk::DescriptorSetLayoutBinding> m_descriptorSetLayoutBindings;
	vk::UniqueDescriptorSetLayout m_descriptorSetLayout;
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniqueDescriptorPool m_descriptorPool;
	std::vector<vk::UniqueDescriptorSet> m_descriptorSets;
	uint32_t m_minAlignment;
	uint32_t m_size = 0;

	BindlessDrawParamsHandle DeclareParams(size_t dataSize);
	void DefineParams(BindlessDrawParamsHandle handle, void* data, size_t dataSize, uint32_t frameIndex);

	vk::UniqueDescriptorSetLayout CreateDescriptorSetLayout();
	static vk::UniqueDescriptorPool CreateDescriptorPool();
	std::unique_ptr<UniqueBufferWithStaging> CreateBuffer(vk::CommandBuffer& commandBuffer, const std::vector<Range>& ranges);
	void CreateDescriptorSets(vk::DescriptorPool& descriptorPool);
	vk::UniquePipelineLayout CreatePipelineLayout(vk::DescriptorSetLayout bindlessDescriptorsSetLayout);
	void UpdateDescriptorSets();
};

class BindlessDescriptors
{
public:
	static constexpr uint32_t kMaxDescriptorCount = 1024;

	static constexpr uint32_t kUniformBinding = 0;
	static constexpr uint32_t kStorageBinding = 1;
	static constexpr uint32_t kTextureBinding = 2;

	BindlessDescriptors();

	TextureHandle StoreTexture(vk::ImageView imageView, vk::Sampler sampler);
	BufferHandle StoreBuffer(vk::Buffer buffer, vk::BufferUsageFlagBits usage);

	vk::DescriptorSet GetDescriptorSet() const { return m_descriptorSet.get(); }
	vk::DescriptorSetLayout GetDescriptorSetLayout() const { return m_descriptorSetLayout.get(); }
	vk::PipelineLayout GetPipelineLayout() const { return m_pipelineLayout.get(); }
	SmallVector<vk::DescriptorSetLayoutBinding> GetDescriptorSetLayoutBindings() const { return m_descriptorSetLayoutBindings; }

private:
	std::vector<vk::ImageView> m_textures;
	std::vector<vk::Buffer> m_buffers;

	SmallVector<vk::DescriptorSetLayoutBinding> m_descriptorSetLayoutBindings;
	vk::UniqueDescriptorSetLayout m_descriptorSetLayout;
	vk::UniqueDescriptorPool m_descriptorPool;
	vk::UniqueDescriptorSet m_descriptorSet;
	vk::UniquePipelineLayout m_pipelineLayout;

	void CreateDescriptorSetLayout();
	void CreateDescriptorPool();
	void CreateDescriptorSet();
	void CreatePipelineLayout();
};

// todo (hbedard): we just need this because everything is initialized in the constructor
// and we need to interject some logic to set the default pipeline layout
class BindlessFactory
{
public:
	BindlessFactory(
		const BindlessDescriptors& bindlessDescriptors,
		const BindlessDrawParams& bindlessDrawParams,
		GraphicsPipelineSystem& graphicsPipelineSystem);
};
