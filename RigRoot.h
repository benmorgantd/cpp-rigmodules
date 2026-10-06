#pragma once

#include <maya/MStatus.h>
#include <maya/MPxNode.h>
#include <maya/MPxCommand.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>

class RigRootNode : public MPxNode
{
public:
	// instance methods first, the "behavior" of the node
	RigRootNode();
	virtual ~RigRootNode() override; 

	// Define the compute fn
	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	// Static methods next. Static because Maya calls these before an instance exists
	static MStatus initialize();
	static void* creator();
public:
	// second public block is for readability, to separate behavior methods from attributes.
	static MTypeId id;

	// Attribute handles. All node attributes have to be defined here.
	static MObject aRigName;
	static MObject aRigType;
	static MObject aRigVersion;
	static MObject aRigModules;
	static MObject aRigTemplateName;
	static MObject aAssetRoot;
	static MObject aChildren;
public:
	static MObject createRigRoot(
		const MString& name,
		const MString& type,
		int version,
		const MString& templateName,
		MObject oAssetRoot,
		MDGModifier& dgMod);
	static MStatus createRig(const MString& jsonFilePath);
};


/// <summary>
/// Main function to run for creating rigs.
/// </summary>
class CreateRigCmd : public MPxCommand
{
public:
	CreateRigCmd();
	virtual ~CreateRigCmd() override;

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();

	// Command registration attribute
	static const char* aCommandString;

private:
	static const char* kFilePathArgShort;
	static const char* kFilePathArgLong;

	MDGModifier fDgMod;
	MDagModifier fDagMod;
};