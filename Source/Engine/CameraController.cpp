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
	: m_initialCamera(camera)
	, m_camera(&camera)
	, m_viewportExtent(viewportExtent)
{
}

void CameraController::Reset(Camera& camera, vk::Extent2D extent)
{
	m_initialCamera = camera;
	m_viewportExtent = extent;
}

bool CameraController::Update(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	bool shouldUpdateCamera = HandleInputs(dt_s, inputs);
	shouldUpdateCamera |= MoveCamera(dt_s);
	return shouldUpdateCamera;
}

bool CameraController::HandleInputs(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	bool shouldUpdateCamera = HandleCameraKeys(dt_s, inputs);

	if (inputs.mouseWasCaptured == false)
	{
		shouldUpdateCamera |= HandleCameraMouseScroll(dt_s, inputs);
		shouldUpdateCamera |= HandleCameraMouseMove(dt_s, inputs);
	}

	return shouldUpdateCamera;
}

bool CameraController::HandleCameraKeys(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	bool shouldUpdateCamera = false;

	for (const std::pair<KeyID, KeyAction>& key : inputs.keyState)
	{
		KeyID keyID = key.first;
		KeyAction keyAction = key.second;

		bool keyValue = keyAction == KeyAction::ePressed || keyAction == KeyAction::eRepeated;
		assert(keyValue || keyAction == KeyAction::eReleased); // it's either pressde or released

		switch (keyID) {
		case GLFW_KEY_W:
			m_keys[Key_W] = keyValue;
			break;
		case GLFW_KEY_A:
			m_keys[Key_A] = keyValue;
			break;
		case GLFW_KEY_S:
			m_keys[Key_S] = keyValue;
			break;
		case GLFW_KEY_D:
			m_keys[Key_D] = keyValue;
			break;
		case GLFW_KEY_F:
			if (keyAction == KeyAction::ePressed)
			{
				if (m_cameraMode == CameraMode::FreeCamera)
				{
					*m_camera = m_initialCamera; // reset to orbit around center
					m_cameraMode = CameraMode::OrbitCamera;
				}
				else
				{
					m_cameraMode = CameraMode::FreeCamera;
				}
				shouldUpdateCamera = true;
			}
			break;
		default:
			break;
		}
	}

	return shouldUpdateCamera;
}

bool CameraController::HandleCameraMouseScroll(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	if (!inputs.scrollOffsetReceived)
	{
		return false;
	}

	const double scrollOffsetY = inputs.scrollOffset.y;

	if ((m_cameraMode == CameraMode::OrbitCamera || m_cameraMode == CameraMode::FreeCamera && !inputs.isRightMouseDown))
	{
		float fov = std::clamp(m_camera->GetFieldOfView() - scrollOffsetY, 30.0, 130.0);
		m_camera->SetFieldOfView(fov);
		return true;
	}
	else if (m_cameraMode == CameraMode::FreeCamera && inputs.isRightMouseDown)
	{
		m_speed += scrollOffsetY;
		return true;
	}

	return false;
}

bool CameraController::HandleCameraMouseMove(std::chrono::duration<float> dt_s, const Inputs& inputs)
{
	bool cameraMoved = false;

	if ((inputs.isRightMouseDown) && m_cameraMode == CameraMode::OrbitCamera)
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
		rotationMatrixX = glm::rotate(rotationMatrixX, -deltaAngle.x, kUpVector);
		position = (rotationMatrixX * (position - target)) + target;

		// Rotate in Y
		glm::mat4x4 rotationMatrixY(1.0f);
		rotationMatrixY = glm::rotate(rotationMatrixY, deltaAngle.y, m_camera->GetRightVector());
		glm::vec3 finalPositionV3 = (rotationMatrixY * (position - target)) + target;

		m_camera->SetCameraView(finalPositionV3, m_camera->GetLookAt(), kUpVector);

		cameraMoved = true;
	}
	else if (inputs.isRightMouseDown && m_cameraMode == CameraMode::FreeCamera)
	{
		glm::vec2 delta = m_mouseSensitivity * glm::vec2(
			inputs.cursorPos.x - inputs.lastCursorPos.x,
			inputs.cursorPos.y -inputs.lastCursorPos.y
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

bool CameraController::MoveCamera(std::chrono::duration<float> dt_s)
{
	bool cameraMoved = false;
	if (m_cameraMode == CameraMode::FreeCamera)
	{
		glm::vec3 forward = glm::normalize(m_camera->GetLookAt() - m_camera->GetEye());
		glm::vec3 right = glm::normalize(glm::cross(forward, m_camera->GetUpVector()));
		float forwardAmount = (m_keys[Key_W] ? 1.0f : 0.0f) + (m_keys[Key_S] ? -1.0f : 0.0f);
		float rightAmount = (m_keys[Key_D] ? 1.0f : 0.0f) + (m_keys[Key_A] ? -1.0f : 0.0f);
		m_camera->Move(forwardAmount * forward + rightAmount * right, m_speed * dt_s.count());
		cameraMoved = forwardAmount > 0.0f && rightAmount > 0.0f;
	}
	return cameraMoved;
}
