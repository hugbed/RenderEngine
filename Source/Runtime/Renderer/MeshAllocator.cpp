#include <Renderer/MeshAllocator.h>

#include <RHI/CommandRingBuffer.h>

void MeshAllocator::GroupMeshes(SceneNodeHandle sceneNodeHandle, const std::vector<Mesh>& meshes)
{
	m_meshEntries.push_back(std::make_pair(sceneNodeHandle, Entry::AppendToOutput(meshes, m_meshes)));
}

void MeshAllocator::UploadToGPU(CommandRingBuffer& commandRingBuffer)
{
	vk::CommandBuffer commandBuffer = commandRingBuffer.GetCommandBuffer();

	// Upload Geometry
	{
		vk::DeviceSize bufferSize = sizeof(m_vertices[0]) * m_vertices.size();
		m_vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
		memcpy(m_vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_vertices.data()), bufferSize);
		m_vertexBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		commandRingBuffer.DestroyAfterSubmit(m_vertexBuffer->ReleaseStagingBuffer());
		m_vertices.clear();
	}
	{
		vk::DeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();
		m_indexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer);
		memcpy(m_indexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_indices.data()), bufferSize);
		m_indexBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		commandRingBuffer.DestroyAfterSubmit(m_indexBuffer->ReleaseStagingBuffer());
		m_indices.clear();
	}
}

void MeshAllocator::BindGeometry(const vk::CommandBuffer& commandBuffer) const
{
	vk::DeviceSize offsets[] = { 0 };
	vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
	commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
	commandBuffer.bindIndexBuffer(m_indexBuffer->Get(), 0, vk::IndexType::eUint32);
}
