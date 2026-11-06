// This is the only file that includes every command header
#include "maya/CommandRegistry.h"
#include "maya/commands/ConstraintCmds.h"
#include "maya/commands/CreateRigCmd.h"
#include "maya/commands/JointMarkupCmd.h"

const std::vector<CommandDescriptor>& CommandRegistry::all()
{
	// Function-local static: built on first call (during initializePlugin).
	static const std::vector<CommandDescriptor> descriptors =
	{
		{ CreateRigCmd::aCommandString, CreateRigCmd::creator, CreateRigCmd::newSyntax },
		{ CreateParentConstraintCmd::aCommandString, CreateParentConstraintCmd::creator, CreateParentConstraintCmd::newSyntax },
		{ SetConstraintSpaceCmd::aCommandString, SetConstraintSpaceCmd::creator, SetConstraintSpaceCmd::newSyntax },
		{ JointMarkupCmd::aCommandString, JointMarkupCmd::creator, JointMarkupCmd::newSyntax }
	};
	return descriptors;
}