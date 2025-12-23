#include "BoundingBox.h"

BoundingBox operator*(const glm::mat4& transform, const BoundingBox& box)
{
	BoundingBox newBox;

	for (auto& corner : box.GetCorners())
	{
		glm::vec4 p = transform * glm::vec4(corner, 1.0f);
		corner = p / p.w;
		newBox.min = (glm::min)(newBox.min, corner);
		newBox.max = (glm::max)(newBox.max, corner);
	}

	return newBox;
}

BoundingBox BoundingBox::FromPoints(const std::vector<glm::vec3> pts)
{
	BoundingBox box;

	for (const auto& p : pts)
	{
		box.min = (glm::min)(box.min, p);
		box.max = (glm::max)(box.max, p);
	}

	return box;
}
