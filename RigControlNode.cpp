#include "RigControlNode.h"

#include <maya/MFnEnumAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MPlug.h>
#include <maya/MVector.h>
#include <maya/MUIDrawManager.h>
#include <maya/M3dView.h>
#include <maya/MHWGeometryUtilities.h>
#include <maya/MGlobal.h>

MTypeId RigControlNode::id(0x0013B5C0);
MString RigControlNode::drawDbClassification("drawdb/geometry/rigControlNode");
MString RigControlNode::drawRegistrantId("RigControlNodePlugin");

MObject RigControlNode::aShapeType;
MObject RigControlNode::aWireColor;
MObject RigControlNode::aWireAlpha;
MObject RigControlNode::aCenterOffset;
MObject RigControlNode::aWidth;
MObject RigControlNode::aHeight;
MObject RigControlNode::aDepth;

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
    eAttr.addField("Capsule", 3);
    eAttr.setKeyable(false);
    eAttr.setStorable(true);
    addAttribute(aShapeType);

    aWireColor = nAttr.createColor("wireframeColor", "wc");
    nAttr.setDefault(1.0f, 0.0f, 0.0f);  // note that this was silently failing when trying to set alpha
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aWireColor);

    aWireAlpha = nAttr.create("wireframeAlpha", "wa", MFnNumericData::kFloat, 1.0f);
    nAttr.setMin(0.0f);
    nAttr.setMax(1.0f);
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aWireAlpha);

    aCenterOffset = nAttr.create("centerOffset", "off", MFnNumericData::k3Double, 0.0f);
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aCenterOffset);

    aWidth = nAttr.create("width", "w", MFnNumericData::kDouble, 1.0f);
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aWidth);

    aHeight = nAttr.create("height", "h", MFnNumericData::kDouble, 1.0f);
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aHeight);

    aDepth = nAttr.create("depth", "d", MFnNumericData::kDouble, 1.0f);
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aDepth);

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

    // 1. Fetch Shape Parameters
    MPlug(node, RigControlNode::aShapeType).getValue(data->shapeType);
    MPlug(node, RigControlNode::aWidth).getValue(data->width);
    MPlug(node, RigControlNode::aHeight).getValue(data->height);
    MPlug(node, RigControlNode::aDepth).getValue(data->depth);

    // Center offset
    MPoint centerOffset;
    MPlug pCenterOffset = MPlug(node, RigControlNode::aCenterOffset);
    if (!pCenterOffset.isNull())
    {
        pCenterOffset.child(0).getValue(centerOffset.x);
        pCenterOffset.child(1).getValue(centerOffset.y);
        pCenterOffset.child(2).getValue(centerOffset.z);
    }
    data->centerOffset = centerOffset;

    // Color with selection highlighting
    MColor color;
    float lineWidth = 1.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    MHWRender::DisplayStatus displayStatus = MHWRender::MGeometryUtilities::displayStatus(objPath);
    if (displayStatus == MHWRender::kActive)
    {
        r = 1.0f, b = 1.0f, g = 1.0f; // white (active selection)
    }
    else if (displayStatus == MHWRender::kLead)
    {
        r = 0.26f, g = 1.0f, b = 0.64f;  // green (selected)
        lineWidth = 2.0f;
    }
    else
    {
        // Fetch dormant color (red, blue, etc)
        MPlug plugColor(node, RigControlNode::aWireColor);
        if (!plugColor.isNull()) 
        {
            plugColor.child(0).getValue(r);
            plugColor.child(1).getValue(g);
            plugColor.child(2).getValue(b);
        }
    }

    MPlug plugAlpha(node, RigControlNode::aWireAlpha);
    if (!plugAlpha.isNull()) 
    {
        plugAlpha.getValue(a);
    }
    
    data->color = MColor(r, g, b, a);
    data->lineWidth = lineWidth;

    return data;
}

void RigControlDrawOverride::addUIDrawables(
    const MDagPath& objPath,
    MHWRender::MUIDrawManager& drawManager,
    const MHWRender::MFrameContext& frameContext,
    const MUserData* data)
{
    const ControlDrawData* controlDrawData = dynamic_cast<const ControlDrawData*>(data);
    if (!controlDrawData) return;

    drawManager.beginDrawable();
    drawManager.setColor(controlDrawData->color);
    drawManager.setLineWidth(controlDrawData->lineWidth);
    drawManager.setDepthPriority(100);

    // Draw strictly in LOCAL OBJECT SPACE (0,0,0) with unit vectors.
    // Maya automatically transforms this local geometry by the DAG node's world matrix.
    //MPoint center(centerOffset.x, centerOffset.y, centerOffset.z);  // TODO: have center up and normal in control draw data
    MPoint center(controlDrawData->centerOffset);
    MVector up(0.0, 1.0, 0.0);
    MVector normal(0.0, 0.0, 1.0);
    double radius = 1.0;
    bool filled = false;

    switch (controlDrawData->shapeType) {
    case 0: // Box / Cube (Center, Up, Normal, ScaleX, ScaleY, ScaleZ)
        drawManager.box(center, up, normal, controlDrawData->width, controlDrawData->height, controlDrawData->depth, filled);
        break;
    case 1: // Sphere (Center, Radius)
        drawManager.sphere(center, controlDrawData->width, filled);
        break;
    case 2: // Circle (Center, Normal, Radius)
        drawManager.circle(center, normal, controlDrawData->width, filled);
        break;
    case 3: // Capsule (Center, Up, Radius, Height, Subdivisions Width, Subdivisions Height, filled
        drawManager.capsule(center, up, controlDrawData->width, controlDrawData->height, 6, 6, filled);
        break;
    default: // Default is Box / Cube
        drawManager.box(center, up, normal, controlDrawData->width, controlDrawData->height, controlDrawData->depth, filled);
        break;
    }

    drawManager.endDrawable();
}