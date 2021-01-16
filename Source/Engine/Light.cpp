#include "Light.h"

#include "Buffers.h"
#include "CommandBufferPool.h"

void LightSystem::UploadToGPU(CommandBufferPool& commandBufferPool)
{
	if (m_lights.empty() == false)
	{
		vk::CommandBuffer& commandBuffer = commandBufferPool.GetCommandBuffer();

		vk::DeviceSize bufferSize = m_lights.size() * sizeof(PhongLight);
		m_lightsUniformBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer);
		memcpy(m_lightsUniformBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_lights.data()), bufferSize);
		m_lightsUniformBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		commandBufferPool.DestroyAfterSubmit(m_lightsUniformBuffer->ReleaseStagingBuffer());
	}
}
