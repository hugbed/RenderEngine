#include <AssimpSceneLoader.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderScene.h>
#include <Renderer/MaterialSystem.h>
#include <Renderer/LightSystem.h>
#include <Renderer/ShadowSystem.h>
#include <Renderer/CameraViewSystem.h>
#include <Renderer/SceneTree.h>
#include <RHI/CommandRingBuffer.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <string>
#include <filesystem>
#include <numbers>

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
			return glm::vec4(glm::vec3(color)/ maxComponent, color.a);

		return color;
	}
}

AssimpSceneLoader::AssimpSceneLoader(
	std::string basePath,
	std::string sceneFilename,
	Renderer& renderer)
	: m_renderer(&renderer)
	, m_sceneDir(std::move(basePath))
	, m_sceneFilename(std::move(sceneFilename))
	, m_boundingBox()
{
}

RenderScene& AssimpSceneLoader::GetRenderScene() { return *m_renderer->GetRenderScene(); }

void AssimpSceneLoader::Load(vk::CommandBuffer commandBuffer)
{
	LoadScene(commandBuffer);
}

void AssimpSceneLoader::ResetCamera()
{
	LoadCamera();
}

void AssimpSceneLoader::LoadScene(vk::CommandBuffer commandBuffer)
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

void AssimpSceneLoader::LoadLights(vk::CommandBuffer buffer)
{
	GetRenderScene().GetLightSystem()->ReserveLights(m_assimp.scene->mNumLights);

	uint32_t nbShadowCastingLights = 0;

	for (int i = 0; i < m_assimp.scene->mNumLights; ++i)
	{
		aiLight* aLight = m_assimp.scene->mLights[i];

		aiNode* node = m_assimp.scene->mRootNode->FindNode(aLight->mName);
		glm::mat4 transform = ::ComputeAiNodeGlobalTransform(node);
		
		Light light;
		light.type = static_cast<uint32_t>(aLight->mType);
		light.color = glm::make_vec4(&aLight->mColorDiffuse.r);
		light.intensity = std::max(light.color.r, std::max(light.color.g, light.color.b));
		if (light.intensity > 1.0f)
		{
			light.color /= light.intensity;
		}
		light.position = transform[3];
		light.intensity = light.intensity / 683.0f;

		bool hasShadows = false;
		if ((aLight->mType == aiLightSource_DIRECTIONAL) || (aLight->mType == aiLightSource_SPOT))
		{
			light.direction = glm::make_vec3(&aLight->mDirection.x);
			light.direction = glm::vec4(transform * glm::vec4(light.direction, 0.0f));

			if (light.type == aiLightSource_DIRECTIONAL)
			{
				light.shadowIndex = nbShadowCastingLights++;
				hasShadows = true;
			}
			if (aLight->mType == aiLightSource_SPOT)
			{
				light.cosInnerAngle = std::cos(aLight->mAngleInnerCone);
				light.cosOuterAngle = std::cos(aLight->mAngleOuterCone);
			}
		}
		else if (aLight->mType == aiLightSource_POINT)
		{
			static constexpr float SMALL_NUMBER = 1.0e-6f;
			static constexpr float BIG_NUMBER = 1.0e6f; // todo (hbedard): that's no good
			light.falloffRadius = aLight->mAttenuationConstant > SMALL_NUMBER ? 1.0f / aLight->mAttenuationConstant : BIG_NUMBER;
		}

		LightID lightID = GetRenderScene().GetLightSystem()->AddLight(std::move(light));
		if (hasShadows)
		{
			ShadowID shadowID = GetRenderScene().GetShadowSystem()->CreateShadowMap(lightID);
			GetRenderScene().GetLightSystem()->SetLightShadowID(lightID, shadowID);
		}
	}

	// todo: support no shadow casting lights
}

void AssimpSceneLoader::LoadCamera()
{
	Camera& sceneCamera = GetRenderScene().GetCameraViewSystem()->GetCamera();

	if (m_assimp.scene->mNumCameras > 0)
	{
		aiCamera* camera = m_assimp.scene->mCameras[0];
		aiNode* node = m_assimp.scene->mRootNode->FindNode(camera->mName);
		glm::mat4 transform = ComputeAiNodeGlobalTransform(node);
		glm::vec3 pos = transform[3];
		glm::vec3 lookat = glm::vec3(0.0f);
		glm::vec3 up = transform[1];
		sceneCamera.SetCameraView(pos, lookat, up);
		sceneCamera.SetFieldOfView(camera->mHorizontalFOV * 180 / M_PI * 2);
	}
	else
	{
		// Init camera to see the model
		kInitOrbitCameraRadius = m_maxVertexDist * 15.0f;
		Camera& sceneCamera = GetRenderScene().GetCameraViewSystem()->GetCamera();
		sceneCamera.SetCameraView(glm::vec3(kInitOrbitCameraRadius, kInitOrbitCameraRadius, kInitOrbitCameraRadius), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
	}
}

void AssimpSceneLoader::LoadSceneNodes(vk::CommandBuffer commandBuffer)
{
	m_maxVertexDist = 0.0f;
	LoadNodeAndChildren(m_assimp.scene->mRootNode, glm::mat4(1.0f));
}

void AssimpSceneLoader::LoadNodeAndChildren(aiNode* node, glm::mat4 transform)
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

SceneNodeHandle AssimpSceneLoader::LoadSceneNode(const aiNode& fileNode, glm::mat4 transform)
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
			mesh.indexOffset = GetRenderScene().GetMeshAllocator()->GetIndexCount();
			mesh.nbIndices = (vk::DeviceSize)aMesh->mNumFaces * aMesh->mFaces->mNumIndices;
			mesh.materialHandle = m_materials[aMesh->mMaterialIndex];
			meshes.push_back(std::move(mesh));
		}

		size_t vertexIndexOffset = GetRenderScene().GetMeshAllocator()->GetVertexCount();

		bool hasUV = aMesh->HasTextureCoords(0);
		bool hasColor = aMesh->HasVertexColors(0);
		bool hasNormals = aMesh->HasNormals();

		GetRenderScene().GetMeshAllocator()->ReserveVertices(aMesh->mNumVertices);
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

			GetRenderScene().GetMeshAllocator()->AddVertex(std::move(vertex));
		}

		GetRenderScene().GetMeshAllocator()->ReserveIndices((size_t)aMesh->mNumFaces * (aMesh->mFaces->mNumIndices + 1));
		for (size_t f = 0; f < aMesh->mNumFaces; ++f)
		{
			for (size_t fi = 0; fi < aMesh->mFaces->mNumIndices; ++fi)
			{
				GetRenderScene().GetMeshAllocator()->AddIndex(aMesh->mFaces[f].mIndices[fi] + vertexIndexOffset);
			}
		}
	}

	SceneNodeHandle sceneNodeID = GetRenderScene().GetSceneTree()->CreateNode(transform, box);
	GetRenderScene().GetMeshAllocator()->GroupMeshes(sceneNodeID, meshes);

	// Transform the bounding box to world space
	box = box.Transform(transform);

	// Then add it to the global world bounding box
	m_boundingBox = m_boundingBox.Union(box);

	return sceneNodeID;
}

