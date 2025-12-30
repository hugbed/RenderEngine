#include <Renderer/CameraViewSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/Bindless.h>
#include <RHI/CommandRingBuffer.h>
#include <RHI/constants.h>

CameraViewSystem::CameraViewSystem(vk::Extent2D imageExtent)
	: m_viewUniforms({})
	, m_camera(
		1.0f * glm::vec3(1.0f, 1.0f, 1.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f), 45.0f, 0.01f, 100.0f,
		imageExtent.width, imageExtent.height)
{
}

CameraViewSystem::~CameraViewSystem() = default;

void CameraViewSystem::Init(Renderer& renderer)
{
	CommandRingBuffer& commandRingBuffer = renderer.GetCommandRingBuffer();
	gsl::not_null<BindlessDescriptors*> bindlessDescriptors = renderer.GetBindlessDescriptors();

	// Per view
	const vk::BufferUsageFlagBits bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer;
	m_viewUniformBuffers.clear();
	m_viewUniformBuffers.reserve(commandRingBuffer.GetNbConcurrentSubmits());
	for (uint32_t i = 0; i < commandRingBuffer.GetNbConcurrentSubmits(); ++i)
	{
		m_viewUniformBuffers.emplace_back(
			vk::BufferCreateInfo(
				{},
				sizeof(ViewProperties),
				bufferUsage | vk::BufferUsageFlagBits::eTransferSrc // needs TransferSrc?
			), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
		);
	}

	m_viewBufferHandles.reserve(m_viewUniformBuffers.size());
	for (uint32_t i = 0; i < m_viewUniformBuffers.size(); ++i)
	{
		m_viewBufferHandles.push_back(bindlessDescriptors->StoreBuffer(m_viewUniformBuffers[i].Get(), bufferUsage));
	}

	// todo (hbedard): somehwere notify other systems that this is set
}

void CameraViewSystem::Reset(vk::Extent2D newImageExtent)
{
	m_camera.SetImageExtent(newImageExtent.width, newImageExtent.height);
}

void CameraViewSystem::UploadToGPU(CommandRingBuffer& commandRingBuffer)
{

}

void CameraViewSystem::Update(uint32_t concurrentFrameIndex)
{
	m_viewUniforms.pos = m_camera.GetEye();
	m_viewUniforms.view = m_camera.GetViewMatrix();
	m_viewUniforms.proj = m_camera.GetProjectionMatrix();

	// Upload to GPU
	assert(concurrentFrameIndex < m_viewUniformBuffers.size());
	auto& uniformBuffer = m_viewUniformBuffers[concurrentFrameIndex];
	memcpy(uniformBuffer.GetMappedData(), reinterpret_cast<const void*>(&m_viewUniforms), sizeof(ViewProperties));
}
