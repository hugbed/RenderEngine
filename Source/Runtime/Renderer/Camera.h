#pragma once

#include "glm_includes.h"

#include <map>
#include <vector>

class Camera
{
public:
	Camera() = default;

	Camera(glm::vec3 eye, glm::vec3 lookat, glm::vec3 upVector, float fieldOfView, float nearPlane, float farPlane, int imageWidth, int imageHeight)
		: m_eye(std::move(eye))
		, m_lookAt(std::move(lookat))
		, m_upVector(std::move(upVector))
		, m_fieldOfView(fieldOfView)
		, m_nearPlane(nearPlane)
		, m_farPlane(farPlane)
		, m_imageWidth(imageWidth)
		, m_imageHeight(imageHeight)
	{
		UpdateViewMatrix();
	}

	glm::mat4 GetViewMatrix() const { return m_viewMatrix; }

	glm::mat4 GetProjectionMatrix() const { return m_projMatrix; }

	glm::vec3 GetEye() const { return m_eye; }

	glm::vec3 GetUpVector() const { return m_upVector; }

	glm::vec3 GetLookAt() const { return m_lookAt; }

	// Camera forward is -z
	glm::vec3 GetForwardVector() const { return -glm::transpose(m_viewMatrix)[2]; }

	glm::vec3 GetRightVector() const { return glm::transpose(m_viewMatrix)[0]; }

	float GetFieldOfView() const { return m_fieldOfView; }

	float GetNearPlane() const { return m_nearPlane; }

	float GetFarPlane() const { return m_farPlane; }

	float GetExposure() const { return m_exposure; }

	std::vector<glm::vec3> ComputeFrustrumCorners() const;

	void SetFieldOfView(float fov)
	{
		m_fieldOfView = fov;
		UpdateProjectionMatrix();
	}

	void SetImageExtent(int width, int height)
	{
		m_imageWidth = width;
		m_imageHeight = height;
		UpdateProjectionMatrix();
	}

	void SetCameraView(glm::vec3 eye, glm::vec3 lookat, glm::vec3 up)
	{
		m_eye = std::move(eye);
		m_lookAt = std::move(lookat);
		m_upVector = std::move(up);
		UpdateViewMatrix();
	}

	void SetExposure(float exposure)
	{
		m_exposure = exposure;
	}

	void Move(const glm::vec3& direction, float speed)
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

	void UpdateProjectionMatrix()
	{
		// OpenGL -> Vulkan invert y, half z
		auto clip = glm::mat4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, -1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.5f, 0.0f,
			0.0f, 0.0f, 0.5f, 1.0f
		);

		m_projMatrix = clip * glm::perspective(
			glm::radians(GetFieldOfView()),
			m_imageWidth / (float)m_imageHeight,
			GetNearPlane(), GetFarPlane()
		);
	}

	glm::mat4 m_viewMatrix;
	glm::mat4 m_projMatrix;
	glm::vec3 m_eye;
	glm::vec3 m_lookAt;
	glm::vec3 m_upVector;
	float m_nearPlane;
	float m_farPlane;
	float m_fieldOfView;
	float m_exposure;
	int m_imageWidth;
	int m_imageHeight;
};
