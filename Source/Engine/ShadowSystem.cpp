#include "ShadowSystem.h"

#include "Scene.h"

#include <utility>

namespace
{
	struct PushConstants
	{
		uint32_t shadowIndex = 0;
		uint32_t modelIndex = 0;
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

	void UpdateModelDescriptorSet(vk::Buffer buffer, size_t size, vk::DescriptorSet descriptorSet)
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

	[[nodiscard]] LitViewProperties ComputeShadowTransform(
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

		return LitViewProperties{
			shadowView, clip * proj, glm::vec3()
		};
	}
}

const AssetPath ShadowSystem::kVertexShaderFile("/Engine/Generated/Shaders/shadow_map_vert.spv");
const AssetPath ShadowSystem::kFragmentShaderFile("/Engine/Generated/Shaders/shadow_map_frag.spv");

ShadowSystem::ShadowSystem(
	vk::Extent2D extent,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	ModelSystem& modelSystem,
	LightSystem& lightSystem
)
	: m_extent(extent)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_modelSystem(&modelSystem)
	, m_lightSystem(&lightSystem)
{
	m_sampler = ::CreateSampler(vk::SamplerAddressMode::eClampToEdge);
	m_renderPass = ::CreateShadowMapRenderPass();
	CreateDescriptorPool();
}

ShadowSystem::~ShadowSystem()
{
	for (auto&& descriptorSet : m_descriptorSets)
		descriptorSet.reset();
	
	g_device->Get().destroyDescriptorPool(m_descriptorPool);
}

void ShadowSystem::CreateDescriptorPool()
{
	for (auto& descriptorSet : m_descriptorSets)
		descriptorSet.reset();

	// Set 0 (view):     { 1 buffer containing all shadow map transforms }
	// Set 1 (model):    { 1 buffer containing all model transforms }
	std::pair<vk::DescriptorType, uint16_t> descriptorCount[] = {
		std::make_pair(vk::DescriptorType::eStorageBuffer, 2),
	};
	const uint32_t descriptorCountSize = sizeof(descriptorCount) / sizeof(descriptorCount[0]);

	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.reserve(descriptorCountSize);
	for (const auto& descriptor : descriptorCount)
		poolSizes.emplace_back(descriptor.first, descriptor.second);

	uint32_t maxNbSets = 2; // View + Model
	m_descriptorPool = g_device->Get().createDescriptorPool(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		maxNbSets,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));
}

void ShadowSystem::Reset(vk::Extent2D extent)
{
	m_extent = extent;

	m_graphicsPipelineSystem->ResetGraphicsPipeline(
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
	m_properties.emplace_back();
	m_transforms.emplace_back();
	m_depthImages.push_back(::CreateDepthImage(m_extent));
	m_framebuffers.push_back(::CreateFramebuffer(*m_renderPass, m_extent, m_depthImages.back()->GetImageView()));
	return id;
}

void ShadowSystem::UploadToGPU()
{
	if (GetShadowCount() == 0)
	{
		return; // nothing to upload
	}

	CreateGraphicsPipeline();
	CreateDescriptorSets();

	m_viewPropertiesBuffer = ::CreateStorageBuffer(m_properties.size() * sizeof(m_properties[0]));
	m_transformsBuffer = ::CreateStorageBuffer(m_transforms.size() * sizeof(m_transforms[0]));

	::UpdateViewDescriptorSet(
		m_viewPropertiesBuffer->Get(), m_viewPropertiesBuffer->Size(),
		GetDescriptorSet(DescriptorSetIndex::View)
	);

	const UniqueBuffer& modelBuffer = m_modelSystem->GetBuffer();
	::UpdateModelDescriptorSet(
		modelBuffer.Get(), modelBuffer.Size(),
		GetDescriptorSet(DescriptorSetIndex::Model)
	);
}

void ShadowSystem::CreateGraphicsPipeline()
{
	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	ShaderID vertexShaderID = shaderSystem.CreateShader(kVertexShaderFile.PathOnDisk());
	ShaderID fragmentShaderID = shaderSystem.CreateShader(kFragmentShaderFile.PathOnDisk());
	ShaderInstanceID vertexShaderInstanceID = shaderSystem.CreateShaderInstance(vertexShaderID);
	ShaderInstanceID fragmentShaderInstanceID = shaderSystem.CreateShaderInstance(fragmentShaderID);
	m_graphicsPipelineID = m_graphicsPipelineSystem->CreateGraphicsPipeline(
		vertexShaderInstanceID,
		fragmentShaderInstanceID,
		::GetGraphicsPipelineInfo(*m_renderPass, m_extent)
	);
}

void ShadowSystem::CreateDescriptorSets()
{
	const int nbDescriptorSets = 2; // View + Model

	std::vector<vk::DescriptorSetLayout> layouts;
	layouts.reserve(nbDescriptorSets);
	for (DescriptorSetIndex setIndex : { DescriptorSetIndex::View, DescriptorSetIndex::Model })
	{
		layouts.push_back(m_graphicsPipelineSystem->GetDescriptorSetLayout(
			m_graphicsPipelineID, (uint8_t)setIndex
		));
	}

	auto descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		m_descriptorPool, static_cast<uint32_t>(layouts.size()), layouts.data()
	));

	for (int i = 0; i < nbDescriptorSets; ++i)
		m_descriptorSets[i] = std::move(descriptorSets[i]);
}

