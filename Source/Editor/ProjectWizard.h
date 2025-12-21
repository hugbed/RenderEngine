#include <filesystem>

class ProjectWizard
{
public:
	static void CreateNewProject(std::filesystem::path projectDir, std::string projectName);
};