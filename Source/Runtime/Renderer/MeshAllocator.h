#pragma once

#include <Renderer/SceneTree.h> // todo (hbedard) only for the ID, that's a shame
#include <Renderer/MaterialDefines.h>
#include <RHI/Buffers.h>
#include <RHI/Device.h>
#include <RHI/ShaderSystem.h> // todo (hbedard): for Entry, that seems odd
#include <BoundingBox.h>

#include <glm_includes.h>
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
	MaterialHandle materialHandle = MaterialHandle::Invalid();
};

using ModelID = uint32_t;

struct MeshDrawInfo
{
	SceneNodeID sceneNodeID = SceneNodeID::Invalid;
	Mesh mesh;
};

class MeshAllocator
{
public:
	// todo (hbedard): store meshes associated to a scene node in the scene instead of here
	void GroupMeshes(SceneNodeID sceneNodeID, const std::vector<Mesh>& meshes);

	// todo (hbedard): implement a "RenderResource" interface
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

	template <class Func>
	void ForEachMesh(Func f)
	{
		for (const auto& [sceneNodeID, meshEntry] : m_meshEntries)
		{
			for (int i = meshEntry.offset; i < meshEntry.offset + meshEntry.size; ++i)
			{
				f(sceneNodeID, m_meshes[i]);
			}
		}
	}

private:
	std::vector<std::pair<SceneNodeID, Entry>> m_meshEntries;

	// Contains all geometry (vertices and indices)
	std::vector<Vertex> m_vertices;
	std::vector<uint32_t> m_indices;
	std::unique_ptr<UniqueBufferWithStaging> m_vertexBuffer{ nullptr };
	std::unique_ptr<UniqueBufferWithStaging> m_indexBuffer{ nullptr };

	// Contains all meshes, referenced by meshOffsets
	std::vector<Mesh> m_meshes;
};
