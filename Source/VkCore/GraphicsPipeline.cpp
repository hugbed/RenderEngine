#include "GraphicsPipeline.h"

#include "Image.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "vk_utils.h"
#include "hash.h"

#include <array>
#include <map>

namespace
{
	uint64_t HashPipelineLayout(
		const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
		const std::vector<vk::PushConstantRange>& pushConstants)
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
}

GraphicsPipelineInfo::GraphicsPipelineInfo()
	: sampleCount(g_physicalDevice->GetMsaaSamples())
{
}

GraphicsPipeline::GraphicsPipeline(
	vk::RenderPass renderPass,
	vk::Extent2D viewportExtent,
	const ShaderSystem& shaderSystem,
	ShaderID vertexShaderID, ShaderID fragmentShaderID,
	const GraphicsPipelineInfo& info)
	: m_shaderSystem(&shaderSystem)
{
	Init(renderPass, viewportExtent, vertexShaderID, fragmentShaderID, info);
}

GraphicsPipeline::GraphicsPipeline(
	vk::RenderPass renderPass,
	vk::Extent2D viewportExtent,
	const ShaderSystem& shaderSystem,
	ShaderID vertexShaderID, ShaderID fragmentShaderID)
	: m_shaderSystem(&shaderSystem)
{
	GraphicsPipelineInfo info = {};
	Init(renderPass, viewportExtent, vertexShaderID, fragmentShaderID, info);
}

namespace
{
	[[nodiscard]] std::vector<std::vector<vk::DescriptorSetLayoutBinding>> CombineDescriptorSetLayoutBindings(
		const std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& bindings1, // bindings[set][binding]
		const std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& bindings2)
	{
		std::vector<std::vector<vk::DescriptorSetLayoutBinding>> descriptorSetLayoutBindings((std::max)(bindings1.size(), bindings2.size()));

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

	[[nodiscard]] std::vector<vk::UniqueDescriptorSetLayout> CreateDescriptorSetLayoutsFromBindings(
		const std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& descriptorSetLayoutBindings)
	{
		std::vector<vk::UniqueDescriptorSetLayout> descriptorSetLayouts;

		for (size_t set = 0; set < descriptorSetLayoutBindings.size(); ++set)
		{
			auto& descriptorSetLayoutBinding = descriptorSetLayoutBindings[set];

			descriptorSetLayouts.push_back(g_device->Get().createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo(
				{}, static_cast<uint32_t>(descriptorSetLayoutBinding.size()), descriptorSetLayoutBinding.data()
			)));
		}

		return descriptorSetLayouts;
	}

	[[nodiscard]] std::vector<vk::UniquePipelineLayout> CreatePipelineLayoutsFromDescriptorSetLayouts(
		const std::vector<vk::UniqueDescriptorSetLayout>& descriptorSetLayouts,
		std::vector<vk::PushConstantRange> pushConstantRanges)
	{
		std::vector<vk::UniquePipelineLayout> pipelineLayouts;

		// Each pipeline layout includes descriptor set layout from previous set:
		// E.g. for sets 1, 2, 3, we have { set1 }, { set1, set2 }, { set1, set2, set3 }
		std::vector<vk::DescriptorSetLayout> pipelineDescriptorSetLayouts;
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

	[[nodiscard]] std::vector<vk::PushConstantRange> CombinePushConstantRanges(
		const std::vector<vk::PushConstantRange>& pushConstantRange1,
		const std::vector<vk::PushConstantRange>& pushConstantRange2)
	{
		std::vector<vk::PushConstantRange> pushConstantRanges(pushConstantRange1.size() + pushConstantRange2.size());
		pushConstantRanges.insert(pushConstantRanges.end(), pushConstantRange1.begin(), pushConstantRange1.end());
		pushConstantRanges.insert(pushConstantRanges.end(), pushConstantRange2.begin(), pushConstantRange2.end());
		return pushConstantRanges;
	}
}

void GraphicsPipeline::Init(
	vk::RenderPass renderPass,
	vk::Extent2D viewportExtent,
	ShaderInstanceID vertexShaderID, ShaderInstanceID fragmentShaderID,
	const GraphicsPipelineInfo& info)
{
	std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
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
		static_cast<float>(viewportExtent.width), static_cast<float>(viewportExtent.height),
		0.0f, 1.0f // depth (min, max)
	);
	vk::Rect2D scissor(vk::Offset2D(0, 0), viewportExtent);
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
	m_descriptorSetLayoutBindings = ::CombineDescriptorSetLayoutBindings(
		m_shaderSystem->GetDescriptorSetLayoutBindings(vertexShaderID),
		m_shaderSystem->GetDescriptorSetLayoutBindings(fragmentShaderID)
	);

	// Create descriptor set layouts
	m_descriptorSetLayouts = ::CreateDescriptorSetLayoutsFromBindings(m_descriptorSetLayoutBindings);

	// Combine push constants from vertex and fragment shader
	auto pushConstantRanges = ::CombinePushConstantRanges(
		m_shaderSystem->GetPushConstantRanges(vertexShaderID),
		m_shaderSystem->GetPushConstantRanges(fragmentShaderID)
	);

	// Build pipeline layouts for each set
	m_pipelineLayouts = ::CreatePipelineLayoutsFromDescriptorSetLayouts(m_descriptorSetLayouts, pushConstantRanges);

	// Compute hash pipeline descriptor bindings and push constants
	// If two Graphics Pipelien have the same hash for a set,
	// it means their pipeline layout for this set is compatible
	for (size_t set = 0; set < m_descriptorSetLayoutBindings.size(); ++set)
	{
		m_pipelineCompatibility.push_back(::HashPipelineLayout(m_descriptorSetLayoutBindings[set], pushConstantRanges));
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
		m_pipelineLayouts.back().get(), // the last one contains all sets
		renderPass
	);

	m_graphicsPipeline = g_device->Get().createGraphicsPipelineUnique({}, graphicsPipelineCreateInfo);
}
