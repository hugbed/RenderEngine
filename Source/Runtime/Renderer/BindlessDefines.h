#pragma once

#include <cstdint>
#include <limits>

enum class TextureHandle : uint32_t { Invalid = (std::numeric_limits<uint32_t>::max)() };
enum class BufferHandle : uint32_t { Invalid = (std::numeric_limits<uint32_t>::max)() };
enum class BindlessDrawParamsHandle : uint32_t { Invalid = (std::numeric_limits<uint32_t>::max)() };

enum class BindlessDescriptorSet
{
	eBindlessDescriptors = 0,
	eDrawParams = 1,
};
