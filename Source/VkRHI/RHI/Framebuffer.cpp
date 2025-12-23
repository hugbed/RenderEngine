#include <RHI/Framebuffer.h>

#include <RHI/Swapchain.h>
#include <RHI/Device.h>

std::vector<Framebuffer> Framebuffer::FromSwapchain(const Swapchain& swapchain, vk::RenderPass renderPass)
{
	std::vector<Framebuffer> framebuffers;
	framebuffers.reserve(swapchain.GetImageCount());

	auto colorImageView = swapchain.GetColorImageView();
	auto depthImageView = swapchain.GetDepthImageView();
	auto imageViews = swapchain.GetImageViews();

	for (size_t i = 0; i < swapchain.GetImageCount(); ++i)
	{
		std::vector<vk::ImageView> attachments = {
			colorImageView,
			depthImageView,
			imageViews[i]
		};
		framebuffers.emplace_back(renderPass, swapchain.GetImageDescription().extent, attachments);
	}

	return framebuffers;
}

Framebuffer::Framebuffer(vk::RenderPass renderPass, vk::Extent2D extent, const std::vector<vk::ImageView>& attachments)
	: m_extent(extent)
{
	vk::FramebufferCreateInfo frameBufferInfo(
		vk::FramebufferCreateFlags(),
		renderPass,
		static_cast<uint32_t>(attachments.size()), attachments.data(),
		extent.width, extent.height,
		1 // layers
	);

	m_framebuffer = g_device->Get().createFramebufferUnique(frameBufferInfo);
}
