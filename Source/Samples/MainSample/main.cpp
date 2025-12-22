#if defined(_WIN32)
#include <Windows.h>
#define _USE_MATH_DEFINES
#endif

#include "ArgumentParser.h"
#include "RenderLoop.h"
#include "Window.h"
#include "Instance.h"
#include "Device.h"
#include "PhysicalDevice.h"
#include "CommandBufferPool.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "GraphicsPipelineSystem.h"
#include "ShaderSystem.h"
#include "Image.h"
#include "Texture.h"
#include "Skybox.h"
#include "vk_utils.h"
#include "file_utils.h"

#include "Camera.h"
#include "TextureSystem.h"
#include "LightSystem.h"
#include "MaterialSystem.h"
#include "ModelSystem.h"
#include "ShadowSystem.h"
#include "DescriptorSetLayouts.h"
#include "RenderState.h"
#include "Scene.h"
#include "TexturedQuad.h"
#include "InputSystem.h"
#include "CameraController.h"

#include "Grid.h"

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

// For Uniform Buffer
#include "glm_includes.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <iostream>
#include <cmath>

// Use imgui lowercase for our utilities
namespace imgui
{
	struct Resources
	{
		GLFWwindow* window;
		VkInstance instance;
		VkPhysicalDevice physicalDevice;
		VkDevice device;
		uint32_t queueFamily;
		VkQueue queue;
		uint32_t imageCount;
		VkSampleCountFlagBits MSAASamples;
		VkRenderPass renderPass;
	};

	class Context
	{
	public:
		static void CheckVkResult(VkResult result)
		{
			// empty for now
		}

		Context(const Resources& resources, vk::CommandBuffer commandBuffer)
			: m_device(resources.device)
			, m_renderPass(resources.renderPass)
		{
			// Create a descriptor pool specifically for IMGUI
			VkDescriptorPoolSize poolSizes[] =
			{
				{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
				{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
				{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
			};
			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			poolInfo.maxSets = 1000 * (uint32_t)IM_ARRAYSIZE(poolSizes);
			poolInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
			poolInfo.pPoolSizes = poolSizes;
			VkResult result = vkCreateDescriptorPool(g_device->Get(), &poolInfo, nullptr, &m_imguiDescriptorPool);
			if (result != VK_SUCCESS)
			{
				assert(false && "Could not initialize descriptor pool for imgui");
				return;
			}

			// Setup Dear ImGui context
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO(); (void)io;

			// Setup Dear ImGui style
			ImGui::StyleColorsDark();

			// Setup Platform/Renderer backends
			ImGui_ImplGlfw_InitForVulkan(resources.window, true);
			ImGui_ImplVulkan_InitInfo init_info = {};
			init_info.Instance = resources.instance;
			init_info.PhysicalDevice = resources.physicalDevice;
			init_info.Device = m_device;
			init_info.QueueFamily = resources.queueFamily;
			init_info.Queue = resources.queue;
			init_info.PipelineCache = VK_NULL_HANDLE;
			init_info.DescriptorPool = m_imguiDescriptorPool;
			init_info.Allocator = nullptr;
			init_info.MinImageCount = 2;
			init_info.ImageCount = resources.imageCount;
			init_info.CheckVkResultFn = &Context::CheckVkResult;
			init_info.PipelineInfoMain.RenderPass = m_renderPass;
			init_info.PipelineInfoMain.MSAASamples = (VkSampleCountFlagBits)resources.MSAASamples;
			if (!ImGui_ImplVulkan_Init(&init_info))
			{
				vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
				assert(false && "Could not initialize imgui");
			}

			// not required anymore?
			//assert(ImGui_ImplVulkan_CreateFontsTexture(commandBuffer));

			// Create secondary command buffers
			{
				m_imguiCommandBuffers.clear();

				m_secondaryCommandPool = g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
					vk::CommandPoolCreateFlagBits::eResetCommandBuffer, g_physicalDevice->GetQueueFamilies().graphicsFamily.value()
				));

				m_imguiCommandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
					m_secondaryCommandPool.get(), vk::CommandBufferLevel::eSecondary, resources.imageCount
				));
			}
		}

