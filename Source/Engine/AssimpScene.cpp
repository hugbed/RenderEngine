#include "AssimpScene.h"

#include "CommandBufferPool.h"
#include "MaterialSystem.h"
#include "ShadowSystem.h"
#include "SceneTree.h"

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

AssimpScene::AssimpScene(
	std::string basePath,
	std::string sceneFilename,
	CommandBufferPool& commandBufferPool,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	TextureSystem& textureSystem,
	MeshAllocator& meshAllocator,
	LightSystem& lightSystem,
	MaterialSystem& materialSystem,
	ShadowSystem& shadowSystem,
	SceneTree& sceneTree,
	const RenderPass& renderPass, vk::Extent2D imageExtent
)
	: m_commandBufferPool(&commandBufferPool)
	, m_graphicsPipelineSystem(&graphicsPipelineSystem)
	, m_sceneDir(std::move(basePath))
	, m_sceneFilename(std::move(sceneFilename))
	, m_renderPass(&renderPass)
	, m_imageExtent(imageExtent)
	, m_meshAllocator(&meshAllocator)
	, m_textureSystem(&textureSystem)
	, m_lightSystem(&lightSystem)
	, m_viewUniforms({})
	, m_materialSystem(&materialSystem)
	, m_shadowSystem(&shadowSystem)
	, m_sceneTree(&sceneTree)
	, m_camera(
		1.0f * glm::vec3(1.0f, 1.0f, 1.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f), 45.0f, 0.01f, 100.0f,
		m_imageExtent.width, m_imageExtent.height)
	, m_boundingBox()
{
}

void AssimpScene::Reset(vk::CommandBuffer& commandBuffer, const RenderPass& renderPass, vk::Extent2D imageExtent)
{
	m_renderPass = &renderPass;
	m_imageExtent = imageExtent;

	m_camera.SetImageExtent(m_imageExtent.width, m_imageExtent.height);

	m_materialSystem->Reset(m_renderPass->Get(), m_imageExtent);
	m_skybox->Reset(*m_renderPass, m_imageExtent);

	UpdateMaterialDescriptors();
}

void AssimpScene::Load(vk::CommandBuffer commandBuffer)
{
	m_skybox = std::make_unique<Skybox>(*m_renderPass, m_imageExtent, *m_textureSystem, *m_graphicsPipelineSystem);

	LoadScene(commandBuffer);

	m_lightSystem->UploadToGPU(*m_commandBufferPool);
	CreateViewUniformBuffers();
	UpdateMaterialDescriptors();
	m_skybox->UploadToGPU(commandBuffer, *m_commandBufferPool);
}

void AssimpScene::Update(uint32_t imageIndex)
{
	vk::Extent2D extent = m_imageExtent;

	m_viewUniforms.pos = m_camera.GetEye();
	m_viewUniforms.view = m_camera.GetViewMatrix();
	m_viewUniforms.proj = m_camera.GetProjectionMatrix();

	// Upload to GPU
	auto& uniformBuffer = GetViewUniformBuffer(imageIndex);
	memcpy(uniformBuffer.GetMappedData(), reinterpret_cast<const void*>(&m_viewUniforms), sizeof(LitViewProperties));
}

void AssimpScene::DrawOpaqueObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const
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

void AssimpScene::DrawTransparentObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& renderState) const
{
	DrawSceneObjects(commandBuffer, frameIndex, renderState, m_transparentDrawCache);
}

void AssimpScene::DrawSceneObjects(vk::CommandBuffer commandBuffer, uint32_t frameIndex, RenderState& state, const std::vector<MeshDrawInfo>& drawCalls) const
{
	uint32_t currentFrameIndex = frameIndex % m_commandBufferPool->GetNbConcurrentSubmits();

	// Bind the one big vertex + index buffers
	if (drawCalls.empty() == false)
	{
		m_meshAllocator->BindGeometry(commandBuffer);
	}

	for (const auto& drawItem : drawCalls)
	{
		auto shadingModel = drawItem.mesh.shadingModel;
		vk::DescriptorSet viewDescriptorSet = m_materialSystem->GetDescriptorSet(DescriptorSetIndex::View, currentFrameIndex);
		state.BindPipeline(commandBuffer, m_materialSystem->GetGraphicsPipelineID(drawItem.mesh.materialInstanceID));
		state.BindView(commandBuffer, shadingModel, viewDescriptorSet);
		state.BindSceneNode(commandBuffer, drawItem.sceneNodeID);
		state.BindMaterial(commandBuffer, drawItem.mesh.materialInstanceID);

		// Draw
		commandBuffer.drawIndexed(drawItem.mesh.nbIndices, 1, drawItem.mesh.indexOffset, 0, 0);
	}
}

void AssimpScene::ResetCamera()
{
	LoadCamera();
}

