#pragma once

#include "Buffers.h"

#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <utility>

class CommandBufferPool;

struct PhongLight
{
	glm::aligned_int32 type; // LightType
	glm::aligned_vec3 pos;
	glm::aligned_vec3 direction;
	glm::aligned_vec4 ambient;
	glm::aligned_vec4 diffuse;
	glm::aligned_vec4 specular;
	glm::aligned_float32 innerCutoff; // (cos of the inner angle)
	glm::aligned_float32 outerCutoff; // (cos of the outer angle)
	glm::aligned_uint32 shadowIndex;
};

using LightID = uint32_t;

class LightSystem
{
public:
	void ReserveLights(size_t count)
	{
		m_lights.reserve(m_lights.size() + count);
	}
	
	LightID AddLight(PhongLight light)
	{
		LightID id = (LightID)m_lights.size();
		m_lights.push_back(std::move(light));
		return id;
	}

	const std::vector<PhongLight> GetLights() { return m_lights; }

	const PhongLight& GetLight(LightID id) const { return m_lights[id]; }

	size_t GetLightCount() const { return m_lights.size(); }

	void UploadToGPU(CommandBufferPool& commandBufferPool);

	std::pair<vk::Buffer, size_t> GetUniformBuffer() const
	{
		return std::make_pair(m_lightsUniformBuffer->Get(), m_lightsUniformBuffer->Size());
	}

private:
	std::vector<PhongLight> m_lights;
	std::unique_ptr<UniqueBufferWithStaging> m_lightsUniformBuffer;
};
