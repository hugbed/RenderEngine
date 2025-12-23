#include <RHI/ShaderSystem.h>

#include <RHI/Device.h>
#include <file_utils.h>
#include <hash.h>
#include <gsl/span>

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

	void PopulateBufferDescriptorSetLayoutBindings(
		vk::DescriptorType descriptorType,
		const spirv_cross::CompilerReflection& comp,
		const spirv_cross::VectorView<spirv_cross::Resource>& buffers, // uniform_buffers or storage_buffers 
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetLayoutsBindings)
	{
		for (const auto& buffer : buffers)
		{
			uint32_t set = comp.get_decoration(buffer.id, spv::Decoration::DecorationDescriptorSet);
			if (set >= descriptorSetLayoutsBindings.size())
				descriptorSetLayoutsBindings.resize(set + 1ULL);

			auto& bindings = descriptorSetLayoutsBindings[set];

			auto binding = comp.get_decoration(buffer.id, spv::Decoration::DecorationBinding);

			const auto& type = comp.get_type(buffer.type_id);

			bindings.emplace_back(
				binding, // binding
				descriptorType, // uniform/storage buffer
				type.array.empty() ? 1U : type.array[0], // descriptorCount
				spirv_vk::execution_model_to_shader_stage(comp.get_execution_model())
			);

			// We only support 1D arrays for now
			ASSERT(type.array.empty() || type.array.size() == 1);
		}
	}

	void PopulateSampledImagesDescriptorSetLayoutBindings(
		const spirv_cross::CompilerReflection& comp,
		const spirv_cross::VectorView<spirv_cross::Resource>& sampledImages,
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetLayoutBindings)
	{
		for (const auto& sampler : sampledImages)
		{
			auto set = comp.get_decoration(sampler.id, spv::Decoration::DecorationDescriptorSet);
			if (descriptorSetLayoutBindings.size() <= set)
				descriptorSetLayoutBindings.resize(set + 1ULL);

			auto& bindings = descriptorSetLayoutBindings[set];

			auto binding = comp.get_decoration(sampler.id, spv::Decoration::DecorationBinding);

			// to check if it's an array, e.g.: uniform sampler2D uSampler[10];
			const auto& type = comp.get_type(sampler.type_id);

			bindings.emplace_back(
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
			if (!type.array_size_literal.empty() && (bool)type.array_size_literal[0] == false)
			{
				for (const auto& c : comp.get_specialization_constants())
				{
					if (c.id == (spirv_cross::ConstantID)type.array[0])
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

	[[nodiscard]] size_t GetSpecializationEntriesTotalSize(
		const SmallVector<vk::SpecializationMapEntry>& specializationEntries)
	{
		if (specializationEntries.empty())
			return 0;

		uint32_t lastOffset = specializationEntries[0].offset;
		size_t lastSize = specializationEntries[0].size;
		for (int i = 1; i < specializationEntries.size(); ++i)
		{
			const auto& entry = specializationEntries[i];
			if (entry.offset > lastOffset)
			{
				lastOffset = entry.offset;
				lastSize = entry.size;
			}
		}
		return lastOffset + lastSize;
	}

	// Copy specialization entries from data into output buffer
	// Adjust entries offsets to illustrate the offsets in the output buffer.
	void CopySpecializationEntries(
		const void* data,
		SmallVector<vk::SpecializationMapEntry>& entries,
		std::vector<char>& outputBuffer)
	{
		// Prepare data block for specialization constants
		size_t totalSize = GetSpecializationEntriesTotalSize(entries);
		uint32_t baseOffset = outputBuffer.size();
		outputBuffer.resize((size_t)baseOffset + totalSize);

		// Copy data to local data block and adjust offsets
		for (vk::SpecializationMapEntry& entry : entries)
		{
			memcpy(outputBuffer.data() + baseOffset + entry.offset, (const char*)data + entry.offset, entry.size);
			entry.offset += baseOffset;
		}
	}

	void ReplaceSpecializationConstants(
		gsl::span<const char> specializationData, // block of specialization data
		const SmallVector<vk::SpecializationMapEntry>& specializationEntries, // specialization entries in that block
		const SmallVector<ShaderReflection::SpecializationConstantRef>& specializationRefs, // references to specialization constants in bindings
		SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>& descriptorSetLayoutBindings)
	{
		// Replace placeholder array sizes with real values
		for (const auto& specRef : specializationRefs)
		{
			// Find matching specialization constant
			for (const auto& spec : specializationEntries)
			{
				if (spec.constantID == specRef.constantID)
				{
					ASSERT(spec.size == spec.size);

					// Update descriptor count
					for (auto& binding : descriptorSetLayoutBindings[specRef.set])
					{
						if (binding.binding == specRef.binding)
						{
							void* src = (char*)specializationData.data() + spec.offset;
							void* dest = &binding.descriptorCount;
							memcpy(dest, src, spec.size);
							break;
						}
					}

					break; // found this constant
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

ShaderReflection::ShaderReflection(uint32_t* code, size_t codeSize) /* how many uint32_t */
	: comp(code, codeSize)
	, shaderResources(comp.get_shader_resources())
{
	specializationRefs = ::PopulateSampledImagesSpecializationRefs(comp, shaderResources.sampled_images);
	specializationMapEntries = ::PopulateSpecializationMapEntries(comp);
}

// todo (hbedard): take an AssetPath once available
ShaderID ShaderSystem::CreateShader(const std::filesystem::path& filePath)
{
	return CreateShader(filePath, "main");
}

ShaderID ShaderSystem::CreateShader(const std::filesystem::path& filePath, std::string entryPoint)
{
	std::string filePathStr = filePath.string();
	uint64_t filenameID = fnv_hash(reinterpret_cast<const uint8_t*>(filePathStr.c_str()), filePathStr.size());
	auto shaderIt = m_filenameHashToShaderID.find(filenameID);
	if (shaderIt != m_filenameHashToShaderID.end())
		return shaderIt->second;

	auto code = file_utils::ReadFile(filePathStr);
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

ShaderInstanceID ShaderSystem::CreateShaderInstance(
	ShaderID shaderID,
	const void* specializationData,
	SmallVector<vk::SpecializationMapEntry> specializationEntries)
{
	if (specializationEntries.size() == 0)
		return CreateShaderInstance(shaderID);

	ShaderInstanceID id = m_instanceIDToShaderID.size();
	m_instanceIDToShaderID.push_back(shaderID);
	m_specializationBlocks.resize(id + 1);
	CopySpecializationEntries(specializationData, specializationEntries, m_specializationBlocks[id]);
	m_specializationEntries.push_back(std::move(specializationEntries));

	return id;
}

ShaderInstanceID ShaderSystem::CreateShaderInstance(ShaderID shaderID)
{
	ShaderInstanceID id = m_instanceIDToShaderID.size();
	m_instanceIDToShaderID.push_back(shaderID);
	m_specializationEntries.resize(id + 1); // it's possible that the shader doesn't need specialiation constants
	return id;
}

vk::PipelineShaderStageCreateInfo ShaderSystem::GetShaderStageInfo(ShaderInstanceID id, vk::SpecializationInfo& specializationInfo) const
{
	ShaderID shaderID = m_instanceIDToShaderID[id];
	const SmallVector<vk::SpecializationMapEntry>& specializationEntries = m_specializationEntries[id];
	const ShaderReflection& reflection = *m_reflections[shaderID];

	size_t dataSize = ::GetSpecializationEntriesTotalSize(specializationEntries);

	specializationInfo = vk::SpecializationInfo();
	specializationInfo.pData = specializationEntries.size() > 0 ? m_specializationBlocks[id].data() : nullptr;
	specializationInfo.dataSize = dataSize;
	specializationInfo.mapEntryCount = specializationEntries.size();
	specializationInfo.pMapEntries = specializationEntries.data();

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
	const SmallVector<vk::SpecializationMapEntry>& specializationEntries = m_specializationEntries[id];

	SetVector<SmallVector<vk::DescriptorSetLayoutBinding>> descriptorSetLayoutBindings;

	::PopulateBufferDescriptorSetLayoutBindings(
		vk::DescriptorType::eUniformBuffer,
		reflection.comp,
		reflection.shaderResources.uniform_buffers,
		descriptorSetLayoutBindings
	);

	::PopulateBufferDescriptorSetLayoutBindings(
		vk::DescriptorType::eStorageBuffer,
		reflection.comp,
		reflection.shaderResources.storage_buffers,
		descriptorSetLayoutBindings
	);

	::PopulateSampledImagesDescriptorSetLayoutBindings(
		reflection.comp,
		reflection.shaderResources.sampled_images,
		descriptorSetLayoutBindings
	);

	// Sort each layout by binding
	for (auto& descriptorSetLayout : descriptorSetLayoutBindings)
	{
		std::sort(descriptorSetLayout.begin(), descriptorSetLayout.end(), [](const auto& a, const auto& b) {
			return a.binding < b.binding;
		});
	}

	if (specializationEntries.size() > 0)
	{
		const std::vector<char>& block = m_specializationBlocks[id];
		::ReplaceSpecializationConstants(
			gsl::span<const char>(block.data(), block.size()),
			m_specializationEntries[id],
			reflection.specializationRefs,
			descriptorSetLayoutBindings
		);
	}

	return descriptorSetLayoutBindings;
}
