#include <filesystem>

class ProjectSettings
{
public:
	ProjectSettings() = default;
	ProjectSettings(std::filesystem::path projectDir, std::string projectName);

	const std::filesystem::path& GetProjectDir() const { return m_projectDir; }

	const std::string& GetProjectName() const { return m_projectName; }

	static ProjectSettings FromFile(std::filesystem::path settingsFilePath);

	void SaveToFile() const;

private:
	std::filesystem::path m_projectDir;
	std::string m_projectName;
	std::filesystem::path m_assetsDir;
};
