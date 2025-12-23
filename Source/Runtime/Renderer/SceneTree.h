#pragma once

#include "BoundingBox.h"
#include "Buffers.h"

#include "glm_includes.h"

#include <cstdint>
#include <limits>
#include <memory>

class CommandBufferPool;

enum class SceneNodeID : uint32_t { Invalid = std::numeric_limits<uint32_t>::max() };

template <typename EnumType, typename NumberType>
EnumType id_cast(NumberType number)
{
	assert(number < static_cast<size_t>(EnumType::Invalid));
	return static_cast<EnumType>(number);
}

class SceneTree
{
public:
	SceneNodeID CreateNode(glm::mat4 transform, BoundingBox boundingBox, SceneNodeID parent = SceneNodeID::Invalid);

	size_t GetNodeCount() const { return m_transforms.size(); }

	void UploadToGPU(CommandBufferPool& commandBufferPool);

	const UniqueBuffer& GetTransformsBuffer() const { return *m_transformsBuffer; }

	// --- Bounding Box --- //

	BoundingBox ComputeWorldBoundingBox() const
	{
		BoundingBox worldBox;

		for (int i = 0; i < m_transforms.size(); ++i)
		{
			BoundingBox box = m_boundingBoxes[i];
			box = box.Transform(m_transforms[i]);
			worldBox = worldBox.Union(box);
		}

		return worldBox;
	}

	// --- Transforms --- //

	glm::mat4 GetTransform(SceneNodeID id) const { return m_transforms[static_cast<size_t>(id)]; }

	const std::vector<glm::mat4>& GetTransforms() const { return m_transforms; }

	const std::vector<BoundingBox>& GetBoundingBoxes() const { return m_boundingBoxes; }

private:
	// SceneNodeID -> Array Index
	std::vector<BoundingBox> m_boundingBoxes;
	std::vector<glm::mat4> m_transforms;
	std::vector<SceneNodeID> m_parents;

	// GPU resources
	std::unique_ptr<UniqueBuffer> m_transformsBuffer{ nullptr }; // buffer of transforms
};
