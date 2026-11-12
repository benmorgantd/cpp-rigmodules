#pragma once

#include "core/StringUtils.h"   // Maya-free, so the template validator can share it

#include <maya/MArgDatabase.h>
#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MFn.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPoint.h>
#include <maya/MPxTransform.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>

#include <string>
#include <vector>

struct RigControlScope;
struct ValidationReport;

struct TransformAttrEntry
{
	const MObject& attr;
	double value;
};

// Static handle lookup without string parsing during execution
static const TransformAttrEntry kDefaultTransformValues[] =
{
	{ MPxTransform::translateX, 0.0 }, { MPxTransform::translateY, 0.0 }, { MPxTransform::translateZ, 0.0 },
	{ MPxTransform::rotateX,    0.0 }, { MPxTransform::rotateY,    0.0 }, { MPxTransform::rotateZ,    0.0 },
	{ MPxTransform::scaleX,     1.0 }, { MPxTransform::scaleY,     1.0 }, { MPxTransform::scaleZ,     1.0 }
};

class MathUtils
{
public:
	static MMatrix computeAimMatrix(
		const MPoint& pAim,
		const MPoint& pTarget,
		const MPoint& pUp,
		const MMatrix& fallbackMatrix,
		const float epsilon=1e-6);

	// Motion of a transform since rest, in the space both matrices share. Row-major: rest^-1 * world.
	// Inline because solveFkChain calls it once per control per compute.
	static inline MMatrix computeDeltaFromRest(const MMatrix& mRest, const MMatrix& mWorld)
	{
		return mRest.inverse() * mWorld;
	}
};

class DagUtils
{
public:
	static void lockAndHideTranslate(MObject oControl);
	static void lockAndHideRotate(MObject oControl);
	static void lockAndHideScale(MObject oControl);
	static MStatus bakeJointTransformsToOPM(const MObject& oTransform, const MMatrix& mTransformWorld, MDagModifier& dagMod);
	static MStatus zeroLocalTransforms(const MObject& oTransform, MDagModifier& dagMod);
	static MStatus setInheritsTransform(const MObject& transform, bool value, MDagModifier& dagMod);
	static MStatus setSegmentScaleCompensate(MObject& oTransform, bool value, MDagModifier& dagMod);
	// What getDagPathFromName requires of the node it finds.
	enum class NodeKind
	{
		kAnyDagNode,
		kJoint,
		kMesh
	};

	// The two lookups by name (every other one was folded into these).
	// Resolves any DG or DAG node. Reports nothing, the caller decides what a miss means.
	static MStatus getMObjectFromName(const MString& name, MObject& oNode);
	// Resolves a DAG node. Reports "No DAG node named <name>" or "<name> is not a joint." / "... mesh.".
	// kMesh takes a mesh shape or a transform with one, and gives the transform. Modules get joints through
	// JointMarkupScene::claimJoint instead.
	static MStatus getDagPathFromName(const MString& name, MDagPath& outPath, NodeKind kind = NodeKind::kAnyDagNode);
	// A channel a tool may write: not locked, and either unconnected or keyed (an anim curve source). Writing a
	// keyed channel behaves like setAttr: the value holds until the animator keys it or the frame changes.
	static bool isChannelWritable(const MPlug& pChannel);
	static MMatrix getMatrixFromPlug(const MPlug& pMatrixPlug);
	static MStatus setMatrixPlug(MPlug& matrixPlug, MMatrix matrix, MDagModifier& dagMod);

	// The node's namespace as an absolute path for MNamespace calls: ":rig", or ":" for the root namespace.
	static MString getAbsoluteNamespace(const MObject& oNode);

	// The absolute name of shortName in a namespace given without colons at its ends: ":rig:name", or ":name"
	// when the namespace is "", the root.
	static MString getAbsoluteName(const MString& namespaceName, const MString& shortName);

	// The shortest name that is unique in the scene for a DAG node, which is what commands return and messages show.
	static MString getPartialPathName(const MObject& oDagNode);

	// The nearest node upstream of the node whose type matches filter (MFn::kSkinClusterFilter, say), breadth first,
	// or a null object if there is none.
	static MObject findUpstreamNode(const MObject& oNode, MFn::Type filter);
};

class MeshUtils
{
public:
	// The transform of a mesh, given a transform with a mesh shape or a mesh shape. Reports nothing and returns false
	// for anything else, so a command can filter the selection quietly.
	// By name: DagUtils::getDagPathFromName with NodeKind::kMesh.
	static bool findMeshTransform(const MDagPath& path, MDagPath& outTransform);

	// The mesh transform's visible (non-intermediate) mesh shape. Reports a transform without one.
	static MStatus getMeshShape(const MDagPath& mesh, MDagPath& outShape);

