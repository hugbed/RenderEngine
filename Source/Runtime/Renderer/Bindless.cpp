#include <Renderer/Bindless.h>

#include <RHI/GraphicsPipelineSystem.h>
#include <RHI/Device.h>

#include <numeric>

namespace Bindless_Private
{
    // Returns the next closest multiple of minAlignment relative to size
    static constexpr uint32_t PadSizeToMinAlignment(uint32_t size, uint32_t minAlignment)
    {
        return (size + minAlignment - 1) & ~(minAlignment - 1);
    }

	static std::array<vk::PushConstantRange, 2> GetPushConstantRanges()
	{
		// Reserve a push constant block (with 2 uint for indexing) for both vertex and fragment shaders
		return {
			vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, 2 * sizeof(uint32_t)),
			vk::PushConstantRange(vk::ShaderStageFlagBits::eFragment, 0, 2 * sizeof(uint32_t)),
		};
	}
}

BindlessDrawParams::BindlessDrawParams(uint32_t minAlignment, vk::DescriptorSetLayout bindlessDescriptorsSetLayout)
	: m_minAlignment(minAlignment)
	, m_descriptorSetLayout(CreateDescriptorSetLayout())
	, m_descriptorPool(CreateDescriptorPool())
	, m_pipelineLayout(CreatePipelineLayout(bindlessDescriptorsSetLayout))
{
}

BindlessDrawParamsHandle BindlessDrawParams::DeclareParams(size_t dataSize)
{
	using namespace Bindless_Private;

	assert(!m_buffers[0]);

	const uint32_t offset = m_size;
	auto handle = static_cast<BindlessDrawParamsHandle>(offset);
	m_handleToIndex[handle] = m_ranges.back().size();
	uint32_t size = PadSizeToMinAlignment(dataSize, m_minAlignment);
	for (std::vector<Range>& ranges : m_ranges)
	{
		Range range;
		range.offset = offset;
		range.data.resize(size, 0);
		ranges.push_back(std::move(range));
	}
	m_size += size;
	return handle;
}

void BindlessDrawParams::DefineParams(BindlessDrawParamsHandle handle, void* data, size_t dataSize, uint32_t frameIndex)
{
	assert(m_handleToIndex.contains(handle));
	assert(!m_buffers[0]);

	static constexpr uint32_t kInvalidFrameIndex = std::numeric_limits<uint32_t>::max();
	uint32_t rangeIndex = m_handleToIndex.find(handle)->second;
	if (frameIndex != kInvalidFrameIndex)
	{
		assert(frameIndex < m_ranges.size());
		Range& range = m_ranges[frameIndex][rangeIndex];
		memcpy(range.data.data(), data, dataSize);
	}
	else
	{
		for (std::vector<Range>& ranges : m_ranges)
		{
			Range& range = ranges[rangeIndex];
			memcpy(range.data.data(), data, dataSize);
		}
	}
}

void BindlessDrawParams::Build(vk::CommandBuffer& commandBuffer)
{
	bool areParamsEmpty = std::any_of(m_ranges.begin(), m_ranges.end(),
		[] (const auto& ranges) {
			return ranges.empty();
		});
	if (areParamsEmpty)
	{
		return; // no draw params
	}

	for (uint32_t i = 0; i < m_buffers.size(); ++i)
	{
		m_buffers[i] = CreateBuffer(commandBuffer, m_ranges[i]);
	}
	CreateDescriptorSets(m_descriptorPool.get());
	UpdateDescriptorSets();
}

vk::DescriptorSet BindlessDrawParams::GetDescriptorSet(uint32_t concurrentFrameIndex) const
{
	assert(m_descriptorSets[concurrentFrameIndex].get() != VK_NULL_HANDLE);
	return m_descriptorSets[concurrentFrameIndex].get();
}

vk::DescriptorSetLayout BindlessDrawParams::GetDescriptorSetLayout() const
{
	assert(m_descriptorSetLayout.get() != VK_NULL_HANDLE);
	return m_descriptorSetLayout.get();
}

vk::UniqueDescriptorSetLayout BindlessDrawParams::CreateDescriptorSetLayout()
{
	m_descriptorSetLayoutBindings.resize(1);
	vk::DescriptorSetLayoutBinding& binding = m_descriptorSetLayoutBindings.back();
	binding.binding = 0;
	binding.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
	binding.descriptorCount = 1;
	binding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

	vk::DescriptorSetLayoutCreateInfo layoutCreateInfo;
	layoutCreateInfo.bindingCount = 1;
	layoutCreateInfo.pBindings = &binding;
	layoutCreateInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
	return g_device->Get().createDescriptorSetLayoutUnique(layoutCreateInfo);
}

vk::UniqueDescriptorPool BindlessDrawParams::CreateDescriptorPool()
{
	vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBufferDynamic, RHIConstants::kMaxFramesInFlight);

	vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
	descriptorPoolCreateInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
	descriptorPoolCreateInfo.pPoolSizes = &poolSize;
	descriptorPoolCreateInfo.poolSizeCount = 1;
	descriptorPoolCreateInfo.maxSets = RHIConstants::kMaxFramesInFlight;

	return g_device->Get().createDescriptorPoolUnique(descriptorPoolCreateInfo);
}

