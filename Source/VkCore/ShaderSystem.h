#pragma once

#include "SmallVector.h"

#include "spirv_vk.h"

#include <vulkan/vulkan.hpp>

#include <string>
#include <optional>
#include <vector>
#include <map>
#include <filesystem>

struct Entry
{
	template <class T>
	static Entry AppendToOutput(const std::vector<T>& input, std::vector<T>& output)
	{
		Entry entry;
		entry.offset = output.size();
		entry.size = input.size();
		const size_t writeIndex = output.size();
		output.resize(writeIndex + input.size());
		std::copy(input.begin(), input.end(), &output[writeIndex]);
		return entry;
	}

	uint32_t offset = 0;
	uint32_t size = 0;
};

using ShaderID = uint32_t;
using ShaderInstanceID = uint32_t;

// We don't expect more than 4 sets bound for a shader
constexpr size_t kMaxNumSets = 4;

template <class T>
using SetVector = SmallVector<T, kMaxNumSets>;

// Used to automatically generate vulkan structures for building graphics pipelines.
struct ShaderReflection
{
	ShaderReflection(uint32_t* code, size_t codeSize); /* how many uint32_t */

	spirv_cross::CompilerReflection comp;
	spirv_cross::ShaderResources shaderResources;

	struct SpecializationConstantRef
	{
		uint32_t set;
		uint32_t binding;
		uint32_t constantID;
	};

	// Extracted info
	SmallVector<SpecializationConstantRef> specializationRefs;
	SmallVector<vk::SpecializationMapEntry> specializationMapEntries;
};

class ShaderSystem
{
public:
	// --- Shader Creation --- //

	ShaderID CreateShader(const std::filesystem::path& filePath); // entryPoint defaults to main
	ShaderID CreateShader(const std::filesystem::path& filePath, std::string entryPoint);
	ShaderID CreateShader(const char* data, size_t size, std::string entryPoint);

	ShaderInstanceID CreateShaderInstance(ShaderID shaderID);
	ShaderInstanceID CreateShaderInstance(
		ShaderID shaderID,
		const void* specializationData,
		SmallVector<vk::SpecializationMapEntry> specializationConstants
	);

	// --- Helpers to create generate graphics pipeline creation info --- //

	// Note: pointers in this structure are invalidated when CreateShaderInstance is called
	auto GetShaderStageInfo(
		ShaderInstanceID id,
		vk::SpecializationInfo& specializationInfo
	) const -> vk::PipelineShaderStageCreateInfo;

	auto GetVertexInputStateInfo(
		ShaderInstanceID id,
		SmallVector<vk::VertexInputAttributeDescription>& attributeDescriptions, // will be populated
		vk::VertexInputBindingDescription& bindingDescription // will be populated
	) const -> vk::PipelineVertexInputStateCreateInfo;
	
	auto GetDescriptorSetLayoutBindings(
		ShaderInstanceID id
	) const -> SetVector<SmallVector<vk::DescriptorSetLayoutBinding>>; // [set][binding]

	auto GetPushConstantRanges(
		ShaderInstanceID id
	) const -> SmallVector<vk::PushConstantRange>;

private:
	// --- Base Shader --- //

	// ShaderID -> Array Index
	std::vector<vk::UniqueShaderModule> m_modules;
	std::vector<std::string> m_entryPoints; // usually "main", could be standardized to remove this vector
	std::vector<std::unique_ptr<ShaderReflection>> m_reflections;

	// To know if a shader for this file already exists
	std::map<uint64_t, ShaderID> m_filenameHashToShaderID;

	// --- Shader Instance (base shader with specific specialization constants) --- //

	std::vector<std::vector<char>> m_specializationBlocks; // specialization constants data blocks

	// ShaderInstanceID -> Array Index
	std::vector<ShaderID> m_instanceIDToShaderID;
	std::vector<SmallVector<vk::SpecializationMapEntry>> m_specializationEntries;
};
