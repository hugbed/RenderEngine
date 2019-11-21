#include "RenderPass.h"

#include "PhysicalDevice.h"
#include "Device.h"

#include "Framebuffer.h"

RenderPass::RenderPass(vk::Format imageFormat)
{
	vk::AttachmentDescription colorAttachment(
		vk::AttachmentDescriptionFlags(),
		imageFormat,
		g_physicalDevice->GetMsaaSamples(),
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eStore,
		vk::AttachmentLoadOp::eDontCare, // stencilLoadOp
		vk::AttachmentStoreOp::eDontCare, // stencilStoreOp
		vk::ImageLayout::eUndefined, // initialLayout
		vk::ImageLayout::eColorAttachmentOptimal // finalLayout
	);
	vk::AttachmentReference colorAttachmentRef(
		0, vk::ImageLayout::eColorAttachmentOptimal
	);

	vk::AttachmentDescription depthAttachment(
		vk::AttachmentDescriptionFlags(),
		g_physicalDevice->FindDepthFormat(),
		g_physicalDevice->GetMsaaSamples(),
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

	vk::AttachmentDescription colorAttachmentResolve(
		vk::AttachmentDescriptionFlags(),
		imageFormat,
		vk::SampleCountFlagBits::e1,
		vk::AttachmentLoadOp::eDontCare,
		vk::AttachmentStoreOp::eStore,
		vk::AttachmentLoadOp::eDontCare, // stencilLoadOp
		vk::AttachmentStoreOp::eDontCare, // stencilStoreOp
		vk::ImageLayout::eUndefined, // initialLayout
		vk::ImageLayout::ePresentSrcKHR	// finalLayout
	);
	vk::AttachmentReference colorAttachmentResolveRef(
		2, vk::ImageLayout::eColorAttachmentOptimal
	);

	vk::SubpassDescription subpass(
		vk::SubpassDescriptionFlags(),
		vk::PipelineBindPoint::eGraphics,
		0, nullptr, // input attachments
		1, &colorAttachmentRef,
		&colorAttachmentResolveRef,
		&depthAttachmentRef
	);

	std::array<vk::AttachmentDescription, 3 > attachmentDescriptions = {
		colorAttachment, depthAttachment, colorAttachmentResolve
	};

	vk::RenderPassCreateInfo renderPassCreateInfo(
		vk::RenderPassCreateFlags(),
		static_cast<size_t>(attachmentDescriptions.size()), attachmentDescriptions.data(),
		1, &subpass
	);

	m_renderPass = g_device->Get().createRenderPassUnique(renderPassCreateInfo);
}

void RenderPass::Begin(vk::CommandBuffer& commandBuffer, const Framebuffer& framebuffer, std::array<vk::ClearValue, 2> clearValues)
{
	commandBuffer.beginRenderPass(vk::RenderPassBeginInfo(
		m_renderPass.get(),
		framebuffer.Get(),
		vk::Rect2D(vk::Offset2D(0, 0), framebuffer.GetExtent()),
		static_cast<uint32_t>(clearValues.size()), clearValues.data()
	), vk::SubpassContents::eInline);
}
