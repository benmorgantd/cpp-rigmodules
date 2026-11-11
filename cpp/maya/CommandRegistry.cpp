// This is the only file that includes every command header
#include "maya/CommandRegistry.h"
#include "maya/commands/ConstraintCmds.h"
#include "maya/commands/CreateRigCmd.h"
#include "maya/commands/JointMarkupCmd.h"
#include "maya/commands/RigControlCmds.h"
#include "maya/commands/RigInfoCmd.h"
#include "maya/commands/RigPoseCmds.h"
#include "maya/commands/SkinWeightCmds.h"

const std::vector<CommandDescriptor>& CommandRegistry::all()
{
	// Function-local static: built on first call (during initializePlugin).
	static const std::vector<CommandDescriptor> descriptors =
	{
		{ CreateRigCmd::aCommandString, CreateRigCmd::creator, CreateRigCmd::newSyntax },
		{ CreateRigConstraintCmd::aCommandString, CreateRigConstraintCmd::creator, CreateRigConstraintCmd::newSyntax },
		{ SetConstraintSpaceCmd::aCommandString, SetConstraintSpaceCmd::creator, SetConstraintSpaceCmd::newSyntax },
		{ JointMarkupCmd::aCommandString, JointMarkupCmd::creator, JointMarkupCmd::newSyntax },
		{ GetRigControlsCmd::aCommandString, GetRigControlsCmd::creator, GetRigControlsCmd::newSyntax },
		{ SaveRigControlShapesCmd::aCommandString, SaveRigControlShapesCmd::creator, SaveRigControlShapesCmd::newSyntax },
		{ LoadRigControlShapesCmd::aCommandString, LoadRigControlShapesCmd::creator, LoadRigControlShapesCmd::newSyntax },
		{ GetRigMirrorControlCmd::aCommandString, GetRigMirrorControlCmd::creator, GetRigMirrorControlCmd::newSyntax },
		{ GetRigControlChannelsCmd::aCommandString, GetRigControlChannelsCmd::creator, GetRigControlChannelsCmd::newSyntax },
		{ GetRigControlInfoCmd::aCommandString, GetRigControlInfoCmd::creator, GetRigControlInfoCmd::newSyntax },
		{ MirrorRigControlShapesCmd::aCommandString, MirrorRigControlShapesCmd::creator, MirrorRigControlShapesCmd::newSyntax },
		{ ZeroRigControlsCmd::aCommandString, ZeroRigControlsCmd::creator, ZeroRigControlsCmd::newSyntax },
		{ MirrorRigPoseCmd::aCommandString, MirrorRigPoseCmd::creator, MirrorRigPoseCmd::newSyntax },
		{ FlipRigPoseCmd::aCommandString, FlipRigPoseCmd::creator, FlipRigPoseCmd::newSyntax },
		{ SaveRigPoseCmd::aCommandString, SaveRigPoseCmd::creator, SaveRigPoseCmd::newSyntax },
		{ LoadRigPoseCmd::aCommandString, LoadRigPoseCmd::creator, LoadRigPoseCmd::newSyntax },
		{ GetRigInfoCmd::aCommandString, GetRigInfoCmd::creator, GetRigInfoCmd::newSyntax },
		{ SetRigMeshIdCmd::aCommandString, SetRigMeshIdCmd::creator, SetRigMeshIdCmd::newSyntax },
		{ SaveRigSkinWeightsCmd::aCommandString, SaveRigSkinWeightsCmd::creator, SaveRigSkinWeightsCmd::newSyntax },
		{ LoadRigSkinWeightsCmd::aCommandString, LoadRigSkinWeightsCmd::creator, LoadRigSkinWeightsCmd::newSyntax },
		{ PruneToMaxInfluencesCmd::aCommandString, PruneToMaxInfluencesCmd::creator, PruneToMaxInfluencesCmd::newSyntax }
	};
	return descriptors;
}