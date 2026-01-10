#pragma once

#include <RHI/SmallVector.h>
#include <vulkan/vulkan.hpp>

struct RenderingInfo
{
	const vk::RenderingInfo& Get(uint32_t imageIndex) const { return info; }

	vk::RenderingAttachmentInfo colorAttachment;
	vk::RenderingAttachmentInfo depthAttachment;
	vk::RenderingInfo info;
};

struct PipelineRenderingCreateInfo
{
	const vk::PipelineRenderingCreateInfo& Get() const { return info; }

	SmallVector<vk::Format> colorAttachmentFormats;
	vk::PipelineRenderingCreateInfo info;
};
