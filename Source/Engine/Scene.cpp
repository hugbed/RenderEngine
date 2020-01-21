#include "Scene.h"

#include "CommandBufferPool.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string>

namespace
{
	glm::mat4 ComputeAiNodeGlobalTransform(const aiNode* node)
	{
		// Convert from row-major (aiMatrix4x4) to column-major (glm::mat4)
		glm::mat4 transform = glm::transpose(glm::make_mat4(&node->mTransformation.a1));

		// Apply all parents in inverse order
		while (node->mParent != nullptr)
		{
			node = node->mParent;
			glm::mat4 parentTransform = glm::transpose(glm::make_mat4(&node->mTransformation.a1));
			transform = parentTransform * transform;
		}

		return transform;
	}

	glm::vec4 ClampColor(glm::vec4 color)
	{
		float maxComponent = std::max(color.x, std::max(color.y, color.z));

		if (maxComponent > 1.0f)
			return color / maxComponent;

		return color;
	}
}

Scene::Scene(
	std::string basePath,
	std::string sceneFilename,
	CommandBufferPool& commandBufferPool,
	const RenderPass& renderPass, vk::Extent2D imageExtent
)
	: m_commandBufferPool(&commandBufferPool)
	, m_basePath(std::move(basePath))
	, m_sceneFilename(std::move(sceneFilename))
	, m_renderPass(&renderPass)
	, m_imageExtent(imageExtent)
	, m_textureCache(std::make_unique<TextureCache>(m_basePath))
	, m_materialCache(std::make_unique<MaterialCache>(renderPass.Get(), imageExtent))
	, m_camera(1.0f * glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 45.0f, 0.01f, 100.0f)
{
}

void Scene::Reset(vk::CommandBuffer& commandBuffer, const RenderPass& renderPass, vk::Extent2D imageExtent)
{
	m_renderPass = &renderPass;
	m_imageExtent = imageExtent;

	m_materialCache->Reset(m_renderPass->Get(), m_imageExtent);
	m_skybox->Reset(*m_renderPass, m_imageExtent);

	CreateDescriptorLayouts();
	UpdateMaterialDescriptors();
}

void Scene::Load(vk::CommandBuffer commandBuffer)
{
	m_skybox = std::make_unique<Skybox>(*m_renderPass, m_textureCache.get(), m_imageExtent);
	LoadScene(commandBuffer);

	CreateLightsUniformBuffers(commandBuffer);
	CreateViewUniformBuffers();

	CreateDescriptorSets();

	UploadToGPU(commandBuffer);
}

void Scene::Update(uint32_t imageIndex)
{
	vk::Extent2D extent = m_imageExtent;

	// OpenGL -> Vulkan invert y, half z
	auto clip = glm::mat4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f
	);

	m_viewUniforms.pos = m_camera.GetEye();
	m_viewUniforms.view = m_camera.GetViewMatrix();
	m_viewUniforms.proj = clip * glm::perspective(
		glm::radians(m_camera.GetFieldOfView()),
		extent.width / (float)extent.height,
		m_camera.GetNearPlane(), m_camera.GetFarPlane()
	);

	// Upload to GPU
	auto& uniformBuffer = GetViewUniformBuffer(imageIndex);
	memcpy(uniformBuffer.GetMappedData(), reinterpret_cast<const void*>(&m_viewUniforms), sizeof(ViewUniforms));
}

void Scene::DrawOpaqueObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const
{
	DrawSceneObjects(commandBuffer, frameIndex, renderState, m_opaqueDrawCache);

	// With Skybox last (to prevent processing fragments for nothing)
	{
		uint32_t concurrentFrameIndex = frameIndex % m_commandBufferPool->GetNbConcurrentSubmits();

		auto shadingModel = ShadingModel::Unlit;
		auto& viewDescriptorSet = m_viewDescriptorSets[(size_t)shadingModel][concurrentFrameIndex].get();
		renderState.BindPipeline(commandBuffer, &m_skybox->GetGraphicsPipeline());
		renderState.BindView(commandBuffer, ShadingModel::Unlit, viewDescriptorSet);
		m_skybox->Draw(commandBuffer, frameIndex);
	}
}

