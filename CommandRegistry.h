#pragma once
#include <vector>
#include <maya/MSyntax.h>

using CommandCreatorFn = void* (*)();
using CommandSyntaxFn = MSyntax(*)();

struct CommandDescriptor
{
	const char* name;           // Maya command name
	CommandCreatorFn creator;
	CommandSyntaxFn syntax;
};

class CommandRegistry
{
public:
	static const std::vector<CommandDescriptor>& all();
};