		void Reset(const Resources& resources, vk::CommandBuffer commandBuffer)
		{
			ImGui_ImplVulkan_Shutdown();

			m_device = resources.device;
			m_renderPass = resources.renderPass;

			ImGui_ImplVulkan_InitInfo init_info = {};
			init_info.Instance = resources.instance;
			init_info.PhysicalDevice = resources.physicalDevice;
			init_info.Device = m_device;
			init_info.QueueFamily = resources.queueFamily;
			init_info.Queue = resources.queue;
			init_info.PipelineCache = VK_NULL_HANDLE;
			init_info.DescriptorPool = m_imguiDescriptorPool;
			init_info.Allocator = nullptr;
			init_info.MinImageCount = 2;
			init_info.ImageCount = resources.imageCount;
			init_info.PipelineInfoMain.RenderPass = m_renderPass;
			init_info.PipelineInfoMain.MSAASamples = (VkSampleCountFlagBits)resources.MSAASamples;
			init_info.CheckVkResultFn = &Context::CheckVkResult;
			assert(ImGui_ImplVulkan_Init(&init_info) && "Could not initialize imgui");
			//assert(ImGui_ImplVulkan_CreateFontsTexture(commandBuffer));
		}

		~Context()
		{
			m_imguiCommandBuffers.clear();
			m_secondaryCommandPool.reset();

			if (m_imguiDescriptorPool != VK_NULL_HANDLE)
				vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);

			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
		}

		void BeginFrame()
		{
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
		}

		void EndFrame()
		{
			ImGui::EndFrame();
			ImGui::Render();
		}

		void RecordCommands(uint32_t frameIndex, VkFramebuffer framebuffer)
		{
			auto& commandBuffer = m_imguiCommandBuffers[frameIndex];
			vk::CommandBufferInheritanceInfo info(
				m_renderPass, 0, framebuffer
			);
			commandBuffer->begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
			{
				ImDrawData* drawData = ImGui::GetDrawData();
				if (drawData != nullptr)
					ImGui_ImplVulkan_RenderDrawData(drawData, *commandBuffer);
			}
			commandBuffer->end();
		}

		vk::CommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return m_imguiCommandBuffers[frameIndex].get(); }

	private:
		VkDevice m_device = VK_NULL_HANDLE;
		VkRenderPass m_renderPass = VK_NULL_HANDLE;

		ImGui_ImplVulkanH_Window m_imguiWindow;
		VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;
		std::vector<vk::UniqueCommandBuffer> m_imguiCommandBuffers;
		vk::UniqueCommandPool m_secondaryCommandPool;
	};
}

class App : public RenderLoop
{
public:
	struct Options
	{
		bool showGrid = true;
		bool showShadowMapPreview = false;
	} m_options;

