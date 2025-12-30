#pragma once

#include <Renderer/Camera.h>
#include <InputSystem.h>
#include <glm_includes.h>
#include <vulkan/vulkan.hpp>
#include <gsl/gsl>

#include <chrono>

// todo (hbedard): split that into 2 different classes
enum class CameraMode { OrbitCamera, FreeCamera };

class CameraController
{
public:
	// assimp uses +Y as the up vector
	static constexpr glm::vec3 kUpVector = glm::vec3(0.0f, 1.0f, 0.0f);

	CameraController(Camera& camera, vk::Extent2D viewportExtent);

	void Reset(Camera& camera, vk::Extent2D extent);

	bool Update(std::chrono::duration<float> dt_s, const Inputs& inputs);

private:
	enum Key : uint8_t {
		Key_W = 0,
		Key_A,
		Key_S,
		Key_D,
		Key_Count
	};
	
	Camera m_initialCamera;
	CameraMode m_cameraMode = CameraMode::OrbitCamera;
	std::array<bool, Key_Count> m_keys = {};
	gsl::not_null<Camera*> m_camera;
	vk::Extent2D m_viewportExtent;
	float m_speed = 10.0f; // in m/s
	float m_mouseSensitivity = 45.0f; // in pixels (todo should be in % of screen)

	bool HandleInputs(std::chrono::duration<float> dt_s, const Inputs& inputs);
	bool HandleCameraKeys(std::chrono::duration<float> dt_s, const Inputs& inputs);
	bool HandleCameraMouseScroll(std::chrono::duration<float> dt_s, const Inputs& inputs);
	bool HandleCameraMouseMove(std::chrono::duration<float> dt_s, const Inputs& inputs);

	bool MoveCamera(std::chrono::duration<float> dt_s);
};