	// One component holding every vertex of a mesh with vertexCount vertices, for whole-table weight calls.
	static MObject createAllVerticesComponent(unsigned int vertexCount);
};

class MelUtils
{
public:
	// text in double quotes, for a name or value spliced into a MEL command. Names never contain quotes.
	static MString quoted(const MString& text);
};

class CommandUtils
{
public:
	// The rig root that is the command's one string object. Reports a parse failure (parseStatus is the
	// MArgDatabase constructor's), a wrong object count, and a node that isn't a rig root.
	static MStatus readRigRoot(const MArgDatabase& argData, MStatus parseStatus, const char* commandName, MObject& oOutRigRoot);

	// The command's objects as a rig control scope (RigTraversal::getControlScope): one rig root, or one or more
	// controls of one rig. Reports a parse failure and anything else.
	static MStatus readControlScope(const MArgDatabase& argData, MStatus parseStatus, const char* commandName, RigControlScope& outScope);

	// The syntax of a command that takes a rig control scope: one or more string objects, never the selection. The
	// command adds its flags.
	static MSyntax controlScopeSyntax();

	// The command's one string object. Reports "<command> takes exactly one <what>." for none or several.
	static MStatus readSingleObject(const MArgDatabase& argData, const char* commandName, const char* what, MString& outName);

	// Appends the string argument of every use of a multi-use flag, in the order given. None when the flag isn't set.
	static MStatus readStringList(const MArgDatabase& argData, const char* flag, std::vector<std::string>& outValues);

	// The string argument of a required file flag. Reports "<command> needs <flag>." when it is missing or empty.
	static MStatus readFilePath(const MArgDatabase& argData, const char* flag, const char* commandName, MString& outPath);

	// The mesh transforms of the command's string objects, or with none named, of every selected mesh (D35: only
	// commands that document it read the selection). Selected nodes that aren't meshes are skipped; a named one that
	// isn't a mesh is an error, and so is an empty result.
	static MStatus readMeshesOrSelection(const MArgDatabase& argData, const char* commandName, MDagPathArray& outMeshes);

	// The whole file as text. Reports "Could not read the <kind> file: <path>".
	static MStatus readTextFile(const MString& path, const char* kind, std::string& outText);

	// Replaces the file with text. Reports "Could not write the <kind> file: <path>".
	static MStatus writeTextFile(const MString& path, const char* kind, const std::string& text);
};

class ReportUtils
{
public:
	// Displays every warning, then every error, each as its own Maya message.
	static void displayReport(const ValidationReport& report);
};

// The plugin's category in Maya's Profiler ("RigModules"), so its events can be found and filtered there.
// Scopes cost almost nothing while the profiler isn't recording. initializePlugin adds the category and
// uninitializePlugin removes it.
class ProfilerUtils
{
public:
	static void addCategory();
	static void removeCategory();
	static int category();
};

// The active selection as a set of nodes, kept current by a selection-changed callback, so code that runs once
// per drawn object (a draw override's prepareForDraw) looks a node up in O(1) instead of copying the whole list.
// A DAG item counts as its node, so an instanced node counts as selected through any of its paths.
// initializePlugin starts it and uninitializePlugin stops it. Main thread only, like the callback.
class ActiveSelection
{
public:
	static MStatus start();
	static void stop();
	static bool contains(const MObject& oNode);
	// The length of the active selection list: items, not distinct nodes.
	static unsigned int length();
};

class DagPlugAndSocketUtils
{
public:
	// Stores every name of socket `index` as a JSON string list in socketNames[index], e.g. ["index_0","start"].
	static MStatus setParentSocketNames(const MObject& oModule, const unsigned int index, const std::vector<std::string>& names, MDagModifier& dagMod);
	static MStatus setChildPlugName(const MObject& oModule, const unsigned int index, const char* name, MDagModifier& dagMod);
};

// Nodes are created and renamed relative to the current namespace, so every dagMod.doIt of a rig (the build and
// every redo) must run with the rig namespace current. RAII: the destructor restores the user's namespace on
// every exit path. Absolute paths, so the result does not depend on which namespace the user had current.
// The namespace is not part of the dagMod, so undo leaves it behind, empty.
// Used by createRig and by CreateRigCmd::redoIt.
class ScopedRigNamespace
{
public:
	explicit ScopedRigNamespace(const MString& rigNamespace);
	~ScopedRigNamespace();

	ScopedRigNamespace(const ScopedRigNamespace&) = delete;
	ScopedRigNamespace& operator=(const ScopedRigNamespace&) = delete;

	MStatus status() const
	{
		return fStatus;
	}

private:
	MString fOriginalNamespace;
	MStatus fStatus;
};
