#include "Scene.h"

#include "CommandBufferPool.h"
#include "Material.h"
#include "ShadowMap.h"

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
	GraphicsPipelineSystem& graphicsPipelineSystem,
	TextureCache& textureCache,
	ModelSystem& modelSystem,
	MaterialSystem& materialSystem,
	const RenderPass& renderPass, vk::Extent2D imageExtent
)
	: m_commandBufferPool(&commandBufferPool)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_basePath(std::move(basePath))
	, m_sceneFilename(std::move(sceneFilename))
	, m_renderPass(&renderPass)
	, m_imageExtent(imageExtent)
	, m_modelSystem(&modelSystem)
	, m_textureCache(&textureCache)
	, m_materialSystem(&materialSystem)
	, m_camera(
		1.0f * glm::vec3(1.0f, 1.0f, 1.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f), 45.0f, 0.01f, 100.0f,
		m_imageExtent.width, m_imageExtent.height)
	, m_boundingBox()
{
}

void Scene::Reset(vk::CommandBuffer& commandBuffer, const RenderPass& renderPass, vk::Extent2D imageExtent)
{
	m_renderPass = &renderPass;
	m_imageExtent = imageExtent;

	m_camera.SetImageExtent(m_imageExtent.width, m_imageExtent.height);

	m_materialSystem->Reset(m_renderPass->Get(), m_imageExtent);
	m_skybox->Reset(*m_renderPass, m_imageExtent);

	UpdateMaterialDescriptors();
}

void Scene::Load(vk::CommandBuffer commandBuffer)
{
	m_skybox = std::make_unique<Skybox>(*m_renderPass, m_imageExtent, *m_textureCache, *m_graphicsPipelineSystem);
	LoadScene(commandBuffer);

	CreateLightsUniformBuffers(commandBuffer);
	CreateViewUniformBuffers();
	UpdateMaterialDescriptors();

	UploadToGPU(commandBuffer);
}

void Scene::Update(uint32_t imageIndex)
{
	vk::Extent2D extent = m_imageExtent;

	m_viewUniforms.pos = m_camera.GetEye();
	m_viewUniforms.view = m_camera.GetViewMatrix();
	m_viewUniforms.proj = m_camera.GetProjectionMatrix();

	// Upload to GPU
	auto& uniformBuffer = GetViewUniformBuffer(imageIndex);
	memcpy(uniformBuffer.GetMappedData(), reinterpret_cast<const void*>(&m_viewUniforms), sizeof(LitViewProperties));
}

void Scene::DrawOpaqueObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const
{
	DrawSceneObjects(commandBuffer, frameIndex, renderState, m_opaqueDrawCache);

	// With Skybox last (to prevent processing fragments for nothing)
	{
		uint32_t concurrentFrameIndex = frameIndex % m_commandBufferPool->GetNbConcurrentSubmits();
		vk::DescriptorSet viewDescriptorSet = m_unlitViewDescriptorSets[concurrentFrameIndex].get();
		renderState.BindPipeline(commandBuffer, m_skybox->GetGraphicsPipelineID());
		renderState.BindView(commandBuffer, Material::ShadingModel::Unlit, viewDescriptorSet);
		m_skybox->Draw(commandBuffer, frameIndex);
	}
}

void Scene::DrawTransparentObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const
{
	DrawSceneObjects(commandBuffer, frameIndex, renderState, m_transparentDrawCache);
}

void Scene::DrawAllWithoutShading(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, vk::PipelineLayout modelPipelineLayout, vk::DescriptorSet modelDescriptorSet) const
{
	DrawWithoutShading(commandBuffer, frameIndex, modelPipelineLayout, modelDescriptorSet, m_opaqueDrawCache);
	DrawWithoutShading(commandBuffer, frameIndex, modelPipelineLayout, modelDescriptorSet, m_transparentDrawCache);
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
		auto shadingModel = drawItem.mesh.shadingModel;
		vk::DescriptorSet viewDescriptorSet = m_materialSystem->GetDescriptorSet(DescriptorSetIndex::View, currentFrameIndex);
		state.BindPipeline(commandBuffer, m_materialSystem->GetGraphicsPipelineID(drawItem.mesh.materialInstanceID));
		state.BindView(commandBuffer, shadingModel, viewDescriptorSet);
		state.BindModel(commandBuffer, drawItem.model);
		state.BindMaterial(commandBuffer, drawItem.mesh.materialInstanceID);

		// Draw
		commandBuffer.drawIndexed(drawItem.mesh.nbIndices, 1, drawItem.mesh.indexOffset, 0, 0);
	}
}

