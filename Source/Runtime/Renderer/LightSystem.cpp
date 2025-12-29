#include <Renderer/LightSystem.h>

#include <RHI/Buffers.h>
#include <RHI/CommandBufferPool.h>

LightSystem::LightSystem(BindlessDescriptors& bindlessDescriptors)
	: m_bindlessDescriptors(&bindlessDescriptors)
{
}

void LightSystem::UploadToGPU(CommandBufferPool& commandBufferPool)
{
	if (m_lights.empty() == false)
	{
		vk::CommandBuffer& commandBuffer = commandBufferPool.GetCommandBuffer();

		const vk::BufferUsageFlagBits bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer;
		vk::DeviceSize bufferSize = m_lights.size() * sizeof(PhongLight);
		m_lightsBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, bufferUsage);
		memcpy(m_lightsBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_lights.data()), bufferSize);
		m_lightsBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		commandBufferPool.DestroyAfterSubmit(m_lightsBuffer->ReleaseStagingBuffer());

		m_lightsBufferHandle = m_bindlessDescriptors->StoreBuffer(m_lightsBuffer->Get(), bufferUsage);
	}
}
