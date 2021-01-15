#include "ShadowMap.h"

#include <utility>

namespace
{
	enum class ConstantIDs
	{
		// Fragment
		eNbModels = 0,
	};

	SmallVector<vk::SpecializationMapEntry> GetSpecializationMapEntries()
	{
		using VSConst = ShadowMap::VertexShaderConstants;

		return SmallVector<vk::SpecializationMapEntry>{
			vk::SpecializationMapEntry((uint32_t)ConstantIDs::eNbModels, offsetof(VSConst, nbModels), sizeof(VSConst::nbModels))
		};
	}
}

ShadowMap::ShadowMap(
	vk::Extent2D extent,
	const PhongLight& light,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	const Scene& scene,
	VertexShaderConstants constants
)
	: m_extent(extent)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_scene(&scene)
	, m_light(light)
	, m_constants(constants)
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
	clearValues.depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
	auto renderPassInfo = vk::RenderPassBeginInfo(
		m_renderPass.get(), m_framebuffer.get(),
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
			viewPipelineLayout, (uint32_t)DescriptorSetIndex::View,
			1, &m_descriptorSets[viewSetIndex].get(),
			0, nullptr
		);

		uint8_t modelSetIndex = (uint8_t)DescriptorSetIndex::Model;
		vk::PipelineLayout modelPipelineLayout = m_graphicsPipelineSystem->GetPipelineLayout(m_graphicsPipelineID, modelSetIndex);
		m_scene->DrawAllWithoutShading(commandBuffer, frameIndex, modelPipelineLayout, m_descriptorSets[modelSetIndex].get());
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
	ShaderSystem& shaderSystem = m_graphicsPipelineSystem->GetShaderSystem();
	
	ShaderID vertexShaderID = shaderSystem.CreateShader(vertexShaderFile);
	ShaderID fragmentShaderID = shaderSystem.CreateShader(fragmentShaderFile);

	ShaderInstanceID vertexShaderInstanceID = shaderSystem.CreateShaderInstance(
		vertexShaderID, (const void*)&m_constants, GetSpecializationMapEntries()
	);
	ShaderInstanceID fragmentShaderInstanceID = shaderSystem.CreateShaderInstance(
		fragmentShaderID
	);

	GraphicsPipelineInfo info(*m_renderPass, m_extent);
	info.sampleCount = vk::SampleCountFlagBits::e1;

	// Use front culling to prevent peter-panning
	// note that this prevents from rendering shadows
	// for meshes that don't have a back face (e.g. a plane)
	// eBack could be used in this case.
	info.cullMode = vk::CullModeFlagBits::eFront;
	m_graphicsPipelineID = m_graphicsPipelineSystem->CreateGraphicsPipeline(
		vertexShaderInstanceID, fragmentShaderInstanceID, info
	);
}

void ShadowMap::CreateDescriptorPool()
{
	for (auto& descriptorSet : m_descriptorSets)
		descriptorSet.reset();

	m_descriptorPool.reset();

	// Set 0 (view):     { 1 uniform buffer for view uniforms }
	// Set 1 (model):    { 1 uniform buffer containing all models }
	std::pair<vk::DescriptorType, uint16_t> descriptorCount[] = {
		std::make_pair(vk::DescriptorType::eUniformBuffer, 2),
	};
	const uint32_t descriptorCountSize = sizeof(descriptorCount) / sizeof(descriptorCount[0]);

	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.reserve(descriptorCountSize);
	for (const auto& descriptor : descriptorCount)
		poolSizes.emplace_back(descriptor.first, descriptor.second);

	uint32_t maxNbSets = 2; // View + Model
	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		maxNbSets,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));
}

void ShadowMap::CreateDescriptorSets()
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
		m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
	));

	for (int i = 0; i < nbDescriptorSets; ++i)
		m_descriptorSets[i] = std::move(descriptorSets[i]);
}

void ShadowMap::CreateViewUniformBuffers()
{
	m_viewUniformBuffer = std::make_unique<UniqueBuffer>(
		vk::BufferCreateInfo(
			{},
			sizeof(LitViewProperties),
			vk::BufferUsageFlagBits::eUniformBuffer
		), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
	);
}

void ShadowMap::UpdateViewUniforms()
{
	// Compute camera frustrum corners
	std::vector<glm::vec3> camFrustrumPts = m_scene->GetCamera().ComputeFrustrumCorners();

	// Simplify to bounding box in world coordinates
	BoundingBox camBox_world = BoundingBox::FromPoints(camFrustrumPts);

	// Choose a right vector to find the up vector
	glm::vec3 right(0.0f, 0.0f, 1.0f);
	if (std::abs(glm::dot(m_light.direction, right)) > 0.9999f)
		right = glm::vec3(1.0f, 0.0f, 0.0f);

	// Use the light direction for the view matrix. View doesn't need to be centered in the box.
	// If it's not, the orthographic projection will take translation into account.
	glm::mat4 view = glm::lookAt(
		glm::vec3(0.0f, 0.0f, 0.0f),
		m_light.direction,
		glm::cross(m_light.direction, right)
	);

	// Transform to light space and fit to scene objects
	BoundingBox camBox_view = view * camBox_world;
	BoundingBox sceneBox_view = view * m_scene->GetBoundingBox();

	// Make sure to keep all objects that can cast shadow into the camera frustrum.
	// Light is looking at -z so bring near plane to the fartest object in the light direction (-z).
	camBox_view.max.z = sceneBox_view.max.z;

	// else tightly fix objects from scene inside the frustrum (left, right and far planes)
	camBox_view.min = (glm::max)(camBox_view.min, sceneBox_view.min);
	camBox_view.max.x = (std::min)(camBox_view.max.x, sceneBox_view.max.x);
	camBox_view.max.y = (std::min)(camBox_view.max.y, sceneBox_view.max.y);

	// The projection matrix maps this box in light space between -1.0 and 1.0
	glm::mat4 proj = glm::ortho(
		camBox_view.min.x, camBox_view.max.x,
		camBox_view.min.y, camBox_view.max.y,
		camBox_view.min.z, camBox_view.max.z
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

	m_viewUniforms = {};
	m_viewUniforms.view = view;
	m_viewUniforms.proj = clip * proj;

	// Write values to uniform buffer
	memcpy(m_viewUniformBuffer->GetMappedData(), reinterpret_cast<const void*>(&m_viewUniforms), sizeof(LitViewProperties));

	m_viewUniformBuffer->Flush(0, sizeof(LitViewProperties));
}

void ShadowMap::UpdateDescriptorSets()
{
	uint32_t binding = 0;
	vk::DescriptorBufferInfo descriptorBufferInfoView(m_viewUniformBuffer->Get(), 0, sizeof(LitViewProperties));
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet(
			m_descriptorSets[(size_t)DescriptorSetIndex::View].get(), binding++, {},
			1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoView
		) // binding = 0
	};
	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}
