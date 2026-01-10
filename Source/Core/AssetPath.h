#pragma once

#include <filesystem>

// Wrapper around asset paths to abstract away the physical location on disk
class AssetPath
{
public:
	explicit AssetPath(std::filesystem::path path)
		: m_assetPath(std::move(path))
	{
	}

	// returns the asset path passed in the constructor
	std::filesystem::path Get() const { return m_assetPath; }

	std::string ToString() const { return m_assetPath.string(); }

	// Returns the path on disk
	std::filesystem::path GetPathOnDisk() const;

	// Must be called at launch to be able to resolve any asset path
	static void SetEngineDirectory(std::filesystem::path directory);
	static void SetGameDirectory(std::filesystem::path directory);

private:
	static std::filesystem::path m_engineDirectory;
	static std::filesystem::path m_gameDirectory;

	std::filesystem::path m_assetPath;
};
