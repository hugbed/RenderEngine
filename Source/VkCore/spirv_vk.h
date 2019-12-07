#pragma once

#include <vulkan/vulkan.hpp>

#include <spirv_reflect.hpp>

namespace spirv_vk
{
	// This is only intended for vertex input types so not all types are listed here
	vk::Format get_vk_format_from_variable(const spirv_cross::Compiler& comp, spirv_cross::ID variableId)
	{
		using Type = spirv_cross::SPIRType::BaseType;

		auto type = comp.get_type_from_variable(variableId);

		if (type.columns == 1)
		{
			if (type.vecsize == 1) // single
			{
				switch (type.basetype)
				{
				case Type::SByte:
					return vk::Format::eR8Sint;
				case Type::UByte:
					return vk::Format::eR8Uint;
				case Type::Short:
					return vk::Format::eR16Sint;
				case Type::UShort:
					return vk::Format::eR16Uint;
				case Type::Int:
					return vk::Format::eR32Sint;
				case Type::UInt:
					return vk::Format::eR32Uint;
				case Type::Int64:
					return vk::Format::eR64Sint;
				case Type::UInt64:
					return vk::Format::eR64Uint;
				case Type::Float:
					return vk::Format::eR32Sfloat;
				case Type::Double:
					return vk::Format::eR64Sfloat;
				default:
					break;
				}
			}
			else if (type.vecsize == 2) // vec2
			{
				// vector
				switch (type.basetype)
				{
				case Type::SByte:
					return vk::Format::eR8G8Sint;
				case Type::UByte:
					return vk::Format::eR8G8Uint;
				case Type::Short:
					return vk::Format::eR16G16Sint;
				case Type::UShort:
					return vk::Format::eR16G16Uint;
				case Type::Int:
					return vk::Format::eR32G32Sint;
				case Type::UInt:
					return vk::Format::eR32G32Uint;
				case Type::Int64:
					return vk::Format::eR64G64Sint;
				case Type::UInt64:
					return vk::Format::eR64G64Uint;
				case Type::Float:
					return vk::Format::eR32G32Sfloat;
				case Type::Double:
					return vk::Format::eR64G64Sfloat;
				default:
					break;
				}
			}
			else if (type.vecsize == 3) // vec3
			{
				// vector
				switch (type.basetype)
				{
				case Type::SByte:
					return vk::Format::eR8G8B8Sint;
				case Type::UByte:
					return vk::Format::eR8G8B8Uint;
				case Type::Short:
					return vk::Format::eR16G16B16Sint;
				case Type::UShort:
					return vk::Format::eR16G16B16Uint;
				case Type::Int:
					return vk::Format::eR32G32B32Sint;
				case Type::UInt:
					return vk::Format::eR32G32B32Uint;
				case Type::Int64:
					return vk::Format::eR64G64B64Sint;
				case Type::UInt64:
					return vk::Format::eR64G64B64Uint;
				case Type::Float:
					return vk::Format::eR32G32B32Sfloat;
				case Type::Double:
					return vk::Format::eR64G64B64Sfloat;
				default:
					break;
				}
			}
			else if (type.vecsize == 4) // vec4
			{
				// vector
				switch (type.basetype)
				{
				case Type::SByte:
					return vk::Format::eR8G8B8A8Sint;
				case Type::UByte:
					return vk::Format::eR8G8B8A8Uint;
				case Type::Short:
					return vk::Format::eR16G16B16A16Sint;
				case Type::UShort:
					return vk::Format::eR16G16B16A16Uint;
				case Type::Int:
					return vk::Format::eR32G32B32A32Sint;
				case Type::UInt:
					return vk::Format::eR32G32B32A32Uint;
				case Type::Int64:
					return vk::Format::eR64G64B64A64Sint;
				case Type::UInt64:
					return vk::Format::eR64G64B64A64Uint;
				case Type::Float:
					return vk::Format::eR32G32B32A32Sfloat;
				case Type::Double:
					return vk::Format::eR64G64B64A64Sfloat;
				default:
					break;
				}
			}
		}
		else
		{
			// matrix (todo: support this)
		}

		throw std::runtime_error("Invalid/Unsupported type");
		return {};
	}