void AssimpScene::LoadScene(vk::CommandBuffer commandBuffer)
{
	int flags = aiProcess_Triangulate
		| aiProcess_GenNormals
		| aiProcess_JoinIdenticalVertices;

	auto sceneName = AssetPath(m_sceneDir + "/" + m_sceneFilename);

	m_assimp.importer = std::make_unique<Assimp::Importer>();
	std::string scenePathStr = sceneName.PathOnDisk().string();
	m_assimp.scene = m_assimp.importer->ReadFile(scenePathStr, 0);
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

void AssimpScene::LoadLights(vk::CommandBuffer buffer)
{
	m_lightSystem->ReserveLights(m_assimp.scene->mNumLights);

	uint32_t nbShadowCastingLights = 0;

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
		bool hasShadows = false;
		if (light.type == aiLightSource_DIRECTIONAL)
		{
			light.shadowIndex = nbShadowCastingLights++;
			hasShadows = true;
		}

		LightID id = m_lightSystem->AddLight(std::move(light));
		if (hasShadows)
			m_shadowSystem->CreateShadowMap(id);
	}

	// todo: support no shadow casting lights
}

void AssimpScene::LoadCamera()
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

void AssimpScene::LoadSceneNodes(vk::CommandBuffer commandBuffer)
{
	assert(m_sceneTree->GetNodeCount() == 0);
	m_maxVertexDist = 0.0f;

	LoadNodeAndChildren(m_assimp.scene->mRootNode, glm::mat4(1.0f));

	m_meshAllocator->UploadToGPU(*m_commandBufferPool);
	m_meshAllocator->ForEachMesh([this](SceneNodeID sceneNodeID, Mesh mesh) {
		MeshDrawInfo info = { sceneNodeID, std::move(mesh) };
		if (m_materialSystem->IsTransparent(mesh.materialInstanceID) == false)
			m_opaqueDrawCache.push_back(std::move(info));
		else
			m_transparentDrawCache.push_back(std::move(info));
	});

	m_sceneTree->UploadToGPU(*m_commandBufferPool);

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
			// Then scene node
			else
				return a.sceneNodeID < b.sceneNodeID;
		});

	// Transparent materials need to be sorted by distance every time the camera moves
}

void AssimpScene::SortTransparentObjects()
{
	glm::mat4 viewInverse = glm::inverse(m_camera.GetViewMatrix());
	glm::vec3 cameraPosition = viewInverse[3]; // m_camera.GetPosition();
	glm::vec3 front = viewInverse[2]; // todo: m_camera.GetForwardVector();

	// todo: assign 64 bit number to each MeshDrawInfo for sorting and
	// use this here also instead of copy pasting the sorting logic here.
	std::sort(m_transparentDrawCache.begin(), m_transparentDrawCache.end(),
		[&cameraPosition, &front, this](const MeshDrawInfo& a, const MeshDrawInfo& b) {
			glm::vec3 dx_a = cameraPosition - glm::vec3(m_sceneTree->GetTransform(a.sceneNodeID)[3]);
			glm::vec3 dx_b = cameraPosition - glm::vec3(m_sceneTree->GetTransform(b.sceneNodeID)[3]);
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
				return a.sceneNodeID < b.sceneNodeID;
		});
}

void AssimpScene::LoadNodeAndChildren(aiNode* node, glm::mat4 transform)
{
	// Convert from row-major (aiMatrix4x4) to column-major (glm::mat4)
	// Note: don't know if all formats supported by assimp are row-major but Collada is.
	glm::mat4 nodeTransform = glm::transpose(glm::make_mat4(&node->mTransformation.a1));

	glm::mat4 newTransform = transform * nodeTransform;

	if (node->mNumMeshes > 0)
		LoadSceneNode(*node, newTransform);

	for (int i = 0; i < node->mNumChildren; ++i)
		LoadNodeAndChildren(node->mChildren[i], newTransform);
}

