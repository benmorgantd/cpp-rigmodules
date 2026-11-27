#pragma once

#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"

#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// An FK control for the root joint and every joint below it (design/fk-hierarchy.md). FkChain's controls form one
// chain; these form a tree, with each control carried by its own parent's motion. One child plug, start (the root).
// One socket per joint, named from the joint's markup: left_spine_00.
class FkHierarchyModuleNode : public RigModuleWithJointsNode<FkHierarchyModuleNode>
{
public:
	FkHierarchyModuleNode();
	virtual ~FkHierarchyModuleNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

public:
	static MTypeId id;
	// By control slot: the slot of the control's parent, or -1 for the root. A parent's slot is always lower.
	inline static MObject aControlParentIndex;

public:
	// Everything per control is by slot, in depth-first order: oJoints, mJointMatrices, controlMarkups, parentIndex and socketNames.
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const MObjectArray& oJoints,
		const MMatrixArray& mJointMatrices,
		const std::vector<ControlMarkup>& controlMarkups,
		const std::vector<int>& parentIndex,
		const std::vector<std::string>& socketNames,
		MObject& oRigRoot,
		MDagModifier& dagMod
	);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// Derives every control's transform from the joints: the chain inverse, with each control under its own parent.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);

private:
	// One solve writes control OPM, joint OPM and socket matrix together, walking the controls once in slot order.
	MStatus computeHierarchyOutputs(const MPlug& plug, MDataBlock& data);

	// Scratch for the solve, kept so compute() allocates only when the control count changes (the first evaluation
	// after a build or a load). A node evaluates on one thread at a time, so it is not shared.
	std::vector<int> fParent;          // by slot: the parent slot read from controlParentIndex, -1 for the root
	std::vector<char> fHasChild;       // by slot: whether a later control hangs from it, so a leaf skips its matrix inverse
	std::vector<MMatrix> fMotion;      // by slot: the control's motion since rest, kept only when fHasChild
};

// "root": ["Center", "root"], "terminators": [["Center", "mag"]] (optional).
struct FkHierarchyArgs
{
	static constexpr const char* RootKey{ "root" };               // Must match the field names the macro below maps.
	static constexpr const char* TerminatorsKey{ "terminators" };
	JointSelector root;
	std::vector<JointSelector> terminators;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FkHierarchyArgs, root, terminators)