void Scene::DrawWithoutShading(vk::CommandBuffer& commandBuffer, uint32_t frameIndex, vk::PipelineLayout modelPipelineLayout, vk::DescriptorSet modelDescriptorSet, const std::vector<MeshDrawInfo>& drawCalls) const
{
	uint32_t currentFrameIndex = frameIndex % m_commandBufferPool->GetNbConcurrentSubmits();

	if (drawCalls.empty() == false)
	{
		// Bind the one big vertex + index buffers
		vk::DeviceSize offsets[] = { 0 };
		vk::Buffer vertexBuffers[] = { m_vertexBuffer->Get() };
		commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
		commandBuffer.bindIndexBuffer(m_indexBuffer->Get(), 0, vk::IndexType::eUint32);

		// Bind the model transforms uniform buffer
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			modelPipelineLayout, (uint32_t)DescriptorSetIndex::Model,
			1, &modelDescriptorSet,
			0, nullptr
		);
	}

	for (const auto& drawItem : drawCalls)
	{
		uint32_t modelIndex = drawItem.model;

		// Set model index push constant
		commandBuffer.pushConstants(
			modelPipelineLayout,
			vk::ShaderStageFlagBits::eVertex,
			0, sizeof(uint32_t), &modelIndex
		);

		commandBuffer.drawIndexed(drawItem.mesh.nbIndices, 1, drawItem.mesh.indexOffset, 0, 0);
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

	auto sceneName = m_basePath + "/" + m_sceneFilename;

	m_assimp.importer = std::make_unique<Assimp::Importer>();
	m_assimp.scene = m_assimp.importer->ReadFile(sceneName.c_str(), 0);
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
	m_nbShadowCastingLights = 0;
	for (int i = 0; i < m_assimp.scene->mNumLights; ++i)
	{
		aiLight* aLight = m_assimp.scene->mLights[i];

		aiNode* node = m_assimp.scene->mRootNode->FindNode(aLight->mName);
		glm::mat4 transform = ::ComputeAiNodeGlobalTransform(node);

		PhongLight light;
		light.type = (int)aLight->mType;
		light.ambient = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f); // add a little until we have global illumination
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

		// mustdo: different shadow types (2d, cascade, cube) will need a different index count
		if (light.type == aiLightSource_DIRECTIONAL)
			light.shadowIndex = m_nbShadowCastingLights++;

		m_lights.push_back(std::move(light));
	}

	// todo: support no shadow casting lights

	if (m_nbShadowCastingLights > 0)
	{
		// Reserve shadow data for each light
		m_shadowDataBuffer = std::make_unique<UniqueBuffer>(
			vk::BufferCreateInfo(
				{},
				m_nbShadowCastingLights * sizeof(ShadowData),
				vk::BufferUsageFlagBits::eUniformBuffer
			), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
		);
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

	m_modelSystem->UploadUniformBuffer(*m_commandBufferPool);

	m_modelSystem->ForEachMesh([this](ModelID id, Mesh mesh) {
		MeshDrawInfo info = { id, std::move(mesh) };
		if (m_materialSystem->IsTransparent(mesh.materialInstanceID) == false)
			m_opaqueDrawCache.push_back(std::move(info));
		else
			m_transparentDrawCache.push_back(std::move(info));
	});

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
			if (a.mesh.shadingModel != b.mesh.shadingModel)
				return a.mesh.shadingModel < b.mesh.shadingModel;
			// Then material instance
			else if (a.mesh.materialInstanceID != b.mesh.materialInstanceID)
				return a.mesh.materialInstanceID < b.mesh.materialInstanceID;
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
		[&cameraPosition, &front, this](const MeshDrawInfo& a, const MeshDrawInfo& b) {
			glm::vec3 dx_a = cameraPosition - glm::vec3(m_modelSystem->GetTransform(a.model)[3]);
			glm::vec3 dx_b = cameraPosition - glm::vec3(m_modelSystem->GetTransform(b.model)[3]);
			float distA = glm::dot(front, dx_a);
			float distB = glm::dot(front, dx_b);

			// Sort by distance first
			if (distA != distB)
				return distA > distB; // back to front
			// Then by material type
			if (a.mesh.shadingModel != b.mesh.shadingModel)
				return a.mesh.shadingModel < b.mesh.shadingModel;
			// Then material instance
			else if (a.mesh.materialInstanceID != b.mesh.materialInstanceID)
				return a.mesh.materialInstanceID < b.mesh.materialInstanceID;
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
		LoadModel(*node, newTransform);
	}

	for (int i = 0; i < node->mNumChildren; ++i)
		LoadNodeAndChildren(node->mChildren[i], newTransform);
}

