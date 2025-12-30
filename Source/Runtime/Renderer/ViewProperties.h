#pragma once

#include <glm_includes.h>

struct ViewProperties
{
	glm::aligned_mat4 view;
	glm::aligned_mat4 proj;
	glm::aligned_vec3 pos;
};
