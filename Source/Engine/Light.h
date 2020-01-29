#pragma once

#include "glm_includes.h"

class Camera;

enum class LightType
{
	Directional = 1,
	Point = 2,
	Spot = 3,
	Count
};

struct Light
{
	glm::mat4 ComputeTransform(const Camera& camera);

	glm::aligned_int32 type; // LightType
	glm::aligned_vec3 pos;
	glm::aligned_vec3 direction;
	glm::aligned_vec4 ambient;
	glm::aligned_vec4 diffuse;
	glm::aligned_vec4 specular;
	glm::aligned_float32 innerCutoff; // (cos of the inner angle)
	glm::aligned_float32 outerCutoff; // (cos of the outer angle)
	glm::aligned_int32 shadowIndex;

private:
	glm::mat4 ComputeDirectionalLightTransform(const Camera& camera);
	glm::mat4 ComputePointLightTransform(const Camera& camera);
	glm::mat4 ComputeSpotLightTransform(const Camera& camera);
};
