#pragma once

// Precompiled header for maya/. RigModules.vcxproj force-includes it into every .cpp under maya/, so a file does not
// need to include it. It is core/'s precompiled header plus the Maya headers most files use. Add a header only if
// many files use it, because a change here recompiles everything under maya/.

#include "core/CorePch.h"

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>
#include <maya/MDataBlock.h>
#include <maya/MDGModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MPxCommand.h>
#include <maya/MPxNode.h>
#include <maya/MPxTransform.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MSyntax.h>
#include <maya/MTypeId.h>
#include <maya/MVector.h>
