#if defined(_WIN32)
#include <Windows.h>
#define _USE_MATH_DEFINES
#endif

#include "RenderLoop.h"
#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "CommandBufferPool.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "GraphicsPipeline.h"
#include "Shader.h"
#include "Image.h"
#include "Texture.h"
#include "Skybox.h"
#include "vk_utils.h"
#include "file_utils.h"

#include "Camera.h"
#include "TextureCache.h"
#include "MaterialCache.h"
#include "DescriptorSetLayouts.h"
#include "Model.h"
#include "RenderState.h"
#include "Scene.h"
#include "ShadowMap.h"

#include "Grid.h"

#include <GLFW/glfw3.h>

// For Uniform Buffer
#include "glm_includes.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <iostream>
#include <cmath>

class App : public RenderLoop
{
public:
	App(vk::SurfaceKHR surface, vk::Extent2D extent, Window& window, std::string basePath, std::string sceneFile)
		: RenderLoop(surface, extent, window)
		, m_renderPass(std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format))
		, m_framebuffers(Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get()))
		, m_scene(std::make_unique<Scene>(
			std::move(basePath), std::move(sceneFile),
			m_commandBufferPool,
			*m_renderPass,
			m_swapchain->GetImageDescription().extent)
		)
		, m_grid(std::make_unique<Grid>(*m_renderPass, m_swapchain->GetImageDescription().extent))
	{
		window.SetMouseButtonCallback(reinterpret_cast<void*>(this), OnMouseButton);
		window.SetMouseScrollCallback(reinterpret_cast<void*>(this), OnMouseScroll);
		window.SetCursorPositionCallback(reinterpret_cast<void*>(this), OnCursorPosition);
		window.SetKeyCallback(reinterpret_cast<void*>(this), OnKey);
	}

	using RenderLoop::Init;

