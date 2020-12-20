#include "TexturedQuad.h"

TexturedQuad::TexturedQuad(
	CombinedImageSampler combinedImageSampler,
	const RenderPass& renderPass,
	vk::Extent2D swapchainExtent,
	vk::ImageLayout imageLayout
)
	: m_combinedImageSampler(combinedImageSampler)
	, m_imageLayout(imageLayout)
{
	ShaderID vertexShaderID = m_shaderSystem.CreateShader("textured_quad_vert.spv", "main");
	ShaderID fragmentShaderID = m_shaderSystem.CreateShader("textured_quad_frag.spv", "main");

	uint32_t isGrayscale = 1;
	m_vertexShader = m_shaderSystem.CreateShaderInstance(vertexShaderID);
	m_fragmentShader = m_shaderSystem.CreateShaderInstance(
		fragmentShaderID,
		imageLayout == vk::ImageLayout::eDepthStencilReadOnlyOptimal ? 
			SpecializationConstant::Create(isGrayscale) :
			SpecializationConstant{}
	);

	GraphicsPipelineInfo info;
	info.primitiveTopology = vk::PrimitiveTopology::eTriangleStrip;
	m_graphicsPipeline = std::make_unique<GraphicsPipeline>(
		renderPass.Get(),
		swapchainExtent,
		m_shaderSystem, m_vertexShader, m_fragmentShader,
		info
	);

	CreateDescriptorPool();
	CreateDescriptorSets();
	UpdateDescriptorSets();
}

void TexturedQuad::Reset(CombinedImageSampler combinedImageSampler, const RenderPass& renderPass, vk::Extent2D swapchainExtent)
{
	m_combinedImageSampler = combinedImageSampler;

	GraphicsPipelineInfo info;
	info.primitiveTopology = vk::PrimitiveTopology::eTriangleStrip;
	m_graphicsPipeline = std::make_unique<GraphicsPipeline>(
		renderPass.Get(), swapchainExtent,
		m_shaderSystem, m_vertexShader, m_fragmentShader,
		info
	);

	CreateDescriptorPool();
	CreateDescriptorSets();
	UpdateDescriptorSets();
}

void TexturedQuad::Draw(vk::CommandBuffer& commandBuffer)
{
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline->Get());

	vk::PipelineLayout layout = m_graphicsPipeline->GetPipelineLayout(0);

	commandBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		layout, 0,
		1, &m_descriptorSet.get(), 0, nullptr
	);

	commandBuffer.pushConstants(
		layout,
		vk::ShaderStageFlagBits::eVertex,
		0, sizeof(Properties), (const void*)&m_properties
	);

	commandBuffer.draw(4, 1, 0, 0);
}

void TexturedQuad::CreateDescriptorPool()
{
	m_descriptorSet.reset();
	m_descriptorPool.reset();

	// Count descriptors per type for pipeline
	std::map<vk::DescriptorType, uint32_t> descriptorCount;
	const auto& bindings = m_graphicsPipeline->GetDescriptorSetLayoutBindings(0);
	for (const auto& binding : bindings)
		descriptorCount[binding.descriptorType] += binding.descriptorCount;

	// Make sure that we can allocate these descriptors
	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.reserve(descriptorCount.size());
	for (const auto& descriptor : descriptorCount)
		poolSizes.emplace_back(descriptor.first, descriptor.second);

	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		1,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));
}

void TexturedQuad::CreateDescriptorSets()
{
	vk::DescriptorSetLayout viewSetLayouts = m_graphicsPipeline->GetDescriptorSetLayout(0);
	std::vector<vk::DescriptorSetLayout> layouts(1, viewSetLayouts);
	auto descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
	));
	m_descriptorSet = std::move(descriptorSets.front());
}

void TexturedQuad::UpdateDescriptorSets()
{
	uint32_t binding = 0;
	vk::DescriptorImageInfo imageInfo(
		m_combinedImageSampler.sampler,
		m_combinedImageSampler.texture->GetImageView(),
		m_imageLayout
			// vk::ImageLayout::eDepthStencilReadOnlyOptimal :
			// vk::ImageLayout::eShaderReadOnlyOptimal
	);

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet(
			m_descriptorSet.get(), binding++, {},
			1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr
		) // binding = 0
	};
	g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}
