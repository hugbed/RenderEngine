#pragma once

#include <BoundingBox.h>
#include <Renderer/Camera.h>
#include <Renderer/MeshAllocator.h>
#include <Renderer/LightSystem.h>
#include <RHI/RenderPass.h>
#include <RHI/Swapchain.h>
#include <RHI/GraphicsPipelineCache.h>
#include <RHI/ShaderCache.h>
#include <RHI/Framebuffer.h>
#include <RHI/Texture.h>
#include <RHI/Image.h>
#include <RHI/Device.h>
#include <RHI/PhysicalDevice.h>
#include <AssetPath.h>
#include <hash.h>
#include <glm_includes.h>
#include <vulkan/vulkan.hpp>
#include <gsl/pointers>

#include <limits>

class Scene;
struct ViewProperties;
struct CombinedImageSampler;
class CommandRingBuffer;
class RenderCommandEncoder;
class Renderer;

struct ShadowData
{
	glm::mat4 transform;
};

// todo (hbedard): rename ShadowHandle
using ShadowID = uint32_t;

class ShadowSystem
{
public:
	ShadowSystem(vk::Extent2D shadowMapExtent, Renderer& renderer);

	IMPLEMENT_MOVABLE_ONLY(ShadowSystem)

	void Reset();

	ShadowID CreateShadowMap(LightID lightID);

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);
	
	void Update(const Camera& camera, BoundingBox sceneBoundingBox);

	void Render(RenderCommandEncoder& renderCommandEncoder, const std::vector<MeshDrawInfo> drawCommands) const;

	size_t GetShadowCount() const { return m_lights.size(); }

	glm::mat4 GetLightTransform(ShadowID id) const;

	SmallVector<vk::DescriptorImageInfo, 16> GetTexturesInfo() const;

	BufferHandle GetMaterialShadowsBufferHandle() const { return m_materialShadowsBufferHandle; }

	CombinedImageSampler GetCombinedImageSampler(ShadowID id) const;

	TextureHandle GetShadowMapTextureHandle(ShadowID id) const;

private:
	// Only created when we know the VertexShaderConstants
	void CreateGraphicsPipeline();

private:
	struct MaterialShadow
	{
		glm::aligned_mat4 transform;
		TextureHandle shadowMapTextureHandle = TextureHandle::Invalid;
		uint32_t padding[3];
	};

	struct ShadowMapDrawParams
	{
		BufferHandle meshTransforms = BufferHandle::Invalid;
		BufferHandle shadowViews = BufferHandle::Invalid;
		uint32_t padding[2] = { 0 , 0 };
	};
	ShadowMapDrawParams m_drawParams = {};
	BindlessDrawParamsHandle m_drawParamsHandle = BindlessDrawParamsHandle::Invalid;

	static const AssetPath kVertexShaderFile;
	static const AssetPath kFragmentShaderFile;

	vk::Format m_depthFormat;
	vk::Extent2D m_shadowMapExtent;
	gsl::not_null<Renderer*> m_renderer;

	// ShadowID -> Array index
	std::vector<LightID> m_lights; // casting shadows
	std::vector<ViewProperties> m_shadowViews;
	std::vector<MaterialShadow> m_materialShadows;
	std::vector<std::unique_ptr<Image>> m_depthImages; // todo: replace with Image (remove nullptr)
	//std::vector<vk::UniqueFramebuffer> m_framebuffers;

	// Use these resources for all shadow map rendering
	vk::UniqueSampler m_sampler; // use the same sampler for all images
	GraphicsPipelineID m_graphicsPipelineID = (std::numeric_limits<uint32_t>::max)(); // all shadows use the same shaders
	std::unique_ptr<UniqueBuffer> m_shadowViewsBuffer; // for rendering shadow maps
	std::unique_ptr<UniqueBuffer> m_materialShadowsBuffer; // for using shadows in material rendering
	BufferHandle m_materialShadowsBufferHandle = BufferHandle::Invalid;
};
