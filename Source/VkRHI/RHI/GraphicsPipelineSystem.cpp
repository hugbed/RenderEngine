#include <RHI/GraphicsPipelineSystem.h>

#include <RHI/Image.h>
#include <RHI/Device.h>
#include <RHI/PhysicalDevice.h>
#include <RHI/vk_utils.h>
#include <hash.h>

#include <array>
#include <map>
#include <ranges>

namespace
{
	template <class T>
	void ReserveIndex(GraphicsPipelineID id, std::vector<T>& v)
	{
		v.resize((std::max)((size_t)id + 1, v.size()));
	}
}

namespace GraphicsPipelineHelpers
{
	[[nodiscard]] uint64_t HashPipelineLayout(
		const VectorView<vk::DescriptorSetLayoutBinding>& bindings,
		const VectorView<vk::PushConstantRange>& pushConstants)
	{
		// todo (hbedard): use chaining operation instead
		size_t bindingsSize = bindings.size() * sizeof(vk::DescriptorSetLayoutBinding);
		size_t pushConstantsSize = pushConstants.size() * sizeof(vk::PushConstantRange);

		// todo (hbedard): implement templates
		std::vector<uint8_t> buffer;
		buffer.resize(bindingsSize + pushConstantsSize, 0);
		
		size_t offset = 0;

		memcpy((void*)(buffer.data() + offset), bindings.data(), bindingsSize);
		offset += bindingsSize;

		memcpy((void*)(buffer.data() + offset), pushConstants.data(), pushConstantsSize);
		offset += pushConstantsSize;

		ASSERT(offset == buffer.size());

		return fnv_hash(buffer.data(), buffer.size());
	}

	[[nodiscard]] uint64_t HashDescriptorSetLayoutBinding(uint32_t setIndex, const vk::DescriptorSetLayoutBinding& binding)
	{
		uint64_t hash = fnv_hash(setIndex);
		hash = fnv_hash(binding.binding, hash);
		hash = fnv_hash(binding.descriptorType, hash);
		return hash;
	}

	[[nodiscard]] SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> CombineDescriptorSetLayoutBindings(
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& vertexBindings, // bindings[set][binding]
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& fragmentBindings)
	{
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> descriptorSetLayoutBindings((std::max)(vertexBindings.size(), fragmentBindings.size()));

		for (size_t set = 0; set < descriptorSetLayoutBindings.size(); ++set)
		{
			auto& descriptorSetLayoutBinding = descriptorSetLayoutBindings[set];

			if (set < vertexBindings.size())
			{
				const SmallVector<vk::DescriptorSetLayoutBinding>& vertexSetLayoutBindings = vertexBindings[set];
				for (vk::DescriptorSetLayoutBinding binding : vertexSetLayoutBindings)
				{
					binding.stageFlags |= vk::ShaderStageFlagBits::eVertex;
					descriptorSetLayoutBinding.push_back(binding);
				}
			}
			if (set < fragmentBindings.size())
			{
				const SmallVector<vk::DescriptorSetLayoutBinding>& fragmentSetLayoutBindings = fragmentBindings[set];
				for (vk::DescriptorSetLayoutBinding binding : fragmentSetLayoutBindings)
				{
					//assert(!"this doesn't work");
					auto it = std::find_if(descriptorSetLayoutBinding.begin(), descriptorSetLayoutBinding.end(),
						[binding, set](const vk::DescriptorSetLayoutBinding& otherBinding) {
							return HashDescriptorSetLayoutBinding(set, binding) == HashDescriptorSetLayoutBinding(set, otherBinding)
								&& binding.stageFlags != otherBinding.stageFlags;
						});
					if (it != descriptorSetLayoutBinding.end())
					{
						it->stageFlags |= vk::ShaderStageFlagBits::eFragment;
					}
					else
					{
						binding.stageFlags |= vk::ShaderStageFlagBits::eFragment;
						descriptorSetLayoutBinding.push_back(binding);
					}
				}
			}

			// and sort them by bindings
			std::sort(descriptorSetLayoutBinding.begin(), descriptorSetLayoutBinding.end(), [](const auto& a, const auto& b) {
				return a.binding < b.binding;
			});
		}

		return descriptorSetLayoutBindings;
	}

