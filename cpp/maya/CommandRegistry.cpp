// This is the only file that includes every command header
#include "maya/CommandRegistry.h"
#include "maya/commands/ConstraintCmds.h"
#include "maya/commands/CreateRigCmd.h"
#include "maya/commands/JointMarkupCmd.h"
#include "maya/commands/RigControlCmds.h"

const std::vector<CommandDescriptor>& CommandRegistry::all()
{
	// Function-local static: built on first call (during initializePlugin).
	static const std::vector<CommandDescriptor> descriptors =
	{
		{ CreateRigCmd::aCommandString, CreateRigCmd::creator, CreateRigCmd::newSyntax },
		{ CreateParentConstraintCmd::aCommandString, CreateParentConstraintCmd::creator, CreateParentConstraintCmd::newSyntax },
		{ SetConstraintSpaceCmd::aCommandString, SetConstraintSpaceCmd::creator, SetConstraintSpaceCmd::newSyntax },
		{ JointMarkupCmd::aCommandString, JointMarkupCmd::creator, JointMarkupCmd::newSyntax },
		{ GetRigControlsCmd::aCommandString, GetRigControlsCmd::creator, GetRigControlsCmd::newSyntax },
		{ SaveRigControlShapesCmd::aCommandString, SaveRigControlShapesCmd::creator, SaveRigControlShapesCmd::newSyntax },
		{ LoadRigControlShapesCmd::aCommandString, LoadRigControlShapesCmd::creator, LoadRigControlShapesCmd::newSyntax },
		{ GetRigMirrorControlCmd::aCommandString, GetRigMirrorControlCmd::creator, GetRigMirrorControlCmd::newSyntax },
		{ MirrorRigControlShapesCmd::aCommandString, MirrorRigControlShapesCmd::creator, MirrorRigControlShapesCmd::newSyntax }
	};
	return descriptors;
}