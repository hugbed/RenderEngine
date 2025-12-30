#pragma once

#include <BoundingBox.h>
#include <Renderer/Bindless.h>
#include <RHI/Buffers.h>

#include <glm_includes.h>
#include <gsl/pointers>
#include <cstdint>
#include <limits>
#include <memory>

class CommandRingBuffer;

enum class SceneNodeHandle : uint32_t { Invalid = std::numeric_limits<uint32_t>::max() };

template <typename EnumType, typename NumberType>
EnumType id_cast(NumberType number)
{
	assert(number < static_cast<size_t>(EnumType::Invalid));
	return static_cast<EnumType>(number);
}

class SceneTree
{
public:
	SceneTree(BindlessDescriptors& bindlessDescriptors)
		: m_bindlessDescriptors(&bindlessDescriptors)
	{
	}

	SceneNodeHandle CreateNode(glm::mat4 transform, BoundingBox boundingBox, SceneNodeHandle parent = SceneNodeHandle::Invalid);

	size_t GetNodeCount() const { return m_transforms.size(); }

	void UploadToGPU(CommandRingBuffer& commandRingBuffer);

	BufferHandle GetTransformsBufferHandle() const { return m_transformsBufferHandle; }

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

	glm::mat4 GetTransform(SceneNodeHandle id) const { return m_transforms[static_cast<size_t>(id)]; }

	const std::vector<glm::mat4>& GetTransforms() const { return m_transforms; }

	const std::vector<BoundingBox>& GetBoundingBoxes() const { return m_boundingBoxes; }

	const BoundingBox& GetSceneBoundingBox() const { return m_sceneBoundingBox; }

private:
	// SceneNodeID -> Array Index
	std::vector<BoundingBox> m_boundingBoxes;
	std::vector<glm::mat4> m_transforms;
	std::vector<SceneNodeHandle> m_parents;
	BoundingBox m_sceneBoundingBox;

	// GPU resources
	std::unique_ptr<UniqueBuffer> m_transformsBuffer{ nullptr }; // buffer of transforms
	BufferHandle m_transformsBufferHandle = BufferHandle::Invalid;
	gsl::not_null<BindlessDescriptors*> m_bindlessDescriptors;
};
