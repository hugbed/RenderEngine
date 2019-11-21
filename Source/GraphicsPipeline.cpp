#include "GraphicsPipeline.h"

#include "Device.h"
#include "PhysicalDevice.h"

#include <fstream>

#include <array>

vk::VertexInputBindingDescription Vertex::GetBindingDescription()
{
	return { 0, sizeof(Vertex), vk::VertexInputRate::eVertex };
}

std::array<vk::VertexInputAttributeDescription, 3> Vertex::GetAttributeDescriptions()
{
	std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions;

	attributeDescriptions[0].binding = 0;
	attributeDescriptions[0].location = 0;
	attributeDescriptions[0].format = vk::Format::eR32G32B32A32Sfloat;
	attributeDescriptions[0].offset = offsetof(Vertex, pos);

	attributeDescriptions[1].binding = 0;
	attributeDescriptions[1].location = 1;
	attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
	attributeDescriptions[1].offset = offsetof(Vertex, color);

	attributeDescriptions[2].binding = 0;
	attributeDescriptions[2].location = 2;
	attributeDescriptions[2].format = vk::Format::eR32G32Sfloat;
	attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

	return attributeDescriptions;
}

static std::vector<char> ReadFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error("failed to open file!");
	
	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();
	
	return buffer;
}

vk::UniqueShaderModule CreateShaderModule(const std::vector<char>& code) {
	return g_device->Get().createShaderModuleUnique(
		vk::ShaderModuleCreateInfo(
			vk::ShaderModuleCreateFlags(),
			code.size(),
			reinterpret_cast<const uint32_t*>(code.data())
		)
	);
}

GraphicsPipeline::GraphicsPipeline(vk::Extent2D imageExtent, vk::Format imageFormat, vk::RenderPass renderPass)
{
	// Shaders

	auto vertShaderCode = ReadFile("vert.spv");
	vk::UniqueShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
	vk::PipelineShaderStageCreateInfo vertexShaderStateInfo(
		vk::PipelineShaderStageCreateFlags(),
		vk::ShaderStageFlagBits::eVertex,
		vertShaderModule.get(),
		"main"
	);
	auto fragShaderCode = ReadFile("frag.spv");
	vk::UniqueShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);
	vk::PipelineShaderStageCreateInfo fragmentShaderStateInfo(
		vk::PipelineShaderStageCreateFlags(),
		vk::ShaderStageFlagBits::eFragment,
		fragShaderModule.get(),
		"main"
	);
	vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexShaderStateInfo, fragmentShaderStateInfo };

	// Vertex shader inputs
	auto bindingDescription = Vertex::GetBindingDescription();
	auto attributeDescription = Vertex::GetAttributeDescriptions();
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo(
		{},
		1, &bindingDescription,
		static_cast<uint32_t>(attributeDescription.size()), attributeDescription.data()
	);

	vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

	// Viewport state

	vk::Viewport viewport(
		0.0f, 0.0f, // x, y
		static_cast<float>(imageExtent.width), static_cast<float>(imageExtent.height),
		0.0f, 1.0f // depth (min, max)
	);
	vk::Rect2D scissor(vk::Offset2D(0, 0), imageExtent);
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

	// Descriptors

	vk::DescriptorSetLayoutBinding uboLayoutBinding(
		0, // binding
		vk::DescriptorType::eUniformBuffer,
		1, // descriptorCount
		vk::ShaderStageFlagBits::eVertex
	);
	vk::DescriptorSetLayoutBinding samplerBinding(
		1, // binding
		vk::DescriptorType::eCombinedImageSampler,
		1, // descriptorCount
		vk::ShaderStageFlagBits::eFragment
	);
	std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerBinding };
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

GraphicsPipeline::Descriptors GraphicsPipeline::CreateDescriptorSets(std::vector<vk::Buffer> uniformBuffers, vk::ImageView textureImageView, vk::Sampler textureSampler)
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
		vk::DescriptorBufferInfo descriptorBufferInfo(uniformBuffers[i], 0, sizeof(UniformBufferObject));
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
