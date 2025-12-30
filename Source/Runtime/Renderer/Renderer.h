#pragma once

#include <Renderer/Bindless.h>
#include <RHI/RenderLoop.h>
#include <vulkan/vulkan.hpp>
#include <gsl/pointers>

#include <memory>

class BindlessDescriptors;
class BindlessDrawParams;
class Framebuffer;
class GraphicsPipelineCache;
class RenderPass;
class RenderScene;
class ShaderCache;
class SurfaceLitMaterialSystem;
class Swapchain;
class TextureCache;
class Window;

class Renderer : public RenderLoop
{
public:
	Renderer(
		vk::Instance instance,
		vk::SurfaceKHR surface,
		vk::Extent2D extent,
		Window& window);
	~Renderer();

	// todo (hbedard): currently implemented by App
	virtual void Init(vk::CommandBuffer& commandBuffer) override;
	//virtual void OnSwapchainRecreated() {}
	
	//virtual void Render(uint32_t imageIndex, vk::CommandBuffer commandBuffer) {}

	void Reset(vk::CommandBuffer commandBuffer);
	void Update(uint32_t concurrentFrameIndex);
	void Render(vk::CommandBuffer commandBuffer, uint32_t concurrentFrameIndex);

	vk::RenderPass GetRenderPass() const;
	vk::Extent2D GetImageExtent() const;
	vk::Instance GetInstance() const;
	const Window& GetWindow() const;
	const Swapchain& GetSwapchain() const;

	gsl::not_null<GraphicsPipelineCache*> GetGraphicsPipelineCache() const;
	gsl::not_null<BindlessDescriptors*> GetBindlessDescriptors() const;
	gsl::not_null<BindlessDrawParams*> GetBindlessDrawParams() const;
	gsl::not_null<TextureCache*> GetTextureCache() const;
	gsl::not_null<RenderScene*> GetRenderScene() const;

protected:
	vk::Instance m_instance;
	std::unique_ptr<RenderPass> m_renderPass;
	std::vector<Framebuffer> m_framebuffers;
	std::unique_ptr<ShaderCache> m_shaderCache;
	std::unique_ptr<GraphicsPipelineCache> m_graphicsPipelineCache;
	std::unique_ptr<BindlessDescriptors> m_bindlessDescriptors;
	std::unique_ptr<BindlessDrawParams> m_bindlessDrawParams;
	std::unique_ptr<BindlessFactory> m_bindlessFactory; // todo (hbedard): remove that
	std::unique_ptr<TextureCache> m_textureCache;
	std::unique_ptr<RenderScene> m_renderScene;
};
