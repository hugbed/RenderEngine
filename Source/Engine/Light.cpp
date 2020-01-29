#include "Light.h"

#include "Camera.h"

#include "defines.h"

namespace
{
	// OpenGL -> Vulkan invert y, half z
	static glm::mat4 vulkan_clip_matrix(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f
	);
}

glm::mat4 Light::ComputeDirectionalLightTransform(const Camera& camera)
{
	glm::mat4 view = glm::lookAt(pos, camera.GetLookAt(), glm::vec3(0.0f, 1.0f, 0.0));

	// mustdo: compute this from camera frustrum
	float camSize = 5.0f;
	float near_plane = 0.0f;
	float far_plane = 5.0f;
	glm::mat4 proj = glm::ortho(-camSize, camSize, -camSize, camSize, near_plane, far_plane); // ortho if directional
	proj = ::vulkan_clip_matrix * proj;

	return proj * view;
}

glm::mat4 Light::ComputePointLightTransform(const Camera& camera)
{
	return glm::mat4(1.0);
}

glm::mat4 Light::ComputeSpotLightTransform(const Camera& camera)
{
	glm::mat4 view = glm::lookAt(pos, camera.GetLookAt(), glm::vec3(0.0f, 1.0f, 0.0));

	// mustdo: use perspective
	float camSize = 5.0f;
	float near_plane = 0.0f;
	float far_plane = 5.0f;
	glm::mat4 proj = glm::ortho(-camSize, camSize, -camSize, camSize, near_plane, far_plane); // ortho if directional
	proj = ::vulkan_clip_matrix * proj;

	return proj * view;
}

// mustdo: this
// This is all great but we also need info about the scene
// to compute the light frustrum.
// We also need to decouple this into view and proj

glm::mat4 Light::ComputeTransform(const Camera& camera)
{
	if ((LightType)type == LightType::Directional)
	{
		return ComputeDirectionalLightTransform(camera);
	}
	else if ((LightType)type == LightType::Point)
	{
		return ComputePointLightTransform(camera);
	}
	else if ((LightType)type == LightType::Spot)
	{
		return ComputeSpotLightTransform(camera);
	}
	else
	{
		ASSERT(false && "invalid light type");
	}

	return glm::mat4(1.0f);
}
