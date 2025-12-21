#include "ProjectWizard.h"

#include "ProjectSettings.h"

void ProjectWizard::CreateNewProject(std::filesystem::path projectDir, std::string projectName)
{
	// todo (hbedard): handle if directory is not empty
	// todo (hbedard): create an Assets folder
	ProjectSettings projectSettings(std::move(projectDir), std::move(projectName));
	projectSettings.SaveToFile();
}
