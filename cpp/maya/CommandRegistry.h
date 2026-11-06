#pragma once
#include <maya/MSyntax.h>
#include <vector>

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