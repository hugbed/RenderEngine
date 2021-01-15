#pragma once

#include "DescriptorSetLayouts.h"
#include "Material.h"
#include "BoundingBox.h"

#include "Buffers.h"
#include "Device.h"

#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <memory>

struct Mesh
{
	vk::DeviceSize indexOffset = 0;
	vk::DeviceSize nbIndices = 0;
	Material::ShadingModel shadingModel = Material::ShadingModel::Lit; // todo: remove this
	MaterialInstanceID materialInstanceID = ~0;
};

using ModelID = uint32_t;

struct MeshDrawInfo
{
	ModelID model;
	Mesh mesh;
};

class ModelSystem
{
public:
	ModelID CreateModel(glm::mat4 transform, BoundingBox boundingBox, const std::vector<Mesh>& meshes)
	{
		ModelID id = m_transforms.size();
		m_transforms.push_back(std::move(transform));
		m_boundingBoxes.push_back(std::move(boundingBox));
		m_meshEntries.push_back(Entry::AppendToOutput(meshes, m_meshes));

		// Upload transform

		return id;
	}

	void SetLocalAABB(ModelID id, const BoundingBox& box)
	{
		m_boundingBoxes[id] = m_transforms[id] * box;
	}

	const BoundingBox& GetWorldAABB(ModelID id)
	{
		return m_boundingBoxes[id];
	}

	glm::mat4 GetTransform(ModelID id) const
	{
		return m_transforms[id];
	}

	const UniqueBuffer& GetUniformBuffer() const
	{
		return *m_uniformBuffer;
	}

	const std::vector<glm::mat4>& GetTransforms() const
	{
		return m_transforms;
	}

	size_t GetModelCount() const
	{
		return m_transforms.size();
	}

	void UploadUniformBuffer(CommandBufferPool& commandBufferPool)
	{
		if (m_uniformBuffer == nullptr)
		{
			const void* data = reinterpret_cast<const void*>(m_transforms.data());
			size_t size = m_transforms.size() * sizeof(m_transforms[0]);
			vk::BufferCreateInfo bufferInfo({}, size, vk::BufferUsageFlagBits::eUniformBuffer);
			VmaAllocationCreateInfo allocInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU };
			m_uniformBuffer = std::make_unique<UniqueBuffer>(bufferInfo, allocInfo);
		}

		// todo: handle resize
		size_t writeSize = m_transforms.size() * sizeof(m_transforms[0]);
		memcpy((char*)m_uniformBuffer->GetMappedData(), m_transforms.data(), writeSize);
		m_uniformBuffer->Flush(0, writeSize);
	}

	template <class Func>
	void ForEachMesh(Func f)
	{
		for (auto&& meshEntry : m_meshEntries)
		{
			for (int i = meshEntry.offset; i < meshEntry.offset + meshEntry.size; ++i)
			{
				f((ModelID)i, m_meshes[i]);
			}
		}
	}

private:
	// ModelID -> Array Index
	std::vector<BoundingBox> m_boundingBoxes;
	std::vector<glm::mat4> m_transforms;
	std::vector<Entry> m_meshEntries;

	// Contains all meshes, referenced by meshOffsets for each model
	std::vector<Mesh> m_meshes;

	// GPU resources
	std::unique_ptr<UniqueBuffer> m_uniformBuffer{ nullptr }; // buffer of transforms
};
