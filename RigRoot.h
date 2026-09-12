#pragma once

#include <maya/MStatus.h>
#include <maya/MPxNode.h>

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
	static MObject rigName;
	static MObject rigType;
	static MObject rigVersion;
	static MObject rigModules;
	static MObject rigTemplateName;
	static MObject assetRoot;
	static MObject children;
};