#include <Renderer/ShadowSystem.h>

#include <Renderer/ViewProperties.h>
#include <Renderer/RenderCommandEncoder.h>
#include <Renderer/TextureCache.h>

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

	[[nodiscard]] vk::UniqueRenderPass CreateShadowMapRenderPass()
	{
		vk::AttachmentDescription depthAttachment(
			vk::AttachmentDescriptionFlags(),
			g_physicalDevice->FindDepthFormat(),
			vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, // stencilLoadOp
			vk::AttachmentStoreOp::eDontCare, // stencilStoreOp
			vk::ImageLayout::eUndefined, // initialLayout
			vk::ImageLayout::eDepthStencilReadOnlyOptimal	// finalLayout
		);
		vk::AttachmentReference depthAttachmentRef(
			0, vk::ImageLayout::eDepthStencilAttachmentOptimal
		);

		vk::SubpassDescription subpass(
			vk::SubpassDescriptionFlags(),
			vk::PipelineBindPoint::eGraphics,
			0, nullptr, // input attachments
			0, nullptr, nullptr, // color attachments
			&depthAttachmentRef
		);

		std::array<vk::SubpassDependency, 2> dependencies = {
			vk::SubpassDependency(
				VK_SUBPASS_EXTERNAL, 0,
				vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eEarlyFragmentTests,
				vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
				vk::DependencyFlagBits::eByRegion
			),
			vk::SubpassDependency(
				0, VK_SUBPASS_EXTERNAL,
				vk::PipelineStageFlagBits::eLateFragmentTests, vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eShaderRead,
				vk::DependencyFlagBits::eByRegion
			)
		};

		return g_device->Get().createRenderPassUnique(vk::RenderPassCreateInfo(
			vk::RenderPassCreateFlags(),
			1, &depthAttachment,
			1, &subpass,
			static_cast<uint32_t>(dependencies.size()), dependencies.data()
		));
	}

	[[nodiscard]] GraphicsPipelineInfo GetGraphicsPipelineInfo(vk::RenderPass renderPass, vk::Extent2D extent)
	{
		GraphicsPipelineInfo info(renderPass, extent);
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

	[[nodiscard]] std::unique_ptr<Image> CreateDepthImage(vk::Extent2D extent)
	{
		return std::make_unique<Image>(
			extent.width, extent.height, 1UL,
			g_physicalDevice->FindDepthFormat(),
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageViewType::e2D,
			1, 1, // mipLevels, layerCount
			vk::SampleCountFlagBits::e1
		);
	}

	[[nodiscard]] vk::UniqueFramebuffer CreateFramebuffer(vk::RenderPass renderPass, vk::Extent2D extent, const vk::ImageView& imageView)
	{
		vk::FramebufferCreateInfo createInfo(
			vk::FramebufferCreateFlags(),
			renderPass,
			1, &imageView,
			extent.width, extent.height,
			1 // layers
		);
		return g_device->Get().createFramebufferUnique(createInfo);
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
		const PhongLight& light,
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

		// OpenGL -> Vulkan invert y, half z
		auto clip = glm::mat4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, -1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.5f, 0.0f,
			0.0f, 0.0f, 0.5f, 1.0f
		);

		return ViewProperties{
			shadowView, clip * proj, glm::vec3()
		};
	}
}

const AssetPath ShadowSystem::kVertexShaderFile("/Engine/Generated/Shaders/shadow_map_vert.spv");
const AssetPath ShadowSystem::kFragmentShaderFile("/Engine/Generated/Shaders/shadow_map_frag.spv");

ShadowSystem::ShadowSystem(
	vk::Extent2D extent,
	GraphicsPipelineCache& graphicsPipelineCache,
	BindlessDescriptors& bindlessDescriptors,
	BindlessDrawParams& bindlessDrawParams,
	MeshAllocator& meshAllocator,
	SceneTree& sceneTree,
	LightSystem& lightSystem
)
	: m_extent(extent)
	, m_graphicsPipelineCache(&graphicsPipelineCache)
	, m_meshAllocator(&meshAllocator)
	, m_sceneTree(&sceneTree)
	, m_lightSystem(&lightSystem)
	, m_bindlessDescriptors(&bindlessDescriptors)
	, m_bindlessDrawParams(&bindlessDrawParams)
{
	m_sampler = ::CreateSampler(vk::SamplerAddressMode::eClampToEdge);
	m_renderPass = ::CreateShadowMapRenderPass();
	m_drawParamsHandle = m_bindlessDrawParams->DeclareParams<ShadowMapDrawParams>();
}