void Scene::DrawTransparentObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const
{
	DrawSceneObjects(commandBuffer, frameIndex, renderState, m_transparentDrawCache);
}

void Scene::DrawAllWithoutShading(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, RenderState& renderState) const
{
	DrawWithoutShading(commandBuffer, frameIndex, renderState, m_opaqueDrawCache);
	DrawWithoutShading(commandBuffer, frameIndex, renderState, m_transparentDrawCache);
}

void Scene::DrawSceneObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& state, const std::vector<MeshDrawInfo>& drawCalls) const
{
	uint32_t currentFrameIndex = frameIndex % m_commandBufferPool->GetNbConcurrentSubmits();

	// Bind the one big vertex + index buffers
	if (drawCalls.empty() == false)
	{
		vk::DeviceSize offsets[] = { 0 };
		vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
		commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
		commandBuffer.bindIndexBuffer(m_indexBuffer->Get(), 0, vk::IndexType::eUint32);
	}

	for (const auto& drawItem : drawCalls)
	{
		auto shadingModel = drawItem.mesh->material->shadingModel;
		auto& viewDescriptorSet = m_viewDescriptorSets[(size_t)shadingModel][currentFrameIndex].get();
		state.BindPipeline(commandBuffer, drawItem.mesh->material->pipeline);
		state.BindView(commandBuffer, shadingModel, viewDescriptorSet);
		state.BindModel(commandBuffer, drawItem.model);
		state.BindMaterial(commandBuffer, drawItem.mesh->material);

		// Draw
		commandBuffer.drawIndexed(drawItem.mesh->nbIndices, 1, drawItem.mesh->indexOffset, 0, 0);
	}
}

void Scene::DrawWithoutShading(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, RenderState& state, const std::vector<MeshDrawInfo>& drawCalls) const
{
	uint32_t currentFrameIndex = frameIndex % m_commandBufferPool->GetNbConcurrentSubmits();

	// Bind the one big vertex + index buffers
	if (drawCalls.empty() == false)
	{
		vk::DeviceSize offsets[] = { 0 };
		vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
		commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
		commandBuffer.bindIndexBuffer(m_indexBuffer->Get(), 0, vk::IndexType::eUint32);
	}

	auto shadingModel = ShadingModel::Unlit;
	for (const auto& drawItem : drawCalls)
	{
		state.BindModel(commandBuffer, drawItem.model);
		commandBuffer.drawIndexed(drawItem.mesh->nbIndices, 1, drawItem.mesh->indexOffset, 0, 0);
	}
}

void Scene::ResetCamera()
{
	LoadCamera();
}

void Scene::LoadScene(vk::CommandBuffer commandBuffer)
{
	int flags = aiProcess_Triangulate
		| aiProcess_GenNormals
		| aiProcess_JoinIdenticalVertices;

	m_assimp.importer = std::make_unique<Assimp::Importer>();
	m_assimp.scene = m_assimp.importer->ReadFile(m_sceneFilename.c_str(), 0);
	if (m_assimp.scene == nullptr)
	{
		std::cout << m_assimp.importer->GetErrorString() << std::endl;
		throw std::runtime_error("Cannot load scene, file not found or parsing failed");
	}

	LoadLights(commandBuffer);
	LoadMaterials(commandBuffer);
	LoadSceneNodes(commandBuffer);
	LoadCamera();
}

void Scene::LoadLights(vk::CommandBuffer buffer)
{
	for (int i = 0; i < m_assimp.scene->mNumLights; ++i)
	{
		aiLight* aLight = m_assimp.scene->mLights[i];

		aiNode* node = m_assimp.scene->mRootNode->FindNode(aLight->mName);
		glm::mat4 transform = ::ComputeAiNodeGlobalTransform(node);

		Light light;
		light.type = (int)aLight->mType;
		light.ambient = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // add a little until we have global illumination
		light.diffuse = ::ClampColor(glm::make_vec4(&aLight->mColorDiffuse.r));
		light.specular = ::ClampColor(glm::make_vec4(&aLight->mColorSpecular.r));
		light.pos = transform[3];

		if (aLight->mType != aiLightSource_POINT)
		{
			light.direction = glm::make_vec3(&aLight->mDirection.x);
			light.direction = glm::vec4(transform * glm::vec4(light.direction, 0.0f));
		}

		if (aLight->mType == aiLightSource_SPOT)
		{
			// falloff exponent is not correctly read from collada
			// so set outer angle to 120% of inner angle for now
			light.innerCutoff = std::cos(aLight->mAngleInnerCone);
			light.outerCutoff = std::cos(aLight->mAngleInnerCone * 1.20f);
		}

		m_lights.push_back(std::move(light));
	}
}

