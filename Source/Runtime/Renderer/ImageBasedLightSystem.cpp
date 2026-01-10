#include <Renderer/ImageBasedLightSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderScene.h>
#include <Renderer/TextureCache.h>
#include <Renderer/RenderCommandEncoder.h>
#include <Renderer/Skybox.h>
#include <RHI/CommandRingBuffer.h>
#include <RHI/PhysicalDevice.h>
#include <RHI/Swapchain.h>
#include <RHI/Image.h>

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

    [[nodiscard]] std::unique_ptr<Image> CreateEnvironmentMapImage(vk::Format format, vk::Extent2D extent)
    {
        return std::make_unique<Image>(
            extent.width, extent.height,
            format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D,
            1, 1, // mipLevels, layerCount
            vk::SampleCountFlagBits::e1
        );
    }

    [[nodiscard]] PipelineRenderingCreateInfo GetPipelineRenderingCreateInfo(vk::Format colorAttachmentFormat)
    {
        PipelineRenderingCreateInfo createInfo;
        createInfo.colorAttachmentFormats = { colorAttachmentFormat };
        createInfo.info.colorAttachmentCount = createInfo.colorAttachmentFormats.size();
        createInfo.info.pColorAttachmentFormats = createInfo.colorAttachmentFormats.data();
        return createInfo;
    }

    [[nodiscard]] GraphicsPipelineInfo GetGraphicsPipelineInfo(vk::Format colorFormat, vk::Extent2D imageExtent)
    {
        PipelineRenderingCreateInfo pipelineRenderingCreateInfo = GetPipelineRenderingCreateInfo(colorFormat);
        GraphicsPipelineInfo graphicsPipelineInfo(pipelineRenderingCreateInfo, imageExtent);
        graphicsPipelineInfo.sampleCount = vk::SampleCountFlagBits::e1;
        graphicsPipelineInfo.depthTestEnable = false;
        graphicsPipelineInfo.cullMode = vk::CullModeFlagBits::eNone;
        return graphicsPipelineInfo;
    }

    [[nodiscard]] RenderingInfo GetRenderingInfo(vk::ImageView imageView, vk::Extent2D imageExtent)
    {
        RenderingInfo renderingInfo;

        vk::RenderingAttachmentInfo& colorAttachment = renderingInfo.colorAttachment;
        colorAttachment.imageView = imageView;
        colorAttachment.imageLayout = vk::ImageLayout::eAttachmentOptimal;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

        renderingInfo.info.renderArea.extent = imageExtent;
        renderingInfo.info.colorAttachmentCount = 1;
        renderingInfo.info.pColorAttachments = &renderingInfo.colorAttachment;
        renderingInfo.info.layerCount = 1;

        return renderingInfo;
    }
}

ImageBasedLightSystem::ImageBasedLightSystem(Renderer& renderer)
    : m_renderer(&renderer)
{
    using namespace ImageBasedLightSystem_Private;

    // Create pre-filtered environment map image to render to
    m_preFilteredEnvironmentMapImage = CreateEnvironmentMapImage(m_envMapFormat, m_envMapExtent);
}

void ImageBasedLightSystem::Init()
{
    using namespace ImageBasedLightSystem_Private;

    // Prepare texture
    gsl::not_null<TextureCache*> textureCache = m_renderer->GetTextureCache();
    AssetPath hdriPath("/Game/HDRi/sunny_rose_garden_4k.exr");
    m_hdriTextureHandle = textureCache->LoadHdri(hdriPath);
    m_drawParamsHandle = m_renderer->GetBindlessDrawParams()->DeclareParams<EnvCubeDrawParams>();

    // Prepare shaders
    gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
    ShaderCache& shaderCache = graphicsPipelineCache->GetShaderCache();
    ShaderID vertexShader = shaderCache.CreateShader(AssetPath("/Engine/Generated/Shaders/env_cube_vert.spv").GetPathOnDisk(), "main");
    ShaderID fragmentShader = shaderCache.CreateShader(AssetPath("/Engine/Generated/Shaders/env_cube_frag.spv").GetPathOnDisk(), "main");
    ShaderInstanceID vertexShaderInstance = shaderCache.CreateShaderInstance(vertexShader);
    ShaderInstanceID fragmentShaderInstance = shaderCache.CreateShaderInstance(fragmentShader);

    // Create graphics pipeline
    GraphicsPipelineInfo graphicsPipelineInfo = GetGraphicsPipelineInfo(textureCache->GetTextureFormat(), m_envMapExtent);
    m_envCubePipeline = graphicsPipelineCache->CreateGraphicsPipeline(vertexShaderInstance, fragmentShaderInstance, graphicsPipelineInfo);

    // Prepare MVP matrices (one for each face)
    auto projMatrix = glm_vk::kClip * glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    for (uint32_t i = 0; i < kViewMatrices.size(); ++i)
    {
        m_viewUniforms.mvp[i] = projMatrix * kViewMatrices[i];
    }
}

void ImageBasedLightSystem::Reset(const Swapchain& swapchain)
{
    // environment maps are independent of the swapchain extent
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

    gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
    gsl::not_null<BindlessDescriptors*> bindlessDescriptors = m_renderer->GetBindlessDescriptors();
    gsl::not_null<BindlessDrawParams*> bindlessDrawParams = m_renderer->GetBindlessDrawParams();
    vk::CommandBuffer commandBuffer = m_renderer->GetCommandRingBuffer().GetCommandBuffer();

    gsl::not_null<RenderScene*> renderScene = m_renderer->GetRenderScene();
    gsl::not_null<Skybox*> skybox = renderScene->GetSkybox(); // borrow the skybox cube for now

    RenderingInfo renderingInfo = GetRenderingInfo(m_preFilteredEnvironmentMapImage->GetImageView(), m_envMapExtent);
    commandBuffer.beginRendering(renderingInfo.info);
    {
        RenderCommandEncoder renderCommandEncoder(*graphicsPipelineCache, *bindlessDrawParams);
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
