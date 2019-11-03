#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

class Device;
class PhysicalDevice;

class Swapchain
{
public:
	Swapchain(
		Device& device,
		PhysicalDevice& physicalDevice,
		vk::SurfaceKHR surface,
		vk::Extent2D desiredExtent
	);

	void CreateFramebuffers(vk::Device device, vk::RenderPass renderPass);

	vk::Format GetImageFormat() const { return m_imageFormat; }

	uint32_t GetFramebufferCount() { return m_images.size(); }
	vk::Framebuffer GetFrameBuffer(uint32_t i) { return m_framebuffers[i].get(); }

	explicit operator vk::SwapchainKHR &() { return m_swapchain.get(); }
	explicit operator vk::SwapchainKHR const& () const { return m_swapchain.get(); }

private:
	void CreateImageViews(vk::Device device);

	// Swapchain
	vk::UniqueSwapchainKHR m_swapchain;

	// Images
	std::vector<vk::Image> m_images;
	vk::Format m_imageFormat;
	vk::Extent2D m_imageExtent;
	std::vector<vk::UniqueImageView> m_imageViews;

	std::vector<vk::UniqueFramebuffer> m_framebuffers;
};