void Scene::LoadCamera()
{
	if (m_assimp.scene->mNumCameras > 0)
	{
		aiCamera* camera = m_assimp.scene->mCameras[0];

		aiNode* node = m_assimp.scene->mRootNode->FindNode(camera->mName);
		glm::mat4 transform = ComputeAiNodeGlobalTransform(node);
		glm::vec3 pos = transform[3];
		glm::vec3 lookat = glm::vec3(0.0f);
		glm::vec3 up = transform[1];
		m_camera.SetCameraView(pos, lookat, up);

		m_camera.SetFieldOfView(camera->mHorizontalFOV * 180 / M_PI * 2);
	}
	else
	{
		// Init camera to see the model
		kInitOrbitCameraRadius = m_maxVertexDist * 15.0f;
		m_camera.SetCameraView(glm::vec3(kInitOrbitCameraRadius, kInitOrbitCameraRadius, kInitOrbitCameraRadius), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
	}
}

void Scene::LoadSceneNodes(vk::CommandBuffer commandBuffer)
{
	m_vertices.clear();
	m_indices.clear();
	m_maxVertexDist = 0.0f;

	LoadNodeAndChildren(m_assimp.scene->mRootNode, glm::mat4(1.0f));

	for (auto& model : m_models)
	{
		for (auto& mesh : model.meshes)
		{
			if (mesh.material->isTransparent == false)
				m_opaqueDrawCache.push_back(MeshDrawInfo{ &model, &mesh });
			else
				m_transparentDrawCache.push_back(MeshDrawInfo{ &model, &mesh });
		}
	}

	// Sort opaqe draw calls by material, then materialInstance, then mesh.
	// This minimizes the number of pipeline bindings (costly),
	// then descriptor set bindings (a bit less costly).
	//
	// For example (m = material, i = materialInstance, o = object mesh):
	//
	// | m0, i0, o0 | m0, i0, o1 | m0, i1, o2 | m1, i2, o3 |
	//
	std::sort(m_opaqueDrawCache.begin(), m_opaqueDrawCache.end(),
		[](const MeshDrawInfo& a, const MeshDrawInfo& b) {
			// Sort by material type
			if (a.mesh->material->shadingModel != b.mesh->material->shadingModel)
				return a.mesh->material->shadingModel < b.mesh->material->shadingModel;
			// Then by material pipeline
			else if (a.mesh->material->pipeline != b.mesh->material->pipeline)
				return a.mesh->material->pipeline < b.mesh->material->pipeline;
			// Then material instance
			else if (a.mesh->material != b.mesh->material)
				return a.mesh->material < b.mesh->material;
			// Then model
			else
				return a.model < b.model;
		});

	// Transparent materials need to be sorted by distance every time the camera moves
}

void Scene::SortTransparentObjects()
{
	glm::mat4 viewInverse = glm::inverse(m_camera.GetViewMatrix());
	glm::vec3 cameraPosition = viewInverse[3]; // m_camera.GetPosition();
	glm::vec3 front = viewInverse[2]; // todo: m_camera.GetForwardVector();

	// todo: assign 64 bit number to each MeshDrawInfo for sorting and
	// use this here also instead of copy pasting the sorting logic here.
	std::sort(m_transparentDrawCache.begin(), m_transparentDrawCache.end(),
		[&cameraPosition, &front](const MeshDrawInfo& a, const MeshDrawInfo& b) {
			glm::vec3 dx_a = cameraPosition - glm::vec3(a.model->GetTransform()[3]);
			glm::vec3 dx_b = cameraPosition - glm::vec3(b.model->GetTransform()[3]);
			float distA = glm::dot(front, dx_a);
			float distB = glm::dot(front, dx_b);

			// Sort by distance first
			if (distA != distB)
				return distA > distB; // back to front
			// Then by material type
			if (a.mesh->material->shadingModel != b.mesh->material->shadingModel)
				return a.mesh->material->shadingModel < b.mesh->material->shadingModel;
			// Then by material
			else if (a.mesh->material->pipeline != b.mesh->material->pipeline)
				return a.mesh->material->pipeline < b.mesh->material->pipeline;
			// Then material instance
			else if (a.mesh->material != b.mesh->material)
				return a.mesh->material < b.mesh->material;
			// Then model
			else
				return a.model < b.model;
		});
}

void Scene::LoadNodeAndChildren(aiNode* node, glm::mat4 transform)
{
	// Convert from row-major (aiMatrix4x4) to column-major (glm::mat4)
	// Note: don't know if all formats supported by assimp are row-major but Collada is.
	glm::mat4 nodeTransform = glm::transpose(glm::make_mat4(&node->mTransformation.a1));

	glm::mat4 newTransform = transform * nodeTransform;

	if (node->mNumMeshes > 0)
	{
		// Create a new model if it has mesh(es)
		Model model;
		LoadMeshes(*node, model);
		model.SetTransform(newTransform);
		m_models.push_back(std::move(model));
	}

	for (int i = 0; i < node->mNumChildren; ++i)
		LoadNodeAndChildren(node->mChildren[i], newTransform);
}

void Scene::LoadMeshes(const aiNode& fileNode, Model& model)
{
	model.meshes.reserve(m_assimp.scene->mNumMeshes);
	for (size_t i = 0; i < fileNode.mNumMeshes; ++i)
	{
		aiMesh* aMesh = m_assimp.scene->mMeshes[fileNode.mMeshes[i]];

		Mesh mesh;
		mesh.indexOffset = m_indices.size();
		mesh.nbIndices = (vk::DeviceSize)aMesh->mNumFaces * aMesh->mFaces->mNumIndices;
		mesh.material = m_materials[aMesh->mMaterialIndex];
		size_t vertexIndexOffset = m_vertices.size();

		bool hasUV = aMesh->HasTextureCoords(0);
		bool hasColor = aMesh->HasVertexColors(0);
		bool hasNormals = aMesh->HasNormals();

		for (size_t v = 0; v < aMesh->mNumVertices; ++v)
		{
			Vertex vertex;
			vertex.pos = glm::make_vec3(&aMesh->mVertices[v].x);
			vertex.texCoord = hasUV ? glm::make_vec2(&aMesh->mTextureCoords[0][v].x) : glm::vec2(0.0f);
			vertex.texCoord.y = -vertex.texCoord.y;
			vertex.normal = hasNormals ? glm::make_vec3(&aMesh->mNormals[v].x) : glm::vec3(0.0f);

			m_maxVertexDist = (std::max)(m_maxVertexDist, glm::length(vertex.pos - glm::vec3(0.0f)));

			m_vertices.push_back(vertex);
		}

		for (size_t f = 0; f < aMesh->mNumFaces; ++f)
		{
			for (size_t fi = 0; fi < aMesh->mFaces->mNumIndices; ++fi)
				m_indices.push_back(aMesh->mFaces[f].mIndices[fi] + vertexIndexOffset);
		}

		model.meshes.push_back(std::move(mesh));
	}
}

void Scene::LoadMaterials(vk::CommandBuffer commandBuffer)
{
	// Check if we already have all materials set-up
	if (m_materials.size() == m_assimp.scene->mNumMaterials)
		return;

	// Create a material instance per material description in the scene
	// todo: eventually create materials according to the needs of materials in the scene
	// to support different types of materials
	m_materials.resize(m_assimp.scene->mNumMaterials);
	for (size_t i = 0; i < m_materials.size(); ++i)
	{
		auto& assimpMaterial = m_assimp.scene->mMaterials[i];

		// Properties
		LitMaterialProperties properties;

		aiColor4D color;
		assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color);
		properties.phong.diffuse = glm::make_vec4(&color.r);

		assimpMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color);
		properties.phong.specular = glm::make_vec4(&color.r);

		assimpMaterial->Get(AI_MATKEY_SHININESS, properties.phong.shininess);

		// If a material is transparent, opacity will be fetched
		// from the diffuse alpha channel (material property * diffuse texture)
		float opacity = 1.0f;
		assimpMaterial->Get(AI_MATKEY_OPACITY, opacity);

		// todo: reuse same materials for materials with the same name
		MaterialInfo materialInfo;
		materialInfo.baseMaterial = BaseMaterialID::Phong;
		materialInfo.isTransparent = opacity < 1.0f;
		materialInfo.constants.nbLights = m_lights.size();
		materialInfo.constants.useShadows = 1;
		auto* material = m_materialCache->CreateMaterial(materialInfo);

#ifdef DEBUG_MODE
		aiString name;
		assimpMaterial->Get(AI_MATKEY_NAME, name);
		material->name = std::string(name.C_Str());
#endif

		// Create default textures
		const auto& bindings = material->pipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::Material);
		for (const auto& binding : bindings)
		{
			if (binding.descriptorType == vk::DescriptorType::eCombinedImageSampler)
			{
				for (int i = 0; i < binding.descriptorCount; ++i)
				{
					CombinedImageSampler texture = m_textureCache->LoadTexture("dummy_texture.png");
					material->textures.push_back(std::move(texture));
				}
			}
		}

		// Load textures
		auto loadTexture = [this, &assimpMaterial, &material, &commandBuffer](aiTextureType type, uint32_t binding) { // todo: move this to a function
			int textureCount = assimpMaterial->GetTextureCount(type);
			if (textureCount > 0 && binding < material->textures.size())
			{
				aiString textureFile;
				assimpMaterial->GetTexture(type, 0, &textureFile);
				auto texture = m_textureCache->LoadTexture(textureFile.C_Str());
				material->textures[binding] = std::move(texture); // replace dummy with real texture
			}
		};
		// todo: use sRGB format for color textures if necessary
		// it looks like gamma correction is OK for now but it might
		// not be the case for all textures
		loadTexture(aiTextureType_DIFFUSE, 0);
		loadTexture(aiTextureType_SPECULAR, 1);

		// Environment mapping
		assimpMaterial->Get(AI_MATKEY_REFRACTI, properties.env.ior);
		properties.env.metallic = 0.0f;
		properties.env.transmission = 0.0f;

		CombinedImageSampler skyboxCubeMap = m_skybox->GetCubeMap();
		if (skyboxCubeMap.texture != nullptr)
			material->cubeMaps.push_back(std::move(skyboxCubeMap));

		// Upload properties to uniform buffer
		material->uniformBuffer = std::make_unique<UniqueBufferWithStaging>(sizeof(LitMaterialProperties), vk::BufferUsageFlagBits::eUniformBuffer);
		memcpy(material->uniformBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(&properties), sizeof(LitMaterialProperties));
		material->uniformBuffer->CopyStagingToGPU(commandBuffer);
		m_commandBufferPool->DestroyAfterSubmit(material->uniformBuffer->ReleaseStagingBuffer());

		// Keep ownership of the material instance
		m_materials[i] = material;
	}

	m_textureCache->UploadTextures(commandBuffer, *m_commandBufferPool);
}

