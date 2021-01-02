#include "Shader.h"

#include "Device.h"
#include "file_utils.h"
#include "hash.h"

namespace
{
	void PopulateVertexInputDescriptions(
		const spirv_cross::CompilerReflection& comp,
		const spirv_cross::VectorView<spirv_cross::Resource>& stageInputs,
		SmallVector<vk::VertexInputAttributeDescription>& attributeDescriptions,
		vk::VertexInputBindingDescription& bindingDescription)
	{
		attributeDescriptions.reserve(stageInputs.size());

		for (const auto& stageInput : stageInputs)
		{
			auto location = comp.get_decoration(stageInput.id, spv::Decoration::DecorationLocation);
			auto binding = comp.get_decoration(stageInput.id, spv::Decoration::DecorationBinding);
			auto format = spirv_vk::get_vk_format_from_variable(comp, stageInput.id);

			attributeDescriptions.push_back(vk::VertexInputAttributeDescription(
				location,
				binding,
				format,
				0
			));
		}

		// Sort by location
		std::sort(attributeDescriptions.begin(), attributeDescriptions.end(), [](const auto& a, const auto& b) {
			return a.location < b.location;
		});

		// Compute offsets
		uint32_t offset = 0;
		for (auto& attribute : attributeDescriptions)
		{
			attribute.offset = offset;
			offset += spirv_vk::sizeof_vkformat(attribute.format);
		}

		bindingDescription = { 0, offset, vk::VertexInputRate::eVertex }; // todo: support multiple bindings
	}

	void PopulateUniformBufferDescriptorSetLayouts(
		const spirv_cross::CompilerReflection& comp,
		const spirv_cross::VectorView<spirv_cross::Resource>& uniformBuffers,
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetLayouts)
	{
		for (const auto& ubo : uniformBuffers)
		{
			uint32_t set = comp.get_decoration(ubo.id, spv::Decoration::DecorationDescriptorSet);
			if (set >= descriptorSetLayouts.size())
				descriptorSetLayouts.resize(set + 1ULL);

			auto& descriptorSetLayout = descriptorSetLayouts[set];

			auto binding = comp.get_decoration(ubo.id, spv::Decoration::DecorationBinding);

			const auto& type = comp.get_type(ubo.type_id);

			descriptorSetLayout.emplace_back(
				binding, // binding
				vk::DescriptorType::eUniformBuffer,
				type.array.empty() ? 1U : type.array[0], // descriptorCount
				spirv_vk::execution_model_to_shader_stage(comp.get_execution_model())
			);

			// We only support 1D arrays for now
			ASSERT(type.array.empty() || type.array.size() == 1);
		}
	}

	void PopulateSampledImagesDescriptorSetLayouts(
		const spirv_cross::CompilerReflection& comp,
		const spirv_cross::VectorView<spirv_cross::Resource>& sampledImages,
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetLayouts)
	{
		for (const auto& sampler : sampledImages)
		{
			auto set = comp.get_decoration(sampler.id, spv::Decoration::DecorationDescriptorSet);
			if (descriptorSetLayouts.size() <= set)
				descriptorSetLayouts.resize(set + 1ULL);

			auto& descriptorSetLayout = descriptorSetLayouts[set];

			auto binding = comp.get_decoration(sampler.id, spv::Decoration::DecorationBinding);

			// to check if it's an array, e.g.: uniform sampler2D uSampler[10];
			const auto& type = comp.get_type(sampler.type_id);

			descriptorSetLayout.emplace_back(
				binding, // binding
				vk::DescriptorType::eCombinedImageSampler,
				type.array.empty() ? 1UL : type.array[0],
				spirv_vk::execution_model_to_shader_stage(comp.get_execution_model())
			);

			// We only support 1D arrays for now
			ASSERT(type.array.empty() || type.array.size() == 1);
		}
	}

	SmallVector<ShaderReflection::SpecializationConstantRef> PopulateSampledImagesSpecializationRefs(
		const spirv_cross::CompilerReflection& comp,
		const spirv_cross::VectorView<spirv_cross::Resource>& sampledImages)
	{
		SmallVector<ShaderReflection::SpecializationConstantRef> specializationRefs;

		for (const auto& sampler : sampledImages)
		{
			auto set = comp.get_decoration(sampler.id, spv::Decoration::DecorationDescriptorSet);
			auto binding = comp.get_decoration(sampler.id, spv::Decoration::DecorationBinding);
			const auto& type = comp.get_type(sampler.type_id);

			// Remember that this binding is a specialization constant
			// to replace it with the actual descriptor count when its known
			if ((bool)type.array_size_literal[0])
			{
				for (const auto& c : comp.get_specialization_constants())
				{
					if (c.constant_id == (spirv_cross::ConstantID)type.array[0])
					{
						// keep set, binding, constant_id
						specializationRefs.push_back({ set, binding, c.constant_id });
						break;
					}
				}
			}
		}

		return specializationRefs;
	}

