#pragma once

#include "Camera.h"
#include "InputSystem.h"

#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <gsl/gsl>

#include <chrono>

enum class CameraMode { OrbitCamera, FreeCamera };

class CameraController
{
public:
	// assimp uses +Y as the up vector
	static constexpr glm::vec3 kUpVector = glm::vec3(0.0f, 1.0f, 0.0f);

	CameraController(Camera& camera, vk::Extent2D viewportExtent);

	void SetViewportExtent(vk::Extent2D extent);

	bool Update(std::chrono::duration<float> dt_s, const Inputs& inputs);

	bool HandleCameraKeys(std::chrono::duration<float> dt_s, const Inputs& inputs);

	bool HandleCameraMouseScroll(std::chrono::duration<float> dt_s, const Inputs& inputs);

	bool HandleCameraMouseMove(std::chrono::duration<float> dt_s, const Inputs& inputs);

private:
	CameraMode m_cameraMode = CameraMode::OrbitCamera;
	gsl::not_null<Camera*> m_camera;
	vk::Extent2D m_viewportExtent;
};