void Scene::CreateLightsUniformBuffers(vk::CommandBuffer commandBuffer)
{
	if (m_lights.empty() == false)
	{
		vk::DeviceSize bufferSize = m_lights.size() * sizeof(Light);
		m_lightsUniformBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer);
		memcpy(m_lightsUniformBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_lights.data()), bufferSize);
		m_lightsUniformBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		m_commandBufferPool->DestroyAfterSubmit(m_lightsUniformBuffer->ReleaseStagingBuffer());
	}
}

void Scene::CreateViewUniformBuffers()
{
	// Per view
	m_viewUniformBuffers.clear();
	m_viewUniformBuffers.reserve(m_commandBufferPool->GetNbConcurrentSubmits());
	for (uint32_t i = 0; i < m_commandBufferPool->GetNbConcurrentSubmits(); ++i)
	{
		m_viewUniformBuffers.emplace_back(
			vk::BufferCreateInfo(
				{},
				sizeof(ViewUniforms),
				vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc // needs TransferSrc?
			), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
		);
	}
}

void Scene::CreateDescriptorPool()
{
	// Reset used descriptors while they're still valid
	for (size_t i = 0; i < m_models.size(); ++i)
		m_models[i].descriptorSet.reset();

	for (auto& materialInstance : m_materials)
		materialInstance->descriptorSet.reset();

	// Then reset pool
	m_descriptorPool.reset();

	// Sum view descriptor needs for all material types.
	// Each material can need different view parameters (e.g. Unlit doesn't need lights).
	// Need a set of descriptors per concurrent frame
	std::map<vk::DescriptorType, uint32_t> descriptorCount;
	for (const auto& pipeline : m_materialCache->GetGraphicsPipelines())
	{
		const auto& bindings = pipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::View);
		for (const auto& binding : bindings)
		{
			descriptorCount[binding.descriptorType] += binding.descriptorCount * m_commandBufferPool->GetNbConcurrentSubmits();
		}
	}

	// Sum model descriptors
	for (const auto& model : m_models)
	{
		// Pick model layout from any material, they should be compatible
		const auto* pipeline = model.meshes[0].material->pipeline;
		const auto& bindings = pipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::Model);
		for (const auto& binding : bindings)
		{
			descriptorCount[binding.descriptorType] += binding.descriptorCount;
		}
	}

	// Sum material instance descriptors
	for (const auto& material : m_materials)
	{
		// Each material can have different descriptor set layout
		// Each material instance has its own descriptor sets
		const auto* pipeline = material->pipeline;
		const auto& bindings = pipeline->GetDescriptorSetLayoutBindings((size_t)DescriptorSetIndices::Material);
		for (const auto& binding : bindings)
		{
			descriptorCount[binding.descriptorType] += binding.descriptorCount;
		}
	}

	uint32_t maxNbSets = 0;
	std::vector<vk::DescriptorPoolSize> poolSizes;
	poolSizes.reserve(descriptorCount.size());
	for (const auto& descriptor : descriptorCount)
	{
		poolSizes.emplace_back(descriptor.first, descriptor.second);
		maxNbSets += descriptor.second;
	}

	// If the number of required descriptors were to change at run-time
	// we could have a descriptorPool per concurrent frame and reset the pool
	// to increase its size while it's not in use.
	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		maxNbSets,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));
}