ModelID Scene::LoadModel(const aiNode& fileNode, glm::mat4 transform)
{
	constexpr float maxFloat = std::numeric_limits<float>::max();

	BoundingBox box;
	
	std::vector<Mesh> meshes;
	meshes.reserve(m_assimp.scene->mNumMeshes);

	for (size_t i = 0; i < fileNode.mNumMeshes; ++i)
	{
		aiMesh* aMesh = m_assimp.scene->mMeshes[fileNode.mMeshes[i]];

		Mesh mesh;
		mesh.indexOffset = m_indices.size();
		mesh.nbIndices = (vk::DeviceSize)aMesh->mNumFaces * aMesh->mFaces->mNumIndices;
		mesh.materialInstanceID = m_materials[aMesh->mMaterialIndex];
		mesh.shadingModel = Material::ShadingModel::Lit;
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
			box.min = (glm::min)(box.min, vertex.pos);
			box.max = (glm::max)(box.max, vertex.pos);

			m_vertices.push_back(vertex);
		}

		for (size_t f = 0; f < aMesh->mNumFaces; ++f)
		{
			for (size_t fi = 0; fi < aMesh->mFaces->mNumIndices; ++fi)
				m_indices.push_back(aMesh->mFaces[f].mIndices[fi] + vertexIndexOffset);
		}

		meshes.push_back(std::move(mesh));
	}

	ModelID id = m_modelSystem->CreateModel(transform, box, meshes);

	// Transform the bounding box to world space
	box.Transform(transform);

	// Then add it to the global world bounding box
	m_boundingBox = m_boundingBox.Union(box);

	return id;
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
		LitMaterialInstanceInfo materialInfo;

		aiColor4D color;
		assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color);
		materialInfo.properties.phong.diffuse = glm::make_vec4(&color.r);

		assimpMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color);
		materialInfo.properties.phong.specular = glm::make_vec4(&color.r);

		assimpMaterial->Get(AI_MATKEY_SHININESS, materialInfo.properties.phong.shininess);

		// If a material is transparent, opacity will be fetched
		// from the diffuse alpha channel (material property * diffuse texture)
		float opacity = 1.0f;
		assimpMaterial->Get(AI_MATKEY_OPACITY, opacity);

		// todo: reuse same materials for materials with the same name
		materialInfo.pipelineProperties.isTransparent = opacity < 1.0f;

//#ifdef DEBUG_MODE
//		aiString name;
//		assimpMaterial->Get(AI_MATKEY_NAME, name);
//		material->name = std::string(name.C_Str());
//#endif
	
		// Create default textures
		TextureID dummyTextureID = m_textureCache->LoadTexture("dummy_texture.png");
		for (int textureIndex = 0; textureIndex < (int)PhongMaterialTextures::eCount; ++textureIndex)
			materialInfo.properties.textures[textureIndex] = dummyTextureID;

		// Load textures
		auto loadTexture = [this, &assimpMaterial, &commandBuffer, &materialInfo](aiTextureType type, size_t textureIndex) { // todo: move this to a function
			if (assimpMaterial->GetTextureCount(type) > 0)
			{
				aiString textureFile;
				assimpMaterial->GetTexture(type, 0, &textureFile);
				materialInfo.properties.textures[textureIndex] = m_textureCache->LoadTexture(textureFile.C_Str());
			}
		};

		// todo: use sRGB format for color textures if necessary
		// it looks like gamma correction is OK for now but it might
		// not be the case for all textures
		loadTexture(aiTextureType_DIFFUSE, (size_t)PhongMaterialTextures::eDiffuse);
		loadTexture(aiTextureType_SPECULAR, (size_t)PhongMaterialTextures::eSpecular);

		// Environment mapping
		assimpMaterial->Get(AI_MATKEY_REFRACTI, materialInfo.properties.env.ior);
		materialInfo.properties.env.metallic = 0.0f;
		materialInfo.properties.env.transmission = 0.0f;

		TextureID skyboxCubeMap = m_skybox->GetCubeMap();
		if (skyboxCubeMap != ~0)
			materialInfo.properties.env.cubeMapTexture = skyboxCubeMap;

		auto materialInstanceID = m_materialSystem->CreateMaterialInstance(materialInfo);

		// Keep ownership of the material instance
		m_materials[i] = materialInstanceID;
	}

	m_textureCache->UploadTextures(*m_commandBufferPool);
}