std::unique_ptr<UniqueBufferWithStaging> BindlessDrawParams::CreateBuffer(vk::CommandBuffer& commandBuffer, const std::vector<Range>& ranges)
{
	using namespace Bindless_Private;

	// Make sure that the buffer size is a multiple of the descriptor range
	size_t maxRangeSize = std::accumulate(m_ranges.front().begin(), m_ranges.front().end(), 0ULL,
		[](size_t val, const Range& range) {
			return std::max(val, range.data.size());
		});
	m_size = PadSizeToMinAlignment(m_size, maxRangeSize);

	auto buffer = std::make_unique<UniqueBufferWithStaging>(m_size, vk::BufferUsageFlagBits::eUniformBuffer);
	uint8_t* bufferData = static_cast<uint8_t*>(buffer->GetStagingMappedData());
	for (const Range& range : ranges)
	{
		memcpy(bufferData, range.data.data(), range.data.size());
		bufferData += range.data.size();
	}
	buffer->CopyStagingToGPU(commandBuffer);
	return buffer;
}

void BindlessDrawParams::CreateDescriptorSets(vk::DescriptorPool& descriptorPool)
{
	vk::DescriptorSetLayout layouts[2] = {
		m_descriptorSetLayout.get(),
		m_descriptorSetLayout.get()
	};

	vk::DescriptorSetAllocateInfo allocateInfo{};
	allocateInfo.descriptorPool = descriptorPool;
	allocateInfo.pSetLayouts = &layouts[0];
	allocateInfo.descriptorSetCount = RHIConstants::kMaxFramesInFlight;
	m_descriptorSets = g_device->Get().allocateDescriptorSetsUnique(allocateInfo);
}

vk::UniquePipelineLayout BindlessDrawParams::CreatePipelineLayout(vk::DescriptorSetLayout bindlessDescriptorsSetLayout)
{
	using namespace Bindless_Private;
	
	std::array<vk::DescriptorSetLayout, 2> layouts = { bindlessDescriptorsSetLayout, m_descriptorSetLayout.get() };
	auto pushConstants = GetPushConstantRanges();
	vk::PipelineLayoutCreateInfo createInfo({}, layouts, pushConstants);
	return g_device->Get().createPipelineLayoutUnique(createInfo);
}

