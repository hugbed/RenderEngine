#include "RenderPass.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <fstream>

#include <array>

#include "PhysicalDevice.h"

struct Vertex {
	glm::vec2 pos;
	glm::vec3 color;

	static vk::VertexInputBindingDescription GetBindingDescription()
	{
		return { 0, sizeof(Vertex) };
	}

	static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions() {
		std::array<vk::VertexInputAttributeDescription, 2> attributeDescriptions;

		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
		attributeDescriptions[0].offset = offsetof(Vertex, pos);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		return attributeDescriptions;
	}
};

std::vector<Vertex> vertices = {
	{{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
	{{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
	{{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

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
	: m_vertexBuffer(sizeof(Vertex)*vertices.size(), vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive)
{
	// Shaders

	m_vertexBuffer.Overwrite(vertices.data());

	m_imageExtent = swapchain.GetImageExtent();
	vk::Format imageFormat = swapchain.GetImageFormat();

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
	rasterizerState.frontFace = vk::FrontFace::eClockwise;

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

	vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	m_pipelineLayout = g_device->Get().createPipelineLayoutUnique(pipelineLayoutCreateInfo);

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

	vk::SubpassDescription subpass(
		vk::SubpassDescriptionFlags(),
		vk::PipelineBindPoint::eGraphics,
		0, nullptr, // input attachments
		1, &colorAttachmentRef
	);

	vk::RenderPassCreateInfo renderPassCreateInfo(
		vk::RenderPassCreateFlags(),
		1, &colorAttachment,
		1, &subpass
	);

	m_renderPass = g_device->Get().createRenderPassUnique(renderPassCreateInfo);

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
		nullptr, // depthStencilState
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

	for (size_t i = 0; i < imageViews.size(); i++)
	{
		vk::ImageView attachments[] = {
			imageViews[i]
		};

		vk::FramebufferCreateInfo frameBufferInfo(
			vk::FramebufferCreateFlags(),
			m_renderPass.get(),
			1, attachments,
			m_imageExtent.width, m_imageExtent.height,
			1 // layers
		);

		m_framebuffers.push_back(g_device->Get().createFramebufferUnique(frameBufferInfo));
	}
}

void RenderPass::PopulateRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
{
	vk::ClearValue clearValue(
		vk::ClearColorValue(std::array{ 0.0f, 0.0f, 0.0f, 1.0f })
	);
	vk::RenderPassBeginInfo renderPassBeginInfo(
		m_renderPass.get(),
		m_framebuffers[imageIndex].get(),
		vk::Rect2D(vk::Offset2D(0, 0), m_imageExtent),
		1, &clearValue
	);
	commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
	{
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.get());

		vk::Buffer buffers[] = { m_vertexBuffer.Get() };
		VkDeviceSize offsets[] = { 0 };
		commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
		
		commandBuffer.draw(3, 1, 0, 0);
	}
	commandBuffer.endRenderPass();
}