	SmallVector<vk::SpecializationMapEntry> PopulateSpecializationMapEntries(const spirv_cross::CompilerReflection& comp)
	{
		auto constants = comp.get_specialization_constants();
		SmallVector<vk::SpecializationMapEntry> specializationMapEntries(constants.size());
		for (auto& c : constants)
		{
			const auto& constant = comp.get_constant(c.id);

			specializationMapEntries.emplace_back(
				c.constant_id,
				0,
				spirv_vk::sizeof_constant(comp, constant.constant_type)
			);
		}

		// Sort by constant_id
		std::sort(specializationMapEntries.begin(), specializationMapEntries.end(), [](const auto& a, const auto& b) {
			return a.constantID < b.constantID;
		});

		// Compute offsets
		uint32_t offset = 0;
		for (auto& entry : specializationMapEntries)
		{
			entry.offset = offset;
			offset += entry.size;
		}

		return specializationMapEntries;
	}

	void PopulatePushConstantRanges(
		const spirv_cross::CompilerReflection& comp,
		const spirv_cross::VectorView<spirv_cross::Resource>& pushConstantBuffers,
		SmallVector<vk::PushConstantRange>& pushConstantRanges)
	{
		uint32_t rangeSize = 0;
		uint32_t rangeOffset = (std::numeric_limits<uint32_t>::max)();
		for (int i = 0; i < pushConstantBuffers.size(); ++i)
		{
			auto& buffer = pushConstantBuffers[i];
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
			// There can only be one push constant block, but keep the vector in case this restriction is lifted
			// in the future.
			pushConstantRanges.emplace_back(
				spirv_vk::execution_model_to_shader_stage(comp.get_execution_model()),
				rangeOffset,
				rangeSize
			);
		}
	}

	void ReplaceSpecializationConstants(
		const void* data, size_t dataSize,
		const SmallVector<ShaderReflection::SpecializationConstantRef>& specializationRefs,
		const SmallVector<vk::SpecializationMapEntry>& specializationMapEntries,
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetLayouts
	)
	{
		// Replace placeholder array sizes with real values
		for (const auto& specRef : specializationRefs)
		{
			// Find matching specialization constant
			for (const auto& spec : specializationMapEntries)
			{
				if (spec.constantID == specRef.constantID)
				{
					ASSERT(spec.size == sizeof(uint32_t));

					// update descriptor count
					void* src = (char*)data + spec.offset;
					void* dest = &descriptorSetLayouts[specRef.set][specRef.binding].descriptorCount;
					memcpy(dest, src, sizeof(uint32_t));
				}
			}
		}
	}

	vk::UniqueShaderModule CreateShaderModule(const char* code, size_t codeSize) {
		return g_device->Get().createShaderModuleUnique(
			vk::ShaderModuleCreateInfo(
				vk::ShaderModuleCreateFlags(),
				codeSize,
				reinterpret_cast<const uint32_t*>(code)
			)
		);
	}
}

ShaderID ShaderSystem::CreateShader(const std::string& filename)
{
	return CreateShader(filename, "main");
}

ShaderID ShaderSystem::CreateShader(const std::string& filename, std::string entryPoint)
{
	uint64_t filenameID = fnv_hash(reinterpret_cast<const uint8_t*>(filename.c_str()), filename.size());
	auto shaderIt = m_filenameHashToShaderID.find(filenameID);
	if (shaderIt != m_filenameHashToShaderID.end())
		return shaderIt->second;

	auto code = file_utils::ReadFile(filename);
	auto shaderID = CreateShader(code.data(), code.size(), "main");
	auto [it, wasAdded] = m_filenameHashToShaderID.emplace(filenameID, shaderID);
	return shaderID;
}

ShaderID ShaderSystem::CreateShader(const char* data, size_t size, std::string entryPoint)
{
	ShaderID id = (ShaderID)m_modules.size();
	{
		m_modules.push_back(::CreateShaderModule(data, size));
		m_entryPoints.push_back(std::move(entryPoint));
		m_reflections.emplace_back(std::make_unique<ShaderReflection>((uint32_t*)data, size / sizeof(uint32_t)));
	}
	return id;
}

