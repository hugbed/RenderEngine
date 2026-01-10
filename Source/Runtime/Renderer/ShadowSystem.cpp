#include <Renderer/ShadowSystem.h>

#include <Renderer/ViewProperties.h>
#include <Renderer/RenderCommandEncoder.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderScene.h>

#include <utility>

namespace
{
	struct PushConstants
	{
		uint32_t shadowIndex = 0;
		uint32_t sceneNodeIndex = 0;
	};

	[[nodiscard]] vk::UniqueSampler CreateSampler(vk::SamplerAddressMode addressMode)
	{
		return g_device->Get().createSamplerUnique(
			vk::SamplerCreateInfo(
				{}, // flags
				vk::Filter::eNearest, // magFilter
				vk::Filter::eNearest, // minFilter
				vk::SamplerMipmapMode::eLinear,
				addressMode, addressMode, addressMode, // addressModeU, V, W
				0.0f, // mipLodBias
				true, 1.0f, // anisotropyEnable, maxAnisotropy
				false, vk::CompareOp::eAlways, // compareEnable, compareOp
				0.0f, 1.0f, // minLod, maxLod
				vk::BorderColor::eIntOpaqueWhite, // borderColor
				false // unnormalizedCoordinates
			)
		);
	}

	[[nodiscard]] GraphicsPipelineInfo GetGraphicsPipelineInfo(vk::Format depthFormat, vk::Extent2D shadowMapExtent)
	{
		PipelineRenderingCreateInfo createInfo;
		createInfo.info.colorAttachmentCount = 0;
		createInfo.info.depthAttachmentFormat = depthFormat;

		GraphicsPipelineInfo info(createInfo, shadowMapExtent);
		info.sampleCount = vk::SampleCountFlagBits::e1;

		// Use front culling to prevent peter-panning
		// note that this prevents from rendering shadows
		// for meshes that don't have a back face (e.g. a plane)
		// eBack could be used in this case.
		info.cullMode = vk::CullModeFlagBits::eFront;

		return info;
	}

	[[discard]] std::unique_ptr<UniqueBuffer> CreateStorageBuffer(size_t bufferSize)
	{
		return std::make_unique<UniqueBuffer>(
			vk::BufferCreateInfo(
				{},
				bufferSize,
				vk::BufferUsageFlagBits::eStorageBuffer
			), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
		);
	}

	void UpdateViewDescriptorSet(vk::Buffer buffer, size_t bufferSize, vk::DescriptorSet descriptorSet)
	{
		uint32_t binding = 0;
		vk::DescriptorBufferInfo descriptorBufferInfoView(buffer, 0, bufferSize);
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
			vk::WriteDescriptorSet(
				descriptorSet, binding++, {},
				1, vk::DescriptorType::eStorageBuffer, nullptr, &descriptorBufferInfoView
			) // binding = 0
		};
		g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void UpdateSceneDescriptorSet(vk::Buffer buffer, size_t size, vk::DescriptorSet descriptorSet)
	{
		vk::DescriptorBufferInfo descriptorBufferInfo(buffer, 0, size);
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets;
		const uint32_t binding = 0;
		writeDescriptorSets.push_back(
			vk::WriteDescriptorSet(
				descriptorSet, binding, {},
				1, vk::DescriptorType::eStorageBuffer, nullptr, &descriptorBufferInfo
			)
		);
		g_device->Get().updateDescriptorSets(
			static_cast<uint32_t>(writeDescriptorSets.size()),
			writeDescriptorSets.data(),
			0, nullptr
		);
	}

	[[nodiscard]] std::unique_ptr<Image> CreateDepthImage(vk::Format depthFormat, vk::Extent2D extent)
	{
		return std::make_unique<Image>(
			extent.width, extent.height,
			depthFormat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageViewType::e2D,
			1, 1, // mipLevels, layerCount
			vk::SampleCountFlagBits::e1
		);
	}

	RenderingInfo GetRenderingInfo(vk::ImageView imageView, vk::Extent2D shadowMapExtent)
	{
		RenderingInfo renderingInfo;

		vk::RenderingAttachmentInfo& depthAttachment = renderingInfo.depthAttachment;
		depthAttachment.imageView = imageView;
		depthAttachment.imageLayout = vk::ImageLayout::eAttachmentOptimal;
		depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		depthAttachment.clearValue = vk::ClearDepthStencilValue(1.0f, 0.0f);

		renderingInfo.info.renderArea.extent = shadowMapExtent;
		renderingInfo.info.pDepthAttachment = &renderingInfo.depthAttachment;
		renderingInfo.info.layerCount = 1;

		return renderingInfo;
	}

