#pragma once

#include <Renderer/BindlessDefines.h>
#include <RHI/Buffers.h>

#include <glm_includes.h>
#include <vulkan/vulkan.hpp>
#include <gsl/pointers>
#include <utility>

class CommandRingBuffer;
class BindlessDescriptors;

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
	explicit LightSystem(BindlessDescriptors& bindlessDescriptors);

	void ReserveLights(size_t count)
	{
		m_lights.reserve(m_lights.size() + count);
	}
	
	LightID AddLight(PhongLight light)
	{
		LightID id = static_cast<LightID>(m_lights.size());
		m_lights.push_back(std::move(light));
		return id;
	}

	const std::vector<PhongLight> GetLights() { return m_lights; }

	const PhongLight& GetLight(LightID id) const { return m_lights[id]; }

	uint32_t GetLightCount() const { return static_cast<uint32_t>(m_lights.size()); }

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	BufferHandle GetLightsBufferHandle() const { return m_lightsBufferHandle; }

private:
	std::vector<PhongLight> m_lights;
	std::unique_ptr<UniqueBufferWithStaging> m_lightsBuffer;
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	BufferHandle m_lightsBufferHandle = BufferHandle::Invalid;
};
