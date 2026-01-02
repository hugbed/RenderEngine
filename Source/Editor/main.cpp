#include <ProjectWizard.h>
#include <ArgumentParser.h>
#include <AssetPath.h>

int main(int argc, char* argv[])
{
	ProgramArguments args{
		.name = "Editor.exe", .description = "The editor",
		.options = std::vector {
			Argument{.name = "project", .value = "pathToProjectFile" },
		}
	};
	ArgumentParser argParser(std::move(args));
	if (!argParser.ParseArgs(argc, argv))
	{
		return -1;
	}

	std::optional<std::string> projectFilePath = argParser.GetString("project");
	std::filesystem::path engineDir = std::filesystem::absolute((std::filesystem::current_path()));
	AssetPath::SetEngineDirectory(engineDir);
	AssetPath::SetGameDirectory(std::filesystem::path(projectFilePath.value()).parent_path());

    return 0;
}
