#pragma once

#include <RHI/SmallVector.h>
#include <vulkan/vulkan.hpp>

struct RenderingInfo
{
	const vk::RenderingInfo& Get(uint32_t imageIndex) const { return info; }

	RenderingInfo() = default;

	RenderingInfo(const RenderingInfo& other)
	{
		*this = other;
		UpdateInternalPointers();
	}

	RenderingInfo(RenderingInfo&& other)
	{
		*this = std::move(other);
		UpdateInternalPointers();
	}

	RenderingInfo& operator=(const RenderingInfo& other)
	{
		colorAttachment = other.colorAttachment;
		depthAttachment = other.depthAttachment;
		info = other.info;
		UpdateInternalPointers();
		return *this;
	}

	RenderingInfo& operator=(RenderingInfo&& other)
	{
		colorAttachment = std::move(other.colorAttachment);
		depthAttachment = std::move(other.depthAttachment);
		info = std::move(other.info);
		UpdateInternalPointers();
		return *this;
	}

	void UpdateInternalPointers()
	{
		info.pColorAttachments = &colorAttachment;
		info.pDepthAttachment = &depthAttachment;
	}

	vk::RenderingAttachmentInfo colorAttachment;
	vk::RenderingAttachmentInfo depthAttachment;
	vk::RenderingInfo info;
};

struct PipelineRenderingCreateInfo
{
	const vk::PipelineRenderingCreateInfo& Get() const { return info; }

	PipelineRenderingCreateInfo() = default;

	PipelineRenderingCreateInfo(const PipelineRenderingCreateInfo& other)
	{
		*this = other;
		UpdateInternalPointers();
	}

	PipelineRenderingCreateInfo(PipelineRenderingCreateInfo&& other)
	{
		*this = std::move(other);
	}

	PipelineRenderingCreateInfo& operator=(const PipelineRenderingCreateInfo& other)
	{
		colorAttachmentFormats = other.colorAttachmentFormats;
		info = other.info;
		UpdateInternalPointers();
		return *this;
	}

	PipelineRenderingCreateInfo& operator=(PipelineRenderingCreateInfo&& other)
	{
		colorAttachmentFormats = std::move(other.colorAttachmentFormats);
		info = std::move(other.info);
		UpdateInternalPointers();
		return *this;
	}

	void UpdateInternalPointers()
	{
		if (!colorAttachmentFormats.empty())
		{
			info.pColorAttachmentFormats = colorAttachmentFormats.data();
		}
	}

	SmallVector<vk::Format> colorAttachmentFormats;
	vk::PipelineRenderingCreateInfo info;
};
