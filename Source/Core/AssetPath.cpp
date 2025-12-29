#include <AssetPath.h>

#include <string_view>

#include <cassert>

std::filesystem::path AssetPath::m_engineDirectory;
std::filesystem::path AssetPath::m_gameDirectory;

std::filesystem::path AssetPath::PathOnDisk() const
{
	assert(!m_engineDirectory.empty() && !m_gameDirectory.empty());

	std::string assetPathStr = ToString();

	static constexpr std::string_view enginePrefix("/Engine/");
	if (assetPathStr.starts_with(enginePrefix))
	{
		return m_engineDirectory / "Assets" / assetPathStr.substr(enginePrefix.length());
	}

	static constexpr std::string_view gamePrefix("/Game/");
	if (assetPathStr.starts_with(gamePrefix))
	{
		return m_gameDirectory / "Assets" / assetPathStr.substr(gamePrefix.length());
	}

	return std::filesystem::path();
}

void AssetPath::SetEngineDirectory(std::filesystem::path directory)
{
	m_engineDirectory = std::move(directory);
}

void AssetPath::SetGameDirectory(std::filesystem::path directory)
{
	m_gameDirectory = std::move(directory);
}