void AssimpSceneLoader::LoadMaterials(vk::CommandBuffer commandBuffer)
{
	// Check if we already have all materials set-up
	if (m_materials.size() == m_assimp.scene->mNumMaterials)
		return;

	TextureHandle dummyTextureID = m_renderer->GetTextureCache()->LoadTexture(AssetPath("/Engine/Textures/dummy_texture.png"));

	// Create a material instance per material description in the scene
	// todo: eventually create materials according to the needs of materials in the scene
	// to support different types of materials
	m_materials.resize(m_assimp.scene->mNumMaterials, MaterialHandle(MaterialShadingDomain::Surface, MaterialShadingModel::Lit, 0));
	for (size_t i = 0; i < m_materials.size(); ++i)
	{
		auto& assimpMaterial = m_assimp.scene->mMaterials[i];

		// Properties
		MaterialInstanceInfo materialInfo;

		aiColor4D color = {};
		assimpMaterial->Get(AI_MATKEY_BASE_COLOR, color);
		materialInfo.properties.baseColor = glm::make_vec4(&color.r);

		aiColor4D emissive = {};
		assimpMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
		materialInfo.properties.emissive = glm::make_vec4(&emissive.r);

		float ior = 1.5f;
		assimpMaterial->Get(AI_MATKEY_REFRACTI, ior);
		materialInfo.properties.reflectance = (ior - 1.0f) * (ior - 1.0f) / ((ior + 1.0f) * (ior + 1.0f));

		float metallic = 0.0f;;
		assimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
		materialInfo.properties.metallic = metallic;

		float roughness = 0.0f;;
		assimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
		materialInfo.properties.perceptualRoughness = roughness;

		// todo (hbedard): this only comes from a texture right?
		materialInfo.properties.ambientOcclusion = 0.0f;

		// If a material is transparent, opacity will be fetched
		// from the diffuse alpha channel (material property * diffuse texture)
		float opacity = 1.0f;
		assimpMaterial->Get(AI_MATKEY_OPACITY, opacity);
		materialInfo.pipelineProperties.isTranslucent = opacity < 1.0f;

//#ifdef DEBUG_MODE
//		aiString name;
//		assimpMaterial->Get(AI_MATKEY_NAME, name);
//		material->name = std::string(name.C_Str());
//#endif
	
		// Create default textures
		for (int textureIndex = 0; textureIndex < (int)MaterialTextureType::eCount; ++textureIndex)
			materialInfo.properties.textures[textureIndex] = dummyTextureID;

		// Load textures
		auto loadTexture = [this, &assimpMaterial, &commandBuffer, &materialInfo](aiTextureType type, size_t textureIndex) { // todo: move this to a function
			if (assimpMaterial->GetTextureCount(type) > 0)
			{
				aiString textureFile;
				assimpMaterial->GetTexture(type, 0, &textureFile);
				std::filesystem::path texturePath = m_sceneDir / std::filesystem::path(textureFile.C_Str());
				materialInfo.properties.textures[textureIndex] = m_renderer->GetTextureCache()->LoadTexture(AssetPath(texturePath));
			}
		};

		// todo: use sRGB format for color textures if necessary
		// it looks like gamma correction is OK for now but it might
		// not be the case for all textures
		loadTexture(aiTextureType_BASE_COLOR, static_cast<size_t>(MaterialTextureType::eBaseColor));
		loadTexture(aiTextureType_EMISSIVE, static_cast<size_t>(MaterialTextureType::eEmissive));
		loadTexture(aiTextureType_METALNESS, static_cast<size_t>(MaterialTextureType::eMetallic));
		loadTexture(aiTextureType_DIFFUSE_ROUGHNESS, static_cast<size_t>(MaterialTextureType::eRoughness));
		loadTexture(aiTextureType_NORMALS, static_cast<size_t>(MaterialTextureType::eNormals));
		loadTexture(aiTextureType_LIGHTMAP, static_cast<size_t>(MaterialTextureType::eAmbientOcclusion));

		m_materials[i] = GetRenderScene().GetMaterialSystem()->CreateMaterialInstance(materialInfo);
	}
}
