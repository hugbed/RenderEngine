#pragma once

#include "Swapchain.h"

#include <vulkan/vulkan.hpp>

#include <vector>

#include "Device.h"
#include "PhysicalDevice.h"

class Buffer
{
public:
	Buffer(size_t size, vk::BufferUsageFlags bufferUsage, vk::SharingMode sharingMode)
		: m_size(size)
	{
		vk::BufferCreateInfo bufferInfo({}, size, bufferUsage, sharingMode);
		m_buffer = g_device->Get().createBufferUnique(bufferInfo);

		// We could have a memory allocator or something that knows about the device
		// and the physical device instead of passing it around
		vk::MemoryRequirements memRequirements = g_device->Get().getBufferMemoryRequirements(m_buffer.get());
		vk::MemoryAllocateInfo allocInfo(
			memRequirements.size,
			g_physicalDevice->FindMemoryType(
				memRequirements.memoryTypeBits,
				vk::MemoryPropertyFlagBits::eHostVisible |
				vk::MemoryPropertyFlagBits::eHostCoherent
			)
		);
		m_deviceMemory = g_device->Get().allocateMemoryUnique(allocInfo);
		g_device->Get().bindBufferMemory(m_buffer.get(), m_deviceMemory.get(), 0);
	}

	void Overwrite(void* dataToCopy)
	{
		void* data;
		g_device->Get().mapMemory(m_deviceMemory.get(), 0, m_size, {}, &data);
		{
			memcpy(data, dataToCopy, m_size);
		}
		g_device->Get().unmapMemory(m_deviceMemory.get());
	}

	vk::Buffer Get() const { return m_buffer.get(); }

private:
	size_t m_size;
	vk::UniqueBuffer m_buffer;
	vk::UniqueDeviceMemory m_deviceMemory;
};

class RenderPass
{
public:
	RenderPass(const Swapchain& swapchain);

	void PopulateRenderCommands(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const;

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
