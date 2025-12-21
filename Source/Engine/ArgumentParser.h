#pragma once

#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <iostream>

struct Argument
{
	std::string name;
	std::string help;
	std::optional<std::string> value;
};

struct ProgramArguments
{
	std::string name;
	std::string description;
	std::vector<Argument> options;
};

struct CommandLineArgument
{
	explicit CommandLineArgument(std::string key)
		: key(key)
	{
	}

	std::string key;
	std::optional<std::string> value;
};

class ArgumentParser
{
public:
	ArgumentParser(ProgramArguments programArguments)
		: m_programArguments(std::move(programArguments))
	{
	}

	// todo (hbedard): return result with errors
	bool ParseArgs(int argc, char* argv[]);

	// todo (hbedard): expose an api that takes a template instead
	std::optional<std::string> GetString(const std::string& key);

private:
	// Argument description
	ProgramArguments m_programArguments;

	// Parsing state
	bool m_isProcessingOption = false;
	std::vector<CommandLineArgument> m_arguments;

	bool HasNoRequiredArguments();
	bool IsLongName(std::string_view name);

	bool BuildArgumentList(int argc, char* argv[]);
	void ShowHelp();
};
