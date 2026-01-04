#pragma once

#include <glm_includes.h>

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

	bool Intersects(const BoundingBox& box) const
	{
		return
			(min.x <= box.max.x && max.x >= box.min.x) &&
			(min.y <= box.max.y && max.y >= box.min.y) &&
			(min.z <= box.max.z && max.z >= box.min.z);
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

	BoundingBox Transform(const glm::mat4& transform) const
	{
		std::array<glm::vec3, 8> corners = GetCorners();
		BoundingBox box; // set to default values
		for (glm::vec3 corner : corners)
		{
			glm::vec4 p_4 = transform * glm::vec4(corner, 1.0f);
			glm::vec3 p = glm::vec3(p_4.x, p_4.y, p_4.z) / p_4.w;
			box.min = (glm::min)(box.min, p);
			box.max = (glm::max)(box.max, p);
		}
		return box;
	}

	glm::vec3 min = glm::vec3(+(std::numeric_limits<float>::max)());
	glm::vec3 max = glm::vec3(-(std::numeric_limits<float>::max)());
};

BoundingBox operator*(const glm::mat4& transform, const BoundingBox& box);
