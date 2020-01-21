#pragma once

#include "glm_includes.h"

struct Light
{
	glm::aligned_int32 type;
	glm::aligned_vec3 pos;
	glm::aligned_vec3 direction;
	glm::aligned_vec4 ambient;
	glm::aligned_vec4 diffuse;
	glm::aligned_vec4 specular;
	glm::aligned_float32 innerCutoff; // (cos of the inner angle)
	glm::aligned_float32 outerCutoff; // (cos of the outer angle)
};