void ShadowSystem::Update(const Camera& camera, BoundingBox sceneBoundingBox)
{
	BoundingBox sceneBox = m_modelSystem->ComputeWorldBoundingBox();

	for (ShadowID id = 0; id < m_lights.size(); ++id)
	{
		const PhongLight& light = m_lightSystem->GetLight(m_lights[id]);

		m_properties[id] = ::ComputeShadowTransform(
			m_lightSystem->GetLight(id),
			camera,
			sceneBox,
			m_modelSystem->GetBoundingBoxes(),
			m_modelSystem->GetTransforms()
		);

		m_transforms[id] = m_properties[id].proj * m_properties[id].view;
	}

	// Write values to buffer
	{
		size_t writeSize = m_properties.size() * sizeof(m_properties[0]);
		memcpy(m_viewPropertiesBuffer->GetMappedData(), reinterpret_cast<const void*>(m_properties.data()), writeSize);
		m_viewPropertiesBuffer->Flush(0, writeSize);
	}
	{
		size_t writeSize = m_transforms.size() * sizeof(m_transforms[0]);
		memcpy(m_transformsBuffer->GetMappedData(), reinterpret_cast<const void*>(m_transforms.data()), writeSize);
		m_transformsBuffer->Flush(0, writeSize);
	}
}

void ShadowSystem::Render(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, const std::vector<MeshDrawInfo> drawCommands) const
{
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
			// Bind Pipeline
			commandBuffer.bindPipeline(
				vk::PipelineBindPoint::eGraphics,
				m_graphicsPipelineSystem->GetPipeline(m_graphicsPipelineID)
			);

			// Bind View
			uint8_t viewSetIndex = (uint8_t)DescriptorSetIndex::View;
			vk::PipelineLayout viewPipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(m_graphicsPipelineID, viewSetIndex);
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				viewPipelineLayout, (uint32_t)viewSetIndex,
				1, &m_descriptorSets[viewSetIndex].get(),
				0, nullptr
			);

			// Shadow transforms
			uint32_t shadowIndex = (uint32_t)id;
			commandBuffer.pushConstants(
				viewPipelineLayout,
				vk::ShaderStageFlagBits::eVertex,
				offsetof(PushConstants, shadowIndex), sizeof(PushConstants::shadowIndex), &shadowIndex
			);

			uint8_t modelSetIndex = (uint8_t)DescriptorSetIndex::Model;
			vk::PipelineLayout modelPipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(m_graphicsPipelineID, modelSetIndex);

			// Bind the one big vertex + index buffers
			m_modelSystem->BindGeometry(commandBuffer);

			// Bind the model transforms buffer
			commandBuffer.bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				modelPipelineLayout, (uint32_t)DescriptorSetIndex::Model,
				1, &m_descriptorSets[modelSetIndex].get(),
				0, nullptr
			);
			
			for (const auto& drawItem : drawCommands)
			{
				uint32_t modelIndex = drawItem.model;

				// Set model index push constant
				commandBuffer.pushConstants(
					modelPipelineLayout,
					vk::ShaderStageFlagBits::eVertex,
					offsetof(PushConstants, modelIndex), sizeof(PushConstants::modelIndex), &modelIndex
				);

				commandBuffer.drawIndexed(drawItem.mesh.nbIndices, 1, drawItem.mesh.indexOffset, 0, 0);
			}
		}
		commandBuffer.endRenderPass();
	}
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
