#include <Renderer/ImageBasedLightSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderScene.h>
#include <Renderer/TextureCache.h>
#include <Renderer/RenderCommandEncoder.h>
#include <Renderer/Skybox.h>
#include <RHI/CommandRingBuffer.h>
#include <RHI/PhysicalDevice.h>
#include <RHI/Swapchain.h>

namespace ImageBasedLightSystem_Private
{
    static const std::array<glm::aligned_mat4, 6> kViewMatrices =
    {
       glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
       glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
       glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
       glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
       glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
       glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };
}

ImageBasedLightSystem::ImageBasedLightSystem(Renderer& renderer)
    : m_renderer(&renderer)
{
    // todo (hbedard): do this in init

    // Prepare shaders and pipelines
    gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
    ShaderCache& shaderCache = graphicsPipelineCache->GetShaderCache();
    ShaderID vertexShader = shaderCache.CreateShader(AssetPath("/Engine/Generated/Shaders/env_cube_vert.spv").GetPathOnDisk(), "main");
    ShaderID fragmentShader = shaderCache.CreateShader(AssetPath("/Engine/Generated/Shaders/env_cube_frag.spv").GetPathOnDisk(), "main");
    ShaderInstanceID vertexShaderInstance = shaderCache.CreateShaderInstance(vertexShader);
    ShaderInstanceID fragmentShaderInstance = shaderCache.CreateShaderInstance(fragmentShader);

    const Swapchain& swapchain = m_renderer->GetSwapchain();
    GraphicsPipelineInfo graphicsPipelineInfo(swapchain.GetPipelineRenderingCreateInfo(), swapchain.GetImageExtent());
    graphicsPipelineInfo.depthTestEnable = false;
    graphicsPipelineInfo.cullMode = vk::CullModeFlagBits::eNone;
    m_envCubePipeline = graphicsPipelineCache->CreateGraphicsPipeline(vertexShaderInstance, fragmentShaderInstance, graphicsPipelineInfo);
}

void ImageBasedLightSystem::Init()
{
    using namespace ImageBasedLightSystem_Private;

    // Prepare texture
    AssetPath hdriPath("/Game/HDRi/sunny_rose_garden_4k.exr");
    m_hdriTextureHandle = m_renderer->GetTextureCache()->LoadHdri(hdriPath);
    m_drawParamsHandle = m_renderer->GetBindlessDrawParams()->DeclareParams<EnvCubeDrawParams>();

    // Prepare MVP matrices (one for each face)
    auto projMatrix = glm_vk::kClip * glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    for (uint32_t i = 0; i < kViewMatrices.size(); ++i)
    {
        m_viewUniforms.mvp[i] = projMatrix * kViewMatrices[i];
    }
}

void ImageBasedLightSystem::Reset(const Swapchain& swapchain)
{
    GraphicsPipelineInfo graphicsPipelineInfo(swapchain.GetPipelineRenderingCreateInfo(), swapchain.GetImageExtent());
    m_renderer->GetGraphicsPipelineCache()->ResetGraphicsPipeline(m_envCubePipeline, graphicsPipelineInfo);
}

void ImageBasedLightSystem::UploadToGPU(CommandRingBuffer& commandRingBuffer)
{
    using namespace ImageBasedLightSystem_Private;

    // Transfer MVP uniform buffer data
    vk::CommandBuffer commandBuffer = commandRingBuffer.GetCommandBuffer();
    m_mvpBuffer = std::make_unique<UniqueBufferWithStaging>(kViewMatrices.size() * sizeof(kViewMatrices[0]), vk::BufferUsageFlagBits::eUniformBuffer);
    uint8_t* bufferData = static_cast<uint8_t*>(m_mvpBuffer->GetStagingMappedData());
    memcpy(bufferData, &m_viewUniforms, sizeof(ViewUniforms));
    m_mvpBuffer->CopyStagingToGPU(commandBuffer);

    // Create a bindless resource for this buffer
    m_drawParams.mvpBuffer = m_renderer->GetBindlessDescriptors()->StoreBuffer(m_mvpBuffer->Get(), vk::BufferUsageFlagBits::eUniformBuffer);
    m_renderer->GetBindlessDrawParams()->DefineParams(m_drawParamsHandle, m_drawParams);
}

void ImageBasedLightSystem::Update()
{
    // nop
}

void ImageBasedLightSystem::Render()
{
    using namespace ImageBasedLightSystem_Private;

    // temporarily disabled while we refactor the code to use dynamic rendering
    // so that we can easily manage this new render pass

    gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
    gsl::not_null<BindlessDescriptors*> bindlessDescriptors = m_renderer->GetBindlessDescriptors();
    gsl::not_null<BindlessDrawParams*> bindlessDrawParams = m_renderer->GetBindlessDrawParams();
    vk::CommandBuffer commandBuffer = m_renderer->GetCommandRingBuffer().GetCommandBuffer();

    gsl::not_null<RenderScene*> renderScene = m_renderer->GetRenderScene();
    gsl::not_null<MaterialSystem*> materialSystem = renderScene->GetMaterialSystem();
    gsl::not_null<Skybox*> skybox = renderScene->GetSkybox(); // borrow the skybox cube for now

    RenderingInfo renderingInfo = m_renderer->GetRenderingInfo();
    commandBuffer.beginRendering(renderingInfo.info);
    {
        RenderCommandEncoder renderCommandEncoder(*graphicsPipelineCache, *materialSystem, *bindlessDrawParams);
        renderCommandEncoder.BeginRender(commandBuffer, m_renderer->GetFrameIndex());
        renderCommandEncoder.BindBindlessDescriptorSet(bindlessDescriptors->GetPipelineLayout(), bindlessDescriptors->GetDescriptorSet());

        vk::CommandBuffer commandBuffer = renderCommandEncoder.GetCommandBuffer();
        renderCommandEncoder.BindDrawParams(m_drawParamsHandle);
        renderCommandEncoder.BindPipeline(m_envCubePipeline);

        vk::DeviceSize offset = 0;
        vk::Buffer vertexBuffer = skybox->GetVertexBuffer();
        uint32_t vertexCount = skybox->GetVertexCount();
        commandBuffer.bindVertexBuffers(0, 1, &vertexBuffer, &offset);

        uint32_t mvpIndex = 0;
        renderCommandEncoder.BindPushConstant(1, static_cast<uint32_t>(m_hdriTextureHandle));

        for (uint32_t i = 0; i < kViewMatrices.size(); ++i)
        {
            renderCommandEncoder.BindPushConstant(0, i);
            commandBuffer.draw(vertexCount, 1, 0, 0);
        }

        renderCommandEncoder.EndRender();
    }
    commandBuffer.endRendering();
}
