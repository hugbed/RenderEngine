#include "Shader.h"

#include "Device.h"

#include "spirv_vk.h"

#include <fstream>

namespace file_utils
{
	// todo: move to file utils class or something
	static std::vector<char> ReadFile(const std::string& filename) {
		std::ifstream file(filename, std::ios::ate | std::ios::binary);
		if (!file.is_open())
			throw std::runtime_error("failed to open file!");

		size_t fileSize = (size_t)file.tellg();
		std::vector<char> buffer(fileSize);
		file.seekg(0);
		file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
		file.close();

		return buffer;
	}
}

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

	m_descriptorSetLayouts.reserve(shaderResources.uniform_buffers.size() + shaderResources.sampled_images.size());

	// Uniform buffers
	for (const auto& ubo : shaderResources.uniform_buffers)
	{
		auto binding = comp.get_decoration(ubo.id, spv::Decoration::DecorationBinding);

		const auto& type = comp.get_type(ubo.type_id);

		m_descriptorSetLayouts.push_back(vk::DescriptorSetLayoutBinding(
			binding, // binding
			vk::DescriptorType::eUniformBuffer,
			type.array.empty() ? 1ULL : type.array.size(), // descriptorCount
			m_shaderStage
		));
	}

	// Sampler2D
	for (const auto& sampler : shaderResources.sampled_images)
	{
		auto binding = comp.get_decoration(sampler.id, spv::Decoration::DecorationBinding);

		// to check if it's an array, e.g.: uniform sampler2D uSampler[10];
		const auto& type = comp.get_type(sampler.type_id);

		m_descriptorSetLayouts.push_back(vk::DescriptorSetLayoutBinding(
			binding, // binding
			vk::DescriptorType::eCombinedImageSampler,
			type.array.empty() ? 1ULL : type.array.size(), // descriptorCount
			m_shaderStage
		));
	}

	// Sort by binding
	std::sort(m_descriptorSetLayouts.begin(), m_descriptorSetLayouts.end(), [](const auto& a, const auto& b) {
		return a.binding < b.binding;
	});

	if (shaderResources.separate_images.empty() == false ||
		shaderResources.storage_images.empty() == false ||
		shaderResources.separate_samplers.empty() == false ||
		shaderResources.push_constant_buffers.empty() == false)
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
		m_entryPoint.c_str()
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