void Scene::CreateLightsUniformBuffers(vk::CommandBuffer commandBuffer)
{
	if (m_lights.empty() == false)
	{
		vk::DeviceSize bufferSize = m_lights.size() * sizeof(PhongLight);
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
				sizeof(LitViewProperties),
				vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc // needs TransferSrc?
			), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
		);
	}
}

void Scene::UpdateMaterialDescriptors()
{
	// Create unlit view descriptor set (we don't have a material system for this yet)
	// todo: this could be handled in skybox

	// Create descriptor sets if required (todo: do before this and skip the if)

	if (m_descriptorPool)
	{
		for (auto& set : m_unlitViewDescriptorSets)
			set.reset();

		g_device->Get().resetDescriptorPool(m_descriptorPool.get());
	}

	// Set 0 (view): { 1 uniform buffer for view uniforms per concurrentFrames }
	std::array<std::pair<vk::DescriptorType, uint16_t>, 1ULL> descriptorCount = {
		std::make_pair(vk::DescriptorType::eUniformBuffer, 1U),
	};

	SmallVector<vk::DescriptorPoolSize> poolSizes;
	for (const auto& descriptor : descriptorCount)
		poolSizes.emplace_back(descriptor.first, descriptor.second);

	const uint32_t maxNbSets = m_commandBufferPool->GetNbConcurrentSubmits();
	m_descriptorPool = g_device->Get().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		maxNbSets,
		static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
	));

	vk::DescriptorSetLayout viewSetLayout = m_skybox->GetDescriptorSetLayout((uint8_t)DescriptorSetIndex::View);
	std::vector<vk::DescriptorSetLayout> setLayouts(m_commandBufferPool->GetNbConcurrentSubmits(), viewSetLayout);
	m_unlitViewDescriptorSets = g_device->Get().allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(
		m_descriptorPool.get(), (uint32_t)setLayouts.size(), setLayouts.data()
	));

	for (int frameIndex = 0; frameIndex < m_commandBufferPool->GetNbConcurrentSubmits(); ++frameIndex)
	{
		auto& viewDescriptorSet = m_unlitViewDescriptorSets[frameIndex];

		vk::DescriptorBufferInfo descriptorBufferInfoView(m_viewUniformBuffers[frameIndex].Get(), 0, sizeof(LitViewProperties));
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
			vk::WriteDescriptorSet(
				viewDescriptorSet.get(), 0, {},
				1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfoView
			) // binding = 0
		};

		g_device->Get().updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	MaterialSystem::ShaderConstants constants = {
		MaterialSystem::VertexShaderConstants{
			(uint32_t)m_modelSystem->GetModelCount()
		},
		MaterialSystem::FragmentShaderConstants{
			(uint32_t)m_lights.size(),
			(uint32_t)m_nbShadowCastingLights,
			(uint32_t)m_textureCache->GetTextureCount(ImageViewType::e2D),
			(uint32_t)m_textureCache->GetTextureCount(ImageViewType::eCube),
			(uint32_t)m_materialSystem->GetMaterialInstanceCount()
		}
	};
	m_materialSystem->UploadToGPU(*m_commandBufferPool, std::move(constants));

	// Bind view to material descriptor set
	SmallVector<vk::Buffer> viewUniformBuffers;
	viewUniformBuffers.reserve(m_viewUniformBuffers.size());
	for (int i = 0; i < (int)m_viewUniformBuffers.size(); ++i)
		viewUniformBuffers.push_back(m_viewUniformBuffers[i].Get());

	m_materialSystem->UpdateViewDescriptorSets(
		viewUniformBuffers, m_viewUniformBuffers[0].Size(),
		m_lightsUniformBuffer->Get(), m_lightsUniformBuffer->Size()
	);

	// Bind model to material descriptor set
	const auto& modelUniformBuffer = m_modelSystem->GetUniformBuffer();
	m_materialSystem->UpdateModelDescriptorSet(modelUniformBuffer.Get(), modelUniformBuffer.Size());

	// Update material descriptor sets
	m_materialSystem->UpdateMaterialDescriptorSet();
}

