#include "RigControlNode.h"

#pragma warning(push)
#pragma warning(disable: 26495)
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MPlug.h>
#include <maya/MVector.h>
#include <maya/MUIDrawManager.h>
//#include <maya/MGeometryUtilities.h>
#pragma warning(pop)

MTypeId RigControlNode::id(0x0013B5C0);
MString RigControlNode::drawDbClassification("drawdb/geometry/rigControlNode");
MString RigControlNode::drawRegistrantId("RigControlNodePlugin");

MObject RigControlNode::aShapeType;
MObject RigControlNode::aWireColor;

RigControlNode::RigControlNode() {}
RigControlNode::~RigControlNode() {}

void* RigControlNode::creator() {
    return new RigControlNode();
}

MStatus RigControlNode::initialize() {
    MFnEnumAttribute eAttr;
    MFnNumericAttribute nAttr;

    aShapeType = eAttr.create("shapeType", "st", 0);
    eAttr.addField("Cube", 0);
    eAttr.addField("Sphere", 1);
    eAttr.addField("Circle", 2);
    eAttr.setKeyable(true);
    eAttr.setStorable(true);
    addAttribute(aShapeType);

    aWireColor = nAttr.createColor("wireframeColor", "wc");
    nAttr.setDefault(1.0f, 0.0f, 0.0f);
    nAttr.setKeyable(true);
    nAttr.setStorable(true);
    addAttribute(aWireColor);

    return MS::kSuccess;
}

MHWRender::MPxDrawOverride* RigControlDrawOverride::Creator(const MObject& obj) {
    return new RigControlDrawOverride(obj);
}

RigControlDrawOverride::RigControlDrawOverride(const MObject& obj)
    : MHWRender::MPxDrawOverride(obj, nullptr) {}

MUserData* RigControlDrawOverride::prepareForDraw(
    const MDagPath& objPath,
    const MDagPath& cameraPath,
    const MHWRender::MFrameContext& frameContext,
    MUserData* oldData)
{
    ControlDrawData* data = dynamic_cast<ControlDrawData*>(oldData);
    if (!data) {
        data = new ControlDrawData();
    }

    MObject node = objPath.node();

    // 1. Fetch Shape Type
    MPlug(node, RigControlNode::aShapeType).getValue(data->shapeType);

    // 2. Determine Display Color (Selection Awareness)
    /*
    MHWRender::DisplayStatus status = MHWRender::MGeometryUtilities::displayStatus(objPath);
    if (status == MHWRender::kLead || status == MHWRender::kActive || status == MHWRender::kHilite) {
        // Query standard Maya VP2 wireframe selection colors (Green / White)
        data->color = MHWRender::MGeometryUtilities::wireframeColor(objPath);
    }
    else {
        // Fall back to node's custom wireframe attribute when dormant
        MPlug plugColor(node, RigControlNode::aWireColor);
        float r = 1.0f, g = 0.0f, b = 0.0f;
        plugColor.child(0).getValue(r);
        plugColor.child(1).getValue(g);
        plugColor.child(2).getValue(b);
        data->color = MColor(r, g, b, 1.0f);
    }
    */
    // Fall back to node's custom wireframe attribute when dormant
    MPlug plugColor(node, RigControlNode::aWireColor);
    float r = 1.0f, g = 0.0f, b = 0.0f;
    plugColor.child(0).getValue(r);
    plugColor.child(1).getValue(g);
    plugColor.child(2).getValue(b);
    data->color = MColor(r, g, b, 1.0f);

    return data;
}

void RigControlDrawOverride::addUIDrawables(
    const MDagPath& objPath,
    MHWRender::MUIDrawManager& drawManager,
    const MHWRender::MFrameContext& frameContext,
    const MUserData* data)
{
    const ControlDrawData* cData = dynamic_cast<const ControlDrawData*>(data);
    if (!cData) return;

    drawManager.beginDrawable();
    drawManager.setColor(cData->color);

    // Draw strictly in LOCAL OBJECT SPACE (0,0,0) with unit vectors.
    // Maya automatically transforms this local geometry by the DAG node's world matrix.
    MPoint center(0.0, 0.0, 0.0);
    MVector up(0.0, 1.0, 0.0);
    MVector normal(0.0, 0.0, 1.0);
    double radius = 1.0;

    switch (cData->shapeType) {
    case 0: // Box / Cube (Center, Up, Normal, ScaleX, ScaleY, ScaleZ)
        drawManager.box(center, up, normal, 1.0, 1.0, 1.0, false);
        break;
    case 1: // Sphere (Center, Radius)
        drawManager.sphere(center, radius, false);
        break;
    case 2: // Circle (Center, Normal, Radius)
        drawManager.circle(center, normal, radius, false);
        break;
    default:
        drawManager.box(center, up, normal, 1.0, 1.0, 1.0, false);
        break;
    }

    drawManager.endDrawable();
}