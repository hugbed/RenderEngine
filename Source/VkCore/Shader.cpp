#include "Shader.h"

#include "Device.h"
#include "file_utils.h"

#include "spirv_vk.h"

Shader::Shader(const std::string& filename, std::string entryPoint)
	: m_entryPoint(std::move(entryPoint))
{
	auto code = file_utils::ReadFile(filename);
	m_shaderModule = CreateShaderModule(code);

	// Find shader resources using SPIRV reflection
	spirv_cross::CompilerReflection comp((uint32_t*)code.data(), code.size() / sizeof(uint32_t));
	spirv_cross::ShaderResources shaderResources = comp.get_shader_resources();

	// --- Vertex Input Attribute Description --- //
	
	m_shaderStage = spirv_vk::execution_model_to_shader_stage(comp.get_execution_model());

	if (m_shaderStage == vk::ShaderStageFlagBits::eVertex)
	{
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
	}

	// --- Descriptor Set Layouts  --- //

	// Keep track of descriptor layout for each set (0, 1, 2, ...)

	// Uniform buffers
	for (const auto& ubo : shaderResources.uniform_buffers)
	{
		auto set = comp.get_decoration(ubo.id, spv::Decoration::DecorationDescriptorSet);
		if (set >= m_descriptorSetLayouts.size())
			m_descriptorSetLayouts.resize(set + 1UL);
		
		auto& descriptorSetLayout = m_descriptorSetLayouts[set];

		auto binding = comp.get_decoration(ubo.id, spv::Decoration::DecorationBinding);

		const auto& type = comp.get_type(ubo.type_id);

		descriptorSetLayout.emplace_back(
			binding, // binding
			vk::DescriptorType::eUniformBuffer,
			type.array.empty() ? 1UL : type.array[0], // descriptorCount
			m_shaderStage
		);

		// We only support 1D arrays for now
		ASSERT(type.array.empty() || type.array.size() == 1);
	}

	// Sampler2D
	for (const auto& sampler : shaderResources.sampled_images)
	{
		auto set = comp.get_decoration(sampler.id, spv::Decoration::DecorationDescriptorSet);
		if (m_descriptorSetLayouts.size() <= set)
			m_descriptorSetLayouts.resize(set + 1);

		auto& descriptorSetLayout = m_descriptorSetLayouts[set];

		auto binding = comp.get_decoration(sampler.id, spv::Decoration::DecorationBinding);

		// to check if it's an array, e.g.: uniform sampler2D uSampler[10];
		const auto& type = comp.get_type(sampler.type_id);

		descriptorSetLayout.emplace_back(
			binding, // binding
			vk::DescriptorType::eCombinedImageSampler,
			type.array.empty() ? 1UL : type.array[0],
			m_shaderStage
		);

		// remember that this binding is a specialization constant
		// to replace it later
		if ((bool)type.array_size_literal[0])
		{
			for (const auto& c : comp.get_specialization_constants())
			{
				if (c.constant_id == (spirv_cross::ConstantID)type.array[0])
				{
					// keep set, binding, constant_id
					m_specializationRef.push_back({ set, binding, c.constant_id });
					break;
				}
			}
		}

		// We only support 1D arrays for now
		ASSERT(type.array.empty() || type.array.size() == 1);
	}

	// Sort each layout by binding
	for (auto& descriptorSetLayout : m_descriptorSetLayouts)
	{
		std::sort(descriptorSetLayout.begin(), descriptorSetLayout.end(), [](const auto& a, const auto& b) {
			return a.binding < b.binding;
		});
	}

	// ---- Specialization constants --- //

	auto constants = comp.get_specialization_constants();
	m_specializationMapEntries.reserve(constants.size());
	for (auto& c : constants)
	{
		const auto& constant = comp.get_constant(c.id);

		m_specializationMapEntries.emplace_back(
			c.constant_id,
			0,
			spirv_vk::sizeof_constant(comp, constant.constant_type)
		);
	}

	// Sort by constant_id
	std::sort(m_specializationMapEntries.begin(), m_specializationMapEntries.end(), [](const auto& a, const auto& b) {
		return a.constantID < b.constantID;
	});

	// Compute offsets
	uint32_t offset = 0;
	for (auto& entry : m_specializationMapEntries)
	{
		entry.offset = offset;
		offset += entry.size;
	}

	// ---- Push Constants ---- //

	uint32_t rangeSize = 0;
	uint32_t rangeOffset = (std::numeric_limits<uint32_t>::max)();
	for (int i = 0; i < shaderResources.push_constant_buffers.size(); ++i)
	{
		auto& buffer = shaderResources.push_constant_buffers[i];
		auto& type = comp.get_type(buffer.base_type_id);

		for (uint32_t i = 0; i < type.member_types.size(); ++i)
		{
			auto memberSize = (uint32_t)comp.get_declared_struct_member_size(type, i);
			auto memberOffset = (uint32_t)comp.type_struct_member_offset(type, i);
			rangeOffset = (std::min)(rangeOffset, memberOffset);
			rangeSize += memberSize;
		}
	}
	if (rangeSize > 0)
	{
		m_pushConstantRanges.emplace_back(
			m_shaderStage,
			rangeOffset,
			rangeSize
		);
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
	if (m_attributeDescriptions.size() == 0)
	{
		return vk::PipelineVertexInputStateCreateInfo(
			{}, 0, nullptr,	0, nullptr
		);
	}
	else
	{
		return vk::PipelineVertexInputStateCreateInfo(
			{},
			1, &m_bindingDescription,
			static_cast<uint32_t>(m_attributeDescriptions.size()), m_attributeDescriptions.data()
		);
	}
}

void Shader::SetSpecializationConstants(const void* data, size_t size)
{
	// Replace placeholder array sizes with real values
	for (const auto& specRef : m_specializationRef)
	{
		// Find matching specialization constant
		for (const auto& spec : m_specializationMapEntries)
		{
			if (spec.constantID == specRef.constantID)
			{
				ASSERT(spec.size == sizeof(uint32_t));

				// update descriptor count
				void* src = (char*)data + spec.offset;
				void* dest = &m_descriptorSetLayouts[specRef.set][specRef.binding].descriptorCount;
				memcpy(dest, src, sizeof(uint32_t));
			}
		}
	}

	m_specializationInfo = {};
	m_specializationInfo.dataSize = size;
	m_specializationInfo.mapEntryCount = static_cast<uint32_t>(m_specializationMapEntries.size());
	m_specializationInfo.pMapEntries = m_specializationMapEntries.data();
	m_specializationInfo.pData = data;
}
