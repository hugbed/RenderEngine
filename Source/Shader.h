#pragma once

#include <vulkan/vulkan.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

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
