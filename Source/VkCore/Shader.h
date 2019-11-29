#pragma once

#include <vulkan/vulkan.hpp>

#include <string>
#include <optional>
#include <vector>

class Shader
{
public:
	// todo: support loading byte array directly

	Shader(const std::string& filename , std::string entryPoint);

	static vk::UniqueShaderModule CreateShaderModule(const std::vector<char>& code);

	vk::PipelineShaderStageCreateInfo GetShaderStageInfo() const;

	vk::PipelineVertexInputStateCreateInfo GetVertexInputStateInfo() const;

	const std::vector<vk::DescriptorSetLayoutBinding>& GetDescriptorSetLayoutBindings() const
	{
		return m_descriptorSetLayouts;
	}

protected:
	vk::VertexInputBindingDescription m_bindingDescription;
	std::vector<vk::VertexInputAttributeDescription> m_attributeDescriptions;
	std::vector<vk::DescriptorSetLayoutBinding> m_descriptorSetLayouts;

private:
	vk::ShaderStageFlagBits m_shaderStage;
	std::string m_entryPoint;
	vk::UniqueShaderModule m_shaderModule;
};