void Scene::CreateDescriptorSets()
{
	CreateDescriptorPool();
	CreateDescriptorLayouts();
	UpdateMaterialDescriptors();
}

// todo: this is actually CreateDescriptorSets
void Scene::CreateDescriptorLayouts()
{
	size_t set = (size_t)DescriptorSetIndices::View;

	std::vector<vk::DescriptorSetLayout> viewSetLayouts;
	viewSetLayouts.resize((size_t)ShadingModel::Count);

	// Unlit view layout for Grid and Skybox
	viewSetLayouts[(size_t)ShadingModel::Unlit] = m_skybox->GetGraphicsPipeline().GetDescriptorSetLayout(set);

	// Materials use only lit shading model (for now) todo: that might not always be the case
	viewSetLayouts[(size_t)ShadingModel::Lit] = m_materials.front()->pipeline->GetDescriptorSetLayout(set);

	// View layout and descriptor sets
	for (size_t materialType = 0; materialType < (size_t)ShadingModel::Count; ++materialType)
	{
		auto& litViewSetLayout = viewSetLayouts[materialType];
		std::vector<vk::DescriptorSetLayout> layouts(m_commandBufferPool->GetNbConcurrentSubmits(), litViewSetLayout);
		m_viewDescriptorSets[materialType] = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
			m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
		));
	}
}

