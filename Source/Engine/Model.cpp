#include "Model.h"

Model::Model()
	: uniformBuffer(std::make_unique<UniqueBuffer>(
		vk::BufferCreateInfo(
			{}, sizeof(ModelUniforms), vk::BufferUsageFlagBits::eUniformBuffer
		), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
		))
{
	memcpy(uniformBuffer->GetMappedData(), &transform, sizeof(glm::mat4));
}
