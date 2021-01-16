#pragma once

#include "DescriptorSetLayouts.h"
#include "Material.h"
#include "BoundingBox.h"

#include "Buffers.h"
#include "Device.h"

#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <memory>

struct Vertex
{
	glm::vec3 pos;
	glm::vec2 texCoord;
	glm::vec3 normal;

	bool operator==(const Vertex& other) const
	{
		return pos == other.pos && texCoord == other.texCoord && normal == other.normal;
	}
};

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
	ModelID CreateModel(glm::mat4 transform, BoundingBox boundingBox, const std::vector<Mesh>& meshes);

	size_t GetModelCount() const { return m_transforms.size(); }

	void UploadToGPU(CommandBufferPool& commandBufferPool);

	// Vertices, Indices
	void BindGeometry(const vk::CommandBuffer& commandBuffer) const;

	// --- Vertices, meshes and indices --- //

	// Reserves additional space for "count" items
	void ReserveVertices(size_t count) { m_vertices.reserve(m_vertices.size() + count); }
	void ReserveIndices(size_t count) { m_indices.reserve(m_indices.size() + count); }
	void AddVertex(Vertex vertex) { m_vertices.push_back(std::move(vertex)); }
	void AddIndex(uint32_t index) { m_indices.push_back(index); }
	size_t GetVertexCount() const { return m_vertices.size(); }
	size_t GetIndexCount() const { return m_indices.size(); }

	// --- Bounding boxes --- //

	void SetLocalAABB(ModelID id, const BoundingBox& box) { m_boundingBoxes[id] = m_transforms[id] * box; }

	const BoundingBox& GetWorldAABB(ModelID id) { return m_boundingBoxes[id]; }

	// --- Transforms --- //

	glm::mat4 GetTransform(ModelID id) const { return m_transforms[id]; }

	const UniqueBuffer& GetUniformBuffer() const { return *m_transformsBuffer; }

	const std::vector<glm::mat4>& GetTransforms() const { return m_transforms; }

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

	// Contains all geometry (vertices and indices)
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<UniqueBufferWithStaging> m_indexBuffer{ nullptr };

	// Contains all meshes, referenced by meshOffsets for each model
	std::vector<Mesh> m_meshes;

	// GPU resources
	std::unique_ptr<UniqueBuffer> m_transformsBuffer{ nullptr }; // buffer of transforms
};
