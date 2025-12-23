#include "Renderer/SceneTree.h"

#include "RHI/CommandBufferPool.h"

SceneNodeID SceneTree::CreateNode(glm::mat4 transform, BoundingBox boundingBox, SceneNodeID parent)
{
	SceneNodeID id = id_cast<SceneNodeID>(m_transforms.size());
	m_transforms.push_back(std::move(transform));
	m_boundingBoxes.push_back(std::move(boundingBox));
	m_parents.push_back(parent);
    return id;
}

void SceneTree::UploadToGPU(CommandBufferPool& commandBufferPool)
{
	const void* data = reinterpret_cast<const void*>(m_transforms.data());
	size_t size = m_transforms.size() * sizeof(m_transforms[0]);
	vk::BufferCreateInfo bufferInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer);
	VmaAllocationCreateInfo allocInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU };
	m_transformsBuffer = std::make_unique<UniqueBuffer>(bufferInfo, allocInfo);

	size_t writeSize = m_transforms.size() * sizeof(m_transforms[0]);
	memcpy((char*)m_transformsBuffer->GetMappedData(), m_transforms.data(), writeSize);
	m_transformsBuffer->Flush(0, writeSize);
}
