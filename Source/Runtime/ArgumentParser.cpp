#include "ArgumentParser.h"

bool ArgumentParser::HasNoRequiredArguments()
{
	return m_programArguments.options.empty();
}

bool ArgumentParser::IsLongName(std::string_view name)
{
	return name.substr(0, 2) == "--";
}

bool ArgumentParser::ParseArgs(int argc, char* argv[])
{
	if (argc >= 2 && std::string_view("--help") == argv[1] || std::string_view("-h") == argv[1])
	{
		ShowHelp();
		return false;
	}

	if (!BuildArgumentList(argc, argv))
	{
		return false;
	}

	return true;
}

std::optional<std::string> ArgumentParser::GetString(const std::string& key)
{
	auto it = std::ranges::find_if(m_arguments, [key](const CommandLineArgument& arg) {
		return arg.key.substr(2) == key;
		});
	if (it != m_arguments.end())
	{
		return it->value;
	}
	return std::nullopt;
}

bool ArgumentParser::BuildArgumentList(int argc, char* argv[])
{
	m_arguments.clear();

	bool expectArgValue = false;

	for (int i = 0; i < argc; ++i)
	{
		if (expectArgValue)
		{
			m_arguments.back().value = argv[i];
			expectArgValue = false;
			continue;
		}
		if (IsLongName(argv[i]))
		{
			m_arguments.emplace_back(argv[i]);
			expectArgValue = true;
		}
	}

	return true;
}

void ArgumentParser::ShowHelp()
{
	// Program
	if (!m_programArguments.description.empty())
	{
		std::cout << m_programArguments.description;
	}
	std::cout << std::endl << std::endl;

	// Options
	std::cout << "Usage: " << m_programArguments.name;
	if (!m_programArguments.options.empty())
	{
		std::cout << " [OPTIONS]";
	}
	std::cout << std::endl << std::endl;

	// Options
	if (!m_programArguments.options.empty())
	{
		std::cout << "Options:" << std::endl;
		for (const Argument& arg : m_programArguments.options)
		{
			std::cout << " ";
			std::cout << " --" << arg.name;

			if (arg.value.has_value())
			{
				std::cout << " <" << *arg.value << ">";
			}

			if (!arg.help.empty())
			{
				std::cout << "\t" << arg.help;
			}

			std::cout << std::endl;
		}
	}
}