SceneNodeID AssimpScene::LoadSceneNode(const aiNode& fileNode, glm::mat4 transform)
{
	constexpr float maxFloat = std::numeric_limits<float>::max();

	BoundingBox box;

	std::vector<Mesh> meshes;
	meshes.reserve(m_assimp.scene->mNumMeshes);

	for (size_t i = 0; i < fileNode.mNumMeshes; ++i)
	{
		aiMesh* aMesh = m_assimp.scene->mMeshes[fileNode.mMeshes[i]];

		{
			Mesh mesh;
			mesh.indexOffset = m_meshAllocator->GetIndexCount();
			mesh.nbIndices = (vk::DeviceSize)aMesh->mNumFaces * aMesh->mFaces->mNumIndices;
			mesh.materialInstanceID = m_materials[aMesh->mMaterialIndex];
			mesh.shadingModel = Material::ShadingModel::Lit;
			meshes.push_back(std::move(mesh));
		}

		size_t vertexIndexOffset = m_meshAllocator->GetVertexCount();

		bool hasUV = aMesh->HasTextureCoords(0);
		bool hasColor = aMesh->HasVertexColors(0);
		bool hasNormals = aMesh->HasNormals();

		m_meshAllocator->ReserveVertices(aMesh->mNumVertices);
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

			m_meshAllocator->AddVertex(std::move(vertex));
		}

		m_meshAllocator->ReserveIndices((size_t)aMesh->mNumFaces * (aMesh->mFaces->mNumIndices + 1));
		for (size_t f = 0; f < aMesh->mNumFaces; ++f)
		{
			for (size_t fi = 0; fi < aMesh->mFaces->mNumIndices; ++fi)
			{
				m_meshAllocator->AddIndex(aMesh->mFaces[f].mIndices[fi] + vertexIndexOffset);
			}
		}
	}

	SceneNodeID sceneNodeID = m_sceneTree->CreateNode(transform, box);
	m_meshAllocator->GroupMeshes(sceneNodeID, meshes);

	// Transform the bounding box to world space
	box = box.Transform(transform);

	// Then add it to the global world bounding box
	m_boundingBox = m_boundingBox.Union(box);

	return sceneNodeID;
}

void AssimpScene::LoadMaterials(vk::CommandBuffer commandBuffer)
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
		TextureID dummyTextureID = m_textureSystem->LoadTexture(AssetPath("/Engine/Textures/dummy_texture.png"));
		for (int textureIndex = 0; textureIndex < (int)PhongMaterialTextures::eCount; ++textureIndex)
			materialInfo.properties.textures[textureIndex] = dummyTextureID;

		// Load textures
		auto loadTexture = [this, &assimpMaterial, &commandBuffer, &materialInfo](aiTextureType type, size_t textureIndex) { // todo: move this to a function
			if (assimpMaterial->GetTextureCount(type) > 0)
			{
				aiString textureFile;
				assimpMaterial->GetTexture(type, 0, &textureFile);
				std::filesystem::path texturePath = m_sceneDir / std::filesystem::path(textureFile.C_Str());
				materialInfo.properties.textures[textureIndex] = m_textureSystem->LoadTexture(AssetPath(texturePath));
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

	m_textureSystem->UploadTextures(*m_commandBufferPool);
}

void AssimpScene::CreateViewUniformBuffers()
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

void AssimpScene::UpdateMaterialDescriptors()
{
	// Create unlit view descriptor set (we don't have a material system for this yet)
	// todo: this could be handled in skybox

	if (m_descriptorPool)
	{
		for (auto& set : m_unlitViewDescriptorSets)
			set.reset();

		g_device->Get().resetDescriptorPool(m_descriptorPool.get());
	}

	// Set 0 (view): { 1 uniform buffer for view uniforms per concurrentFrames }
	std::array<std::pair<vk::DescriptorType, uint16_t>, 1ULL> descriptorCount = {
		std::make_pair(vk::DescriptorType::eUniformBuffer, static_cast<uint16_t>(m_commandBufferPool->GetNbConcurrentSubmits())),
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
		static_cast<uint32_t>(m_lightSystem->GetLightCount()),
		std::max(static_cast<uint32_t>(m_shadowSystem->GetShadowCount()), 1U), // used as an array size, so it needs to be at least 1
		static_cast<uint32_t>(m_textureSystem->GetTextureCount(ImageViewType::e2D)),
		static_cast<uint32_t>(m_textureSystem->GetTextureCount(ImageViewType::eCube))
	};
	m_materialSystem->UploadToGPU(*m_commandBufferPool, std::move(constants));

	// Bind view to material descriptor set
	SmallVector<vk::Buffer> viewUniformBuffers;
	viewUniformBuffers.reserve(m_viewUniformBuffers.size());
	for (int i = 0; i < (int)m_viewUniformBuffers.size(); ++i)
		viewUniformBuffers.push_back(m_viewUniformBuffers[i].Get());

	auto [lightsBuffer, size] = m_lightSystem->GetUniformBuffer();
	m_materialSystem->UpdateViewDescriptorSets(
		viewUniformBuffers, m_viewUniformBuffers[0].Size(),
		lightsBuffer, size
	);

	// Add scene bindings from the 2nd descriptor set to the material (3rd) descriptor set
	const auto& transformsBuffer = m_sceneTree->GetTransformsBuffer();
	m_materialSystem->UpdateSceneDescriptorSet(transformsBuffer.Get(), transformsBuffer.Size());

	// Update material descriptor sets
	m_materialSystem->UpdateMaterialDescriptorSet();
}

UniqueBuffer& AssimpScene::GetViewUniformBuffer(uint32_t imageIndex)
{
	return m_viewUniformBuffers[imageIndex % m_commandBufferPool->GetNbConcurrentSubmits()];
}