	glm::mat4 ComputeDirectionalLightViewMatrix(glm::vec3 lightDirection)
	{
		// Choose a right vector to find the up vector
		glm::vec3 right(0.0f, 0.0f, 1.0f);
		if (std::abs(glm::dot(lightDirection, right)) > 0.9999f)
			right = glm::vec3(1.0f, 0.0f, 0.0f);

		glm::vec3 up = glm::cross(lightDirection, up);

		// Use the light direction for the view matrix. View doesn't need to be centered in the box.
		// If it's not, the orthographic projection will take translation into account.
		glm::mat4 view = glm::lookAt(
			glm::vec3(0.0f, 0.0f, 0.0f), // eye
			lightDirection, // center
			up
		);
		return view;
	}

	[[nodiscard]] ViewProperties ComputeShadowTransform(
		const Light& light,
		const Camera& camera,
		BoundingBox sceneBox,
		const std::vector<BoundingBox>& boxes,
		const std::vector<glm::mat4>& transforms)
	{
		// Compute camera frustrum corners
		std::vector<glm::vec3> camFrustrumPts = camera.ComputeFrustrumCorners();

		// Simplify to bounding box in world coordinates
		BoundingBox camBox_world = BoundingBox::FromPoints(camFrustrumPts);

		// We'll transform bounding boxes from world to light coordinates
		// so we can stretch the camera box in the light direction
		// easily (-z).
		glm::mat4 shadowView = ::ComputeDirectionalLightViewMatrix(light.direction);
		BoundingBox camBox_view = shadowView * camBox_world;
		BoundingBox sceneBox_view = shadowView * sceneBox;

		// Make sure to keep all objects that can cast shadow into the camera frustrum.
		// Light is looking at -z so bring near plane to the fartest object in the light direction (-z).
		camBox_view.max.z = sceneBox_view.max.z;

		// Transform this box back to world coordinates
		glm::mat4 shadowViewInverse = glm::inverse(shadowView);
		camBox_world = camBox_view.Transform(shadowViewInverse);

		// Compute bounding box all objects in the extended camera frustrum
		BoundingBox lightBox_world;
		for (int i = 0; i < (int)boxes.size(); ++i)
		{
			const BoundingBox& box_local = boxes[i];
			BoundingBox box_world = box_local.Transform(transforms[i]);
			if (box_world.Intersects(camBox_world))
			{
				lightBox_world = lightBox_world.Union(box_world);
			}
		}

		// Transform again to the light's local view space
		BoundingBox lightBox_view = lightBox_world.Transform(shadowView);

		// The projection matrix maps this box in light space between -1.0 and 1.0
		glm::mat4 proj = glm::ortho(
			lightBox_view.min.x, lightBox_view.max.x,
			lightBox_view.min.y, lightBox_view.max.y,
			lightBox_view.min.z, lightBox_view.max.z
		);

		// skip position for directional lights
		//m_viewUniforms.pos = m_light.pos;

		return ViewProperties{
			shadowView, glm_vk::kClip * proj, glm::vec3()
		};
	}
}

const AssetPath ShadowSystem::kVertexShaderFile("/Engine/Generated/Shaders/shadow_map_vert.spv");
const AssetPath ShadowSystem::kFragmentShaderFile("/Engine/Generated/Shaders/shadow_map_frag.spv");

ShadowSystem::ShadowSystem(vk::Extent2D shadowMapExtent, Renderer& renderer)
	: m_shadowMapExtent(shadowMapExtent)
	, m_renderer(&renderer)
	, m_depthFormat(g_physicalDevice->FindDepthFormat())
{
	m_sampler = ::CreateSampler(vk::SamplerAddressMode::eClampToEdge);
	m_drawParamsHandle = m_renderer->GetBindlessDrawParams()->DeclareParams<ShadowMapDrawParams>();
}

void ShadowSystem::Reset()
{
	m_renderer->GetGraphicsPipelineCache()->ResetGraphicsPipeline(
		m_graphicsPipelineID, ::GetGraphicsPipelineInfo(m_depthFormat, m_shadowMapExtent)
	);
	
	for (ShadowID id = 0; id < m_depthImages.size(); ++id)
		m_depthImages[id] = ::CreateDepthImage(m_depthFormat, m_shadowMapExtent);
}