	void RemoveDuplicateBindings(SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& bindings)
	{
		std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> hashToIndex;
		std::vector<std::pair<uint32_t, uint32_t>> bindingsToRemove;

		for (uint32_t setIndex = 0; setIndex < bindings.size(); ++setIndex)
		{
			const auto& perSetBindings = bindings[setIndex];

			for (uint32_t bindingIndex = 0; bindingIndex < perSetBindings.size(); ++bindingIndex)
			{
				const vk::DescriptorSetLayoutBinding& binding = perSetBindings[bindingIndex];
				uint64_t hash = HashDescriptorSetLayoutBinding(setIndex, binding);
				const std::pair<uint32_t, uint32_t> indices = std::make_pair(setIndex, bindingIndex);
				if (auto [it, wasInserted] = hashToIndex.emplace(hash, indices); !wasInserted)
				{
					std::pair<uint32_t, uint32_t> existingIndices = it->second;
					vk::DescriptorSetLayoutBinding& existingBinding = bindings[existingIndices.first][existingIndices.second];
					existingBinding.descriptorCount = (std::max)(
						existingBinding.descriptorCount,
						binding.descriptorCount);

					bindingsToRemove.push_back(indices);
				}
			}
		}

		for (const auto& [setIndex, bindingIndex] : std::ranges::reverse_view(bindingsToRemove))
		{
			bindings[setIndex].erase(bindings[setIndex].data() + bindingIndex);

			if (bindings[setIndex].empty())
			{
				bindings.erase(bindings.begin() + setIndex);
			}
		}
	}
	
	[[nodiscard]] SetVector<vk::UniqueDescriptorSetLayout> CreateDescriptorSetLayoutsFromBindings(
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetsLayoutBindings)
	{
		SetVector<vk::UniqueDescriptorSetLayout> descriptorSetLayouts;

		for (size_t set = 0; set < descriptorSetsLayoutBindings.size(); ++set)
		{
			auto& setBindings = descriptorSetsLayoutBindings[set];

			vk::DescriptorBindingFlags defaultFlags = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
			SmallVector<vk::DescriptorBindingFlags> bindingFlags;
			bindingFlags.reserve(setBindings.size());
			for (const auto& setBinding : setBindings)
			{
				if (setBinding.descriptorCount == 0)
					bindingFlags.emplace_back(defaultFlags | vk::DescriptorBindingFlagBits::eVariableDescriptorCount);
				else
					bindingFlags.emplace_back(defaultFlags);
			}

			vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingsFlagsInfo;
			bindingsFlagsInfo.bindingCount = (uint32_t)bindingFlags.size();
			bindingsFlagsInfo.pBindingFlags = bindingFlags.data();

			vk::DescriptorSetLayoutCreateInfo createInfo;
			createInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
			createInfo.setBindings(setBindings);
			createInfo.pNext = &bindingsFlagsInfo;
			descriptorSetLayouts.push_back(g_device->Get().createDescriptorSetLayoutUnique(createInfo));
		}

		return descriptorSetLayouts;
	}

	[[nodiscard]] SetVector<vk::UniquePipelineLayout> CreatePipelineLayoutsFromDescriptorSetLayouts(
		const SetVector<vk::UniqueDescriptorSetLayout>& descriptorSetLayouts,
		SmallVector<vk::PushConstantRange> pushConstantRanges)
	{
		SetVector<vk::UniquePipelineLayout> pipelineLayouts;

		// Each pipeline layout includes descriptor set layout from previous set:
		// E.g. for sets 1, 2, 3, we have { set1 }, { set1, set2 }, { set1, set2, set3 }
		SetVector<vk::DescriptorSetLayout> pipelineDescriptorSetLayouts;
		for (size_t set = 0; set < descriptorSetLayouts.size(); ++set)
		{
			pipelineDescriptorSetLayouts.push_back(descriptorSetLayouts[set].get());
			auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo(
				{},
				static_cast<uint32_t>(pipelineDescriptorSetLayouts.size()), pipelineDescriptorSetLayouts.data(),
				static_cast<uint32_t>(pushConstantRanges.size()), pushConstantRanges.data()
			);
			pipelineLayouts.push_back(g_device->Get().createPipelineLayoutUnique(pipelineLayoutInfo));
		}

		return pipelineLayouts;
	}

	[[nodiscard]] SmallVector<vk::PushConstantRange> CombinePushConstantRanges(
		const SmallVector<vk::PushConstantRange>& pushConstantRange1,
		const SmallVector<vk::PushConstantRange>& pushConstantRange2)
	{
		SmallVector<vk::PushConstantRange> pushConstantRanges;
		pushConstantRanges.insert(pushConstantRanges.end(), pushConstantRange1.begin(), pushConstantRange1.end());
		pushConstantRanges.insert(pushConstantRanges.end(), pushConstantRange2.begin(), pushConstantRange2.end());
		return pushConstantRanges;
	}
}

