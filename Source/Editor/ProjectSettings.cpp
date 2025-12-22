#include "ProjectSettings.h"

#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <fstream>

ProjectSettings::ProjectSettings(std::filesystem::path projectDir, std::string projectName)
    : m_projectDir(std::move(projectDir))
    , m_projectName(std::move(projectName))
    , m_assetsDir(m_projectDir / "Assets")
{
}

ProjectSettings ProjectSettings::FromFile(std::filesystem::path settingsFilePath)
{
    toml::parse_result tomlResult = toml::parse_file(settingsFilePath.string());
    std::filesystem::path projectDir = settingsFilePath.parent_path();
    std::string projectName = tomlResult["project"]["name"].value<std::string>().value();
    return ProjectSettings(projectDir, projectName);
}

void ProjectSettings::SaveToFile() const
{
    toml::table tomlTable{
        {
            "project", toml::table {
                { "name", m_projectName }
            }
        }
    };
    std::filesystem::path settingsFilePath = m_projectDir / (m_projectName + std::string(".pproj"));
    std::ofstream os(settingsFilePath, std::ios::binary);
    os << tomlTable;
}
