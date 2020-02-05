#pragma once

#include "glm_includes.h"

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

	std::vector<glm::vec3> GetCorners() const
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

	glm::vec3 min = glm::vec3(+(std::numeric_limits<float>::max)());
	glm::vec3 max = glm::vec3(-(std::numeric_limits<float>::max)());
};

BoundingBox operator*(const glm::mat4& transform, const BoundingBox& box);
