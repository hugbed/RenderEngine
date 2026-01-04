#pragma once

#include <Renderer/BindlessDefines.h>
#include <Renderer/ViewProperties.h>
#include <Renderer/Camera.h>
#include <RHI/Buffers.h>

#include <memory>
#include <vector>

class CommandRingBuffer;
class Renderer;

// todo (hbedard): I don't like the name system everywhere
class CameraViewSystem
{
public:
	CameraViewSystem(vk::Extent2D imageExtent);
	~CameraViewSystem();

	void Init(Renderer& renderer);
	void Reset(vk::Extent2D newImageExtent);
	void UploadToGPU(CommandRingBuffer& commandRingBuffer);
	void Update(uint32_t concurrentFrameIndex);

	const std::vector<BufferHandle>& GetViewBufferHandles() const { return m_viewBufferHandles; }
	const Camera& GetCamera() const { return m_camera; }
	Camera& GetCamera() { return m_camera; }

	void SetViewDebug(ViewDebugInput debugInput, ViewDebugEquation debugEquation);

private:
	std::vector<UniqueBuffer> m_viewUniformBuffers;
	std::vector<BufferHandle> m_viewBufferHandles;
	ViewProperties m_viewUniforms;
	Camera m_camera;
};