protected:
	glm::vec2 m_lastMousePos = glm::vec2(0.0f);
	bool m_isMouseDown = false;
	std::map<int, bool> m_keyState;

	// assimp uses +Y as the up vector
	glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);

	CameraMode m_cameraMode = CameraMode::OrbitCamera;

	bool m_showGrid = true;

	void Init(vk::CommandBuffer& commandBuffer) override
	{
		vk::Extent2D imageExtent = m_swapchain->GetImageDescription().extent;

		m_scene->Load(commandBuffer);
		UpdateShadowMaps();

		CreateSecondaryCommandBuffers();
		RecordRenderPassCommands();
	}

	// Render pass commands are recorded once and executed every frame
	void OnSwapchainRecreated() override
	{
		// Reset resources that depend on the swapchain images
		//m_graphicsPipelines.clear();
		m_framebuffers.clear();

		m_renderPass.reset();
		m_renderPass = std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format);
		m_framebuffers = Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get());

		// --- Recreate everything that depends on the swapchain images --- //

		vk::Extent2D imageExtent = m_swapchain->GetImageDescription().extent;

		// Use any command buffer for init
		auto commandBuffer = m_commandBufferPool.ResetAndGetCommandBuffer();
		commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		{
			m_scene->Reset(commandBuffer, *m_renderPass, imageExtent);
			UpdateShadowMaps();
			m_grid->Reset(*m_renderPass, imageExtent);
		}
		commandBuffer.end();

		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		m_commandBufferPool.Submit(submitInfo);
		m_commandBufferPool.WaitUntilSubmitComplete();

		CreateSecondaryCommandBuffers();
		m_frameDirty = kAllFramesDirty;
	}

	void RecordRenderPassCommands()
	{
		for (size_t i = 0; i < m_framebuffers.size(); ++i)
		{
			RecordFrameRenderPassCommands(i);
		}
	}

	void RecordFrameRenderPassCommands(uint32_t frameIndex)
	{
		auto& commandBuffer = m_renderPassCommandBuffers[frameIndex];
		vk::CommandBufferInheritanceInfo info(
			m_renderPass->Get(), 0, m_framebuffers[frameIndex].Get()
		);
		commandBuffer->begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
		{
			RenderState state;

			// Draw opaque materials first
			m_scene->DrawOpaqueObjects(commandBuffer.get(), frameIndex, state);

			// Then the grid
			if (m_showGrid)
				m_grid->Draw(commandBuffer.get());

			// Draw transparent objects last (sorted by distance to camera)
			m_scene->DrawTransparentObjects(commandBuffer.get(), frameIndex, state);
		}
		commandBuffer->end();
	}

	static constexpr uint8_t kAllFramesDirty = std::numeric_limits<uint8_t>::max();
	uint8_t m_frameDirty = kAllFramesDirty;

	void RenderFrame(uint32_t imageIndex, vk::CommandBuffer commandBuffer) override
	{
		auto& framebuffer = m_framebuffers[imageIndex];

		m_scene->Update(imageIndex);

		// Record commands again if something changed
		if ((m_frameDirty & (1 << (uint8_t)imageIndex)) > 0)
		{
			RenderShadowMaps(commandBuffer, imageIndex);
			RecordFrameRenderPassCommands(imageIndex);
			m_frameDirty &= ~(1 << (uint8_t)imageIndex);
		}

		std::array<vk::ClearValue, 2> clearValues = {
			vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f }),
			vk::ClearDepthStencilValue(1.0f, 0.0f)
		};
		auto renderPassInfo = vk::RenderPassBeginInfo(
			m_renderPass->Get(), framebuffer.Get(),
			vk::Rect2D(vk::Offset2D(0, 0), framebuffer.GetExtent()),
			static_cast<uint32_t>(clearValues.size()), clearValues.data()
		);
		commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);
		{
			commandBuffer.executeCommands(m_renderPassCommandBuffers[imageIndex].get());
		}
		commandBuffer.endRenderPass();
	}

	void CreateSecondaryCommandBuffers()
	{
		m_renderPassCommandBuffers.clear();
		m_secondaryCommandPool.reset();

		// We don't need to repopulate draw commands every frame
		// so keep them in a secondary command buffer
		m_secondaryCommandPool = g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
			vk::CommandPoolCreateFlagBits::eResetCommandBuffer, g_physicalDevice->GetQueueFamilies().graphicsFamily.value()
		));

		// Command Buffers
		m_renderPassCommandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
			m_secondaryCommandPool.get(), vk::CommandBufferLevel::eSecondary, m_framebuffers.size()
		));
	}

	// For shadow map shaders
	ShaderCache m_shaderCache;
	const vk::Extent2D kShadowMapExtent = vk::Extent2D(2*2048, 2*2048);

	void UpdateShadowMaps()
	{
		if (m_shadowMaps.empty() == false)
		{
			for (auto& shadowMap : m_shadowMaps)
				shadowMap.Reset(kShadowMapExtent);
		}
		else
		{
			for (const auto& light : m_scene->GetLights())
			{
				m_shadowMaps.emplace_back(
					kShadowMapExtent, light, m_shaderCache, *m_scene
				);
			}
		}

		std::vector<const ShadowMap*> shadowMaps;
		shadowMaps.reserve(m_shadowMaps.size());
		for (const auto& shadowMap : m_shadowMaps)
			shadowMaps.push_back(&shadowMap);

		m_scene->UpdateShadowMaps(shadowMaps);
	}

	void RenderShadowMaps(vk::CommandBuffer& commandBuffer, uint32_t frameIndex)
	{
		for (const auto& shadowMap : m_shadowMaps)
		{
			shadowMap.Render(commandBuffer, frameIndex);
		}
	}

	Camera& GetCamera() { return m_scene->GetCamera(); }

	void Update() override 
	{
		std::chrono::duration<float> dt_s = GetDeltaTime();

		Camera& camera = GetCamera();

		const float speed = 1.0f; // in m/s

		for (const std::pair<int,bool>& key : m_keyState)
		{
			if (key.second && m_cameraMode == CameraMode::FreeCamera) 
			{
				glm::vec3 forward = glm::normalize(camera.GetLookAt() - camera.GetEye());
				glm::vec3 rightVector = glm::normalize(glm::cross(forward, camera.GetUpVector()));
				float dx = speed * dt_s.count(); // in m / s

				switch (key.first) {
					case GLFW_KEY_W:
						camera.MoveCamera(forward, dx, false);
						break;
					case GLFW_KEY_A:
						camera.MoveCamera(rightVector, -dx, true);
						break;
					case GLFW_KEY_S:
						camera.MoveCamera(forward, -dx, false);
						break;
					case GLFW_KEY_D:
						camera.MoveCamera(rightVector, dx, true);
						break;
					default:
						break;
				}
			}
		}
	}

	static void OnMouseButton(void* data, int button, int action, int mods)
	{
		App* app = reinterpret_cast<App*>(data);

		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
			app->m_isMouseDown = true;
		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
			app->m_isMouseDown = false;
	}

	static void OnMouseScroll(void* data, double xOffset, double yOffset)
	{
		App* app = reinterpret_cast<App*>(data);
		Camera& camera = app->GetCamera();
		float fov = std::clamp(camera.GetFieldOfView() - yOffset, 30.0, 130.0);
		camera.SetFieldOfView(fov);
	}

	template <typename T>
	static T sgn(T val) 
	{
		return (T(0) < val) - (val < T(0));
	}

	static void OnCursorPosition(void* data, double xPos, double yPos)
	{
		App* app = reinterpret_cast<App*>(data);
		int viewportWidth = 0;
		int viewportHeight = 0;
		app->m_window.GetSize(&viewportWidth, &viewportHeight);

		Camera& camera = app->GetCamera();

		if ((app->m_isMouseDown) && app->m_cameraMode == CameraMode::OrbitCamera) 
		{
			glm::vec4 position(camera.GetEye().x, camera.GetEye().y, camera.GetEye().z, 1);
			glm::vec4 target(camera.GetLookAt().x, camera.GetLookAt().y, camera.GetLookAt().z, 1);

			float deltaAngleX = (2 * M_PI / viewportWidth);
			float deltaAngleY = (M_PI / viewportHeight);
			float xDeltaAngle = (app->m_lastMousePos.x - xPos) * deltaAngleX;
			float yDeltaAngle = (app->m_lastMousePos.y - yPos) * deltaAngleY;

			float cosAngle = dot(camera.GetForwardVector(), app->m_upVector);
			if (cosAngle * sgn(yDeltaAngle) > 0.99f)
				yDeltaAngle = 0;

			// Rotate in X
			glm::mat4x4 rotationMatrixX(1.0f);
			rotationMatrixX = glm::rotate(rotationMatrixX, xDeltaAngle, app->m_upVector);
			position = (rotationMatrixX * (position - target)) + target;

			// Rotate in Y
			glm::mat4x4 rotationMatrixY(1.0f);
			rotationMatrixY = glm::rotate(rotationMatrixY, yDeltaAngle, camera.GetRightVector());
			glm::vec3 finalPositionV3 = (rotationMatrixY * (position - target)) + target;

			camera.SetCameraView(finalPositionV3, camera.GetLookAt(), app->m_upVector);

			// We need to recompute transparent object order if camera changes
			if (app->m_scene->HasTransparentObjects())
			{
				app->m_scene->SortTransparentObjects();
				app->m_frameDirty = kAllFramesDirty;
			}
		}
		else if (app->m_isMouseDown && app->m_cameraMode == CameraMode::FreeCamera)
		{
			float xDelta = app->m_lastMousePos.x - xPos;
			float yDelta = app->m_lastMousePos.y - yPos;

			float m_fovV = camera.GetFieldOfView() / viewportWidth * viewportHeight;

			float xDeltaAngle = glm::radians(xDelta * camera.GetFieldOfView() / viewportWidth);
			float yDeltaAngle = glm::radians(yDelta * m_fovV / viewportHeight);

			//Handle case were dir = up vector
			float cosAngle = dot(camera.GetForwardVector(), app->m_upVector);
			if (cosAngle > 0.99f && yDeltaAngle < 0|| cosAngle < -0.99f && yDeltaAngle > 0)
				yDeltaAngle = 0;
			
			glm::vec3 lookat = camera.GetLookAt() - camera.GetUpVector() * yDeltaAngle;
			float length = glm::distance(camera.GetLookAt(), camera.GetEye());

			glm::vec3 rightVector = camera.GetRightVector();
			glm::vec3 newLookat = lookat + rightVector * xDeltaAngle;

			auto lookatDist = glm::distance(newLookat, camera.GetEye());
			camera.LookAt(newLookat, app->m_upVector);
		}
		app->m_lastMousePos.x = xPos; 
		app->m_lastMousePos.y = yPos;
	}

	static void OnKey(void* data, int key, int scancode, int action, int mods) {
		App* app = reinterpret_cast<App*>(data);
		app->m_keyState[key] = action == GLFW_PRESS ? true : action == GLFW_REPEAT ? true : false;

		if (key == GLFW_KEY_F && action == GLFW_PRESS) 
		{
			if (app->m_cameraMode == CameraMode::FreeCamera) 
			{
				app->m_scene->ResetCamera();
			}
			app->m_cameraMode = app->m_cameraMode == CameraMode::FreeCamera ? CameraMode::OrbitCamera : CameraMode::FreeCamera;
		}
		if (key == GLFW_KEY_G && action == GLFW_PRESS)
		{
			app->m_showGrid = !app->m_showGrid;
			app->m_frameDirty = kAllFramesDirty;
		}
	}

