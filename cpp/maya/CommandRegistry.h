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

// A context command and the tool command its strokes run as The tool command is registered with it.
struct ContextDescriptor
{
	const char* name;               // the context command
	CommandCreatorFn creator;
	const char* toolName;           // the tool command
	CommandCreatorFn toolCreator;
	CommandSyntaxFn toolSyntax;
};

class CommandRegistry
{
public:
	static const std::vector<CommandDescriptor>& all();
	static const std::vector<ContextDescriptor>& contexts();
};