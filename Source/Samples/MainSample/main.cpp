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
#include <Renderer/RenderState.h>
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

	using RenderLoop::Init;

protected:
	Inputs m_inputs;

	// assimp uses +Y as the up vector
	glm::vec3 m_upVector = glm::vec3(0.0f, 1.0f, 0.0f);

	CameraMode m_cameraMode = CameraMode::OrbitCamera;

	// For shadow map shaders
	const vk::Extent2D kShadowMapExtent = vk::Extent2D(2 * 2048, 2 * 2048);

	void Init(vk::CommandBuffer& commandBuffer) override
	{
		m_scene->Load(commandBuffer);
		InitShadowMaps();
		Renderer::Init(commandBuffer);
		m_cameraController = std::make_unique<CameraController>(m_renderScene->GetCameraViewSystem()->GetCamera(), GetSwapchain().GetImageDescription().extent);

		// Init ImGui
		ImGuiVulkan::Resources resources = PopulateImGuiResources();
		m_imgui = std::make_unique<ImGuiVulkan>(resources, commandBuffer);

		CreateSecondaryCommandBuffers();
		RecordRenderPassCommands();
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

	ImGuiVulkan::Resources PopulateImGuiResources()
	{
		ImGuiVulkan::Resources resources = {};
		resources.window = GetWindow().GetGLFWWindow();
		resources.instance = GetInstance();
		resources.physicalDevice = g_physicalDevice->Get();
		resources.device = g_device->Get();
		resources.queueFamily = g_physicalDevice->GetQueueFamilies().graphicsFamily.value();
		resources.queue = g_device->GetGraphicsQueue();
		resources.imageCount = GetSwapchain().GetImageCount();
		resources.MSAASamples = (VkSampleCountFlagBits)g_physicalDevice->GetMsaaSamples();
		resources.renderPass = GetRenderPass();
		return resources;
	}

	void UpdateImGui()
	{
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

	// Render pass commands are recorded once and executed every frame
	void OnSwapchainRecreated() override
	{
		// Reset resources that depend on the swapchain images
		m_framebuffers.clear();

		m_renderPass.reset();
		m_renderPass = std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format);
		m_framebuffers = Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get());

		// --- Recreate everything that depends on the swapchain images --- //

		// Use any command buffer for init
		auto commandBuffer = m_commandRingBuffer.ResetAndGetCommandBuffer();
		commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		{
			Renderer::Reset(commandBuffer);

			vk::Extent2D imageExtent = m_swapchain->GetImageDescription().extent;
			m_cameraController->Reset(m_renderScene->GetCameraViewSystem()->GetCamera(), imageExtent);

			// Reset ImGUI
			ImGuiVulkan::Resources resources = PopulateImGuiResources();
			m_imgui->Reset(resources, commandBuffer);
		}
		commandBuffer.end();

		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		m_commandRingBuffer.Submit(submitInfo);
		m_commandRingBuffer.WaitUntilSubmitComplete();

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

	void RecordFrameRenderPassCommands(uint32_t imageIndex)
	{
		auto& commandBuffer = m_renderPassCommandBuffers[imageIndex];
		vk::CommandBufferInheritanceInfo info(
			m_renderPass->Get(), 0, m_framebuffers[imageIndex].Get()
		);
		commandBuffer->begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
		{
			uint32_t concurrentFrameIndex = imageIndex % m_commandRingBuffer.GetNbConcurrentSubmits();
			Renderer::Render(commandBuffer.get(), concurrentFrameIndex);
		}
		commandBuffer->end();
	}

	static constexpr uint8_t kAllFramesDirty = std::numeric_limits<uint8_t>::max();
	uint8_t m_frameDirty = kAllFramesDirty;

	void Render(uint32_t imageIndex, vk::CommandBuffer commandBuffer) override
	{
		auto& framebuffer = m_framebuffers[imageIndex];

		// Record ImGui every frame
		m_imgui->RecordCommands(imageIndex, framebuffer.Get());

		// Record commands again if something changed
		if ((m_frameDirty & (1 << (uint8_t)imageIndex)) > 0)
		{
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
			commandBuffer.executeCommands(m_imgui->GetCommandBuffer(imageIndex));
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

	void InitShadowMaps()
	{
		if (GetRenderScene()->GetShadowSystem()->GetShadowCount() > 0)
		{
			GetRenderScene()->GetShadowSystem()->UploadToGPU(m_commandRingBuffer);
		}

		if (m_options.showShadowMapPreview)
		{
			// Optional view on the depth map
			if (GetRenderScene()->GetShadowSystem()->GetShadowCount() > 0)
			{
				if (m_shadowMapPreviewQuad == nullptr)
				{
					m_shadowMapPreviewQuad = std::make_unique<TexturedQuad>(
						GetRenderScene()->GetShadowSystem()->GetCombinedImageSampler(0),
						*m_renderPass,
						m_swapchain->GetImageDescription().extent,
						*m_graphicsPipelineCache,
						*m_bindlessDescriptors,
						*m_bindlessDrawParams,
						vk::ImageLayout::eDepthStencilReadOnlyOptimal
					);
				}
				else
				{
					m_shadowMapPreviewQuad->Reset(
						GetRenderScene()->GetShadowSystem()->GetCombinedImageSampler(0),
						*m_renderPass,
						m_swapchain->GetImageDescription().extent
					);
				}
			}
		}
	}

	bool HandleOptionsKeys(const Inputs& inputs)
	{
		auto it = inputs.keyState.find(GLFW_KEY_G);
		if (it != inputs.keyState.end() && it->second == KeyAction::ePressed)
		{
			m_options.showGrid = !m_options.showGrid;
			m_frameDirty = kAllFramesDirty;
			return true;
		}
		return false;
	}

	void Update() override 
	{
		Renderer::Update(m_frameIndex);

		m_imgui->BeginFrame();
		{
			m_inputSystem.CaptureMouseInputs(ImGui::GetIO().WantCaptureMouse);
			UpdateImGui();
		}
		m_imgui->EndFrame();

		std::chrono::duration<float> dt_s = GetDeltaTime();

		const Inputs& inputs = m_inputSystem.GetFrameInputs();

		HandleOptionsKeys(inputs);

		m_cameraController->Update(dt_s, inputs);

		m_inputSystem.EndFrame();
	}

private:
	std::unique_ptr<ImGuiVulkan> m_imgui;

	InputSystem m_inputSystem;

	// Secondary command buffers
	vk::UniqueCommandPool m_secondaryCommandPool;
	std::vector<vk::UniqueCommandBuffer> m_renderPassCommandBuffers;

	std::unique_ptr<AssimpScene> m_scene;
	std::unique_ptr<CameraController> m_cameraController;
	std::unique_ptr<TexturedQuad> m_shadowMapPreviewQuad;
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
