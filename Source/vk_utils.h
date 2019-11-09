#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

namespace vk_utils
{
	template <class T, class Dispatch>
	std::vector<T> remove_unique(const std::vector<vk::UniqueHandle<T, Dispatch>>& uniqueValues)
	{
		std::vector<vk::DescriptorSet> values;

		std::transform(uniqueValues.begin(), uniqueValues.end(), std::back_inserter(values),
			[](const auto& value) { return value.get(); });

		return values;
	}
}
