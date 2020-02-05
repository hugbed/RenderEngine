#include "ShadowMap.h"

ShadowMap::ShadowMap(vk::Extent2D extent, const Light& light, ShaderCache& shaderCache, const Scene& scene)
	: m_extent(extent)
	, m_shaderCache(&shaderCache)
	, m_scene(&scene)
	, m_light(light)
{
	CreateDepthImage();
	CreateSampler();
	CreateRenderPass();
	CreateFramebuffer();
	CreateGraphicsPipeline();

	CreateDescriptorPool();
	CreateDescriptorSets();

	CreateViewUniformBuffers();
	UpdateViewUniforms();
	UpdateDescriptorSets();
}

void ShadowMap::Reset(vk::Extent2D extent)
{
	m_extent = extent;

	CreateDepthImage();
	CreateFramebuffer();
	CreateGraphicsPipeline();

	CreateDescriptorPool();
	CreateDescriptorSets();
	UpdateViewUniforms();
	UpdateDescriptorSets();
}

void ShadowMap::Render(vk::CommandBuffer& commandBuffer, uint32_t frameIndex) const
{
	vk::ClearValue clearValues;
	clearValues.depthStencil = { 1.0f, 0 };
	auto renderPassInfo = vk::RenderPassBeginInfo(
		m_renderPass.get(), m_framebuffer.get(),
		vk::Rect2D(vk::Offset2D(0, 0), m_extent),
		1, &clearValues
	);
	commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	{
		RenderState renderState;

		renderState.BindPipeline(commandBuffer, m_graphicsPipeline.get());

		renderState.BindView(commandBuffer, ShadingModel::Unlit, m_viewDescriptorSet.get());

		m_scene->DrawAllWithoutShading(commandBuffer, frameIndex, renderState);
	}
	commandBuffer.endRenderPass();
}

void ShadowMap::CreateDepthImage()
{
	m_depthImage = std::make_unique<Image>(
		m_extent.width, m_extent.height, 1UL,
		g_physicalDevice->FindDepthFormat(),
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
		vk::ImageAspectFlagBits::eDepth,
		vk::ImageViewType::e2D,
		1, 1, // mipLevels, layerCount
		vk::SampleCountFlagBits::e1
	);
}

void ShadowMap::CreateSampler()
{
	auto addressMode = vk::SamplerAddressMode::eClampToEdge;
	m_sampler = g_device->Get().createSamplerUnique(
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

void ShadowMap::CreateRenderPass()
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

	m_renderPass = g_device->Get().createRenderPassUnique(vk::RenderPassCreateInfo(
		vk::RenderPassCreateFlags(),
		1, &depthAttachment,
		1, &subpass,
		static_cast<uint32_t>(dependencies.size()), dependencies.data()
	));
}

void ShadowMap::CreateFramebuffer()
{
	m_framebuffer = g_device->Get().createFramebufferUnique(vk::FramebufferCreateInfo(
		vk::FramebufferCreateFlags(),
		m_renderPass.get(),
		1, &m_depthImage->GetImageView(),
		m_extent.width, m_extent.height,
		1 // layers
	));
}

void ShadowMap::CreateGraphicsPipeline()
{
	const auto& vertexShader = m_shaderCache->Load(vertexShaderFile);
	const auto& fragmentShader = m_shaderCache->Load(fragmentShaderFile);

	GraphicsPipelineInfo info;
	info.sampleCount = vk::SampleCountFlagBits::e1;

	// Use fron culling to prevent peter-panning
	// note that this prevents from rendering shadows
	// for meshes that don't have a back face (e.g. a plane)
	// eBack could be used in this case.
	info.cullMode = vk::CullModeFlagBits::eFront;

	m_graphicsPipeline = std::make_unique<GraphicsPipeline>(
		*m_renderPass, m_extent,
		vertexShader, fragmentShader,
		info
	);
}

void ShadowMap::CreateDescriptorPool()
{
	m_viewDescriptorSet.reset();
	m_descriptorPool.reset();

	std::map<vk::DescriptorType, uint32_t> descriptorCount;
	const auto& bindings = m_graphicsPipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::View);
	for (const auto& binding : bindings)
		descriptorCount[binding.descriptorType] += binding.descriptorCount;

	uint32_t maxNbSets = 0;
	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.reserve(descriptorCount.size());
	for (const auto& descriptor : descriptorCount)
	{
		poolSizes.emplace_back(descriptor.first, descriptor.second);
		maxNbSets += descriptor.second;
	}

	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		maxNbSets,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));
}