GraphicsPipelineInfo::GraphicsPipelineInfo(vk::RenderPass renderPass, vk::Extent2D viewportExtent)
	: sampleCount(g_physicalDevice->GetMsaaSamples())
	, viewportExtent(viewportExtent)
	, renderPass(renderPass)
{
}

GraphicsPipelineSystem::GraphicsPipelineSystem(ShaderSystem& shaderSystem)
	: m_shaderSystem(&shaderSystem)
{}

void GraphicsPipelineSystem::SetDefaultLayout(
	SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> descriptorSetLayoutBindingOverrides,
	SetVector<vk::DescriptorSetLayout> descriptorSetLayoutOverrides,
	SetVector<vk::PipelineLayout> pipelineLayoutOverrides)
{
	m_descriptorSetLayoutBindingOverrides = std::move(descriptorSetLayoutBindingOverrides);
	m_descriptorSetLayoutOverrides = std::move(descriptorSetLayoutOverrides);
	m_pipelineLayoutOverrides = std::move(pipelineLayoutOverrides);
}

GraphicsPipelineID GraphicsPipelineSystem::CreateGraphicsPipeline(
	ShaderInstanceID vertexShaderID,
	ShaderInstanceID fragmentShaderID,
	const GraphicsPipelineInfo& info)
{
	GraphicsPipelineID id = m_nextID;
	m_shaders.push_back({ vertexShaderID, fragmentShaderID });
	ResetGraphicsPipeline(id, info);
	m_nextID++;
	return id;
}

