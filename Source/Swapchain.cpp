#include "SwapChain.h"

#include "Device.h"
#include "PhysicalDevice.h"

#include "defines.h"

namespace
{
	vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
	{
		for (const auto& availableFormat : availableFormats)
		{
			if (availableFormat.format == vk::Format::eB8G8R8A8Unorm &&
				availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			{
				return availableFormat;
			}
		}
		return availableFormats[0];
	}

	vk::PresentModeKHR ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
	{
		auto mode = std::find_if(availablePresentModes.begin(), availablePresentModes.end(), [](const auto& mode) {
			return mode == vk::PresentModeKHR::eMailbox;
			});
		return mode != availablePresentModes.end() ? *mode : vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D desiredExtent)
	{
		if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)())
		{
			return capabilities.currentExtent;
		}
		else
		{
			VkExtent2D actualExtent = desiredExtent;
			actualExtent.width = (std::max)(capabilities.minImageExtent.width, (std::min)(capabilities.maxImageExtent.width, actualExtent.width));
			actualExtent.height = (std::max)(capabilities.minImageExtent.height, (std::min)(capabilities.maxImageExtent.height, actualExtent.height));
			return actualExtent;
		}
	}
}

Swapchain::Swapchain(Device& device, PhysicalDevice& physicalDevice, vk::SurfaceKHR surface, vk::Extent2D desiredExtent)
{
	auto swapChainSupport = physicalDevice.QuerySwapchainSupport();

	vk::SurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
	vk::PresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
	vk::Extent2D imageExtent = ChooseSwapExtent(swapChainSupport.capabilities, desiredExtent);

	uint32_t minImageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 &&
		minImageCount > swapChainSupport.capabilities.maxImageCount)
	{
		minImageCount = swapChainSupport.capabilities.maxImageCount;
	}

	vk::SwapchainCreateInfoKHR createInfo(
		vk::SwapchainCreateFlagsKHR{},
		surface,
		minImageCount,
		surfaceFormat.format,
		surfaceFormat.colorSpace,
		imageExtent,
		1,												// imageArrayLayers
		vk::ImageUsageFlagBits::eColorAttachment,
		vk::SharingMode::eExclusive,
		0,												// queueFamilyIndexCount
		nullptr,										// pQueueFamilyIndices
		swapChainSupport.capabilities.currentTransform, // preTransform
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		presentMode,
		VK_TRUE											// clipped
	);

	// Handle having a different queue for graphics and presentation
	auto queueFamilies = physicalDevice.GetQueueFamilies();
	uint32_t queueFamilyIndices[] = { queueFamilies.graphicsFamily.value(), queueFamilies.presentFamily.value() };
	if (queueFamilies.graphicsFamily != queueFamilies.presentFamily)
	{
		createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	}

	vk::Device vkDevice = static_cast<vk::Device>(device);

	// Create Swapchain
	m_swapchain = vkDevice.createSwapchainKHRUnique(createInfo);

	// Fetch images and image info
	m_images = vkDevice.getSwapchainImagesKHR(m_swapchain.get());
	m_imageFormat = surfaceFormat.format;
	m_imageExtent = imageExtent;
	CreateImageViews(vkDevice);
}

void Swapchain::CreateImageViews(vk::Device device)
{
	m_imageViews.clear();
	m_imageViews.reserve(m_images.size());

	for (size_t i = 0; i < m_images.size(); i++) {
		vk::ImageViewCreateInfo createInfo(
			vk::ImageViewCreateFlags(),
			m_images[i],
			vk::ImageViewType::e2D,
			m_imageFormat,
			vk::ComponentMapping(vk::ComponentSwizzle::eIdentity),
			vk::ImageSubresourceRange(
				vk::ImageAspectFlagBits::eColor,
				0, // baseMipLevel
				1, // levelCount
				0, // baseArrayLayer
				1 // layerCount
			)
		);

		m_imageViews.push_back(device.createImageViewUnique(createInfo));
	}
}

void Swapchain::CreateFramebuffers(vk::Device device, vk::RenderPass renderPass)
{
	m_framebuffers.clear();
	m_framebuffers.reserve(m_imageViews.size());

	for (size_t i = 0; i < m_imageViews.size(); i++)
	{
		vk::ImageView attachments[] = {
			m_imageViews[i].get()
		};

		vk::FramebufferCreateInfo frameBufferInfo(
			vk::FramebufferCreateFlags(),
			renderPass,
			1, attachments,
			m_imageExtent.width, m_imageExtent.height,
			1 // layers
		);

		m_framebuffers.push_back(device.createFramebufferUnique(frameBufferInfo));
	}
}
