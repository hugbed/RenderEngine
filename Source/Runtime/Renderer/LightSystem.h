#pragma once

#include <Renderer/BindlessDefines.h>
#include <RHI/Buffers.h>

#include <glm_includes.h>
#include <vulkan/vulkan.hpp>
#include <gsl/pointers>
#include <utility>

class CommandRingBuffer;
class BindlessDescriptors;

enum class LightType : uint32_t
{
	Directional = 1,
	Point = 2,
	Spot = 3,
	Count
};

struct Light
{
	glm::aligned_vec4 color;
	glm::aligned_vec3 position;
	glm::aligned_vec3 direction; // directional/spot
	glm::aligned_float32 intensity; // illuminance in lx (directional) or luminous power in lm
	glm::aligned_float32 falloffRadius; // point/spot
	glm::aligned_float32 cosInnerAngle; // spot (cos of the innerAngle)
	glm::aligned_float32 cosOuterAngle; // spot (cos of the outerAngle)
	glm::aligned_uint32 shadowIndex;
	glm::aligned_uint32 type; // LightType
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
	
	LightID AddLight(Light light)
	{
		LightID id = static_cast<LightID>(m_lights.size());
		m_lights.push_back(std::move(light));
		return id;
	}

	// todo (hbedard): move ShadowID outside of ShadowSystem.h to include it here
	void SetLightShadowID(LightID id, uint32_t shadowID);

	const std::vector<Light> GetLights() { return m_lights; }

	const Light& GetLight(LightID id) const { return m_lights[id]; }

	uint32_t GetLightCount() const { return static_cast<uint32_t>(m_lights.size()); }

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	BufferHandle GetLightsBufferHandle() const { return m_lightsBufferHandle; }

private:
	std::vector<Light> m_lights;
	std::unique_ptr<UniqueBufferWithStaging> m_lightsBuffer;
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
	BufferHandle m_lightsBufferHandle = BufferHandle::Invalid;
};