void Scene::UpdateMaterialDescriptors()
{
	// Create view descriptor set.
	// Ask any graphics pipeline to provide the view layout
	// since all surface materials should share this layout
	for (size_t materialType = 0; materialType < (size_t)ShadingModel::Count; ++materialType)
	{
		auto& viewDescriptorSets = m_viewDescriptorSets[materialType];

		// Update view descriptor sets
		for (size_t i = 0; i < viewDescriptorSets.size(); ++i)
		{
			uint32_t binding = 0;
			vk::DescriptorBufferInfo descriptorBufferInfoView(m_viewUniformBuffers[i].Get(), 0, sizeof(ViewUniforms));
			std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
				vk::WriteDescriptorSet(
					viewDescriptorSets[i].get(), 0, {},
					1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoView
				) // binding = 0
			};

			if ((ShadingModel)materialType == ShadingModel::Lit)
			{
				vk::DescriptorBufferInfo descriptorBufferInfoLights(m_lightsUniformBuffer->Get(), 0, sizeof(Light) * m_lights.size());
				writeDescriptorSets.push_back(
					vk::WriteDescriptorSet(
						viewDescriptorSets[i].get(), 1, {},
						1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoLights
					) // binding = 2
				);
			}

			g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	// Create model descriptor sets
	// Ask any graphics pipeline to provide the model layout
	// since all material of this type should share this layout
	{
		// Then allocate one descriptor set per model
		uint32_t set = (uint32_t)DescriptorSetIndices::Model;
		auto modelSetLayout = m_materials[0]->pipeline->GetDescriptorSetLayout((size_t)DescriptorSetIndices::Model);
		std::vector<vk::DescriptorSetLayout> layouts(m_models.size(), modelSetLayout);
		auto modelDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
			m_descriptorPool.get(), static_cast<uint32_t>(layouts.size()), layouts.data()
		));

		// and update each one
		for (size_t i = 0; i < m_models.size(); ++i)
		{
			m_models[i].descriptorSet = std::move(modelDescriptorSets[i]);

			uint32_t binding = 0;
			vk::DescriptorBufferInfo descriptorBufferInfo(m_models[i].uniformBuffer->Get(), 0, sizeof(ModelUniforms));
			std::array<vk::WriteDescriptorSet, 1> writeDescriptorSets = {
				vk::WriteDescriptorSet(
					m_models[i].descriptorSet.get(), binding, {},
					1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
				) // binding = 0
			};
			g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	// Create material instance descriptor sets.
	for (auto& material : m_materials)
	{
		size_t set = (size_t)DescriptorSetIndices::Material;
		auto descriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
			m_descriptorPool.get(), 1, &material->pipeline->GetDescriptorSetLayout(set)
		));
		material->descriptorSet = std::move(descriptorSets[0]);

		uint32_t binding = 0;
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets;

		// Material properties in uniform buffer
		vk::DescriptorBufferInfo descriptorBufferInfo(material->uniformBuffer->Get(), 0, material->uniformBuffer->Size());
		writeDescriptorSets.push_back(
			vk::WriteDescriptorSet(
				material->descriptorSet.get(), binding++, {},
				1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
			) // binding = 0
		);

		// Material's textures
		std::vector<vk::DescriptorImageInfo> imageInfos;
		if (material->textures.empty() == false)
		{
			for (const auto& materialTexture : material->textures)
			{
				imageInfos.emplace_back(
					materialTexture.sampler,
					materialTexture.texture->GetImageView(),
					vk::ImageLayout::eShaderReadOnlyOptimal
				);
			}
			writeDescriptorSets.push_back(
				vk::WriteDescriptorSet(
					material->descriptorSet.get(), binding++, {},
					static_cast<uint32_t>(imageInfos.size()), vk::DescriptorType::eCombinedImageSampler, imageInfos.data(), nullptr
				) // binding = 1
			);
		}

		// Material's cubemaps on a separate binding
		std::vector<vk::DescriptorImageInfo> cubemapsInfo;
		if (material->cubeMaps.empty() == false)
		{
			for (const auto& materialTexture : material->cubeMaps)
			{
				cubemapsInfo.emplace_back(
					materialTexture.sampler,
					materialTexture.texture->GetImageView(),
					vk::ImageLayout::eShaderReadOnlyOptimal
				);
			}
			writeDescriptorSets.push_back(
				vk::WriteDescriptorSet(
					material->descriptorSet.get(), binding++, {},
					static_cast<uint32_t>(cubemapsInfo.size()), vk::DescriptorType::eCombinedImageSampler, cubemapsInfo.data(), nullptr
				) // binding = 2
			);
		}

		g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}
}