void ShadowMap::CreateDescriptorSets()
{
	size_t set = (size_t)DescriptorSetIndices::View;
	vk::DescriptorSetLayout viewSetLayouts = m_graphicsPipeline->GetDescriptorSetLayout(set);
	std::vector<vk::DescriptorSetLayout> layouts(1, viewSetLayouts);
	auto descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
	));
	m_viewDescriptorSet = std::move(descriptorSets.front());
}

void ShadowMap::CreateViewUniformBuffers()
{
	m_viewUniformBuffer = std::make_unique<UniqueBuffer>(
		vk::BufferCreateInfo(
			{},
			sizeof(ViewUniforms),
			vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc // todo: needs eTransferSrc?
		), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
	);
}

void ShadowMap::UpdateViewUniforms()
{
	// --- Update values --- //

	m_viewUniforms = {};

#ifdef SHADOWMAP_CAM_FRUSTRUM_CULLING
	// todo: fit light matrix to only the relevant
	// shadow casters/receivers. We'd need to also take
	// into account shadow casters outside cam frustrum
	// casting shadow into the camera frustrum.

	// Compute camera frustrum corners
	std::vector<glm::vec3> camFrustrumPts = m_scene->GetCamera().ComputeFrustrumCorners();

	// Simplify to bounding box in world coordinates
	BoundingBox box_world = BoundingBox::FromPoints(camFrustrumPts);

	// Find bounding box of scene objects inside view frustrum
	box_world = box_world.Intersection(m_scene->GetBoundingBox());
#else
	// Until then, capture all the scene in the shadow map
	BoundingBox box_world = m_scene->GetBoundingBox();
#endif

	// Center the light in the middle of this box.
	// Computing the projection matrix will then only require
	// to compute the scaling matrix to bring this box between -1 and 1.
	glm::vec3 center = box_world.min + (box_world.max - box_world.min)/2.0f;

	// Choose a right vector to find the up vector
	glm::vec3 right(0.0f, 0.0f, 1.0f);
	if (std::abs(glm::dot(m_light.direction, right)) > 0.9999f)
		right = glm::vec3(1.0f, 0.0f, 0.0f);

	// Use this center and the light direction for the view matrix
	glm::vec3 l_eye = center;
	glm::vec3 l_center = center + m_light.direction;
	glm::vec3 l_up = glm::cross(m_light.direction, right);
	m_viewUniforms.view = glm::lookAt(
		l_eye, l_center, l_up
	);

	// The projection matrix maps this box in light space between -1.0 and 1.0
	BoundingBox box_light = m_viewUniforms.view * box_world;
	m_viewUniforms.proj = glm::ortho(
		box_light.min.x, box_light.max.x,
		box_light.min.y, box_light.max.y,
		box_light.min.z, box_light.max.z
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
	m_viewUniforms.proj = clip * m_viewUniforms.proj;

	// Write values to uniform buffer
	memcpy(m_viewUniformBuffer->GetMappedData(), reinterpret_cast<const void*>(&m_viewUniforms), sizeof(ViewUniforms));
}

void ShadowMap::UpdateDescriptorSets()
{
	uint32_t binding = 0;
	vk::DescriptorBufferInfo descriptorBufferInfoView(m_viewUniformBuffer->Get(), 0, sizeof(ViewUniforms));
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet(
			m_viewDescriptorSet.get(), binding++, {},
			1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoView
		) // binding = 0
	};
	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}
