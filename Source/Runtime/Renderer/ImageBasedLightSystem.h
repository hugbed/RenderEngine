#pragma once

#include <Renderer/BindlessDefines.h>
#include <RHI/GraphicsPipelineCache.h> // todo (hbedard): only required for the GraphicsPipelineID
#include <AssetPath.h>
#include <gsl/pointers>
#include <vulkan/vulkan.hpp>

class BindlessDescriptors;
class BindlessDrawParams;
class CommandRingBuffer;
class GraphicsPipelineCache;
class TextureCache;
class RenderCommandEncoder;
class RenderScene;
class Swapchain;

class ImageBasedLightSystem
{
public:
    ImageBasedLightSystem(
        const Swapchain& swapchain,
        RenderScene& renderScene,
        GraphicsPipelineCache& graphicsPipelineCache,
        BindlessDescriptors& bindlessDescriptors,
        BindlessDrawParams& bindlessDrawParams,
        TextureCache& textureCache);

    void Init(); // todo (hbedard): add Init and Shutdown to all systems (in an interface?)
    void Reset(const Swapchain& swapchain);
    void UploadToGPU(CommandRingBuffer& commandRingBuffer);
    void Update();
    void Render(RenderCommandEncoder& renderCommandEncoder);

private:
    struct PushConstants
    {
        uint32_t mvpIndex;
        TextureHandle textureHandle;
    };
    struct ViewUniforms
    {
        glm::mat4 mvp[6];
    };
    struct EnvCubeDrawParams
    {
        BufferHandle mvpBuffer = BufferHandle::Invalid;
        uint32_t padding[3];
    };
    EnvCubeDrawParams m_drawParams;
    ViewUniforms m_viewUniforms;
    BindlessDrawParamsHandle m_drawParamsHandle = BindlessDrawParamsHandle::Invalid;
    std::vector<BufferHandle> m_viewBufferHandles;

    gsl::not_null<GraphicsPipelineCache*> m_graphicsPipelineCache;
    gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
    gsl::not_null<BindlessDrawParams*> m_bindlessDrawParams;
    gsl::not_null<TextureCache*> m_textureCache;
    gsl::not_null<RenderScene*> m_renderScene;

    GraphicsPipelineID m_envCubePipeline = kInvalidGraphicsPipelineID;
    std::unique_ptr<UniqueBufferWithStaging> m_mvpBuffer;

    // Unprocessed texture
    TextureHandle m_hdriTextureHandle = TextureHandle::Invalid;

    TextureHandle m_dfgLutHandle = TextureHandle::Invalid;
    TextureHandle m_preFilteredEnvironmentMapHandle = TextureHandle::Invalid;
};