void Scene::SetShadowCaster(vk::CommandBuffer& commandBuffer, const glm::mat4& transform, uint32_t shadowMapIndex, uint32_t imageIndex)
{
	// Light transform

	m_viewUniforms.lightTransform = transform;
	m_viewUniforms.shadowMapIndex = shadowMapIndex;

	// Upload to GPU
	const auto& viewUniformBuffer = m_viewUniformBuffers[imageIndex % m_commandBufferPool->GetNbConcurrentSubmits()];
	memcpy(
		viewUniformBuffer.GetMappedData(),
		reinterpret_cast<const void*>(&m_viewUniforms), sizeof(ViewUniforms)
	);

	// Push constant
	if (m_materials.empty() == false)
	{
		commandBuffer.pushConstants(
			m_materials[0]->pipeline->GetPipelineLayout(0),
			vk::ShaderStageFlagBits::eFragment, 0, sizeof(uint32_t), &shadowMapIndex
		);
	}
}

void Scene::UpdateShadowMaps(const std::vector<CombinedImageSampler>& shadowMaps)
{
	const uint32_t kShadowMapsBinding = 2;

	uint32_t nbShadowTextures = 0;

	// Make sure we match the number of shadow maps from the pipeline
	auto* material = m_materials[0];
	const auto& bindings = m_materials[0]->pipeline->GetDescriptorSetLayoutBindings(0);
	for (const auto& binding : bindings)
	{
		if (binding.binding == kShadowMapsBinding)
		{
			nbShadowTextures = binding.descriptorCount;
			break;
		}
	}

	std::vector<vk::DescriptorImageInfo> texturesInfo;
	texturesInfo.reserve(shadowMaps.size());
	for (int i = 0; i < nbShadowTextures; ++i)
	{
		const auto& shadowMap = shadowMaps[i % shadowMaps.size()]; // mustdo: don't wrap, replace with dummy instead
		texturesInfo.emplace_back(
			shadowMap.sampler,
			shadowMap.texture->GetImageView(),
			vk::ImageLayout::eDepthStencilReadOnlyOptimal
		);
	}

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets;
	writeDescriptorSets.reserve(m_viewDescriptorSets[(size_t)ShadingModel::Lit].size());
	for (auto& viewDescriptorSet : m_viewDescriptorSets[(size_t)ShadingModel::Lit])
	{
		writeDescriptorSets.push_back(vk::WriteDescriptorSet(
			viewDescriptorSet.get(), kShadowMapsBinding, {},
			static_cast<uint32_t>(texturesInfo.size()),
			vk::DescriptorType::eCombinedImageSampler, texturesInfo.data(), nullptr
		));
	}
	g_device->Get().updateDescriptorSets(
		static_cast<uint32_t>(writeDescriptorSets.size()),
		writeDescriptorSets.data(),
		0, nullptr
	);
}

