#include <Renderer/Renderer.h>

#include <Renderer/RenderScene.h>
#include <Renderer/RenderState.h>
#include <Renderer/TextureCache.h>
#include <Renderer/SurfaceLitMaterialSystem.h>
#include <RHI/Framebuffer.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/PhysicalDevice.h>
#include <RHI/RenderPass.h>
#include <RHI/ShaderCache.h>
#include <RHI/Swapchain.h>
#include <RHI/Window.h>

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

void Renderer::Init(vk::CommandBuffer& commandBuffer)
{
	m_renderScene->Init(commandBuffer);
	m_textureCache->UploadTextures(m_commandRingBuffer);
	m_bindlessDrawParams->Build(commandBuffer);
}

void Renderer::Reset(vk::CommandBuffer commandBuffer)
{
	m_renderScene->Reset(commandBuffer);
}

void Renderer::Update(uint32_t concurrentFrameIndex)
{
	m_renderScene->Update(concurrentFrameIndex);
}

void Renderer::Render(vk::CommandBuffer commandBuffer, uint32_t concurrentFrameIndex)
{
	RenderState renderState(*m_graphicsPipelineCache, *m_renderScene->GetMaterialSystem(), *m_bindlessDrawParams);
	renderState.BeginRender(commandBuffer, concurrentFrameIndex);
	renderState.BindBindlessDescriptorSet(m_bindlessDescriptors->GetPipelineLayout(), m_bindlessDescriptors->GetDescriptorSet());
	m_renderScene->Render(renderState, concurrentFrameIndex);
	renderState.EndRender();
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