void Scene::UpdateShadowMapsTransforms(const std::vector<glm::mat4>& transforms)
{
	if (m_shadowDataBuffer == nullptr)
		return;

	auto* transformData = m_shadowDataBuffer->GetMappedData();
	ASSERT(transforms.size() * sizeof(ShadowData) <= m_shadowDataBuffer->Size());
	for (int i = 0; i < transforms.size(); ++i)
	{
		ShadowData data = { transforms[i] };
		void* dest = (char*)transformData + i * sizeof(ShadowData);
		memcpy(dest, &data, sizeof(ShadowData));
	}
}

void Scene::InitShadowMaps(const std::vector<const ShadowMap*>& shadowMaps)
{
	if (m_shadowDataBuffer == nullptr)
		return;

	const uint32_t kShadowMapsBinding = 2;
	const uint32_t kShadowDataBinding = 3;

	uint32_t nbShadowTextures = 0;

	// Make sure we match the number of shadow maps from the pipeline
	const auto& pipelineSystem = m_materialSystem->GetGraphicsPipelineSystem();
	const auto& bindings = pipelineSystem.GetDescriptorSetLayoutBindings(m_materialSystem->GetGraphicsPipelinesIDs().front(), 0);
	for (const auto& binding : bindings)
	{
		if (binding.binding == kShadowMapsBinding)
		{
			nbShadowTextures = binding.descriptorCount;
			break;
		}
	}

	SmallVector<vk::DescriptorImageInfo, 16> texturesInfo;
	texturesInfo.reserve(nbShadowTextures);
	for (int i = 0; i < nbShadowTextures; ++i)
	{
		const auto& shadowMap = *shadowMaps[i % shadowMaps.size()]; // mustdo: don't wrap, replace with dummy instead
		auto combinedImageSampler = shadowMap.GetCombinedImageSampler();
		texturesInfo.emplace_back(
			combinedImageSampler.sampler,
			combinedImageSampler.texture->GetImageView(),
			vk::ImageLayout::eDepthStencilReadOnlyOptimal
		);
	}

	// Update shadow transforms
	auto* transformData = m_shadowDataBuffer->GetMappedData();
	ASSERT(shadowMaps.size() * sizeof(ShadowData) <= m_shadowDataBuffer->Size());
	for (int i = 0; i < shadowMaps.size(); ++i)
	{
		const auto& shadowMap = *shadowMaps[i];
		ShadowData data = { shadowMap.GetLightTransform() };
		void* dest = (char*)transformData + i * sizeof(ShadowData);
		memcpy(dest, &data, sizeof(ShadowData));
	}

	// Update shadow system shader view descriptor sets
	m_materialSystem->UpdateShadowDescriptorSets(
		texturesInfo, m_shadowDataBuffer->Get(), m_shadowDataBuffer->Size()
	);

	// todo: move this to shadow system

	// Update shadow system model descriptor set
	const UniqueBuffer& uniformBuffer = m_modelSystem->GetUniformBuffer();
	const auto& transforms = m_modelSystem->GetTransforms();
	vk::DescriptorBufferInfo descriptorBufferInfo(uniformBuffer.Get(), 0, uniformBuffer.Size());

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets;
	for (auto& shadowMap : shadowMaps)
	{
		const uint32_t binding = 0;
		vk::DescriptorSet descriptorSet = shadowMap->GetDescriptorSet(DescriptorSetIndex::Model);
		writeDescriptorSets.push_back(
			vk::WriteDescriptorSet(
				descriptorSet, binding, {},
				1, vk::DescriptorType::eUniformBuffer, nullptr, &descriptorBufferInfo
			)
		);
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

	// Skybox
	m_skybox->UploadToGPU(commandBuffer, *m_commandBufferPool);
}

UniqueBuffer& Scene::GetViewUniformBuffer(uint32_t imageIndex)
{
	return m_viewUniformBuffers[imageIndex % m_commandBufferPool->GetNbConcurrentSubmits()];
}