void ShadowSystem::Reset(vk::Extent2D extent)
{
	m_extent = extent;

	m_graphicsPipelineCache->ResetGraphicsPipeline(
		m_graphicsPipelineID, ::GetGraphicsPipelineInfo(*m_renderPass, m_extent)
	);
	
	for (ShadowID id = 0; id < m_depthImages.size(); ++id)
		m_depthImages[id] = ::CreateDepthImage(m_extent);

	for (ShadowID id = 0; id < m_framebuffers.size(); ++id)
		m_framebuffers[id] = ::CreateFramebuffer(*m_renderPass, m_extent, m_depthImages[id]->GetImageView());
}

ShadowID ShadowSystem::CreateShadowMap(LightID lightID)
{
	ShadowID id = (ShadowID)m_lights.size();
	m_lights.push_back(id);
	m_shadowViews.push_back({});
	m_depthImages.push_back(::CreateDepthImage(m_extent));
	m_framebuffers.push_back(::CreateFramebuffer(*m_renderPass, m_extent, m_depthImages.back()->GetImageView()));
	TextureHandle textureHandle = m_bindlessDescriptors->StoreTexture(m_depthImages.back()->GetImageView(), m_sampler.get());
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
	
	m_shadowViewsBuffer = ::CreateStorageBuffer(m_shadowViews.size() * sizeof(m_shadowViews[0]));
	m_materialShadowsBuffer = ::CreateStorageBuffer(m_materialShadows.size() * sizeof(m_materialShadows[0]));
	m_materialShadowsBufferHandle = m_bindlessDescriptors->StoreBuffer(m_materialShadowsBuffer->Get(), vk::BufferUsageFlagBits::eStorageBuffer);

	m_drawParams.meshTransforms = m_sceneTree->GetTransformsBufferHandle();
	m_drawParams.shadowViews = m_bindlessDescriptors->StoreBuffer(m_shadowViewsBuffer->Get(), vk::BufferUsageFlagBits::eStorageBuffer);
	m_bindlessDrawParams->DefineParams(m_drawParamsHandle, m_drawParams);
}

void ShadowSystem::CreateGraphicsPipeline()
{
	ShaderCache& shaderCache = m_graphicsPipelineCache->GetShaderCache();
	ShaderID vertexShaderID = shaderCache.CreateShader(kVertexShaderFile.PathOnDisk());
	ShaderID fragmentShaderID = shaderCache.CreateShader(kFragmentShaderFile.PathOnDisk());
	ShaderInstanceID vertexShaderInstanceID = shaderCache.CreateShaderInstance(vertexShaderID);
	ShaderInstanceID fragmentShaderInstanceID = shaderCache.CreateShaderInstance(fragmentShaderID);
	m_graphicsPipelineID = m_graphicsPipelineCache->CreateGraphicsPipeline(
		vertexShaderInstanceID,
		fragmentShaderInstanceID,
		::GetGraphicsPipelineInfo(*m_renderPass, m_extent)
	);
}

void ShadowSystem::Update(const Camera& camera, BoundingBox sceneBoundingBox)
{
	BoundingBox sceneBox = m_sceneTree->ComputeWorldBoundingBox();

	for (ShadowID id = 0; id < m_lights.size(); ++id)
	{
		const PhongLight& light = m_lightSystem->GetLight(m_lights[id]);

		m_shadowViews[id] = ::ComputeShadowTransform(
			m_lightSystem->GetLight(id),
			camera,
			sceneBox,
			m_sceneTree->GetBoundingBoxes(),
			m_sceneTree->GetTransforms()
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

void ShadowSystem::Render(RenderCommandEncoder& renderCommandEncoder, const std::vector<MeshDrawInfo> drawCommands) const
{
	renderCommandEncoder.BindDrawParams(m_drawParamsHandle);

	vk::CommandBuffer commandBuffer = renderCommandEncoder.GetCommandBuffer();

	// Bind the one big vertex + index buffers
	m_meshAllocator->BindGeometry(commandBuffer);

	for (ShadowID id = 0; id < (ShadowID)m_framebuffers.size(); ++id)
	{
		vk::ClearValue clearValues;
		clearValues.depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
		auto renderPassInfo = vk::RenderPassBeginInfo(
			m_renderPass.get(), m_framebuffers[id].get(),
			vk::Rect2D(vk::Offset2D(0, 0), m_extent),
			1, &clearValues
		);
		commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
		{
			renderCommandEncoder.BindPipeline(m_graphicsPipelineID);

			// Shadow transforms
			vk::PipelineLayout pipelineLayout = m_graphicsPipelineCache->GetPipelineLayout(m_graphicsPipelineID);
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
		commandBuffer.endRenderPass();
	}
}

CombinedImageSampler ShadowSystem::GetCombinedImageSampler(ShadowID id) const
{
	return CombinedImageSampler{ m_depthImages[id].get(), m_sampler.get() };
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
