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

	template <class T>
	std::vector<typename T::value_type> get_all(const std::vector<T>& wrappers)
	{
		std::vector<typename T::value_type> values;

		std::transform(wrappers.begin(), wrappers.end(), std::back_inserter(values),
			[](const T& value) -> T::value_type { return value.Get(); });

		return values;
	}
}
