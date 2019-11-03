#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <iostream>

#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "Swapchain.h"

#include <fstream>
static std::vector<char> ReadFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}
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

int main()
{
	vk::Extent2D extent{ 800, 600 };

	Window window(extent, "Vulkan");

	Instance instance(window);

	vk::UniqueSurfaceKHR surface = window.CreateSurface(static_cast<vk::Instance>(instance));

	PhysicalDevice physicalDevice(static_cast<vk::Instance>(instance), surface.get());

	Device device(physicalDevice);
	auto vkDevice = static_cast<vk::Device>(device);

	// Queues
	auto indices = physicalDevice.GetQueueFamilies();
	auto graphicsQueue = device.GetQueue(indices.graphicsFamily.value());
	auto presentQueue = device.GetQueue(indices.presentFamily.value());

	Swapchain swapchain(
		device,
		physicalDevice,
		surface.get(),
		extent
	);

	// ---- Graphics pipeline ---- //

	// Shaders
	
	auto vertShaderCode = ReadFile("vert.spv");
	vk::UniqueShaderModule vertShaderModule = CreateShaderModule(vkDevice, vertShaderCode);
	vk::PipelineShaderStageCreateInfo vertexShaderStateInfo(
		vk::PipelineShaderStageCreateFlags(),
		vk::ShaderStageFlagBits::eVertex,
		vertShaderModule.get(),
		"main"
	);
	auto fragShaderCode = ReadFile("frag.spv");
	vk::UniqueShaderModule fragShaderModule = CreateShaderModule(vkDevice, fragShaderCode);
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
		static_cast<float>(extent.width), static_cast<float>(extent.height),
		0.0f, 1.0f // depth (min, max)
	);
	vk::Rect2D scissor(vk::Offset2D(0, 0), extent);
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
	vk::UniquePipelineLayout pipelineLayout = vkDevice.createPipelineLayoutUnique(pipelineLayoutCreateInfo);

	// Render passes

	vk::AttachmentDescription colorAttachment(
		vk::AttachmentDescriptionFlags(),
		swapchain.GetImageFormat(),
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

	vk::UniqueRenderPass renderPass = vkDevice.createRenderPassUnique(renderPassCreateInfo);

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
		pipelineLayout.get(),
		renderPass.get()
	);

	vk::UniquePipeline graphicsPipeline = vkDevice.createGraphicsPipelineUnique({}, graphicsPipelineCreateInfo);

	// Framebuffers
	swapchain.CreateFramebuffers(vkDevice, renderPass.get());

	// Commands

	// Pool
	vk::CommandPoolCreateInfo poolInfo({}, indices.graphicsFamily.value());
	vk::UniqueCommandPool commandPool = vkDevice.createCommandPoolUnique(poolInfo);

	// Buffer
	vk::CommandBufferAllocateInfo commandBufferAllocateInfo(
		commandPool.get(), vk::CommandBufferLevel::ePrimary, swapchain.GetFramebufferCount()
	);

	// Create command buffers
	std::vector<vk::UniqueCommandBuffer> commandBuffers = vkDevice.allocateCommandBuffersUnique(commandBufferAllocateInfo);

	// Record commands
	for (size_t i = 0; i < commandBuffers.size(); i++) {

		auto& commandBuffer = commandBuffers[i];

		vk::CommandBufferBeginInfo beginInfo;
		commandBuffer->begin(beginInfo);
		{
			vk::ClearValue clearValue(
				vk::ClearColorValue(std::array{ 0.0f, 0.0f, 0.0f, 1.0f })
			);
			vk::RenderPassBeginInfo renderPassBeginInfo(
				renderPass.get(),
				swapchain.GetFrameBuffer(i),
				vk::Rect2D(vk::Offset2D(0, 0), extent),
				1, &clearValue
			);
			commandBuffer->beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
			{
				commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.get());
				commandBuffer->draw(3, 1, 0, 0);
			}
			commandBuffer->endRenderPass();
		}
		commandBuffer->end();
	}

	// Synchronization

	vk::UniqueSemaphore imageAvailableSemaphore = vkDevice.createSemaphoreUnique({});
	vk::UniqueSemaphore renderFinishedSemaphore = vkDevice.createSemaphoreUnique({});

	auto vkSwapchain = static_cast<vk::SwapchainKHR>(swapchain);

	window.MainLoop(
		[&vkDevice, &vkSwapchain, &imageAvailableSemaphore, &renderFinishedSemaphore, &commandBuffers, &graphicsQueue, &presentQueue]()
		{
			// Acquire image from swapchain (this could be in swapchain?)
			auto [result, imageIndex] = vkDevice.acquireNextImageKHR(vkSwapchain, uint64_t(UINT64_MAX), imageAvailableSemaphore.get(), nullptr);
			if (result != vk::Result::eSuccess)
				return;

			// Submit command buffer on graphics queue
			vk::Semaphore waitSemaphores[] = { imageAvailableSemaphore.get() };
			vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
			vk::Semaphore signalSemaphores[] = { renderFinishedSemaphore.get() };
			vk::SubmitInfo submitInfo(
				1, waitSemaphores, waitStages,
				1, &commandBuffers[imageIndex].get(),
				1, signalSemaphores
			);
			graphicsQueue.submit(submitInfo, nullptr);

			// Subpass dependencies
			vk::SubpassDependency dependency;
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependency.srcAccessMask = vk::AccessFlags();
			dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

			// Presentation
			vk::PresentInfoKHR presentInfo = {};
			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = signalSemaphores;
			
			vk::SwapchainKHR swapChains[] = { vkSwapchain };
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = swapChains;
			presentInfo.pImageIndices = &imageIndex;

			result = presentQueue.presentKHR(presentInfo);
			if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
				return; // recreate swapchain

			vkQueueWaitIdle(presentQueue);
		}
	);

	return 0;
}
