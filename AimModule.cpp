#include "AimModule.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"
#include "Utilities.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMatrixData.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MGlobal.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>

MTypeId AimModuleNode::id(0x00218D);

// Discrete Attribute Declarations
MObject AimModuleNode::aInputAimRestMatrix;
MObject AimModuleNode::aInputTargetRestMatrix;
MObject AimModuleNode::aInputUpRestMatrix;

MObject AimModuleNode::aTargetControlMatrix;
MObject AimModuleNode::aUpControlMatrix;

MObject AimModuleNode::aOutputTargetControlOPM;
MObject AimModuleNode::aOutputUpControlOPM;

MObject AimModuleNode::aOutputAimJointOPM;
MObject AimModuleNode::aOutputTargetJointOPM;
MObject AimModuleNode::aOutputUpJointOPM;

AimModuleNode::AimModuleNode() {}
AimModuleNode::~AimModuleNode() {}

const MString AimModuleNode::aCommandString = "aimModule";

void* AimModuleNode::creator()
{
    return new AimModuleNode();
}

MStatus AimModuleNode::initialize()
{
    MStatus status;

    status = RigModuleNodeBase::initializeBaseAttributes();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MFnMatrixAttribute mAttr;

    // 1. DISCRETE INPUT REST MATRICES
    aInputAimRestMatrix = mAttr.create("inputAimRestMatrix", "iarm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setStorable(true);
    mAttr.setKeyable(true);
    addAttribute(aInputAimRestMatrix);

    aInputTargetRestMatrix = mAttr.create("inputTargetRestMatrix", "itrm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setStorable(true);
    mAttr.setKeyable(true);
    addAttribute(aInputTargetRestMatrix);

    aInputUpRestMatrix = mAttr.create("inputUpRestMatrix", "iurm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setStorable(true);
    mAttr.setKeyable(true);
    addAttribute(aInputUpRestMatrix);

    // 2. DISCRETE CONTROL INPUT MATRICES
    aTargetControlMatrix = mAttr.create("targetControlMatrix", "tcm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setStorable(true);
    mAttr.setKeyable(true);
    addAttribute(aTargetControlMatrix);

    aUpControlMatrix = mAttr.create("upControlMatrix", "ucm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setStorable(true);
    mAttr.setKeyable(true);
    addAttribute(aUpControlMatrix);

    // 3. DISCRETE OUTPUT CONTROL OPMs
    aOutputTargetControlOPM = mAttr.create("outputTargetControlOPM", "tcopm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setWritable(false);
    mAttr.setStorable(false);
    addAttribute(aOutputTargetControlOPM);

    aOutputUpControlOPM = mAttr.create("outputUpControlOPM", "ucopm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setWritable(false);
    mAttr.setStorable(false);
    addAttribute(aOutputUpControlOPM);

    // 4. DISCRETE OUTPUT JOINT OPMs
    aOutputAimJointOPM = mAttr.create("outputAimJointOPM", "ajopm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setWritable(false);
    mAttr.setStorable(false);
    addAttribute(aOutputAimJointOPM);

    aOutputTargetJointOPM = mAttr.create("outputTargetJointOPM", "tjopm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setWritable(false);
    mAttr.setStorable(false);
    addAttribute(aOutputTargetJointOPM);

    aOutputUpJointOPM = mAttr.create("outputUpJointOPM", "ujopm", MFnMatrixAttribute::kDouble, &status);
    mAttr.setWritable(false);
    mAttr.setStorable(false);
    addAttribute(aOutputUpJointOPM);

    // 5. AFFECTS RELATIONSHIPS
    // Control OPM Outputs
    attributeAffects(aParentWorldMatrix, aOutputTargetControlOPM);
    attributeAffects(aParentModuleOffset, aOutputTargetControlOPM);
    attributeAffects(aInputTargetRestMatrix, aOutputTargetControlOPM);

    attributeAffects(aParentWorldMatrix, aOutputUpControlOPM);
    attributeAffects(aParentModuleOffset, aOutputUpControlOPM);
    attributeAffects(aInputUpRestMatrix, aOutputUpControlOPM);

    // Joint OPM Outputs
    attributeAffects(aParentWorldMatrix, aOutputAimJointOPM);
    attributeAffects(aParentModuleOffset, aOutputAimJointOPM);
    attributeAffects(aInputAimRestMatrix, aOutputAimJointOPM);
    attributeAffects(aInputTargetRestMatrix, aOutputAimJointOPM);
    attributeAffects(aInputUpRestMatrix, aOutputAimJointOPM);
    attributeAffects(aTargetControlMatrix, aOutputAimJointOPM);
    attributeAffects(aUpControlMatrix, aOutputAimJointOPM);

    attributeAffects(aParentWorldMatrix, aOutputTargetJointOPM);
    attributeAffects(aParentModuleOffset, aOutputTargetJointOPM);
    attributeAffects(aInputTargetRestMatrix, aOutputTargetJointOPM);
    attributeAffects(aTargetControlMatrix, aOutputTargetJointOPM);

    attributeAffects(aParentWorldMatrix, aOutputUpJointOPM);
    attributeAffects(aParentModuleOffset, aOutputUpJointOPM);
    attributeAffects(aInputUpRestMatrix, aOutputUpJointOPM);
    attributeAffects(aUpControlMatrix, aOutputUpJointOPM);

    // Socket Matrix Outputs
    attributeAffects(aParentWorldMatrix, aOutputSocketMatrix);
    attributeAffects(aParentModuleOffset, aOutputSocketMatrix);
    attributeAffects(aInputAimRestMatrix, aOutputSocketMatrix);
    attributeAffects(aInputTargetRestMatrix, aOutputSocketMatrix);
    attributeAffects(aInputUpRestMatrix, aOutputSocketMatrix);
    attributeAffects(aTargetControlMatrix, aOutputSocketMatrix);
    attributeAffects(aUpControlMatrix, aOutputSocketMatrix);

    return MStatus::kSuccess;
}

MStatus AimModuleNode::evaluateModuleSolver(const MPlug& plug, MDataBlock& data)
{
    // -------------------------------------------------------------------
    // 1. Output Control Offset Parent Matrices
    // -------------------------------------------------------------------
    if (plug == aOutputTargetControlOPM || plug == aOutputUpControlOPM)
    {
        MMatrix mParentWorld = getInputMatrix(data, aParentWorldMatrix);
        MMatrix mParentOffset = getInputMatrix(data, aParentModuleOffset);

        // Row-Major: Local Step * Parent World
        MMatrix mModuleBaseWorld = mParentOffset * mParentWorld;

        MMatrix mRestTarget = getInputMatrix(data, aInputTargetRestMatrix);
        MMatrix mRestUp = getInputMatrix(data, aInputUpRestMatrix);

        setOutputMatrix(data, aOutputTargetControlOPM, mRestTarget * mModuleBaseWorld);
        setOutputMatrix(data, aOutputUpControlOPM, mRestUp * mModuleBaseWorld);

        data.setClean(plug);
        return MStatus::kSuccess;
    }

    // -------------------------------------------------------------------
    // 2. Output Joint Offset Parent Matrices & Socket Matrices
    // -------------------------------------------------------------------
    if (plug == aOutputAimJointOPM || plug == aOutputTargetJointOPM || plug == aOutputUpJointOPM ||
        plug == aOutputSocketMatrix || plug.array() == aOutputSocketMatrix)
    {
        MMatrix mParentWorld = getInputMatrix(data, aParentWorldMatrix);
        MMatrix mParentOffset = getInputMatrix(data, aParentModuleOffset);

        MMatrix mModuleBaseWorld = mParentOffset * mParentWorld;

        // Aim joint world base position
        MMatrix mRestAim = getInputMatrix(data, aInputAimRestMatrix);
        MMatrix mAimBaseWorld = mRestAim * mModuleBaseWorld;

        // Target control world matrix
        MMatrix mRestTarget = getInputMatrix(data, aInputTargetRestMatrix);
        MMatrix mControlTarget = getInputMatrix(data, aTargetControlMatrix);
        MMatrix mTargetWorld = mControlTarget * mRestTarget * mModuleBaseWorld;

        // Up control world matrix
        MMatrix mRestUp = getInputMatrix(data, aInputUpRestMatrix);
        MMatrix mControlUp = getInputMatrix(data, aUpControlMatrix);
        MMatrix mUpWorld = mControlUp * mRestUp * mModuleBaseWorld;

        MPoint pAim(mAimBaseWorld[3][0], mAimBaseWorld[3][1], mAimBaseWorld[3][2]);
        MPoint pTarget(mTargetWorld[3][0], mTargetWorld[3][1], mTargetWorld[3][2]);
        MPoint pUp(mUpWorld[3][0], mUpWorld[3][1], mUpWorld[3][2]);

        // Calculate aim solution matrix (+X aim, +Z up)
        MMatrix mAimSolvedJointWorld = MathUtils::computeAimMatrix(pAim, pTarget, pUp, mAimBaseWorld);

        // Drive Discrete Joint OPM outputs
        setOutputMatrix(data, aOutputAimJointOPM, mAimSolvedJointWorld);
        setOutputMatrix(data, aOutputTargetJointOPM, mTargetWorld);
        setOutputMatrix(data, aOutputUpJointOPM, mUpWorld);

        // Drive Output Socket Matrices for child modules (0: Aim, 1: Target, 2: Up)
        setOutputMatrix(data, aOutputSocketMatrix, 0, mAimSolvedJointWorld);
        setOutputMatrix(data, aOutputSocketMatrix, 1, mTargetWorld);
        setOutputMatrix(data, aOutputSocketMatrix, 2, mUpWorld);

        MArrayDataHandle hOutSocketArray = data.outputArrayValue(aOutputSocketMatrix);
        hOutSocketArray.setAllClean();

        data.setClean(plug);
        return MStatus::kSuccess;
    }

    return MStatus::kUnknownParameter;
}

MObject AimModuleNode::createModule(
    const MString& moduleName,
    const MDagPath& aimJoint,
    const MDagPath& targetJoint,
    const MDagPath& upJoint,
    MObject oParentModule,
    unsigned int parentModuleSocketIndex,
    MObject oRigRoot,
    MDGModifier& dgMod,
    MDagModifier& dagMod,
    MStatus* status)
{
    MStatus localStat;

    // 1. Create the AimModule node
    MObject oModule = dgMod.createNode("aimModule");
    dgMod.renameNode(oModule, moduleName);

    // Retrieve discrete module plugs
    MFnDependencyNode moduleFn(oModule);

    MPlug pInputAimRestMatrix = moduleFn.findPlug("inputAimRestMatrix", false);
    MPlug pInputTargetRestMatrix = moduleFn.findPlug("inputTargetRestMatrix", false);
    MPlug pInputUpRestMatrix = moduleFn.findPlug("inputUpRestMatrix", false);

    MPlug pTargetControlMatrix = moduleFn.findPlug("targetControlMatrix", false);
    MPlug pUpControlMatrix = moduleFn.findPlug("upControlMatrix", false);

    MPlug pOutputTargetControlOPM = moduleFn.findPlug("outputTargetControlOPM", false);
    MPlug pOutputUpControlOPM = moduleFn.findPlug("outputUpControlOPM", false);

    MPlug pOutputAimJointOPM = moduleFn.findPlug("outputAimJointOPM", false);
    MPlug pOutputTargetJointOPM = moduleFn.findPlug("outputTargetJointOPM", false);
    MPlug pOutputUpJointOPM = moduleFn.findPlug("outputUpJointOPM", false);

    const unsigned int numSockets = 3;

    // Define numSockets for downstream module connections
    MPlug pNumSockets = moduleFn.findPlug("numSockets", false);
    if (!pNumSockets.isNull())
    {
        dgMod.newPlugValueShort(pNumSockets, static_cast<short>(numSockets));
    }

    // 2. Bake Joint Transforms & Input Rest Matrices using Utilities
    DagUtils::bakeJointOpmAndRestMatrix(aimJoint, pInputAimRestMatrix);
    DagUtils::bakeJointOpmAndRestMatrix(targetJoint, pInputTargetRestMatrix);
    DagUtils::bakeJointOpmAndRestMatrix(upJoint, pInputUpRestMatrix);

    // Connect Aim Joint OPM directly (Aim joint has no control)
    MFnDagNode aimJointFn(aimJoint);
    MPlug pAimJointOpm = aimJointFn.findPlug("offsetParentMatrix", false);
    dgMod.connect(pOutputAimJointOPM, pAimJointOpm);

    // 3. Create Target Control & Wire Plugs
    MObject oTargetControl = RigControlNode::createRigControl(oModule, dagMod, targetJoint.partialPathName());
    DagUtils::lockAndHideRotate(oTargetControl);
    DagUtils::lockAndHideScale(oTargetControl);

    MFnDependencyNode targetControlMFn(oTargetControl);
    MPlug pTargetControlMatrixPlug = targetControlMFn.findPlug("matrix", false);
    MPlug pTargetControlOpmPlug = targetControlMFn.findPlug("offsetParentMatrix", false);

    MFnDagNode targetJointFn(targetJoint);
    MPlug pTargetJointOpm = targetJointFn.findPlug("offsetParentMatrix", false);

    dgMod.connect(pTargetControlMatrixPlug, pTargetControlMatrix);
    dgMod.connect(pOutputTargetControlOPM, pTargetControlOpmPlug);
    dgMod.connect(pOutputTargetJointOPM, pTargetJointOpm);

    // 4. Create Up Control & Wire Plugs
    MObject oUpControl = RigControlNode::createRigControl(oModule, dagMod, upJoint.partialPathName());
    DagUtils::lockAndHideRotate(oUpControl);
    DagUtils::lockAndHideScale(oUpControl);

    MFnDependencyNode upControlMFn(oUpControl);
    MPlug pUpControlMatrixPlug = upControlMFn.findPlug("matrix", false);
    MPlug pUpControlOpmPlug = upControlMFn.findPlug("offsetParentMatrix", false);

    MFnDagNode upJointFn(upJoint);
    MPlug pUpJointOpm = upJointFn.findPlug("offsetParentMatrix", false);

    dgMod.connect(pUpControlMatrixPlug, pUpControlMatrix);
    dgMod.connect(pOutputUpControlOPM, pUpControlOpmPlug);
    dgMod.connect(pOutputUpJointOPM, pUpJointOpm);

    // Force creation of controls in scene to retrieve Target control's world position
    dgMod.doIt();
    dagMod.doIt();

    MSelectionList sel;
    sel.add(oTargetControl);
    MDagPath targetControlDag;
    sel.getDagPath(0, targetControlDag);
    MMatrix mFirstControlWorld = targetControlDag.inclusiveMatrix();

    // 5. Connect to Parent Module Socket
    if (!oParentModule.isNull())
    {
        localStat = RigModuleCommandHelpers::connectToParentModule(
            oParentModule,
            oModule,
            parentModuleSocketIndex,
            dgMod,
            mFirstControlWorld
        );

        if (!localStat)
        {
            MGlobal::displayError("Failed to connect Aim module to parent.");
            if (status)
            {
                *status = localStat;
            }
            return MObject::kNullObj;
        }
    }

    // 6. Connect to Rig Root Node
    if (!oRigRoot.isNull())
    {
        RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dgMod, oModule);
    }

    dgMod.doIt();
    dagMod.doIt();

    if (status)
    {
        *status = MStatus::kSuccess;
    }
    return oModule;
}

MObject AimModuleNode::createModule(
    const RigModuleData& moduleData,
    MObject rigRoot,
    MObject parentModule,
    MDGModifier& dgMod,
    MDagModifier& dagMod)
{
    MStatus status;

    const auto* aimArgs = std::get_if<AimModuleArgs>(&moduleData.moduleArgs);
    if (!aimArgs)
    {
        MGlobal::displayError(MString("Invalid moduleArgs variant type for AimModule: ") + moduleData.name.c_str());
        return MObject::kNullObj;
    }

    const std::string jointNames[3] = { aimArgs->aimJoint, aimArgs->targetJoint, aimArgs->upJoint };
    MDagPath jointDags[3];

    for (unsigned int i = 0; i < 3; ++i)
    {
        MSelectionList selList;
        status = selList.add(jointNames[i].c_str());
        if (status && selList.length() > 0)
        {
            status = selList.getDagPath(0, jointDags[i]);
            if (!status)
            {
                MGlobal::displayError(MString("Failed to retrieve MDagPath for joint: ") + jointNames[i].c_str());
                return MObject::kNullObj;
            }
        }
        else
        {
            MGlobal::displayError(MString("Could not find joint in scene: ") + jointNames[i].c_str());
            return MObject::kNullObj;
        }
    }

    unsigned int socketIndex = 0;
    if (moduleData.parentSocketIndex >= 0)
    {
        socketIndex = static_cast<unsigned int>(moduleData.parentSocketIndex);
    }
    else
    {
        short numSockets = 0;
        if (!parentModule.isNull())
        {
            MFnDependencyNode fnParent(parentModule);
            MPlug pNumSockets = fnParent.findPlug("numSockets", false);
            if (!pNumSockets.isNull())
            {
                numSockets = pNumSockets.asShort();
            }

            if (numSockets > 0)
            {
                socketIndex = static_cast<unsigned int>(numSockets - 1);
            }
            else
            {
                MGlobal::displayError(MString("Parent module did not define numSockets: ") + MString(fnParent.name()));
                socketIndex = 0;
            }
        }
    }

    MStatus executionStatus;
    MObject oModule = AimModuleNode::createModule(
        moduleData.name.c_str(),
        jointDags[0],
        jointDags[1],
        jointDags[2],
        parentModule,
        socketIndex,
        rigRoot,
        dgMod,
        dagMod,
        &executionStatus
    );

    if (!executionStatus)
    {
        MGlobal::displayError(MString("Failed to build Aim module: ") + moduleData.name.c_str());
        return MObject::kNullObj;
    }

    return oModule;
}