#pragma once

#include "Shader.h"
#include "hash.h"

#include <vulkan/vulkan.hpp>

#include <string>
#include <map>
#include <memory>
#include <cstdint>

class ShaderCache
{
public:
	Shader& Load(const std::string& filename)
	{
		uint64_t id = fnv_hash(reinterpret_cast<const uint8_t*>(filename.c_str()), filename.size());
		auto shaderIt = m_shaders.find(id);
		if (shaderIt != m_shaders.end())
			return *(shaderIt->second);

		auto shader = std::make_unique<Shader>(filename, "main");
		auto [it, wasAdded] = m_shaders.emplace(id, std::move(shader));
		return *it->second;
	}

private:
	std::map<uint64_t, std::unique_ptr<Shader>> m_shaders;
};
