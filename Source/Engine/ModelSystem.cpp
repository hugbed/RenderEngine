#include "ModelSystem.h"

#include "CommandBufferPool.h"

ModelID ModelSystem::CreateModel(glm::mat4 transform, BoundingBox boundingBox, const std::vector<Mesh>& meshes)
{
	ModelID id = m_transforms.size();
	m_transforms.push_back(std::move(transform));
	m_boundingBoxes.push_back(std::move(boundingBox));
	m_meshEntries.push_back(Entry::AppendToOutput(meshes, m_meshes));

	return id;
}

void ModelSystem::UploadToGPU(CommandBufferPool& commandBufferPool)
{
	vk::CommandBuffer commandBuffer = commandBufferPool.GetCommandBuffer();

	// Upload Geometry
	{
		vk::DeviceSize bufferSize = sizeof(m_vertices[0]) * m_vertices.size();
		m_vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
		memcpy(m_vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_vertices.data()), bufferSize);
		m_vertexBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		commandBufferPool.DestroyAfterSubmit(m_vertexBuffer->ReleaseStagingBuffer());
		m_vertices.clear();
	}
	{
		vk::DeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();
		m_indexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer);
		memcpy(m_indexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_indices.data()), bufferSize);
		m_indexBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		commandBufferPool.DestroyAfterSubmit(m_indexBuffer->ReleaseStagingBuffer());
		m_indices.clear();
	}

	// Upload transforms
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
}

void ModelSystem::BindGeometry(const vk::CommandBuffer& commandBuffer) const
{
	vk::DeviceSize offsets[] = { 0 };
	vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
	commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
	commandBuffer.bindIndexBuffer(m_indexBuffer->Get(), 0, vk::IndexType::eUint32);
}
