#if defined(_WIN32)
#include <Windows.h>
#define _USE_MATH_DEFINES
#endif

#include <AssimpSceneLoader.h>
#include <InputSystem.h>
#include <CameraController.h>
#include <Renderer/Camera.h>
#include <Renderer/CameraViewSystem.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderScene.h>
#include <RHI/Window.h>
#include <RHI/vk_utils.h>
#include <ArgumentParser.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <chrono>
#include <iostream>

class App : public Renderer
{
public:
	struct Options
	{
		bool showGrid = true;
		bool showShadowMapPreview = false;
	} m_options;

	App(VkInstance instance, vk::SurfaceKHR surface, vk::Extent2D extent, Window& window, std::string basePath, std::string sceneFile)
		: Renderer(instance, surface, extent, window)
		, m_scene(std::make_unique<AssimpSceneLoader>(std::move(basePath), std::move(sceneFile), *this))
	{
		window.SetMouseButtonCallback(reinterpret_cast<void*>(&m_inputSystem), InputSystem::OnMouseButton);
		window.SetMouseScrollCallback(reinterpret_cast<void*>(&m_inputSystem), InputSystem::OnMouseScroll);
		window.SetCursorPositionCallback(reinterpret_cast<void*>(&m_inputSystem), InputSystem::OnCursorPosition);
		window.SetKeyCallback(reinterpret_cast<void*>(&m_inputSystem), InputSystem::OnKey);
	}

protected:
	Inputs m_inputs;

	// assimp uses +Y as the up vector
	glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);

	CameraMode m_cameraMode = CameraMode::OrbitCamera;

	// For shadow map shaders
	const vk::Extent2D kShadowMapExtent = vk::Extent2D(2 * 2048, 2 * 2048);

	void OnInit() override
	{
		vk::CommandBuffer commandBuffer = m_commandRingBuffer.GetCommandBuffer();

		// Load asimp scene
		m_scene->Load(commandBuffer);

		// Initialize renderer
		Renderer::OnInit();

		// and finaly the camera controller
		m_cameraController = std::make_unique<CameraController>(m_renderScene->GetCameraViewSystem()->GetCamera(), m_swapchain->GetImageDescription().extent);
	}

	void OnSwapchainRecreated() override
	{
		Renderer::OnSwapchainRecreated();

		m_cameraController->Reset(m_renderScene->GetCameraViewSystem()->GetCamera(), GetImageExtent());
	}

	void Update() override
	{
		std::chrono::duration<float> dt_s = GetDeltaTime();
		const Inputs& inputs = m_inputSystem.GetFrameInputs();
		HandleOptionsKeys(inputs);
		m_renderScene->GetCameraViewSystem()->GetCamera().SetExposure(m_imGuiState.cameraExposure);
		m_cameraController->Update(dt_s, inputs);
		m_inputSystem.EndFrame();

		Renderer::Update();
	}

	struct ImGuiState
	{
		bool isCameraMenuOpen = false;
		float cameraExposure = 0.213f;
		int selectedViewDebugInputOption = 0;
		int selectedViewDebugEquation = 0;
	} m_imGuiState;

	void UpdateImGui()
	{
		m_inputSystem.CaptureMouseInputs(ImGui::GetIO().WantCaptureMouse);

		// Add ImGui widgets here
		ImGui::Begin("Options");
		
		ImGui::SliderFloat("Exposure", &m_imGuiState.cameraExposure, 0.0f, 0.5f);

		bool viewDebugOptionsChanged = false;

		const char* viewDebugInputOptions[] = {
			"None",
			"BaseColor",
			"DiffuseColor",
			"Normal",
			"Occlusion",
			"Emissive",
			"Metallic",
			"Roughness",
		};
		if (ImGui::Combo("View Debug Input",
				&m_imGuiState.selectedViewDebugInputOption,
				viewDebugInputOptions, IM_ARRAYSIZE(viewDebugInputOptions)))
		{
			viewDebugOptionsChanged = true;
		}

		const char* viewDebugEquationOptions[] = {
			"None",
			"Diffuse",
			"F",
			"G",
			"D",
			"Specular",
		};
		if (ImGui::Combo("View Debug Equation",
				&m_imGuiState.selectedViewDebugEquation,
				viewDebugEquationOptions, IM_ARRAYSIZE(viewDebugEquationOptions)))
		{
			viewDebugOptionsChanged = true;
		}

		if (viewDebugOptionsChanged)
		{
			m_renderScene->GetCameraViewSystem()->SetViewDebug(
				static_cast<ViewDebugInput>(m_imGuiState.selectedViewDebugInputOption),
				static_cast<ViewDebugEquation>(m_imGuiState.selectedViewDebugEquation));
		}

		ImGui::End();
	}

	bool HandleOptionsKeys(const Inputs& inputs)
	{
		auto it = inputs.keyState.find(GLFW_KEY_G);
		if (it != inputs.keyState.end() && it->second == KeyAction::ePressed)
		{
			m_options.showGrid = !m_options.showGrid;
			return true;
		}
		return false;
	}

private:
	InputSystem m_inputSystem;
	std::unique_ptr<AssimpSceneLoader> m_scene;
	std::unique_ptr<CameraController> m_cameraController;
};

int main(int argc, char* argv[])
{
	ProgramArguments args{
		.name = "MainSample.exe", .description = "The main sample",
		.options = std::vector {
			Argument{ .name = "gameDir", .value = "dirPath" },
			Argument{ .name = "scenePath", .value = "filePath.dae" }
		}
	};
	ArgumentParser argParser(std::move(args));
	if (!argParser.ParseArgs(argc, argv))
	{
		return -1;
	}

	// todo (hbedard): only if args are valid
	std::optional<std::string> gameDirectory = argParser.GetString("gameDir");
	std::optional<std::string> sceneFilePathStr = argParser.GetString("scenePath");
	// todo (hbedard): check that those are good :)

	std::filesystem::path engineDir = std::filesystem::absolute((std::filesystem::current_path()));
	AssetPath::SetEngineDirectory(engineDir);
	AssetPath::SetGameDirectory(std::filesystem::path(gameDirectory.value()));

	vk::Extent2D extent(800, 600);
	Window window(extent, "Vulkan");
	window.SetInputMode(GLFW_STICKY_KEYS, GLFW_TRUE);

	Instance instance(window);
	vk::UniqueSurfaceKHR surface(window.CreateSurface(instance.Get()));

	PhysicalDevice::Init(instance.Get(), surface.get());
	Device::Init(instance, *g_physicalDevice);
	{
		std::filesystem::path scenePath(sceneFilePathStr.value());
		App app(instance.Get(), surface.get(), extent, window, scenePath.parent_path().string(), scenePath.filename().string());
		app.Init();
		app.Run();
	}
	Device::Term();
	PhysicalDevice::Term();

	return 0;
}
