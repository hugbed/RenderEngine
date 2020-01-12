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

	Camera(glm::vec3 eye, glm::vec3 lookat, glm::vec3 upVector, float fieldOfView, float nearPlane, float farPlane)
		: m_eye(std::move(eye))
		, m_lookAt(std::move(lookat))
		, m_upVector(std::move(upVector))
		, m_fieldOfView(fieldOfView)
		, m_nearPlane(nearPlane)
		, m_farPlane(farPlane)
	{
		UpdateViewMatrix();
	}

	// Create orthonormal camera system
	void UpdateViewMatrix()
	{
		// Get direction vector
		glm::vec3 directionVector = glm::normalize(m_eye - m_lookAt);

		// new right vector (orthogonal to direction, up)
		glm::vec3 rightVector = glm::normalize(glm::cross(m_upVector, directionVector));

		glm::vec3 upVector = cross(directionVector, rightVector);

		// make sure matrix is orthonormal
		m_upVector = upVector;

		// generate view matrix
		m_viewMatrix = glm::lookAt(m_eye, m_lookAt, m_upVector);
	}

	glm::mat4x4 GetViewMatrix() const { return m_viewMatrix; }

	glm::vec3 GetEye() const { return m_eye; }

	glm::vec3 GetUpVector() const { return m_upVector; }

	glm::vec3 GetLookAt() const { return m_lookAt; }

	// Camera forward is -z
	glm::vec3 GetForwardVector() const { return -glm::transpose(m_viewMatrix)[2]; }

	glm::vec3 GetRightVector() const { return glm::transpose(m_viewMatrix)[0]; }

	float GetFieldOfView() const { return m_fieldOfView; }

	float GetNearPlane() const { return m_nearPlane; }

	float GetFarPlane() const { return m_farPlane; }

	void SetFieldOfView(float fov) { m_fieldOfView = fov; }

	void SetCameraView(glm::vec3 eye, glm::vec3 lookat, glm::vec3 up)
	{
		m_eye = std::move(eye);
		m_lookAt = std::move(lookat);
		m_upVector = std::move(up);
		UpdateViewMatrix();
	}

	// todo: rename this to Move()
	void MoveCamera(const glm::vec3& direction, float speed, bool updateLookat)
	{
		m_eye += speed * direction;
		m_lookAt += speed * direction;
		UpdateViewMatrix();
	}

	void LookAt(glm::vec3 lookat, glm::vec3 up)
	{
		m_lookAt = std::move(lookat);
		m_upVector = std::move(up);
		UpdateViewMatrix();
	}

private:
	glm::mat4x4 m_viewMatrix;
	glm::vec3 m_eye;
	glm::vec3 m_lookAt;
	glm::vec3 m_upVector;
	float m_nearPlane;
	float m_farPlane;
	float m_fieldOfView;
};
