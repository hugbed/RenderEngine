#include "GraphicsPipeline.h"

#include "Image.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "vk_utils.h"

#include <array>
#include <map>

GraphicsPipeline::GraphicsPipeline(vk::RenderPass renderPass, vk::Extent2D viewportExtent, const Shader& vertexShader, const Shader& fragmentShader)
{
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo = vertexShader.GetVertexInputStateInfo();

	vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexShader.GetShaderStageInfo(), fragmentShader.GetShaderStageInfo() };

	vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);

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
	multisampling.rasterizationSamples = g_physicalDevice->GetMsaaSamples();

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(
		false // blendEnable
	);
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

	// Get each descriptor set layout bindings (1 list per descriptor set)
	const auto& vertexShaderBindings = vertexShader.GetDescriptorSetLayoutBindings();
	const auto& fragmentShaderBindings = fragmentShader.GetDescriptorSetLayoutBindings();

	// Combine sets from vertex and fragment shader
 	m_descriptorSetLayoutBindings.clear();
	m_descriptorSetLayoutBindings.resize((std::max(vertexShaderBindings.size(), fragmentShaderBindings.size())));
	
	for (size_t set = 0; set < m_descriptorSetLayoutBindings.size(); ++set)
	{
		auto& descriptorSetLayoutBinding = m_descriptorSetLayoutBindings[set];

		if (set < vertexShaderBindings.size())
		{
			auto& vertexSetLayoutBindings = vertexShaderBindings[set];
			descriptorSetLayoutBinding.insert(descriptorSetLayoutBinding.end(), vertexSetLayoutBindings.begin(), vertexSetLayoutBindings.end());
		}
		if (set < fragmentShaderBindings.size())
		{
			auto& fragmentSetLayoutBindings = fragmentShaderBindings[set];
			descriptorSetLayoutBinding.insert(descriptorSetLayoutBinding.end(), fragmentSetLayoutBindings.begin(), fragmentSetLayoutBindings.end());
		}

		// and sort them by bindings
		std::sort(descriptorSetLayoutBinding.begin(), descriptorSetLayoutBinding.end(), [](const auto& a, const auto& b) {
			return a.binding < b.binding;
		});

		m_descriptorSetLayouts.push_back(g_device->Get().createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo(
			{}, static_cast<uint32_t>(descriptorSetLayoutBinding.size()), descriptorSetLayoutBinding.data()
		)));
	}

	// Combine push constants from vertex and fragment shader
	const auto& vertexPushConstantRanges = vertexShader.GetPushConstantRanges();
	const auto& fragmentPushConstantRanges = fragmentShader.GetPushConstantRanges();

	m_pushConstantRanges.reserve(vertexPushConstantRanges.size() + fragmentPushConstantRanges.size());
	m_pushConstantRanges.insert(m_pushConstantRanges.end(), vertexPushConstantRanges.begin(), vertexPushConstantRanges.end());
	m_pushConstantRanges.insert(m_pushConstantRanges.end(), fragmentPushConstantRanges.begin(), fragmentPushConstantRanges.end());

	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = vk_utils::remove_unique(m_descriptorSetLayouts);
	m_pipelineLayout = g_device->Get().createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
		{},
		static_cast<uint32_t>(descriptorSetLayouts.size()), descriptorSetLayouts.data(),
		static_cast<uint32_t>(m_pushConstantRanges.size()), m_pushConstantRanges.data()
	));

	vk::PipelineDepthStencilStateCreateInfo depthStencilState(
		{},
		true, // depthTestEnable
		true, // depthWriteEnable
		vk::CompareOp::eLess, // depthCompareOp
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
		m_pipelineLayout.get(),
		renderPass
	);

	m_graphicsPipeline = g_device->Get().createGraphicsPipelineUnique({}, graphicsPipelineCreateInfo);
}

void GraphicsPipeline::Draw(
	vk::CommandBuffer& commandBuffer,
	uint32_t indexCount,
	vk::Buffer vertexBuffer,
	vk::Buffer indexBuffer,
	VkDeviceSize* vertexOffsets,
	vk::DescriptorSet descriptorSet) // todo: add constants
{
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.get());

	commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

	vk::Buffer vertexBuffers[] = { vertexBuffer };
	commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, vertexOffsets);

	commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout.get(), 0, 1, &descriptorSet, 0, nullptr);

	commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
}
