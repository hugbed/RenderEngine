#pragma once

#include <Renderer/Bindless.h>
#include <RHI/RenderLoop.h>
#include <RHI/vk_structs.h>
#include <gsl/pointers>

#include <memory>
#include <optional>

class BindlessDescriptors;
class BindlessDrawParams;
class ImGuiVulkan;
class Framebuffer;
class GraphicsPipelineCache;
class RenderPass;
class RenderScene;
class ShaderCache;
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

	void OnInit() override;
	void OnSwapchainRecreated() override;
	void Update() override;
	void Render(vk::CommandBuffer commandBuffer, uint32_t imageIndex) override;
	virtual void UpdateImGui() {}

	vk::Extent2D GetImageExtent() const;
	vk::Instance GetInstance() const;
	const Window& GetWindow() const;
	const Swapchain& GetSwapchain() const;
	uint32_t GetImageIndex() const;
	uint32_t GetImageCount() const;

	RenderingInfo GetRenderingInfo(
		std::optional<vk::ClearColorValue> clearColorValue = std::nullopt,
		std::optional<vk::ClearDepthStencilValue> clearDepthValue = std::nullopt) const;

	gsl::not_null<GraphicsPipelineCache*> GetGraphicsPipelineCache() const;
	gsl::not_null<BindlessDescriptors*> GetBindlessDescriptors() const;
	gsl::not_null<BindlessDrawParams*> GetBindlessDrawParams() const;
	gsl::not_null<TextureCache*> GetTextureCache() const;
	gsl::not_null<RenderScene*> GetRenderScene() const;

protected:
	vk::Instance m_instance;
	std::unique_ptr<ShaderCache> m_shaderCache;
	std::unique_ptr<GraphicsPipelineCache> m_graphicsPipelineCache;
	std::unique_ptr<BindlessDescriptors> m_bindlessDescriptors;
	std::unique_ptr<BindlessDrawParams> m_bindlessDrawParams;
	std::unique_ptr<BindlessFactory> m_bindlessFactory; // todo (hbedard): remove that
	std::unique_ptr<TextureCache> m_textureCache;
	std::unique_ptr<RenderScene> m_renderScene;
	std::unique_ptr<ImGuiVulkan> m_imGui;
};
