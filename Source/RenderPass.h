#pragma once

#include "Swapchain.h"

#include <vulkan/vulkan.hpp>

#include <vector>

#include "Device.h"
#include "PhysicalDevice.h"

class Buffer
{
public:
	Buffer(vk::Device device, const PhysicalDevice& physicalDevice, size_t size, vk::BufferUsageFlags bufferUsage, vk::SharingMode sharingMode)
		: m_size(size)
		, device(device)
	{
		vk::BufferCreateInfo bufferInfo({}, size, bufferUsage, sharingMode);
		m_buffer = device.createBufferUnique(bufferInfo);

		// We could have a memory allocator or something that knows about the device
		// and the physical device instead of passing it around
		vk::MemoryRequirements memRequirements = device.getBufferMemoryRequirements(m_buffer.get());
		vk::MemoryAllocateInfo allocInfo(
			memRequirements.size,
			physicalDevice.FindMemoryType(
				memRequirements.memoryTypeBits,
				vk::MemoryPropertyFlagBits::eHostVisible |
				vk::MemoryPropertyFlagBits::eHostCoherent
			)
		);
		m_deviceMemory = device.allocateMemoryUnique(allocInfo);
		device.bindBufferMemory(m_buffer.get(), m_deviceMemory.get(), 0);
	}

	void Overwrite(void* dataToCopy)
	{
		void* data;
		device.mapMemory(m_deviceMemory.get(), 0, m_size, {}, &data);
		memcpy(data, dataToCopy, m_size);
		device.unmapMemory(m_deviceMemory.get());
	}

	vk::Buffer Get() const { return m_buffer.get(); }

private:
	vk::Device device; // Ok, we should be able to access the device from anywhere

	size_t m_size;
	vk::UniqueBuffer m_buffer;
	vk::UniqueDeviceMemory m_deviceMemory;
};

class RenderPass
{
public:
	RenderPass(vk::Device device, const PhysicalDevice& physicalDevice, const Swapchain& swapchain);

	void AddRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

	size_t GetFrameBufferCount() const { return m_framebuffers.size(); }

private:
	vk::Extent2D m_imageExtent;
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniqueRenderPass m_renderPass;
	vk::UniquePipeline m_graphicsPipeline;
	std::vector<vk::UniqueFramebuffer> m_framebuffers;

	// Put this here for now:
	Buffer m_vertexBuffer;
};
