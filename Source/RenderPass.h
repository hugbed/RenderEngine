#pragma once

#include <vulkan/vulkan.hpp>

#include <array>

class Framebuffer;

class RenderPass
{
public:
	using value_type = vk::RenderPass;

	// todo: this should take a render pass description
	//       right now this is fixed: { color, depth, resolve }
	RenderPass(vk::Format colorAttachmentFormat); // should pass params for all attachments

	// todo: move commands that take a CommandBuffer to CommandBufferBuilder
	void Begin(vk::CommandBuffer& commandBuffer, const Framebuffer& framebuffer, std::array<vk::ClearValue, 2> clearValues);

	value_type Get() const { return m_renderPass.get(); }

private:
	vk::UniqueRenderPass m_renderPass;
};