	// This is only intended for vertex input types so not all types are listed here
	uint32_t sizeof_vkformat(vk::Format format)
	{
		switch (format)
		{
		case vk::Format::eUndefined: return 0;
		case vk::Format::eR4G4UnormPack8: return 1;
		case vk::Format::eR4G4B4A4UnormPack16: return 2;
		case vk::Format::eB4G4R4A4UnormPack16: return 2;
		case vk::Format::eR5G6B5UnormPack16: return 2;
		case vk::Format::eB5G6R5UnormPack16: return 2;
		case vk::Format::eR5G5B5A1UnormPack16: return 2;
		case vk::Format::eB5G5R5A1UnormPack16: return 2;
		case vk::Format::eA1R5G5B5UnormPack16: return 2;
		case vk::Format::eR8Unorm: return 1;
		case vk::Format::eR8Snorm: return 1;
		case vk::Format::eR8Uscaled: return 1;
		case vk::Format::eR8Sscaled: return 1;
		case vk::Format::eR8Uint: return 1;
		case vk::Format::eR8Sint: return 1;
		case vk::Format::eR8Srgb: return 1;
		case vk::Format::eR8G8Unorm: return 2;
		case vk::Format::eR8G8Snorm: return 2;
		case vk::Format::eR8G8Uscaled: return 2;
		case vk::Format::eR8G8Sscaled: return 2;
		case vk::Format::eR8G8Uint: return 2;
		case vk::Format::eR8G8Sint: return 2;
		case vk::Format::eR8G8Srgb: return 2;
		case vk::Format::eR8G8B8Unorm: return 3;
		case vk::Format::eR8G8B8Snorm: return 3;
		case vk::Format::eR8G8B8Uscaled: return 3;
		case vk::Format::eR8G8B8Sscaled: return 3;
		case vk::Format::eR8G8B8Uint: return 3;
		case vk::Format::eR8G8B8Sint: return 3;
		case vk::Format::eR8G8B8Srgb: return 3;
		case vk::Format::eB8G8R8Unorm: return 3;
		case vk::Format::eB8G8R8Snorm: return 3;
		case vk::Format::eB8G8R8Uscaled: return 3;
		case vk::Format::eB8G8R8Sscaled: return 3;
		case vk::Format::eB8G8R8Uint: return 3;
		case vk::Format::eB8G8R8Sint: return 3;
		case vk::Format::eB8G8R8Srgb: return 3;
		case vk::Format::eR8G8B8A8Unorm: return 4;
		case vk::Format::eR8G8B8A8Snorm: return 4;
		case vk::Format::eR8G8B8A8Uscaled: return 4;
		case vk::Format::eR8G8B8A8Sscaled: return 4;
		case vk::Format::eR8G8B8A8Uint: return 4;
		case vk::Format::eR8G8B8A8Sint: return 4;
		case vk::Format::eR8G8B8A8Srgb: return 4;
		case vk::Format::eB8G8R8A8Unorm: return 4;
		case vk::Format::eB8G8R8A8Snorm: return 4;
		case vk::Format::eB8G8R8A8Uscaled: return 4;
		case vk::Format::eB8G8R8A8Sscaled: return 4;
		case vk::Format::eB8G8R8A8Uint: return 4;
		case vk::Format::eB8G8R8A8Sint: return 4;
		case vk::Format::eB8G8R8A8Srgb: return 4;
		case vk::Format::eA8B8G8R8UnormPack32: return 4;
		case vk::Format::eA8B8G8R8SnormPack32: return 4;
		case vk::Format::eA8B8G8R8UscaledPack32: return 4;
		case vk::Format::eA8B8G8R8SscaledPack32: return 4;
		case vk::Format::eA8B8G8R8UintPack32: return 4;
		case vk::Format::eA8B8G8R8SintPack32: return 4;
		case vk::Format::eA8B8G8R8SrgbPack32: return 4;
		case vk::Format::eA2R10G10B10UnormPack32: return 4;
		case vk::Format::eA2R10G10B10SnormPack32: return 4;
		case vk::Format::eA2R10G10B10UscaledPack32: return 4;
		case vk::Format::eA2R10G10B10SscaledPack32: return 4;
		case vk::Format::eA2R10G10B10UintPack32: return 4;
		case vk::Format::eA2R10G10B10SintPack32: return 4;
		case vk::Format::eA2B10G10R10UnormPack32: return 4;
		case vk::Format::eA2B10G10R10SnormPack32: return 4;
		case vk::Format::eA2B10G10R10UscaledPack32: return 4;
		case vk::Format::eA2B10G10R10SscaledPack32: return 4;
		case vk::Format::eA2B10G10R10UintPack32: return 4;
		case vk::Format::eA2B10G10R10SintPack32: return 4;
		case vk::Format::eR16Unorm: return 2;
		case vk::Format::eR16Snorm: return 2;
		case vk::Format::eR16Uscaled: return 2;
		case vk::Format::eR16Sscaled: return 2;
		case vk::Format::eR16Uint: return 2;
		case vk::Format::eR16Sint: return 2;
		case vk::Format::eR16Sfloat: return 2;
		case vk::Format::eR16G16Unorm: return 4;
		case vk::Format::eR16G16Snorm: return 4;
		case vk::Format::eR16G16Uscaled: return 4;
		case vk::Format::eR16G16Sscaled: return 4;
		case vk::Format::eR16G16Uint: return 4;
		case vk::Format::eR16G16Sint: return 4;
		case vk::Format::eR16G16Sfloat: return 4;
		case vk::Format::eR16G16B16Unorm: return 6;
		case vk::Format::eR16G16B16Snorm: return 6;
		case vk::Format::eR16G16B16Uscaled: return 6;
		case vk::Format::eR16G16B16Sscaled: return 6;
		case vk::Format::eR16G16B16Uint: return 6;
		case vk::Format::eR16G16B16Sint: return 6;
		case vk::Format::eR16G16B16Sfloat: return 6;
		case vk::Format::eR16G16B16A16Unorm: return 8;
		case vk::Format::eR16G16B16A16Snorm: return 8;
		case vk::Format::eR16G16B16A16Uscaled: return 8;
		case vk::Format::eR16G16B16A16Sscaled: return 8;
		case vk::Format::eR16G16B16A16Uint: return 8;
		case vk::Format::eR16G16B16A16Sint: return 8;
		case vk::Format::eR16G16B16A16Sfloat: return 8;
		case vk::Format::eR32Uint: return 4;
		case vk::Format::eR32Sint: return 4;
		case vk::Format::eR32Sfloat: return 4;
		case vk::Format::eR32G32Uint: return 8;
		case vk::Format::eR32G32Sint: return 8;
		case vk::Format::eR32G32Sfloat: return 8;
		case vk::Format::eR32G32B32Uint: return 12;
		case vk::Format::eR32G32B32Sint: return 12;
		case vk::Format::eR32G32B32Sfloat: return 12;
		case vk::Format::eR32G32B32A32Uint: return 16;
		case vk::Format::eR32G32B32A32Sint: return 16;
		case vk::Format::eR32G32B32A32Sfloat: return 16;
		case vk::Format::eR64Uint: return 8;
		case vk::Format::eR64Sint: return 8;
		case vk::Format::eR64Sfloat: return 8;
		case vk::Format::eR64G64Uint: return 16;
		case vk::Format::eR64G64Sint: return 16;
		case vk::Format::eR64G64Sfloat: return 16;
		case vk::Format::eR64G64B64Uint: return 24;
		case vk::Format::eR64G64B64Sint: return 24;
		case vk::Format::eR64G64B64Sfloat: return 24;
		case vk::Format::eR64G64B64A64Uint: return 32;
		case vk::Format::eR64G64B64A64Sint: return 32;
		case vk::Format::eR64G64B64A64Sfloat: return 32;
		case vk::Format::eB10G11R11UfloatPack32: return 4;
		case vk::Format::eE5B9G9R9UfloatPack32: return 4;
		default:
			throw std::runtime_error("Unsupported format");
		}

		return {};
	}

