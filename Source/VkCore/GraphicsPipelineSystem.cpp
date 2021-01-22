#include "GraphicsPipelineSystem.h"

#include "Image.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "vk_utils.h"
#include "hash.h"

#include <array>
#include <map>

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
		size_t bindingsSize = bindings.size() * sizeof(vk::DescriptorSetLayoutBinding);
		size_t pushConstantsSize = pushConstants.size() * sizeof(vk::PushConstantRange);

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

	[[nodiscard]] SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> CombineDescriptorSetLayoutBindings(
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& bindings1, // bindings[set][binding]
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& bindings2)
	{
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> descriptorSetLayoutBindings((std::max)(bindings1.size(), bindings2.size()));

		for (size_t set = 0; set < descriptorSetLayoutBindings.size(); ++set)
		{
			auto& descriptorSetLayoutBinding = descriptorSetLayoutBindings[set];

			if (set < bindings1.size())
			{
				auto& vertexSetLayoutBindings = bindings1[set];
				descriptorSetLayoutBinding.insert(descriptorSetLayoutBinding.end(), vertexSetLayoutBindings.begin(), vertexSetLayoutBindings.end());
			}
			if (set < bindings2.size())
			{
				auto& fragmentSetLayoutBindings = bindings2[set];
				descriptorSetLayoutBinding.insert(descriptorSetLayoutBinding.end(), fragmentSetLayoutBindings.begin(), fragmentSetLayoutBindings.end());
			}

			// and sort them by bindings
			std::sort(descriptorSetLayoutBinding.begin(), descriptorSetLayoutBinding.end(), [](const auto& a, const auto& b) {
				return a.binding < b.binding;
			});
		}

		return descriptorSetLayoutBindings;
	}

	[[nodiscard]] SetVector<vk::UniqueDescriptorSetLayout> CreateDescriptorSetLayoutsFromBindings(
		const SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetsLayoutBindings)
	{
		SetVector<vk::UniqueDescriptorSetLayout> descriptorSetLayouts;

		for (size_t set = 0; set < descriptorSetsLayoutBindings.size(); ++set)
		{
			auto& setBindings = descriptorSetsLayoutBindings[set];

			SmallVector<vk::DescriptorBindingFlags> bindingFlags;
			bindingFlags.reserve(setBindings.size());
			for (const auto& setBinding : setBindings)
			{
				if (setBinding.descriptorCount == 0)
					bindingFlags.emplace_back(vk::DescriptorBindingFlagBits::eVariableDescriptorCount);
				else
					bindingFlags.emplace_back();
			}

			vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingsFlagsInfo;
			bindingsFlagsInfo.bindingCount = (uint32_t)bindingFlags.size();
			bindingsFlagsInfo.pBindingFlags = bindingFlags.data();

			vk::DescriptorSetLayoutCreateInfo createInfo;
			createInfo.pNext = &bindingsFlagsInfo;

			descriptorSetLayouts.push_back(g_device->Get().createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo(
				{}, static_cast<uint32_t>(setBindings.size()), setBindings.data()
			)));
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
	auto vertexBindings = m_shaderSystem->GetDescriptorSetLayoutBindings(vertexShaderID);
	auto fragmentBindings = m_shaderSystem->GetDescriptorSetLayoutBindings(fragmentShaderID);

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
	m_pipelines[id] = g_device->Get().createGraphicsPipelineUnique({}, graphicsPipelineCreateInfo);
}

bool GraphicsPipelineSystem::IsSetLayoutCompatible(GraphicsPipelineID a, GraphicsPipelineID b, uint8_t set) const
{
	const auto& compatibility = m_pipelineCompatibility[a];
	const auto& otherCompatibility = m_pipelineCompatibility[b];
	if (set >= compatibility.size() || set >= otherCompatibility.size())
		return false;

	return compatibility[set] == otherCompatibility[set];
}
