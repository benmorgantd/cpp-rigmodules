#pragma once
#include "maya/shared/RigModule.h"

#include <maya/MString.h>

// A Layout module has no joints, and is only used as a control hierarchy.
// Its rest position is always at the origin.
// It is also always the first module in the hierarchy and has no parent.
class LayoutModuleNode : public RigModuleWithControlsNode<LayoutModuleNode>
{
public:
	LayoutModuleNode();
	virtual ~LayoutModuleNode() override;

	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	static void* creator();
	static MStatus initialize();

public:
	static MTypeId id;
public:
	static RigModuleBuildResult createModule(MObject& oRigRoot, MDagModifier& dagMod, const unsigned short& numControls);

	// Names of socket index among count controls: index_N, plus end on the last. No child plugs.
	// createRig's layout plan and createModule both read this, so they cannot drift apart.
	static std::vector<std::string> socketNamesAt(unsigned int index, unsigned int count);
	static ModuleInterface makeInterface(unsigned int count);
	// Layout controls have no joint, so their markup is (Center, layout_N). Also their base name.
	static ControlMarkup controlMarkupAt(unsigned int index);
};