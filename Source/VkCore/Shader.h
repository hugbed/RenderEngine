#pragma once

#include <vulkan/vulkan.hpp>

#include <string>
#include <optional>
#include <vector>
#include <map>

class Shader
{
public:
	// todo: support loading byte array directly

	Shader(const std::string& filename, std::string entryPoint);

	static vk::UniqueShaderModule CreateShaderModule(const std::vector<char>& code);

	template <class T>
	void SetSpecializationConstants(const T& obj)
	{
		SetSpecializationConstants(reinterpret_cast<const void*>(&obj), sizeof(T));
	}

	void SetSpecializationConstants(const void* data, size_t size);

	vk::PipelineShaderStageCreateInfo GetShaderStageInfo() const;

	vk::PipelineVertexInputStateCreateInfo GetVertexInputStateInfo() const;

	const std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& GetDescriptorSetLayoutBindings() const
	{
		return m_descriptorSetLayouts;
	}

	const std::vector<vk::PushConstantRange>& GetPushConstantRanges() const
	{
		return m_pushConstantRanges;
	}

protected:
	vk::VertexInputBindingDescription m_bindingDescription;
	std::vector<vk::VertexInputAttributeDescription> m_attributeDescriptions;
	std::vector<std::vector<vk::DescriptorSetLayoutBinding>> m_descriptorSetLayouts;
	std::vector<vk::SpecializationMapEntry> m_specializationMapEntries;
	vk::SpecializationInfo m_specializationInfo;
	std::vector<vk::PushConstantRange> m_pushConstantRanges;

private:
	vk::ShaderStageFlagBits m_shaderStage;
	std::string m_entryPoint;
	vk::UniqueShaderModule m_shaderModule;
};
