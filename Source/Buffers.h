#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

class CommandBuffer;

class Buffer
{
public:
	using value_type = vk::Buffer;

	Buffer(size_t size, vk::BufferUsageFlags bufferUsage, vk::MemoryPropertyFlags memoryProperties);

	void Overwrite(const void* dataToCopy);

	value_type Get() const { return m_buffer.get(); }
	vk::DeviceMemory GetMemory() const { return m_deviceMemory.get(); }
	size_t size() const { return m_size; }

private:
	size_t m_size; // this one might not be necessary
	vk::UniqueBuffer m_buffer;
	vk::UniqueDeviceMemory m_deviceMemory;
};

class BufferWithStaging
{
public:
	BufferWithStaging(size_t size, vk::BufferUsageFlags bufferUsage);

	void Overwrite(vk::CommandBuffer& commandBuffer, const void* dataToCopy);

	vk::Buffer Get() const { return m_buffer.Get(); }

	size_t size() const { return m_buffer.size(); }

private:
	Buffer m_buffer;
	Buffer m_stagingBuffer;
};

class VertexBuffer : public BufferWithStaging
{
public:
	using value_type = vk::Buffer;

	VertexBuffer(size_t size)
		: BufferWithStaging(size, vk::BufferUsageFlagBits::eVertexBuffer)
	{}

	const std::vector<vk::DeviceSize>& GetOffsets() const { return m_offsets; }

private:
	std::vector<vk::DeviceSize> m_offsets;
};

class IndexBuffer : public BufferWithStaging
{
public:
	using value_type = vk::Buffer;

	IndexBuffer(size_t size)
		: BufferWithStaging(size, vk::BufferUsageFlagBits::eIndexBuffer)
	{}
};

class UniformBuffer : public Buffer
{
public:
	using value_type = vk::Buffer;

	UniformBuffer(size_t size)
		: Buffer(size, vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostCoherent |
			vk::MemoryPropertyFlagBits::eHostCoherent)
	{}
};
