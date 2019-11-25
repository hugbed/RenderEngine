#include "GraphicsPipeline.h"

#include "Image.h"
#include "Shader.h"
#include "Device.h"
#include "PhysicalDevice.h"

#include <array>

GraphicsPipeline::GraphicsPipeline(vk::RenderPass renderPass, vk::Extent2D viewportExtent)
{
	Shader vertexShader("vert.spv", "main");
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo = vertexShader.GetVertexInputStateInfo();

	Shader fragmentShader("frag.spv", "main");
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

	// Descriptors (take this from shaders and combine them)

	const auto& vertexShaderBindings = vertexShader.GetDescriptorSetLayoutBindings();
	const auto& fragmentShaderBindings = fragmentShader.GetDescriptorSetLayoutBindings();

	std::vector<vk::DescriptorSetLayoutBinding> bindings;
	bindings.insert(bindings.end(), vertexShaderBindings.begin(), vertexShaderBindings.end());
	bindings.insert(bindings.end(), fragmentShaderBindings.begin(), fragmentShaderBindings.end());

	std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) {
		return a.binding < b.binding;
	});

	m_descriptorSetLayout = g_device->Get().createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo(
		{}, static_cast<uint32_t>(bindings.size()), bindings.data()
	));
	
	vk::DescriptorSetLayout descriptorSetLayouts[] = { m_descriptorSetLayout.get() };
	m_pipelineLayout = g_device->Get().createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
		{}, 1, descriptorSetLayouts
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

GraphicsPipeline::Descriptors GraphicsPipeline::CreateDescriptorSets(std::vector<vk::Buffer> uniformBuffers, size_t uniformBufferSize, vk::ImageView textureImageView, vk::Sampler textureSampler)
{
	Descriptors descriptors;

	std::array<vk::DescriptorPoolSize, 2> poolSizes = {
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, uniformBuffers.size()),
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, uniformBuffers.size()),
	};
	descriptors.descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		uniformBuffers.size(),
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));

	std::vector<vk::DescriptorSetLayout> layouts(uniformBuffers.size(), m_descriptorSetLayout.get());
	descriptors.descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		descriptors.descriptorPool.get(), uniformBuffers.size(), layouts.data()
	));
	for (uint32_t i = 0; i < uniformBuffers.size(); ++i)
	{
		vk::DescriptorBufferInfo descriptorBufferInfo(uniformBuffers[i], 0, uniformBufferSize);
		vk::DescriptorImageInfo imageInfo(textureSampler, textureImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
		std::array<vk::WriteDescriptorSet, 2> writeDescriptorSets = {
			vk::WriteDescriptorSet(descriptors.descriptorSets[i].get(), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo), // binding = 0
			vk::WriteDescriptorSet(descriptors.descriptorSets[i].get(), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr) // binding = 1
		};
		g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	return descriptors;
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
