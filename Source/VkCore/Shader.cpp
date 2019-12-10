#include "Shader.h"

#include "Device.h"
#include "file_utils.h"

#include "spirv_vk.h"

Shader::Shader(const std::string& filename, std::string entryPoint)
	: m_entryPoint(std::move(entryPoint))
{
	auto code = file_utils::ReadFile(filename);
	m_shaderModule = CreateShaderModule(code);

	// --- Find shader resources using SPIRV reflection --- //

	spirv_cross::CompilerReflection comp((uint32_t*)code.data(), code.size() / sizeof(uint32_t));

	spirv_cross::ShaderResources shaderResources = comp.get_shader_resources();

	/* Vertex Input Attribute Description */

	m_attributeDescriptions.reserve(shaderResources.stage_inputs.size());
	for (const auto& stageInput : shaderResources.stage_inputs)
	{
		auto location = comp.get_decoration(stageInput.id, spv::Decoration::DecorationLocation);
		auto binding = comp.get_decoration(stageInput.id, spv::Decoration::DecorationBinding);
		auto format = spirv_vk::get_vk_format_from_variable(comp, stageInput.id);

		m_attributeDescriptions.push_back(vk::VertexInputAttributeDescription(
			location, // location
			binding,
			format,
			0
		));
	}

	// Sort by location
	std::sort(m_attributeDescriptions.begin(), m_attributeDescriptions.end(), [](const auto& a, const auto& b) {
		return a.location < b.location;
	});

	// Compute offsets
	uint32_t offset = 0;
	for (auto& attribute : m_attributeDescriptions)
	{
		attribute.offset = offset;
		offset += spirv_vk::sizeof_vkformat(attribute.format);
	}

	m_bindingDescription = { 0, offset, vk::VertexInputRate::eVertex }; // todo: support multiple bindings

	/* Descriptor Set Layouts */

	m_shaderStage = spirv_vk::execution_model_to_shader_stage(comp.get_execution_model());

	// Keep track of descriptor layout for each set (0, 1, 2, ...)

	// Uniform buffers
	for (const auto& ubo : shaderResources.uniform_buffers)
	{
		auto set = comp.get_decoration(ubo.id, spv::Decoration::DecorationDescriptorSet);
		if (set >= m_descriptorSetLayouts.size())
			m_descriptorSetLayouts.resize(set + 1);
		
		auto& descriptorSetLayout = m_descriptorSetLayouts[set];

		auto binding = comp.get_decoration(ubo.id, spv::Decoration::DecorationBinding);

		const auto& type = comp.get_type(ubo.type_id);

		descriptorSetLayout.push_back(vk::DescriptorSetLayoutBinding(
			binding, // binding
			vk::DescriptorType::eUniformBuffer,
			type.array.empty() ? 1ULL : type.array.size(), // descriptorCount
			m_shaderStage
		));
	}

	// Sampler2D
	for (const auto& sampler : shaderResources.sampled_images)
	{
		auto set = comp.get_decoration(sampler.id, spv::Decoration::DecorationDescriptorSet);
		if (m_descriptorSetLayouts.size() < set)
			m_descriptorSetLayouts.resize(set + 1);

		auto& descriptorSetLayout = m_descriptorSetLayouts[set];

		auto binding = comp.get_decoration(sampler.id, spv::Decoration::DecorationBinding);

		// to check if it's an array, e.g.: uniform sampler2D uSampler[10];
		const auto& type = comp.get_type(sampler.type_id);

		descriptorSetLayout.push_back(vk::DescriptorSetLayoutBinding(
			binding, // binding
			vk::DescriptorType::eCombinedImageSampler,
			type.array.empty() ? 1ULL : type.array.size(), // descriptorCount
			m_shaderStage
		));
	}

	// Sort each layout by binding
	for (auto& descriptorSetLayout : m_descriptorSetLayouts)
	{
		std::sort(descriptorSetLayout.begin(), descriptorSetLayout.end(), [](const auto& a, const auto& b) {
			return a.binding < b.binding;
		});
	}

	//  ---- Specialization constants --- //

	auto constants = comp.get_specialization_constants();
	m_specializationMapEntries.reserve(constants.size());
	for (auto& c : constants)
	{
		const auto& constant = comp.get_constant(c.id);

		m_specializationMapEntries.push_back(vk::SpecializationMapEntry(
			c.constant_id,
			0,
			spirv_vk::sizeof_constant(comp, constant.constant_type)
		));
	}

	// Sort by constant_id
	std::sort(m_specializationMapEntries.begin(), m_specializationMapEntries.end(), [](const auto& a, const auto& b) {
		return a.constantID < b.constantID;
	});

	// Compute offsets
	offset = 0;
	for (auto& entry : m_specializationMapEntries)
	{
		entry.offset = offset;
		offset += entry.size;
	}

	// ---- Push Constants ---- //

	size_t rangeSize = 0;
	size_t rangeOffset = (std::numeric_limits<size_t>::max)();
	for (int i = 0; i < shaderResources.push_constant_buffers.size(); ++i)
	{
		auto& buffer = shaderResources.push_constant_buffers[i];
		auto& type = comp.get_type(buffer.base_type_id);

		for (uint32_t i = 0; i < type.member_types.size(); ++i)
		{
			size_t memberSize = comp.get_declared_struct_member_size(type, i);
			size_t memberOffset = comp.type_struct_member_offset(type, i);
			rangeOffset = (std::min)(rangeOffset, memberOffset);
			rangeSize += memberSize;
		}
	}
	if (rangeSize > 0)
	{
		m_pushConstantRanges.push_back(vk::PushConstantRange(
			m_shaderStage,
			rangeOffset,
			rangeSize
		));
	}

	if (shaderResources.separate_images.empty() == false ||
		shaderResources.storage_images.empty() == false ||
		shaderResources.separate_samplers.empty() == false)
	{
		throw std::runtime_error("Reflection not implemented for shader resource, please implement.");
	}

	// todo: support { sampler_buffer array, storage_images + storage_images array + separate_samplers }
}

vk::UniqueShaderModule Shader::CreateShaderModule(const std::vector<char>& code) {
	return g_device->Get().createShaderModuleUnique(
		vk::ShaderModuleCreateInfo(
			vk::ShaderModuleCreateFlags(),
			code.size(),
			reinterpret_cast<const uint32_t*>(code.data())
		)
	);
}

vk::PipelineShaderStageCreateInfo Shader::GetShaderStageInfo() const
{
	return vk::PipelineShaderStageCreateInfo(
		vk::PipelineShaderStageCreateFlags(),
		m_shaderStage,
		m_shaderModule.get(),
		m_entryPoint.c_str(),
		&m_specializationInfo
	);
}

vk::PipelineVertexInputStateCreateInfo Shader::GetVertexInputStateInfo() const
{
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo(
		{},
		1, & m_bindingDescription,
		static_cast<uint32_t>(m_attributeDescriptions.size()), m_attributeDescriptions.data()
	);
	return vertexInputInfo;
}

void Shader::SetSpecializationConstants(const void* data, size_t size)
{
	m_specializationInfo = {};
	m_specializationInfo.dataSize = size;
	m_specializationInfo.mapEntryCount = static_cast<uint32_t>(m_specializationMapEntries.size());
	m_specializationInfo.pMapEntries = m_specializationMapEntries.data();
	m_specializationInfo.pData = data;
}
