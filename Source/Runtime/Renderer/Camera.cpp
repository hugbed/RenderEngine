#include <Renderer/Camera.h>

std::vector<glm::vec3> Camera::ComputeFrustrumCorners() const
{
	glm::mat4 transform = glm::inverse(m_viewMatrix) * glm::inverse(m_projMatrix);

	std::vector<glm::vec3> points = {
		glm::vec3(-1.0f, -1.0f, -1.0f),
		glm::vec3(-1.0f, -1.0f,  1.0f),
		glm::vec3(-1.0f,  1.0f, -1.0f),
		glm::vec3(-1.0f,  1.0f,  1.0f),
		glm::vec3( 1.0f, -1.0f, -1.0f),
		glm::vec3( 1.0f, -1.0f,  1.0f),
		glm::vec3( 1.0f,  1.0f, -1.0f),
		glm::vec3( 1.0f,  1.0f,  1.0f),
	};

	for (auto& point : points)
	{
		glm::vec4 pointWorld = transform * glm::vec4(point, 1.0f);
		point = pointWorld / pointWorld.w;
	}

	return points;
}
