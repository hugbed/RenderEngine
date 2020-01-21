#pragma once

#include <vulkan/vulkan.hpp>

#include <array>
#include <vector>

class Swapchain;

class Framebuffer
{
public:
	using value_type = vk::Framebuffer;

	static std::vector<Framebuffer> FromSwapchain(const Swapchain& swapchain, vk::RenderPass renderPass);

	Framebuffer(vk::RenderPass renderPass, vk::Extent2D extent, const std::vector<vk::ImageView>& attachments);

	const vk::Extent2D& GetExtent() const { return m_extent; }

	value_type Get() const { return m_framebuffer.get(); }

private:
	vk::Extent2D m_extent;
	vk::UniqueFramebuffer m_framebuffer;
};
