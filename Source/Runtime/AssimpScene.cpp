#include <AssimpScene.h>

#include <RHI/CommandBufferPool.h>
#include <Renderer/MaterialSystem.h>
#include <Renderer/ShadowSystem.h>
#include <Renderer/SceneTree.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <string>
#include <filesystem>

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

// todo (hbedard): makes no sense to create stuff outside and pass all this here :)
AssimpScene::AssimpScene(
	std::string basePath,
	std::string sceneFilename,
	CommandBufferPool& commandBufferPool,
	GraphicsPipelineSystem& graphicsPipelineSystem,
	BindlessDescriptors& bindlessDescriptors,
	BindlessDrawParams& bindlessDrawParams,
	TextureSystem& textureSystem,
	MeshAllocator& meshAllocator,
	LightSystem& lightSystem,
	SurfaceLitMaterialSystem& materialSystem,
	ShadowSystem& shadowSystem,
	SceneTree& sceneTree,
	Grid& grid,
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
	, m_grid(&grid)
	, m_camera(
		1.0f * glm::vec3(1.0f, 1.0f, 1.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f), 45.0f, 0.01f, 100.0f,
		m_imageExtent.width, m_imageExtent.height)
	, m_boundingBox()
	, m_bindlessDrawParams(&bindlessDrawParams)
	, m_bindlessDescriptors(&bindlessDescriptors)
{
}

void AssimpScene::Reset(vk::CommandBuffer& commandBuffer, const RenderPass& renderPass, vk::Extent2D imageExtent)
{
	m_renderPass = &renderPass;
	m_imageExtent = imageExtent;

	m_camera.SetImageExtent(m_imageExtent.width, m_imageExtent.height);

	m_materialSystem->Reset(m_renderPass->Get(), m_imageExtent);
	m_skybox->Reset(*m_renderPass, m_imageExtent);
}

void AssimpScene::Load(vk::CommandBuffer commandBuffer)
{
	m_skybox = std::make_unique<Skybox>(
		*m_renderPass,
		m_imageExtent,
		*m_textureSystem,
		*m_graphicsPipelineSystem,
		*m_bindlessDescriptors,
		*m_bindlessDrawParams);

	LoadScene(commandBuffer);

	m_lightSystem->UploadToGPU(*m_commandBufferPool);
	CreateViewUniformBuffers();
	m_materialSystem->UploadToGPU(*m_commandBufferPool);
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

void AssimpScene::BeginRender(RenderState& renderState, uint32_t frameIndex)
{
	m_concurrentFrameIndex = frameIndex % m_commandBufferPool->GetNbConcurrentSubmits();
}

void AssimpScene::DrawOpaqueObjects(RenderState& renderState) const
{
	DrawSceneObjects(renderState, m_opaqueDrawCache);

	// With Skybox last (to prevent processing fragments for nothing)
	m_skybox->Draw(renderState);
}

void AssimpScene::EndRender()
{
	// todo (hbedard): need to do anything here?
}

void AssimpScene::DrawTransparentObjects(RenderState& renderState) const
{
	DrawSceneObjects(renderState, m_transparentDrawCache);
}

void AssimpScene::DrawSceneObjects(RenderState& renderState, const std::vector<MeshDrawInfo>& drawCalls) const
{
	// Bind the one big vertex + index buffers
	if (drawCalls.empty() == false)
	{
		vk::CommandBuffer commandBuffer = renderState.GetCommandBuffer();
		m_meshAllocator->BindGeometry(commandBuffer);
	}

	m_materialSystem->Draw(renderState, gsl::span(drawCalls.data(), drawCalls.size()));
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
		if (m_materialSystem->IsTransparent(mesh.materialHandle) == false)
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
			// Then material instance
			if (a.mesh.materialHandle != b.mesh.materialHandle)
				return a.mesh.materialHandle < b.mesh.materialHandle;
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
			// Then material
			if (a.mesh.materialHandle != b.mesh.materialHandle)
				return a.mesh.materialHandle < b.mesh.materialHandle;
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
			mesh.materialHandle = m_materials[aMesh->mMaterialIndex];
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
		TextureHandle dummyTextureID = m_textureSystem->LoadTexture(AssetPath("/Engine/Textures/dummy_texture.png"));
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

		TextureHandle skyboxTexture = m_skybox->GetTextureHandle();
		if (skyboxTexture != TextureHandle::Invalid)
			materialInfo.properties.env.cubeMapTexture = static_cast<glm::aligned_int32>(skyboxTexture);

		auto materialHandle = m_materialSystem->CreateMaterialInstance(materialInfo);

		// Keep ownership of the material instance
		m_materials[i] = materialHandle;
	}
}

void AssimpScene::CreateViewUniformBuffers()
{
	// Per view
	const vk::BufferUsageFlagBits bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer;
	m_viewUniformBuffers.clear();
	m_viewUniformBuffers.reserve(m_commandBufferPool->GetNbConcurrentSubmits());
	for (uint32_t i = 0; i < m_commandBufferPool->GetNbConcurrentSubmits(); ++i)
	{
		m_viewUniformBuffers.emplace_back(
			vk::BufferCreateInfo(
				{},
				sizeof(LitViewProperties),
				bufferUsage | vk::BufferUsageFlagBits::eTransferSrc // needs TransferSrc?
			), VmaAllocationCreateInfo{ VMA_ALLOCATION_CREATE_MAPPED_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU }
		);
	}

	m_viewBufferHandles.reserve(m_viewUniformBuffers.size());
	for (uint32_t i = 0; i < m_viewUniformBuffers.size(); ++i)
	{
		m_viewBufferHandles.push_back(m_bindlessDescriptors->StoreBuffer(m_viewUniformBuffers[i].Get(), bufferUsage));
	}

	m_grid->SetViewBufferHandles(m_viewBufferHandles);
	m_skybox->SetViewBufferHandles(m_viewBufferHandles);
	m_materialSystem->SetViewBufferHandles(m_viewBufferHandles);
}

UniqueBuffer& AssimpScene::GetViewUniformBuffer(uint32_t imageIndex)
{
	return m_viewUniformBuffers[imageIndex % m_commandBufferPool->GetNbConcurrentSubmits()];
}
