#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <map>

class Camera
{
public:
	Camera() = default;

	Camera(glm::vec3 eye, glm::vec3 lookat, glm::vec3 upVector, float fieldOfView)
		: m_eye(std::move(eye))
		, m_lookAt(std::move(lookat))
		, m_upVector(std::move(upVector))
		, m_fieldOfView(fieldOfView)
	{
		UpdateViewMatrix();
	}

	// Create orthonormal camera system
	void UpdateViewMatrix()
	{
		// create new direction vector
		glm::vec3 directionVector = glm::normalize(m_lookAt - m_eye);

		// new right vector (orthogonal to direction, up)
		glm::vec3 rightVector = glm::normalize(glm::cross(directionVector, m_upVector));

		// generate view matrix
		m_viewMatrix = glm::lookAt(m_eye, m_lookAt, m_upVector);
	}

	glm::mat4x4 GetViewMatrix() const { return m_viewMatrix; }

	glm::vec3 GetEye() const { return m_eye; }

	glm::vec3 GetUpVector() const { return m_upVector; }

	glm::vec3 GetLookAt() const { return m_lookAt; }

	float GetFieldOfView() const { return m_fieldOfView; }

	void SetFieldOfView(float fov) { m_fieldOfView = fov; }

	void SetCameraView(glm::vec3 eye, glm::vec3 up, glm::vec3 lookat)
	{
		m_eye = std::move(eye);
		m_upVector = std::move(up);
		m_lookAt = std::move(lookat);
		UpdateViewMatrix();
	}

	void MoveCamera(const glm::vec3& direction, float speed, bool updateLookat)
	{
		m_eye += speed * direction;
		m_lookAt += speed * direction;
		UpdateViewMatrix();
	}

	void LookAt(glm::vec3 lookat)
	{
		m_lookAt = std::move(lookat);
		UpdateViewMatrix();
	}

private:
	glm::mat4x4 m_viewMatrix;
	glm::vec3 m_eye;
	glm::vec3 m_lookAt;
	glm::vec3 m_upVector;
	float m_fieldOfView;
};
