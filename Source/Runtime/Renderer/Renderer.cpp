#include <Renderer/Renderer.h>

#include <Renderer/ImGuiVulkan.h>
#include <Renderer/RenderScene.h>
#include <Renderer/RenderCommandEncoder.h>
#include <Renderer/TextureCache.h>
#include <RHI/Framebuffer.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/PhysicalDevice.h>
#include <RHI/RenderPass.h>
#include <RHI/ShaderCache.h>
#include <RHI/Swapchain.h>
#include <RHI/Window.h>

namespace Renderer_Private
{
	ImGuiVulkan::Resources PopulateImGuiResources(const Window& window, vk::Instance instance, vk::Extent2D extent, const Swapchain& swapchain)
	{
		ImGuiVulkan::Resources resources = {};
		resources.window = window.GetGLFWWindow();
		resources.instance = instance;
		resources.physicalDevice = g_physicalDevice->Get();
		resources.device = g_device->Get();
		resources.queueFamily = g_physicalDevice->GetQueueFamilies().graphicsFamily.value();
		resources.queue = g_device->GetGraphicsQueue();
		resources.imageCount = swapchain.GetImageCount();
		resources.MSAASamples = (VkSampleCountFlagBits)g_physicalDevice->GetMsaaSamples();
		resources.extent = extent;
		vk::PipelineRenderingCreateInfo renderingCreateInfo;
		renderingCreateInfo.colorAttachmentCount = 1;
		renderingCreateInfo.pColorAttachmentFormats = &swapchain.GetColorAttachmentFormat();
		renderingCreateInfo.depthAttachmentFormat = swapchain.GetDepthAttachmentFormat();
		resources.pipelineRenderingCreateInfo = renderingCreateInfo;
		return resources;
	}
}

Renderer::Renderer(vk::Instance instance, vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
	: RenderLoop(surface, extent, window)
	, m_instance(instance)
	, m_shaderCache(std::make_unique<ShaderCache>())
	, m_graphicsPipelineCache(std::make_unique<GraphicsPipelineCache>(*m_shaderCache))
	, m_bindlessDescriptors(std::make_unique<BindlessDescriptors>())
	, m_bindlessDrawParams(std::make_unique<BindlessDrawParams>(g_physicalDevice->GetMinUniformBufferOffsetAlignment(), m_bindlessDescriptors->GetDescriptorSetLayout()))
	, m_bindlessFactory(std::make_unique<BindlessFactory>(*m_bindlessDescriptors, *m_bindlessDrawParams, *m_graphicsPipelineCache))
	, m_textureCache(std::make_unique<TextureCache>(*m_bindlessDescriptors))
	, m_renderScene(std::make_unique<RenderScene>(*this))
{
}

Renderer::~Renderer() = default;

void Renderer::OnInit()
{
	using namespace Renderer_Private;

	vk::CommandBuffer commandBuffer = m_commandRingBuffer.GetCommandBuffer();
	m_renderScene->Init();
	m_textureCache->UploadTextures(m_commandRingBuffer);
	m_bindlessDrawParams->Build(commandBuffer);

	ImGuiVulkan::Resources resources = PopulateImGuiResources(
		m_window,
		m_instance,
		GetImageExtent(),
		*m_swapchain);
	m_imGui = std::make_unique<ImGuiVulkan>(resources);
}

void Renderer::OnSwapchainRecreated()
{
	using namespace Renderer_Private;

	// --- Recreate everything that depends on the swapchain images --- //

	// Use any command buffer for init
	auto commandBuffer = m_commandRingBuffer.ResetAndGetCommandBuffer();
	commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	{
		m_renderScene->Reset();

		ImGuiVulkan::Resources resources = PopulateImGuiResources(
			m_window,
			m_instance,
			GetImageExtent(),
			*m_swapchain);
		m_imGui->Reset(resources);
	}
	commandBuffer.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	m_commandRingBuffer.Submit(submitInfo);
	m_commandRingBuffer.WaitUntilSubmitComplete();
}

void Renderer::Update()
{
	m_renderScene->Update();

	m_imGui->BeginFrame();
	UpdateImGui();
	m_imGui->EndFrame();
}

void Renderer::Render(vk::CommandBuffer commandBuffer, uint32_t imageIndex)
{
	m_renderScene->Render();
	m_imGui->Render(commandBuffer, imageIndex, *m_swapchain);
}

vk::Extent2D Renderer::GetImageExtent() const
{
	return m_swapchain->GetImageDescription().extent;
}

vk::Instance Renderer::GetInstance() const
{
	return m_instance;
}

const Window& Renderer::GetWindow() const
{
	return m_window;
}

const Swapchain& Renderer::GetSwapchain() const
{
	assert(m_swapchain.get() != nullptr);
	return *m_swapchain;
}

uint32_t Renderer::GetImageIndex() const
{
	return m_imageIndex;
}

uint32_t Renderer::GetImageCount() const
{
	return static_cast<uint32_t>(m_swapchain->GetImageCount());
}

RenderingInfo Renderer::GetRenderingInfo(std::optional<vk::ClearColorValue> clearColorValue, std::optional<vk::ClearDepthStencilValue> clearDepthValue) const
{
	return m_swapchain->GetRenderingInfo(m_imageIndex, clearColorValue, clearDepthValue);
}

gsl::not_null<GraphicsPipelineCache*> Renderer::GetGraphicsPipelineCache() const
{
	return m_graphicsPipelineCache.get();
}

gsl::not_null<BindlessDescriptors*> Renderer::GetBindlessDescriptors() const
{
	return m_bindlessDescriptors.get();
}

gsl::not_null<BindlessDrawParams*> Renderer::GetBindlessDrawParams() const
{
	return m_bindlessDrawParams.get();
}

gsl::not_null<TextureCache*> Renderer::GetTextureCache() const
{
	return m_textureCache.get();
}

gsl::not_null<RenderScene*> Renderer::GetRenderScene() const
{
	return m_renderScene.get();
}

