#pragma once

#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// A selection set to create: its absolute name (":rig:controlsSet"), its setMarkup text, the DAG nodes it holds
// directly and the sets nested in it, as indices into the same spec list.
struct SelectionSetSpec
{
	MString name;
	MString markup;
	MObjectArray members;
	std::vector<unsigned int> children;
};

class SelectionSetScene
{
public:
	// Queues, for each spec in order, deleting an objectSet already holding that name, then creating the set with
	// its setMarkup attribute, its members and its nested sets. Undo restores the old sets. The markup attribute is
	// added by an internal dagMod.doIt, because a plug must exist before its value is queued, so call this after the
	// caller's own earlier doIt. The rest runs on the caller's next doIt. outSets holds the new sets in spec order.
	static MStatus replaceSets(const std::vector<SelectionSetSpec>& specs, MDagModifier& dagMod, MObjectArray& outSets);
};