	vk::ShaderStageFlagBits execution_model_to_shader_stage(spv::ExecutionModel executionModel)
	{
		switch (executionModel)
		{
		case spv::ExecutionModelVertex:
			return vk::ShaderStageFlagBits::eVertex;
		case spv::ExecutionModelFragment:
			return vk::ShaderStageFlagBits::eFragment;
		case spv::ExecutionModelGeometry:
			return vk::ShaderStageFlagBits::eGeometry;
		case spv::ExecutionModelTessellationControl:
			return vk::ShaderStageFlagBits::eTessellationControl;
		case spv::ExecutionModelTessellationEvaluation:
			return vk::ShaderStageFlagBits::eTessellationEvaluation;
		default:
			throw std::runtime_error("Unknown execution model");
		}

		return {};
	}

	uint32_t sizeof_constant(const spirv_cross::Compiler& comp, spirv_cross::TypeID type)
	{
		using Type = spirv_cross::SPIRType::BaseType;

		switch (type)
		{
		case Type::Boolean:
		case Type::SByte:
		case Type::UByte:
			return 1;
		case Type::Short:
		case Type::UShort:
		case Type::Half:
			return 2;
		case Type::Int:
		case Type::UInt:
			return 4;
		case Type::Int64:
		case Type::UInt64:
		case Type::Float:
		case Type::Double:
			return 8;
		default:
			throw std::runtime_error("Unsupported specialization_constant type");
		}

		return {};
	}
}
