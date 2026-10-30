#pragma once
#include "RigModule.h"

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
};