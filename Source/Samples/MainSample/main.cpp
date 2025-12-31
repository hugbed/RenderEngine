#if defined(_WIN32)
#include <Windows.h>
#define _USE_MATH_DEFINES
#endif

#include <AssimpScene.h>
#include <InputSystem.h>
#include <CameraController.h>
#include <Renderer/Camera.h>
#include <Renderer/CameraViewSystem.h>
#include <Renderer/TextureCache.h>
#include <Renderer/LightSystem.h>
#include <Renderer/SurfaceLitMaterialSystem.h>
#include <Renderer/MeshAllocator.h>
#include <Renderer/ShadowSystem.h>
#include <Renderer/RenderCommandEncoder.h>
#include <Renderer/TexturedQuad.h>
#include <Renderer/SceneTree.h>
#include <Renderer/Grid.h>
#include <Renderer/Skybox.h>
#include <Renderer/ImGuiVulkan.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderScene.h>
#include <RHI/RenderLoop.h>
#include <RHI/Window.h>
#include <RHI/Instance.h>
#include <RHI/Device.h>
#include <RHI/PhysicalDevice.h>
#include <RHI/CommandRingBuffer.h>
#include <RHI/Swapchain.h>
#include <RHI/RenderPass.h>
#include <RHI/Framebuffer.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/ShaderCache.h>
#include <RHI/Image.h>
#include <RHI/Texture.h>
#include <RHI/vk_utils.h>
#include <ArgumentParser.h>
#include <file_utils.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm_includes.h> // for Uniform Buffer

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <iostream>
#include <cmath>

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
		, m_scene(std::make_unique<AssimpScene>(std::move(basePath), std::move(sceneFile), *this))
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
		m_cameraController->Update(dt_s, inputs);
		m_inputSystem.EndFrame();

		Renderer::Update();
	}

	void ShowMenuFile()
	{
		if (ImGui::MenuItem("Open", "Ctrl+O")) {}
		if (ImGui::BeginMenu("Open Recent"))
		{
			ImGui::MenuItem("fish_hat.c");
			ImGui::MenuItem("fish_hat.inl");
			ImGui::MenuItem("fish_hat.h");
			ImGui::EndMenu();
		}
	}

	void UpdateImGui()
	{
		m_inputSystem.CaptureMouseInputs(ImGui::GetIO().WantCaptureMouse);

		// Add ImGui widgets here
		/*ImGui::ShowDemoWindow();*/

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				ShowMenuFile();
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
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
	std::unique_ptr<AssimpScene> m_scene;
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

	std::filesystem::path engineDir = std::filesystem::absolute((std::filesystem::path(argv[0]) / "../../../../.."));
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
