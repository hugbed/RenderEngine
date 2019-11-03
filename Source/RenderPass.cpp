#include "RenderPass.h"

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

vk::UniqueShaderModule CreateShaderModule(vk::Device device, const std::vector<char>& code) {
	return device.createShaderModuleUnique(
		vk::ShaderModuleCreateInfo(
			vk::ShaderModuleCreateFlags(),
			code.size(),
			reinterpret_cast<const uint32_t*>(code.data())
		)
	);
}

RenderPass::RenderPass(vk::Device device, const Swapchain& swapchain)
{
	// Shaders

	m_imageExtent = swapchain.GetImageExtent();
	vk::Format imageFormat = swapchain.GetImageFormat();

	auto vertShaderCode = ReadFile("vert.spv");
	vk::UniqueShaderModule vertShaderModule = CreateShaderModule(device, vertShaderCode);
	vk::PipelineShaderStageCreateInfo vertexShaderStateInfo(
		vk::PipelineShaderStageCreateFlags(),
		vk::ShaderStageFlagBits::eVertex,
		vertShaderModule.get(),
		"main"
	);
	auto fragShaderCode = ReadFile("frag.spv");
	vk::UniqueShaderModule fragShaderModule = CreateShaderModule(device, fragShaderCode);
	vk::PipelineShaderStageCreateInfo fragmentShaderStateInfo(
		vk::PipelineShaderStageCreateFlags(),
		vk::ShaderStageFlagBits::eFragment,
		fragShaderModule.get(),
		"main"
	);
	vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexShaderStateInfo, fragmentShaderStateInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

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
	m_pipelineLayout = device.createPipelineLayoutUnique(pipelineLayoutCreateInfo);

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

	m_renderPass = device.createRenderPassUnique(renderPassCreateInfo);

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

	m_graphicsPipeline = device.createGraphicsPipelineUnique({}, graphicsPipelineCreateInfo);

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

		m_framebuffers.push_back(device.createFramebufferUnique(frameBufferInfo));
	}
}

void RenderPass::SendRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
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
		commandBuffer.draw(3, 1, 0, 0);
	}
	commandBuffer.endRenderPass();
}
