#pragma once

#include "glm_includes.h"

#include <array>

struct BoundingBox
{
	BoundingBox Union(const BoundingBox& other) const
	{
		BoundingBox box;
		box.min = (glm::min)(min, other.min);
		box.max = (glm::max)(max, other.max);
		return box;
	}

	BoundingBox Intersection(const BoundingBox& other) const
	{
		BoundingBox box;
		box.min = (glm::max)(min, other.min);
		box.max = (glm::min)(max, other.max);
		return box;
	}

	static BoundingBox FromPoints(const std::vector<glm::vec3> pts);

	std::array<glm::vec3, 8> GetCorners() const
	{
		return {
			glm::vec3(min.x, min.y, min.z),
			glm::vec3(min.x, min.y, max.z),
			glm::vec3(min.x, max.y, min.z),
			glm::vec3(min.x, max.y, max.z),
			glm::vec3(max.x, min.y, min.z),
			glm::vec3(max.x, min.y, max.z),
			glm::vec3(max.x, max.y, min.z),
			glm::vec3(max.x, max.y, max.z)
		};
	}

	void Transform(const glm::mat4& transform)
	{
		std::array<glm::vec3, 8> corners = GetCorners();
		*this = BoundingBox(); // reset to default values
		for (glm::vec3 corner : corners)
		{
			glm::vec4 p_4 = glm::vec4(corner, 1.0f) * transform;
			glm::vec3 p = glm::vec3(p_4.x, p_4.y, p_4.z) / p_4.w;
			min = glm::min(min, p);
			max = glm::max(max, p);
		}
	}

	glm::vec3 min = glm::vec3(+(std::numeric_limits<float>::max)());
	glm::vec3 max = glm::vec3(-(std::numeric_limits<float>::max)());
};

BoundingBox operator*(const glm::mat4& transform, const BoundingBox& box);