void GraphicsPipelineSystem::ResetGraphicsPipeline(
	GraphicsPipelineID id, const GraphicsPipelineInfo& info)
{
	const ShaderInstanceID vertexShaderID = m_shaders[id].vertexShader;
	const ShaderInstanceID fragmentShaderID = m_shaders[id].fragmentShader;

	SmallVector<vk::VertexInputAttributeDescription> attributeDescriptions;
	vk::VertexInputBindingDescription bindingDescription;
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo = m_shaderSystem->GetVertexInputStateInfo(
		vertexShaderID,
		attributeDescriptions,
		bindingDescription
	);

	vk::SpecializationInfo specializationInfo[2] = {};
	vk::PipelineShaderStageCreateInfo shaderStages[] = {
		m_shaderSystem->GetShaderStageInfo(vertexShaderID, specializationInfo[0]),
		m_shaderSystem->GetShaderStageInfo(fragmentShaderID, specializationInfo[1])
	};

	vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, info.primitiveTopology);

	// Viewport state

	vk::Viewport viewport(
		0.0f, 0.0f, // x, y
		static_cast<float>(info.viewportExtent.width), static_cast<float>(info.viewportExtent.height),
		0.0f, 1.0f // depth (min, max)
	);
	vk::Rect2D scissor(vk::Offset2D(0, 0), info.viewportExtent);
	vk::PipelineViewportStateCreateInfo viewportState(
		vk::PipelineViewportStateCreateFlags(),
		1, &viewport,
		1, &scissor
	);

	// Fixed function state

	vk::PipelineRasterizationStateCreateInfo rasterizerState;
	rasterizerState.lineWidth = 1.0f;
	rasterizerState.cullMode = vk::CullModeFlagBits::eBack;
	rasterizerState.frontFace = vk::FrontFace::eCounterClockwise;

	vk::PipelineMultisampleStateCreateInfo multisampling;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.minSampleShading = 1.0f;
	multisampling.rasterizationSamples = info.sampleCount;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment;
	if (info.blendEnable)
	{
		colorBlendAttachment.blendEnable = true;
		colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
		colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
		colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		colorBlendAttachment.alphaBlendOp = vk::BlendOp::eSubtract;
	}

	colorBlendAttachment.colorWriteMask =
		vk::ColorComponentFlagBits::eR |
		vk::ColorComponentFlagBits::eG |
		vk::ColorComponentFlagBits::eB |
		vk::ColorComponentFlagBits::eA;

	vk::PipelineColorBlendStateCreateInfo colorBlending(
		vk::PipelineColorBlendStateCreateFlags(),
		false, // logicOpEnable
		vk::LogicOp::eCopy,
		1, &colorBlendAttachment
	);

	// Combine descriptor set layout bindings from vertex and fragment shader
	if (!m_pipelineLayoutOverrides.empty())
	{
		// todo (hbedard): pretty sure it's not legit to make another unique handle
		// from raw handles that already have a unique handle
		::ReserveIndex(id, m_descriptorSetLayoutBindings);
		m_descriptorSetLayoutBindings[id] = m_descriptorSetLayoutBindingOverrides;

		::ReserveIndex(id, m_descriptorSetLayouts);
		m_descriptorSetLayouts[id].reserve(m_descriptorSetLayoutOverrides.size());
		for (const auto& layout : m_descriptorSetLayoutOverrides)
		{
			m_descriptorSetLayouts[id].emplace_back(layout);
		}

		::ReserveIndex(id, m_pipelineLayouts);
		m_pipelineLayouts[id].reserve(m_pipelineLayoutOverrides.size());
		for (const auto& layout : m_pipelineLayoutOverrides)
		{
			m_pipelineLayouts[id].emplace_back(layout);
		}
	}
	else
	{
		auto vertexBindings = m_shaderSystem->GetDescriptorSetLayoutBindings(vertexShaderID);
		GraphicsPipelineHelpers::RemoveDuplicateBindings(vertexBindings);

		auto fragmentBindings = m_shaderSystem->GetDescriptorSetLayoutBindings(fragmentShaderID);
		GraphicsPipelineHelpers::RemoveDuplicateBindings(fragmentBindings);

		::ReserveIndex(id, m_descriptorSetLayoutBindings);
		m_descriptorSetLayoutBindings[id] = GraphicsPipelineHelpers::CombineDescriptorSetLayoutBindings(vertexBindings, fragmentBindings);

		// Create descriptor set layouts
		::ReserveIndex(id, m_descriptorSetLayouts);
		m_descriptorSetLayouts[id] = GraphicsPipelineHelpers::CreateDescriptorSetLayoutsFromBindings(m_descriptorSetLayoutBindings[id]);

		// Combine push constants from vertex and fragment shader
		auto vertexPushConstantRanges = m_shaderSystem->GetPushConstantRanges(vertexShaderID);
		auto fragmentPushConstantRanges = m_shaderSystem->GetPushConstantRanges(fragmentShaderID);
		auto pushConstantRanges = GraphicsPipelineHelpers::CombinePushConstantRanges(vertexPushConstantRanges, fragmentPushConstantRanges);

		// Build pipeline layouts for each set
		::ReserveIndex(id, m_pipelineLayouts);
		m_pipelineLayouts[id] = GraphicsPipelineHelpers::CreatePipelineLayoutsFromDescriptorSetLayouts(m_descriptorSetLayouts[id], pushConstantRanges);

		// Compute hash pipeline descriptor bindings and push constants
		// If two Graphics Pipelien have the same hash for a set,
		// it means their pipeline layout for this set is compatible
		::ReserveIndex(id, m_pipelineCompatibility);
		m_pipelineCompatibility[id].resize(m_descriptorSetLayoutBindings[id].size());
		for (size_t set = 0; set < m_descriptorSetLayoutBindings[id].size(); ++set)
		{
			m_pipelineCompatibility[id][set] = GraphicsPipelineHelpers::HashPipelineLayout(m_descriptorSetLayoutBindings[id][set], pushConstantRanges);
		}
	}

	vk::PipelineDepthStencilStateCreateInfo depthStencilState(
		{},
		info.depthTestEnable, // depthTestEnable
		info.depthWriteEnable, // depthWriteEnable
		vk::CompareOp::eLessOrEqual, // depthCompareOp
		false, // depthBoundsTestEnable
		false, // stencilTestEnable
		{}, // front
		{}, // back
		0.0f, 1.0f // depthBounds (min, max)
	);
	vk::GraphicsPipelineCreateInfo graphicsPipelineCreateInfo(
		vk::PipelineCreateFlags(),
		2, // stageCount
		shaderStages,
		&vertexInputInfo,
		&inputAssembly,
		nullptr, // tesselation
		&viewportState,
		&rasterizerState,
		&multisampling,
		&depthStencilState,
		&colorBlending,
		nullptr, // dynamicState
		m_pipelineLayouts[id].back().get(), // the last one contains all sets
		info.renderPass
	);

	::ReserveIndex(id, m_pipelines);
	m_pipelines[id] = g_device->Get().createGraphicsPipelineUnique({}, graphicsPipelineCreateInfo).value; // todo (hbedard): only if it succeeds
}

bool GraphicsPipelineSystem::IsSetLayoutCompatible(GraphicsPipelineID a, GraphicsPipelineID b, uint8_t set) const
{
	const auto& compatibility = m_pipelineCompatibility[a];
	const auto& otherCompatibility = m_pipelineCompatibility[b];
	if (set >= compatibility.size() || set >= otherCompatibility.size())
		return false;

	return compatibility[set] == otherCompatibility[set];
}

vk::PipelineLayout GraphicsPipelineSystem::GetPipelineLayout(GraphicsPipelineID id)
{
	return m_pipelineLayouts[id].back().get(); // last one contains all sets
}
