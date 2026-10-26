#pragma once

#include "RigModule.h"
#include <maya/MObject.h>
#include <maya/MString.h>
#include <maya/MDagPath.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>

class AimModuleNode : public RigModuleNodeBase
{
public:
    AimModuleNode();
    virtual ~AimModuleNode() override;

    static void* creator();
    static MStatus initialize();
    virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

    static MObject createModule(
        const MString& moduleName,
        const MDagPath& aimJoint,
        const MDagPath& targetJoint,
        const MDagPath& upJoint,
        MObject oRigRoot,
        MDGModifier& dgMod,
        MDagModifier& dagMod,
        MStatus* status = nullptr
    );

    static MObject createModule(
        const RigModuleData& moduleData,
        MObject rigRoot,
        MDGModifier& dgMod,
        MDagModifier& dagMod
    );

    static MTypeId id;
    static const MString aCommandString;

    // Discrete Input Rest Matrices
    static MObject aInputAimRestMatrix;
    static MObject aInputTargetRestMatrix;
    static MObject aInputUpRestMatrix;

    // Discrete Control Input Matrices (Target & Up controls only)
    static MObject aTargetControlMatrix;
    static MObject aUpControlMatrix;

    // Discrete Control Output OPMs
    static MObject aOutputTargetControlOPM;
    static MObject aOutputUpControlOPM;

    // Discrete Joint Output OPMs
    static MObject aOutputAimJointOPM;
    static MObject aOutputTargetJointOPM;
    static MObject aOutputUpJointOPM;

public: // ---- Shared Attributes All Modules Have (but that must be unique) ----
    static MObject moduleData;               // JSON string payload for custom module serialization
    static MObject aRigRoot;                 // Message link to central RigRoot node
    static MObject aOutputSocketMatrix;      // Output matrix array providing connection sockets for downstream modules
    static MObject aSide;                    // Module side (Center, Left, Right)
};