void Scene::UploadToGPU(vk::CommandBuffer& commandBuffer)
{
	// Upload Geometry
	{
		vk::DeviceSize bufferSize = sizeof(m_vertices[0]) * m_vertices.size();
		m_vertexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer);
		memcpy(m_vertexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_vertices.data()), bufferSize);
		m_vertexBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		m_commandBufferPool->DestroyAfterSubmit(m_vertexBuffer->ReleaseStagingBuffer());
		m_vertices.clear();
	}
	{
		vk::DeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();
		m_indexBuffer = std::make_unique<UniqueBufferWithStaging>(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer);
		memcpy(m_indexBuffer->GetStagingMappedData(), reinterpret_cast<const void*>(m_indices.data()), bufferSize);
		m_indexBuffer->CopyStagingToGPU(commandBuffer);

		// We won't need the staging buffer after the initial upload
		m_commandBufferPool->DestroyAfterSubmit(m_indexBuffer->ReleaseStagingBuffer());
		m_indices.clear();
	}

	// Upload textures
	m_textureCache->UploadTextures(commandBuffer, *m_commandBufferPool);

	// Skybox
	m_skybox->UploadToGPU(commandBuffer, *m_commandBufferPool);
}

UniqueBuffer& Scene::GetViewUniformBuffer(uint32_t imageIndex)
{
	return m_viewUniformBuffers[imageIndex % m_commandBufferPool->GetNbConcurrentSubmits()];
}