ShadowID ShadowSystem::CreateShadowMap(LightID lightID)
{
	ShadowID id = static_cast<ShadowID>(m_lights.size());
	m_lights.push_back(lightID);
	m_shadowViews.push_back({});
	m_depthImages.push_back(::CreateDepthImage(m_depthFormat, m_shadowMapExtent));
	TextureHandle textureHandle = m_renderer->GetBindlessDescriptors()->StoreTexture(m_depthImages.back()->GetImageView(), m_sampler.get());
	m_materialShadows.push_back(MaterialShadow{ glm::identity<glm::aligned_mat4>(), textureHandle });
	return id;
}

void ShadowSystem::UploadToGPU(CommandRingBuffer& commandRingBuffer)
{
	if (GetShadowCount() == 0)
	{
		return; // nothing to upload
	}

	CreateGraphicsPipeline();

	gsl::not_null<BindlessDescriptors*> bindlessDescriptors = m_renderer->GetBindlessDescriptors();
	gsl::not_null<BindlessDrawParams*> bindlessDrawParams = m_renderer->GetBindlessDrawParams();
	
	m_shadowViewsBuffer = ::CreateStorageBuffer(m_shadowViews.size() * sizeof(m_shadowViews[0]));
	m_materialShadowsBuffer = ::CreateStorageBuffer(m_materialShadows.size() * sizeof(m_materialShadows[0]));
	m_materialShadowsBufferHandle = bindlessDescriptors->StoreBuffer(m_materialShadowsBuffer->Get(), vk::BufferUsageFlagBits::eStorageBuffer);

	gsl::not_null<RenderScene*> renderScene = m_renderer->GetRenderScene();
	m_drawParams.meshTransforms = renderScene->GetSceneTree()->GetTransformsBufferHandle();
	m_drawParams.shadowViews = bindlessDescriptors->StoreBuffer(m_shadowViewsBuffer->Get(), vk::BufferUsageFlagBits::eStorageBuffer);
	bindlessDrawParams->DefineParams(m_drawParamsHandle, m_drawParams);
}

void ShadowSystem::CreateGraphicsPipeline()
{
	gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
	ShaderCache& shaderCache = graphicsPipelineCache->GetShaderCache();
	ShaderID vertexShaderID = shaderCache.CreateShader(kVertexShaderFile.GetPathOnDisk());
	ShaderID fragmentShaderID = shaderCache.CreateShader(kFragmentShaderFile.GetPathOnDisk());
	ShaderInstanceID vertexShaderInstanceID = shaderCache.CreateShaderInstance(vertexShaderID);
	ShaderInstanceID fragmentShaderInstanceID = shaderCache.CreateShaderInstance(fragmentShaderID);
	m_graphicsPipelineID = graphicsPipelineCache->CreateGraphicsPipeline(
		vertexShaderInstanceID,
		fragmentShaderInstanceID,
		::GetGraphicsPipelineInfo(m_depthFormat, m_shadowMapExtent)
	);
}

void ShadowSystem::Update(const Camera& camera, BoundingBox sceneBoundingBox)
{
	if (GetShadowCount() == 0)
	{
		return;
	}

	gsl::not_null<RenderScene*> renderScene = m_renderer->GetRenderScene();
	gsl::not_null<SceneTree*> sceneTree = renderScene->GetSceneTree();
	gsl::not_null<LightSystem*> lightSystem = renderScene->GetLightSystem();

	m_drawParams.meshTransforms = sceneTree->GetTransformsBufferHandle();
	BoundingBox sceneBox = sceneTree->ComputeWorldBoundingBox();

	for (ShadowID id = 0; id < m_lights.size(); ++id)
	{
		const Light& light = lightSystem->GetLight(m_lights[id]);

		m_shadowViews[id] = ::ComputeShadowTransform(
			lightSystem->GetLight(id),
			camera,
			sceneBox,
			sceneTree->GetBoundingBoxes(),
			sceneTree->GetTransforms()
		);

		m_materialShadows[id].transform = m_shadowViews[id].proj * m_shadowViews[id].view;
	}

	// Write values to buffer
	{
		size_t writeSize = m_shadowViews.size() * sizeof(m_shadowViews[0]);
		memcpy(m_shadowViewsBuffer->GetMappedData(), reinterpret_cast<const void*>(m_shadowViews.data()), writeSize);
		m_shadowViewsBuffer->Flush(0, writeSize);
	}
	{
		size_t writeSize = m_materialShadows.size() * sizeof(m_materialShadows[0]);
		memcpy(m_materialShadowsBuffer->GetMappedData(), reinterpret_cast<const void*>(m_materialShadows.data()), writeSize);
		m_materialShadowsBuffer->Flush(0, writeSize);
	}
}