ShaderInstanceID ShaderSystem::CreateShaderInstance(ShaderID shaderID, SpecializationConstant specialization)
{
	if (specialization.size == 0)
		return CreateShaderInstance(shaderID);

	ShaderInstanceID id = m_instanceIDToShaderID.size();
	m_instanceIDToShaderID.push_back(shaderID);
	Entry entry = Entry::AppendToOutput(
		{ specialization.data, specialization.data + specialization.size },
		m_specializationBlock
	);
	m_specializations.push_back(std::move(entry));
	return id;
}

ShaderInstanceID ShaderSystem::CreateShaderInstance(ShaderID shaderID)
{
	ShaderInstanceID id = m_instanceIDToShaderID.size();
	m_instanceIDToShaderID.push_back(shaderID);
	m_specializations.push_back({}); // it's possible that the shader doesn't need specialiation constants
	return id;
}

vk::PipelineShaderStageCreateInfo ShaderSystem::GetShaderStageInfo(ShaderInstanceID id, vk::SpecializationInfo& specializationInfo) const
{
	ShaderID shaderID = m_instanceIDToShaderID[id];
	const Entry& specialization = m_specializations[id];
	const ShaderReflection& reflection = *m_reflections[shaderID];

	specializationInfo = {};
	specializationInfo.pData = specialization.size > 0 ? &m_specializationBlock[specialization.offset] : nullptr;
	specializationInfo.dataSize = specialization.size;
	specializationInfo.mapEntryCount = static_cast<uint32_t>(reflection.specializationMapEntries.size());
	specializationInfo.pMapEntries = reflection.specializationMapEntries.data();

	return vk::PipelineShaderStageCreateInfo(
		vk::PipelineShaderStageCreateFlags(),
		spirv_vk::execution_model_to_shader_stage(reflection.comp.get_execution_model()),
		m_modules[shaderID].get(),
		m_entryPoints[shaderID].c_str(),
		&specializationInfo
	);
}

vk::PipelineVertexInputStateCreateInfo ShaderSystem::GetVertexInputStateInfo(
	ShaderInstanceID id,
	SmallVector<vk::VertexInputAttributeDescription>& attributeDescriptions,
	vk::VertexInputBindingDescription& bindingDescription) const
{
	ShaderID shaderID = m_instanceIDToShaderID[id];
	const ShaderReflection& reflection = *m_reflections[shaderID];

	if (spirv_vk::execution_model_to_shader_stage(reflection.comp.get_execution_model()) == vk::ShaderStageFlagBits::eVertex)
	{
		attributeDescriptions.clear();
		::PopulateVertexInputDescriptions(
			reflection.comp,
			reflection.shaderResources.stage_inputs,
			attributeDescriptions,
			bindingDescription
		);
	}

	if (attributeDescriptions.size() == 0)
	{
		return vk::PipelineVertexInputStateCreateInfo(
			{}, 0, nullptr, 0, nullptr
		);
	}
	else
	{
		return vk::PipelineVertexInputStateCreateInfo(
			{},
			1, &bindingDescription,
			static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data()
		);
	}
}

SmallVector<vk::PushConstantRange> ShaderSystem::GetPushConstantRanges(ShaderInstanceID id) const
{
	ShaderID shaderID = m_instanceIDToShaderID[id];
	const ShaderReflection& reflection = *m_reflections[shaderID];

	SmallVector<vk::PushConstantRange> pushConstantRanges;

	::PopulatePushConstantRanges(
		reflection.comp,
		reflection.shaderResources.push_constant_buffers,
		pushConstantRanges
	);

	return pushConstantRanges;
}

SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> ShaderSystem::GetDescriptorSetLayoutBindings(ShaderInstanceID id) const
{
	ShaderID shaderID = m_instanceIDToShaderID[id];
	const ShaderReflection& reflection = *m_reflections[shaderID];
	const Entry& specialization = m_specializations[id];

	SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> descriptorSetLayouts;

	::PopulateUniformBufferDescriptorSetLayouts(
		reflection.comp,
		reflection.shaderResources.uniform_buffers,
		descriptorSetLayouts
	);

	::PopulateSampledImagesDescriptorSetLayouts(
		reflection.comp,
		reflection.shaderResources.sampled_images,
		descriptorSetLayouts
	);

	// Sort each layout by binding
	for (auto& descriptorSetLayout : descriptorSetLayouts)
	{
		std::sort(descriptorSetLayout.begin(), descriptorSetLayout.end(), [](const auto& a, const auto& b) {
			return a.binding < b.binding;
		});
	}

	if (specialization.size > 0)
	{
		::ReplaceSpecializationConstants(
			&m_specializationBlock[specialization.offset],
			specialization.size,
			reflection.specializationRefs,
			reflection.specializationMapEntries,
			descriptorSetLayouts
		);
	}

	return descriptorSetLayouts;
}