void BindlessDrawParams::UpdateDescriptorSets()
{
	size_t maxRangeSize = std::accumulate(m_ranges.front().begin(), m_ranges.front().end(), 0ULL,
		[](size_t val, const Range& range) {
			return std::max(val, range.data.size());
		});

	std::array<vk::DescriptorBufferInfo, RHIConstants::kMaxFramesInFlight> bufferInfos;
	std::array<vk::WriteDescriptorSet, RHIConstants::kMaxFramesInFlight> writes;
	for (uint32_t i = 0; i < m_descriptorSets.size(); ++i)
	{
		vk::UniqueDescriptorSet& descriptorSet = m_descriptorSets[i];

		vk::DescriptorBufferInfo& bufferInfo = bufferInfos[i];
		bufferInfo.buffer = m_buffers[i]->Get();
		bufferInfo.offset = 0;
		bufferInfo.range = maxRangeSize;

		vk::WriteDescriptorSet& write = writes[i];
		write.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
		write.dstSet = descriptorSet.get();
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.pBufferInfo = &bufferInfo;
	}
	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

BindlessDescriptors::BindlessDescriptors()
{
	CreateDescriptorSetLayout();
	CreateDescriptorPool();
	CreateDescriptorSet();
	CreatePipelineLayout();
}

TextureHandle BindlessDescriptors::StoreTexture(vk::ImageView imageView, vk::Sampler sampler)
{
	uint32_t textureHandle = static_cast<uint32_t>(m_textures.size());
	m_textures.push_back(imageView);

	vk::DescriptorImageInfo imageInfo;
	imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	imageInfo.imageView = imageView;
	imageInfo.sampler = sampler;

	vk::WriteDescriptorSet write;
	write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
	write.dstSet = m_descriptorSet.get();
	write.dstBinding = kTextureBinding;
	write.dstArrayElement = textureHandle;
	write.pImageInfo = &imageInfo;
	write.descriptorCount = 1;

	g_device->Get().updateDescriptorSets(1, &write, 0, nullptr);
    return static_cast<TextureHandle>(textureHandle);
}

BufferHandle BindlessDescriptors::StoreBuffer(vk::Buffer buffer, vk::BufferUsageFlagBits usage)
{
	uint32_t bufferHandle = static_cast<uint32_t>(m_buffers.size());
	m_buffers.push_back(buffer);

	vk::DescriptorBufferInfo bufferInfo;
	bufferInfo.buffer = buffer;
	bufferInfo.offset = 0;
	bufferInfo.range = vk::WholeSize;

	// Prepare both write for uniform or storage.
	// We'll use either one or the other depending on the usage flags.
	std::array<vk::WriteDescriptorSet, 2> writes;
	for (uint32_t i = 0; i < writes.size(); ++i)
	{
		vk::WriteDescriptorSet& write = writes[i];
		write.dstSet = m_descriptorSet.get();
		write.dstArrayElement = bufferHandle;
		write.pBufferInfo = &bufferInfo;
		write.descriptorCount = 1;
	}

	// It's either a uniform buffer
	uint32_t count = 0;
	uint32_t index = 0;
	if (usage & vk::BufferUsageFlagBits::eUniformBuffer)
	{
		writes[index].dstBinding = kUniformBinding;
		writes[index].descriptorType = vk::DescriptorType::eUniformBuffer;
		++index;
		++count;
	}

	// or a storage buffer
	if (usage & vk::BufferUsageFlagBits::eStorageBuffer)
	{
		writes[index].dstBinding = kStorageBinding;
		writes[index].descriptorType = vk::DescriptorType::eStorageBuffer;
		++count;
	}

	// Use index to determine which write to do (uniform or storage)
	g_device->Get().updateDescriptorSets(count, writes.data(), 0, nullptr);
	return static_cast<BufferHandle>(bufferHandle);
}

void BindlessDescriptors::CreateDescriptorSetLayout()
{
	constexpr size_t descriptorTypeCount = 3;

	m_descriptorSetLayoutBindings.resize(descriptorTypeCount);
	std::array<vk::DescriptorBindingFlags, descriptorTypeCount> flags = {};
	std::array<vk::DescriptorType, descriptorTypeCount> types = {
		vk::DescriptorType::eUniformBuffer,
		vk::DescriptorType::eStorageBuffer,
		vk::DescriptorType::eCombinedImageSampler,
	};
	for (uint32_t i = 0; i < descriptorTypeCount; ++i)
	{
		vk::DescriptorSetLayoutBinding& binding = m_descriptorSetLayoutBindings[i];
		binding.binding = i;
		binding.descriptorType = types[i];
		binding.descriptorCount = kMaxDescriptorCount;
		binding.stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex;
		flags[i] = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
	}

	vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo;
	bindingFlagsCreateInfo.pBindingFlags = flags.data();
	bindingFlagsCreateInfo.bindingCount = flags.size();

	vk::DescriptorSetLayoutCreateInfo layoutCreateInfo;
	layoutCreateInfo.pBindings = m_descriptorSetLayoutBindings.data();
	layoutCreateInfo.bindingCount = static_cast<uint32_t>(m_descriptorSetLayoutBindings.size());
	layoutCreateInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
	layoutCreateInfo.pNext = &bindingFlagsCreateInfo;

	m_descriptorSetLayout = g_device->Get().createDescriptorSetLayoutUnique(layoutCreateInfo, nullptr);
}

void BindlessDescriptors::CreateDescriptorPool()
{
	constexpr size_t descriptorTypeCount = 3;

	std::array<vk::DescriptorPoolSize, descriptorTypeCount> poolSizes = {
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, kMaxDescriptorCount),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, kMaxDescriptorCount),
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, kMaxDescriptorCount),
	};

	vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
	descriptorPoolCreateInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
	descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
	descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	descriptorPoolCreateInfo.maxSets = 1;

	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(descriptorPoolCreateInfo);
}

void BindlessDescriptors::CreateDescriptorSet()
{
	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = m_descriptorPool.get();
	allocateInfo.pSetLayouts = &m_descriptorSetLayout.get();
	allocateInfo.descriptorSetCount = 1;

	std::vector<vk::UniqueDescriptorSet> descriptorSets = g_device->Get().allocateDescriptorSetsUnique(allocateInfo);
	assert(!descriptorSets.empty());
	m_descriptorSet = std::move(descriptorSets[0]);
}

void BindlessDescriptors::CreatePipelineLayout()
{
	using namespace Bindless_Private;
	
	std::array<vk::DescriptorSetLayout, 1> layouts = { m_descriptorSetLayout.get() };
	std::array pushConstants = GetPushConstantRanges();
	vk::PipelineLayoutCreateInfo createInfo({}, layouts, pushConstants);
	m_pipelineLayout = g_device->Get().createPipelineLayoutUnique(createInfo);
}

BindlessFactory::BindlessFactory(
	const BindlessDescriptors& bindlessDescriptors,
	const BindlessDrawParams& bindlessDrawParams,
	GraphicsPipelineSystem& graphicsPipelineSystem)
{
	SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> bindings = {
		bindlessDescriptors.GetDescriptorSetLayoutBindings(),
		bindlessDrawParams.GetDescriptorSetLayoutBindings(),
	};
	SetVector<vk::DescriptorSetLayout> descriptorSetLayouts = {
		bindlessDescriptors.GetDescriptorSetLayout(),
		bindlessDrawParams.GetDescriptorSetLayout()
	};
	SetVector<vk::PipelineLayout> pipelineLayouts = {
		bindlessDescriptors.GetPipelineLayout(),
		bindlessDrawParams.GetPipelineLayout()
	};
	graphicsPipelineSystem.SetCommonLayout(std::move(bindings), std::move(descriptorSetLayouts), std::move(pipelineLayouts));
}
