#include "RHI/RenderPass.h"

#include "RHI/PhysicalDevice.h"
#include "RHI/Device.h"
#include "RHI/Framebuffer.h"

RenderPass::RenderPass(vk::Format colorAttachmentFormat)
{
	vk::AttachmentDescription colorAttachment(
		vk::AttachmentDescriptionFlags(),
		colorAttachmentFormat,
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
		colorAttachmentFormat,
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