private:
	std::unique_ptr<RenderPass> m_renderPass;
	std::vector<Framebuffer> m_framebuffers;

	// Secondary command buffers
	vk::UniqueCommandPool m_secondaryCommandPool;
	std::vector<vk::UniqueCommandBuffer> m_renderPassCommandBuffers;

	std::vector<ShadowMap> m_shadowMaps;
	std::unique_ptr<Scene> m_scene;
	std::unique_ptr<Grid> m_grid;
};

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		std::cout << "Missing argument(s), expecting '\"path/to/assets/\" \"scene_file.dae\"'" << std::endl;
		return 1;
	}
	
	std::string basePath = argv[1];
	std::string sceneFile = argv[2];

	vk::Extent2D extent(800, 600);
	Window window(extent, "Vulkan");
	window.SetInputMode(GLFW_STICKY_KEYS, GLFW_TRUE);

	Instance instance(window);
	vk::UniqueSurfaceKHR surface(window.CreateSurface(instance.Get()));

	PhysicalDevice::Init(instance.Get(), surface.get());
	Device::Init(*g_physicalDevice);
	{
		App app(surface.get(), extent, window, std::move(basePath), std::move(sceneFile));
		app.Init();
		app.Run();
	}
	Device::Term();
	PhysicalDevice::Term();

	return 0;
}
