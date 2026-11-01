// This is the only file that includes every command header
#include "CommandRegistry.h"
#include "RigRoot.h"
#include "Constraints.h"

const std::vector<CommandDescriptor>& CommandRegistry::all()
{
	// Function-local static: built on first call (during initializePlugin).
	static const std::vector<CommandDescriptor> descriptors =
	{
		{ CreateRigCmd::aCommandString, CreateRigCmd::creator, CreateRigCmd::newSyntax },
		{ CreateParentConstraintCmd::aCommandString, CreateParentConstraintCmd::creator, CreateParentConstraintCmd::newSyntax },
		{ SetConstraintSpaceCmd::aCommandString, SetConstraintSpaceCmd::creator, SetConstraintSpaceCmd::newSyntax }
	};
	return descriptors;
}