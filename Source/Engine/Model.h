#pragma once

#include "DescriptorSetLayouts.h"
#include "Material.h"

#include "Buffers.h"

#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <memory>

struct Mesh
{
	vk::DeviceSize indexOffset;
	vk::DeviceSize nbIndices;
	const Material* material;
};

struct ModelUniforms
{
	glm::aligned_mat4 transform;
};

struct Model
{
	Model()
		: uniformBuffer(std::make_unique<UniqueBuffer>(
			vk::BufferCreateInfo(
				{}, sizeof(ModelUniforms), vk::BufferUsageFlagBits::eUniformBuffer
			), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
			))
	{
		memcpy(uniformBuffer->GetMappedData(), &transform, sizeof(glm::mat4));
	}

	void SetTransform(glm::mat4 transform)
	{
		this->transform = std::move(transform);
		memcpy(uniformBuffer->GetMappedData(), &this->transform, sizeof(glm::mat4));
	}

	glm::mat4& GetTransform() { return transform; }

	void Bind(vk::CommandBuffer& commandBuffer) const
	{
		uint32_t set = (uint32_t)DescriptorSetIndices::Model;
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			meshes[0].material->pipeline->GetPipelineLayout(set), set,
			1, &descriptorSet.get(), 0, nullptr
		);
	}

	std::unique_ptr<UniqueBuffer> uniformBuffer;

	// A model as multiple parts (meshes)
	std::vector<Mesh> meshes;

	// Per-object descriptors
	vk::UniqueDescriptorSet descriptorSet;

private:
	glm::mat4 transform = glm::mat4(1.0f);
};
