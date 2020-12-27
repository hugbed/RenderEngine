#pragma once

#include "DescriptorSetLayouts.h"
#include "Material.h"
#include "BoundingBox.h"

#include "Buffers.h"

#include "glm_includes.h"

#include <vulkan/vulkan.hpp>

#include <memory>

struct Mesh
{
	vk::DeviceSize indexOffset;
	vk::DeviceSize nbIndices;
	Material::ShadingModel shadingModel;
	GraphicsPipelineID graphicsPipelineID;
	MaterialInstanceID materialInstanceID;
};

struct ModelUniforms
{
	glm::aligned_mat4 transform;
};

struct Model
{
	Model();

	void UpdateTransform(glm::mat4 transform)
	{
		this->transform = std::move(transform);
		memcpy(uniformBuffer->GetMappedData(), &this->transform, sizeof(glm::mat4));
	}

	void SetLocalAABB(const BoundingBox& box)
	{
		boundingBox = transform * box;
	}

	const BoundingBox& GetWorldAABB()
	{
		return boundingBox;
	}

	glm::mat4& GetTransform() { return transform; }

	std::unique_ptr<UniqueBuffer> uniformBuffer;

	// A model as multiple parts (meshes)
	std::vector<Mesh> meshes;

	// Per-object descriptors
	vk::UniqueDescriptorSet descriptorSet;

private:
	BoundingBox boundingBox = {};
	glm::mat4 transform = glm::mat4(1.0f);
};
