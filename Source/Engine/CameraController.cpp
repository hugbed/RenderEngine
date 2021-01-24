#include "CameraController.h"

#include <GLFW/glfw3.h>

#define M_PI 3.14159265358979323846264338327

namespace
{
	template <typename T>
	static T sgn(T val)
	{
		return (T(0) < val) - (val < T(0));
	}
}

CameraController::CameraController(Camera& camera, vk::Extent2D viewportExtent)
	: m_camera(&camera)
	, m_viewportExtent(viewportExtent)
{
}

void CameraController::SetViewportExtent(vk::Extent2D extent)
{
	m_viewportExtent = extent;
}

bool CameraController::Update(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	bool cameraMoved = HandleCameraKeys(dt_s, inputs);

	if (inputs.mouseWasCaptured == false)
	{
		cameraMoved = HandleCameraMouseScroll(dt_s, inputs) || cameraMoved;
		cameraMoved = HandleCameraMouseMove(dt_s, inputs) || cameraMoved;
	}

	return cameraMoved;
}

bool CameraController::HandleCameraKeys(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	bool cameraMoved = false;

	const float speed = 1.0f; // in m/s

	for (const std::pair<KeyID, KeyAction>& key : inputs.keyState)
	{
		KeyID keyID = key.first;
		KeyAction keyAction = key.second;

		// Handle free camera
		if (keyAction == KeyAction::ePressed || keyAction == KeyAction::eRepeated && m_cameraMode == CameraMode::FreeCamera)
		{
			glm::vec3 forward = glm::normalize(m_camera->GetLookAt() - m_camera->GetEye());
			glm::vec3 rightVector = glm::normalize(glm::cross(forward, m_camera->GetUpVector()));
			float dx = speed * dt_s.count(); // in m / s

			switch (keyID) {
			case GLFW_KEY_W:
				m_camera->MoveCamera(forward, dx, false);
				cameraMoved = true;
				break;
			case GLFW_KEY_A:
				m_camera->MoveCamera(rightVector, -dx, true);
				cameraMoved = true;
				break;
			case GLFW_KEY_S:
				m_camera->MoveCamera(forward, -dx, false);
				cameraMoved = true;
				break;
			case GLFW_KEY_D:
				m_camera->MoveCamera(rightVector, dx, true);
				cameraMoved = true;
				break;
			case GLFW_KEY_F:
				//m_scene->ResetCamera(); // m_camera->Reset()
				cameraMoved = true;
				break;
			default:
				break;
			}
		}

		// Handle camera mode change
		if (keyID == GLFW_KEY_F && keyAction == KeyAction::ePressed)
		{
			m_cameraMode = m_cameraMode == CameraMode::FreeCamera ? CameraMode::OrbitCamera : CameraMode::FreeCamera;
			cameraMoved = true;
		}
	}

	return cameraMoved;
}

bool CameraController::HandleCameraMouseScroll(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	const double scrollOffsetY = inputs.scrollOffset.y;
	if (inputs.scrollOffsetReceived)
	{
		float fov = std::clamp(m_camera->GetFieldOfView() - scrollOffsetY, 30.0, 130.0);
		m_camera->SetFieldOfView(fov);
		return true;
	}
	return false;
}

bool CameraController::HandleCameraMouseMove(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	bool cameraMoved = false;

	if ((inputs.isMouseDown) && m_cameraMode == CameraMode::OrbitCamera)
	{
		glm::vec4 position(m_camera->GetEye().x, m_camera->GetEye().y, m_camera->GetEye().z, 1);
		glm::vec4 target(m_camera->GetLookAt().x, m_camera->GetLookAt().y, m_camera->GetLookAt().z, 1);

		glm::vec2 deltaAngle = glm::vec2(2 * M_PI / m_viewportExtent.width, M_PI / m_viewportExtent.height);
		deltaAngle = glm::vec2(
			inputs.lastCursorPos.x - inputs.cursorPos.x,
			inputs.lastCursorPos.y - inputs.cursorPos.y
		) * deltaAngle;

		float cosAngle = dot(m_camera->GetForwardVector(), kUpVector);
		if (cosAngle * sgn(deltaAngle.y) > 0.99f)
			deltaAngle.y = 0;

		// Rotate in X
		glm::mat4x4 rotationMatrixX(1.0f);
		rotationMatrixX = glm::rotate(rotationMatrixX, deltaAngle.x, kUpVector);
		position = (rotationMatrixX * (position - target)) + target;

		// Rotate in Y
		glm::mat4x4 rotationMatrixY(1.0f);
		rotationMatrixY = glm::rotate(rotationMatrixY, deltaAngle.y, m_camera->GetRightVector());
		glm::vec3 finalPositionV3 = (rotationMatrixY * (position - target)) + target;

		m_camera->SetCameraView(finalPositionV3, m_camera->GetLookAt(), kUpVector);

		cameraMoved = true;
	}
	else if (inputs.isMouseDown && m_cameraMode == CameraMode::FreeCamera)
	{
		glm::vec2 delta = glm::vec2(
			inputs.lastCursorPos.x - inputs.cursorPos.x,
			inputs.lastCursorPos.y - inputs.cursorPos.y
		);

		float m_fovV = m_camera->GetFieldOfView() / m_viewportExtent.width * m_viewportExtent.height;

		float xDeltaAngle = glm::radians(delta.x * m_camera->GetFieldOfView() / m_viewportExtent.width);
		float yDeltaAngle = glm::radians(delta.y * m_fovV / m_viewportExtent.height);

		//Handle case were dir = up vector
		float cosAngle = dot(m_camera->GetForwardVector(), kUpVector);
		if (cosAngle > 0.99f && yDeltaAngle < 0 || cosAngle < -0.99f && yDeltaAngle > 0)
			yDeltaAngle = 0;

		glm::vec3 lookat = m_camera->GetLookAt() - m_camera->GetUpVector() * yDeltaAngle;
		float length = glm::distance(m_camera->GetLookAt(), m_camera->GetEye());

		glm::vec3 rightVector = m_camera->GetRightVector();
		glm::vec3 newLookat = lookat + rightVector * xDeltaAngle;

		auto lookatDist = glm::distance(newLookat, m_camera->GetEye());
		m_camera->LookAt(newLookat, kUpVector);

		cameraMoved = true;
	}

	return cameraMoved;
}