	App(VkInstance instance, vk::SurfaceKHR surface, vk::Extent2D extent, Window& window, std::string basePath, std::string sceneFile)
		: RenderLoop(surface, extent, window)
		, m_instance(instance)
		, m_renderPass(std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format))
		, m_framebuffers(Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get()))
		, m_graphicsPipelineSystem(m_shaderSystem)
		, m_textureSystem(basePath)
		, m_materialSystem(m_renderPass->Get(), m_swapchain->GetImageDescription().extent, m_graphicsPipelineSystem, m_textureSystem, m_modelSystem)
		, m_scene(std::make_unique<Scene>(
			std::move(basePath), std::move(sceneFile),
			m_commandBufferPool,
			m_graphicsPipelineSystem,
			m_textureSystem,
			m_modelSystem,
			m_lightSystem,
			m_materialSystem,
			m_shadowSystem,
			*m_renderPass, m_swapchain->GetImageDescription().extent)
		)
		, m_shadowSystem(m_swapchain->GetImageDescription().extent, m_graphicsPipelineSystem, m_modelSystem, m_lightSystem)
		, m_grid(std::make_unique<Grid>(*m_renderPass, m_swapchain->GetImageDescription().extent, m_graphicsPipelineSystem))
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
		vk::Extent2D imageExtent = m_swapchain->GetImageDescription().extent;

		m_scene->Load(commandBuffer);
		m_cameraController = std::make_unique<CameraController>(m_scene->GetCamera(), m_swapchain->GetImageDescription().extent);
		InitShadowMaps(commandBuffer);

		// Init ImGui
		imgui::Resources resources = PopulateImGuiResources();
		m_imgui = std::make_unique<imgui::Context>(resources, commandBuffer);

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

	imgui::Resources PopulateImGuiResources()
	{
		imgui::Resources resources = {};
		resources.window = m_window.GetGLFWWindow();
		resources.instance = m_instance;
		resources.physicalDevice = g_physicalDevice->Get();
		resources.device = g_device->Get();
		resources.queueFamily = g_physicalDevice->GetQueueFamilies().graphicsFamily.value();
		resources.queue = g_device->GetGraphicsQueue();
		resources.imageCount = m_swapchain->GetImageCount();
		resources.MSAASamples = (VkSampleCountFlagBits)g_physicalDevice->GetMsaaSamples();
		resources.renderPass = m_renderPass->Get();
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

		vk::Extent2D imageExtent = m_swapchain->GetImageDescription().extent;

		m_cameraController->SetViewportExtent(imageExtent);

		// Use any command buffer for init
		auto commandBuffer = m_commandBufferPool.ResetAndGetCommandBuffer();
		commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		{
			m_scene->Reset(commandBuffer, *m_renderPass, imageExtent);
			
			// Reset Shadow Maps
			if (m_shadowSystem.GetShadowCount() > 0)
			{
				const UniqueBuffer& shadowPropertiesBuffer = m_shadowSystem.GetShadowTransformsBuffer();
				m_materialSystem.UpdateShadowDescriptorSets(
					m_shadowSystem.GetTexturesInfo(),
					shadowPropertiesBuffer.Get(), shadowPropertiesBuffer.Size()
				);
			}

			m_grid->Reset(*m_renderPass, imageExtent);

			// Reset ImGUI
			imgui::Resources resources = PopulateImGuiResources();
			m_imgui->Reset(resources, commandBuffer);
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
			RenderState state(m_graphicsPipelineSystem, m_materialSystem, m_modelSystem);

			// Draw opaque materials first
			m_scene->DrawOpaqueObjects(commandBuffer.get(), frameIndex, state);

			// Then the grid
			if (m_options.showGrid)
				m_grid->Draw(commandBuffer.get());

			// Draw transparent objects last (sorted by distance to camera)
			m_scene->DrawTransparentObjects(commandBuffer.get(), frameIndex, state);

			// UI/Utilities last
			if (m_options.showShadowMapPreview && m_shadowMapPreviewQuad != nullptr)
				m_shadowMapPreviewQuad->Draw(commandBuffer.get());
		}
		commandBuffer->end();
	}

	static constexpr uint8_t kAllFramesDirty = std::numeric_limits<uint8_t>::max();
	uint8_t m_frameDirty = kAllFramesDirty;

	void RenderFrame(uint32_t imageIndex, vk::CommandBuffer commandBuffer) override
	{
		auto& framebuffer = m_framebuffers[imageIndex];

		m_scene->Update(imageIndex);

		// Record ImGui every frame
		m_imgui->RecordCommands(imageIndex, framebuffer.Get());

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

	void InitShadowMaps(vk::CommandBuffer& commandBuffer)
	{
		if (m_shadowSystem.GetShadowCount() > 0)
		{
			m_shadowSystem.UploadToGPU();
			const UniqueBuffer& shadowPropertiesBuffer = m_shadowSystem.GetShadowTransformsBuffer();
			m_materialSystem.UpdateShadowDescriptorSets(
				m_shadowSystem.GetTexturesInfo(),
				shadowPropertiesBuffer.Get(), shadowPropertiesBuffer.Size()
			);
		}

		if (m_options.showShadowMapPreview)
		{
			// Optional view on the depth map
			if (m_shadowSystem.GetShadowCount() > 0)
			{
				if (m_shadowMapPreviewQuad == nullptr)
				{
					m_shadowMapPreviewQuad = std::make_unique<TexturedQuad>(
						m_shadowSystem.GetCombinedImageSampler(0),
						*m_renderPass,
						m_swapchain->GetImageDescription().extent,
						m_graphicsPipelineSystem,
						vk::ImageLayout::eDepthStencilReadOnlyOptimal
					);
				}
				else
				{
					m_shadowMapPreviewQuad->Reset(
						m_shadowSystem.GetCombinedImageSampler(0),
						*m_renderPass,
						m_swapchain->GetImageDescription().extent
					);
				}
			}
		}
	}

	std::vector<MeshDrawInfo> drawCalls; // to preserve allocated memory

	void RenderShadowMaps(vk::CommandBuffer& commandBuffer, uint32_t frameIndex)
	{
		if (m_shadowSystem.GetShadowCount() > 0)
		{
			const std::vector<MeshDrawInfo>& opaqueDrawCalls = m_scene->GetOpaqueDrawCommands();
			const std::vector<MeshDrawInfo>& transparentDrawCalls = m_scene->GetTransparentDrawCommands();

			drawCalls.resize(opaqueDrawCalls.size() + transparentDrawCalls.size());
			std::copy(opaqueDrawCalls.begin(), opaqueDrawCalls.end(), drawCalls.begin());
			std::copy(transparentDrawCalls.begin(), transparentDrawCalls.end(), drawCalls.begin() + opaqueDrawCalls.size());

			m_shadowSystem.Update(m_scene->GetCamera(), m_scene->GetBoundingBox());
			m_shadowSystem.Render(commandBuffer, frameIndex, drawCalls);
		}
	}

	Camera& GetCamera() { return m_scene->GetCamera(); }

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
		m_imgui->BeginFrame();
		{
			m_inputSystem.CaptureMouseInputs(ImGui::GetIO().WantCaptureMouse);
			UpdateImGui();
		}
		m_imgui->EndFrame();

		std::chrono::duration<float> dt_s = GetDeltaTime();

		const Inputs& inputs = m_inputSystem.GetFrameInputs();

		HandleOptionsKeys(inputs);

		if (m_cameraController->Update(dt_s, inputs))
			OnCameraUpdated();

		m_inputSystem.EndFrame();
	}

	void OnCameraUpdated()
	{
		if (m_scene->HasTransparentObjects() || m_shadowSystem.GetShadowCount() > 0)
		{
			m_scene->SortTransparentObjects();
			m_frameDirty = kAllFramesDirty;
		}
	}

private:
	std::unique_ptr<imgui::Context> m_imgui;

	VkInstance m_instance = VK_NULL_HANDLE;
	std::unique_ptr<RenderPass> m_renderPass;
	std::vector<Framebuffer> m_framebuffers;

	ShaderSystem m_shaderSystem;
	GraphicsPipelineSystem m_graphicsPipelineSystem;
	TextureSystem m_textureSystem;
	LightSystem m_lightSystem;
	MaterialSystem m_materialSystem;
	ModelSystem m_modelSystem;
	ShadowSystem m_shadowSystem;

	InputSystem m_inputSystem;

	// Secondary command buffers
	vk::UniqueCommandPool m_secondaryCommandPool;
	std::vector<vk::UniqueCommandBuffer> m_renderPassCommandBuffers;

	std::unique_ptr<Scene> m_scene;
	std::unique_ptr<CameraController> m_cameraController;
	std::unique_ptr<Grid> m_grid;
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

	std::filesystem::path engineDir = std::filesystem::absolute((std::filesystem::path(argv[0]) / "../../../../../.."));
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
