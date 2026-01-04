#pragma once

#include <glm_includes.h>

enum class ViewDebugInput : uint32_t
{
	None = 0,
	BaseColor,
	DiffuseColor,
	Normal,
	Occlusion,
	Emissive,
	Metallic,
	Roughness
};

enum class ViewDebugEquation : uint32_t
{
	None = 0,
	Diffuse,
	F,
	G,
	D,
	Specular,
};

struct ViewProperties
{
	glm::aligned_mat4 view;
	glm::aligned_mat4 proj;
	glm::aligned_vec3 position;
	float exposure = 1.0f;
	ViewDebugInput debugInput;
	ViewDebugEquation debugEquation;
};