void ShadowSystem::Render(const std::vector<MeshDrawInfo> drawCommands) const
{
	if (GetShadowCount() == 0)
	{
		return;
	}

	gsl::not_null<GraphicsPipelineCache*> graphicsPipelineCache = m_renderer->GetGraphicsPipelineCache();
	gsl::not_null<BindlessDescriptors*> bindlessDescriptors = m_renderer->GetBindlessDescriptors();
	gsl::not_null<BindlessDrawParams*> bindlessDrawParams = m_renderer->GetBindlessDrawParams();
	gsl::not_null<RenderScene*> renderScene = m_renderer->GetRenderScene();
	gsl::not_null<MeshAllocator*> meshAllocator = renderScene->GetMeshAllocator();
	vk::CommandBuffer commandBuffer = m_renderer->GetCommandRingBuffer().GetCommandBuffer();

	// Render into shadow depth maps
	RenderCommandEncoder renderCommandEncoder(*graphicsPipelineCache, *bindlessDrawParams);
	renderCommandEncoder.BeginRender(commandBuffer, m_renderer->GetFrameIndex());
	renderCommandEncoder.BindBindlessDescriptorSet(bindlessDescriptors->GetPipelineLayout(), bindlessDescriptors->GetDescriptorSet());
	renderCommandEncoder.BindDrawParams(m_drawParamsHandle);

	// Bind the one big vertex + index buffers
	meshAllocator->BindGeometry(commandBuffer);

	for (ShadowID id = 0; id < (ShadowID)m_depthImages.size(); ++id)
	{
		RenderingInfo renderingInfo = GetRenderingInfo(m_depthImages[id]->GetImageView(), m_shadowMapExtent);
		commandBuffer.beginRendering(renderingInfo.info);
		{
			renderCommandEncoder.BindPipeline(m_graphicsPipelineID);

			// Shadow transforms
			vk::PipelineLayout pipelineLayout = graphicsPipelineCache->GetPipelineLayout(m_graphicsPipelineID);
			uint32_t shadowIndex = (uint32_t)id;
			commandBuffer.pushConstants(
				pipelineLayout,
				vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				offsetof(PushConstants, shadowIndex), sizeof(PushConstants::shadowIndex), &shadowIndex
			);

			for (const auto& drawItem : drawCommands)
			{
				uint32_t sceneNodeIndex = static_cast<uint32_t>(drawItem.sceneNodeID);

				// Set model index push constant
				commandBuffer.pushConstants(
					pipelineLayout,
					vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
					offsetof(PushConstants, sceneNodeIndex), sizeof(PushConstants::sceneNodeIndex), &sceneNodeIndex
				);

				commandBuffer.drawIndexed(drawItem.mesh.nbIndices, 1, drawItem.mesh.indexOffset, 0, 0);
			}
		}
		commandBuffer.endRendering();
	}

	renderCommandEncoder.EndRender();
}

CombinedImageSampler ShadowSystem::GetCombinedImageSampler(ShadowID id) const
{
	return CombinedImageSampler{ m_depthImages[id].get(), m_sampler.get() };
}

TextureHandle ShadowSystem::GetShadowMapTextureHandle(ShadowID id) const
{
	return m_materialShadows[id].shadowMapTextureHandle;
}

glm::mat4 ShadowSystem::GetLightTransform(ShadowID id) const
{
	return m_shadowViews[id].proj * m_shadowViews[id].view;
}

SmallVector<vk::DescriptorImageInfo, 16> ShadowSystem::GetTexturesInfo() const
{
	SmallVector<vk::DescriptorImageInfo, 16> texturesInfo;
	texturesInfo.reserve(m_lights.size());
	for (ShadowID id = 0; id < m_lights.size(); ++id)
	{
		CombinedImageSampler combinedImageSampler = GetCombinedImageSampler(id);
		texturesInfo.emplace_back(
			combinedImageSampler.sampler,
			combinedImageSampler.texture->GetImageView(),
			vk::ImageLayout::eDepthStencilReadOnlyOptimal
		);
	}
	return texturesInfo;
}
