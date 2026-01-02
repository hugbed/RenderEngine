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
	ImGuiVulkan::Resources PopulateImGuiResources(const Window& window, vk::Instance instance, uint32_t imageCount, vk::RenderPass renderPass)
	{
		ImGuiVulkan::Resources resources = {};
		resources.window = window.GetGLFWWindow();
		resources.instance = instance;
		resources.physicalDevice = g_physicalDevice->Get();
		resources.device = g_device->Get();
		resources.queueFamily = g_physicalDevice->GetQueueFamilies().graphicsFamily.value();
		resources.queue = g_device->GetGraphicsQueue();
		resources.imageCount = imageCount;
		resources.MSAASamples = (VkSampleCountFlagBits)g_physicalDevice->GetMsaaSamples();
		resources.renderPass = renderPass;
		return resources;
	}
}

Renderer::Renderer(vk::Instance instance, vk::SurfaceKHR surface, vk::Extent2D extent, Window& window)
	: RenderLoop(surface, extent, window)
	, m_instance(instance)
	, m_renderPass(std::make_unique<RenderPass>(m_swapchain->GetImageDescription().format))
	, m_framebuffers(Framebuffer::FromSwapchain(*m_swapchain, m_renderPass->Get()))
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

	ImGuiVulkan::Resources resources = PopulateImGuiResources(m_window, m_instance, m_swapchain->GetImageCount(), m_renderPass->Get());
	m_imGui = std::make_unique<ImGuiVulkan>(resources, commandBuffer);

	CreateSecondaryCommandBuffers();
}

void Renderer::OnSwapchainRecreated()
{
	using namespace Renderer_Private;

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
		m_renderScene->Reset();

		ImGuiVulkan::Resources resources = PopulateImGuiResources(
			m_window,
			m_instance,
			m_swapchain->GetImageCount(),
			m_renderPass->Get());
		m_imGui->Reset(resources, commandBuffer);
	}
	commandBuffer.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	m_commandRingBuffer.Submit(submitInfo);
	m_commandRingBuffer.WaitUntilSubmitComplete();

	CreateSecondaryCommandBuffers();
}

void Renderer::CreateSecondaryCommandBuffers()
{
	m_renderCommandBuffers.clear();
	m_secondaryCommandPool.reset();
	m_secondaryCommandPool = g_device->Get().createCommandPoolUnique(vk::CommandPoolCreateInfo(
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer, g_physicalDevice->GetQueueFamilies().graphicsFamily.value()
	));
	m_renderCommandBuffers = g_device->Get().allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
		m_secondaryCommandPool.get(), vk::CommandBufferLevel::eSecondary, m_framebuffers.size()
	));
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
	auto& framebuffer = m_framebuffers[imageIndex];

	RenderCommandEncoder renderCommandEncoder(*m_graphicsPipelineCache, *m_renderScene->GetMaterialSystem(), *m_bindlessDrawParams);
	renderCommandEncoder.BeginRender(commandBuffer, GetFrameIndex());
	renderCommandEncoder.BindBindlessDescriptorSet(m_bindlessDescriptors->GetPipelineLayout(), m_bindlessDescriptors->GetDescriptorSet());
	m_renderScene->RenderShadowMaps(renderCommandEncoder, renderCommandEncoder.GetFrameIndex());
	renderCommandEncoder.EndRender();

	// Render scene to a secondary command buffer
	vk::UniqueCommandBuffer& renderPassCommandBuffer = m_renderCommandBuffers[imageIndex];
	vk::CommandBufferInheritanceInfo info(m_renderPass->Get(), 0, m_framebuffers[imageIndex].Get());
	renderPassCommandBuffer->begin({ vk::CommandBufferUsageFlagBits::eRenderPassContinue, &info });
	{
		RenderCommandEncoder renderCommandEncoder(*m_graphicsPipelineCache, *m_renderScene->GetMaterialSystem(), *m_bindlessDrawParams);
		renderCommandEncoder.BeginRender(renderPassCommandBuffer.get(), GetFrameIndex());
		renderCommandEncoder.BindBindlessDescriptorSet(m_bindlessDescriptors->GetPipelineLayout(), m_bindlessDescriptors->GetDescriptorSet());
		m_renderScene->Render(renderCommandEncoder);
		renderCommandEncoder.EndRender();
	}
	renderPassCommandBuffer->end();

	// Render ImGui to another secondary command buffer
	m_imGui->RecordCommands(imageIndex, framebuffer.Get());

	// Execute both secondary command buffers from the main command buffer
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
		commandBuffer.executeCommands(GetRenderCommandBuffer(imageIndex));
		commandBuffer.executeCommands(m_imGui->GetCommandBuffer(imageIndex));
	}
	commandBuffer.endRenderPass();
}

vk::RenderPass Renderer::GetRenderPass() const
{
	return m_renderPass->Get();
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

vk::CommandBuffer Renderer::GetRenderCommandBuffer(uint32_t imageIndex) const
{
	assert(imageIndex < m_renderCommandBuffers.size());
	return m_renderCommandBuffers[imageIndex].get();
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

