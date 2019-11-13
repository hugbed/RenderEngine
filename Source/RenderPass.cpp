#include "RenderPass.h"

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

RenderPass::RenderPass(const Swapchain& swapchain)
{
	m_imageExtent = swapchain.GetImageExtent();
	vk::Format imageFormat = swapchain.GetImageFormat();

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
		static_cast<float>(m_imageExtent.width), static_cast<float>(m_imageExtent.height),
		0.0f, 1.0f // depth (min, max)
	);
	vk::Rect2D scissor(vk::Offset2D(0, 0), m_imageExtent);
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

	// Render passes

	vk::AttachmentDescription colorAttachment(
		vk::AttachmentDescriptionFlags(),
		imageFormat,
		vk::SampleCountFlagBits::e1,
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eStore,
		vk::AttachmentLoadOp::eDontCare, // stencilLoadOp
		vk::AttachmentStoreOp::eDontCare, // stencilStoreOp
		vk::ImageLayout::eUndefined, // initialLayout
		vk::ImageLayout::ePresentSrcKHR	// finalLayout
	);
	vk::AttachmentReference colorAttachmentRef(
		0, vk::ImageLayout::eColorAttachmentOptimal
	);

	vk::AttachmentDescription depthAttachment(
		vk::AttachmentDescriptionFlags(),
		g_physicalDevice->FindDepthFormat(),
		vk::SampleCountFlagBits::e1,
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eDontCare,
		vk::AttachmentLoadOp::eDontCare, // stencilLoadOp
		vk::AttachmentStoreOp::eDontCare, // stencilStoreOp
		vk::ImageLayout::eUndefined, // initialLayout
		vk::ImageLayout::eDepthStencilAttachmentOptimal	// finalLayout
	);
	vk::AttachmentReference depthAttachmentRef(
		1, vk::ImageLayout::eDepthStencilAttachmentOptimal
	);

	vk::SubpassDescription subpass(
		vk::SubpassDescriptionFlags(),
		vk::PipelineBindPoint::eGraphics,
		0, nullptr, // input attachments
		1, &colorAttachmentRef,
		nullptr, // pResolveAttachment
		&depthAttachmentRef
	);

	std::array<vk::AttachmentDescription, 2 > attachmentDescriptions = {
		colorAttachment, depthAttachment
	};

	vk::RenderPassCreateInfo renderPassCreateInfo(
		vk::RenderPassCreateFlags(),
		static_cast<size_t>(attachmentDescriptions.size()), attachmentDescriptions.data(),
		1, &subpass
	);

	m_renderPass = g_device->Get().createRenderPassUnique(renderPassCreateInfo);

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
		m_renderPass.get()
	);

	m_graphicsPipeline = g_device->Get().createGraphicsPipelineUnique({}, graphicsPipelineCreateInfo);

	// Framebuffers

	m_framebuffers.clear();
	m_framebuffers.reserve(swapchain.GetImageCount());

	auto imageViews = swapchain.GetImageViews();
	auto depthImageView = swapchain.GetDepthImageView();

	for (size_t i = 0; i < imageViews.size(); i++)
	{
		std::array<vk::ImageView, 2> attachments = {
			imageViews[i],
			depthImageView
		};

		vk::FramebufferCreateInfo frameBufferInfo(
			vk::FramebufferCreateFlags(),
			m_renderPass.get(),
			static_cast<uint32_t>(attachments.size()), attachments.data(),
			m_imageExtent.width, m_imageExtent.height,
			1 // layers
		);

		m_framebuffers.push_back(g_device->Get().createFramebufferUnique(frameBufferInfo));
	}
}

RenderPass::Descriptors RenderPass::CreateDescriptorSets(std::vector<vk::Buffer> uniformBuffers, vk::ImageView textureImageView, vk::Sampler textureSampler)
{
	Descriptors descriptors;

	std::array<vk::DescriptorPoolSize, 2> poolSizes = {
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, m_framebuffers.size()),
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, m_framebuffers.size()),
	};
	descriptors.descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		m_framebuffers.size(),
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));

	std::vector<vk::DescriptorSetLayout> layouts(m_framebuffers.size(), m_descriptorSetLayout.get());
	descriptors.descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		descriptors.descriptorPool.get(), m_framebuffers.size(), layouts.data()
	));
	for (uint32_t i = 0; i < m_framebuffers.size(); ++i)
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

void RenderPass::PopulateRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
{
	if (!m_indexBuffer || !m_vertexBuffer || m_descriptorSets.empty())
		return; // nothing to draw

	std::array<vk::ClearValue, 2> clearValues = {
		vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f }),
		vk::ClearDepthStencilValue(1.0f, 0.0f)
	};

	vk::RenderPassBeginInfo renderPassBeginInfo(
		m_renderPass.get(),
		m_framebuffers[imageIndex].get(),
		vk::Rect2D(vk::Offset2D(0, 0), m_imageExtent),
		static_cast<uint32_t>(clearValues.size()), clearValues.data()
	);
	commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
	{
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.get());

		commandBuffer.bindIndexBuffer(m_indexBuffer, 0, vk::IndexType::eUint32);
		
		VkDeviceSize offsets[] = { 0 };
		vk::Buffer vertexBuffers[] = { m_vertexBuffer };
		commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout.get(), 0, 1, &m_descriptorSets[imageIndex], 0, nullptr);

		commandBuffer.drawIndexed(m_nbIndices, 1, 0, 0, 0);
	}
	commandBuffer.endRenderPass();
}
