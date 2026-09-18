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
MObject RigControlNode::aNormalVector;
MObject RigControlNode::aUpVector;
MObject RigControlNode::aWidth;
MObject RigControlNode::aHeight;
MObject RigControlNode::aDepth;

RigControlNode::RigControlNode() 
    : m_drawIsDirty(true)
{}

RigControlNode::~RigControlNode() {}

void* RigControlNode::creator() {
    return new RigControlNode();
}

MStatus RigControlNode::setDependentsDirty(const MPlug& plugBeingDirtied, MPlugArray& affectedPlugs)
{
    // Check if the plug being dirtied matches any visual, transform, or matrix attributes
    MObject attr = plugBeingDirtied.attribute();

    // use this block for simple attributes that have no children (aren't vectors or colors)
    if (attr == aShapeType ||
        attr == aWidth ||
        attr == aHeight ||
        attr == aDepth)
    {
        setDrawDirty(); // Flag for Viewport 2.0 MPxDrawOverride
    }
    else
    {
        // Use this block for all attributes that have children, as child plugs will have to use this to be seen as dirty.
        MObject parentAttr = plugBeingDirtied.parent().attribute();

        if (attr == aCenterOffset || parentAttr == aCenterOffset ||
            attr == aNormalVector || parentAttr == aNormalVector ||
            attr == aUpVector || parentAttr == aUpVector ||
            attr == aWireColor || parentAttr == aWireColor)
        {
            setDrawDirty();
        }
    }

    // Call base class MPxTransform implementation (CRITICAL)
    return MPxTransform::setDependentsDirty(plugBeingDirtied, affectedPlugs);
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

    aCenterOffset = nAttr.createPoint("centerOffset", "off");
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aCenterOffset);

    aNormalVector = nAttr.createPoint("normalVector", "nv");
    nAttr.setDefault(1.0, 0.0, 0.0);
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aNormalVector);

    aUpVector = nAttr.createPoint("upVector", "uv");
    nAttr.setDefault(0.0, 0.0, 1.0);
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aUpVector);

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

//The last bool is important for efficiency. Sets it to not be always dirty so viewport tumbles don't trigger MPlug reads
RigControlDrawOverride::RigControlDrawOverride(const MObject& obj)
    : MHWRender::MPxDrawOverride(obj, nullptr, true) {}  // NOTE: isAlwaysDirty being false means as we edit shape attrs we don't see them change.

// This method only gathers plug data if the plugs have been determined as dirty. Otherwise it is re-using cached MUserData
MUserData* RigControlDrawOverride::prepareForDraw(
    const MDagPath& objPath,
    const MDagPath& cameraPath,
    const MHWRender::MFrameContext& frameContext,
    MUserData* oldData)
{
    ControlDrawData* data = dynamic_cast<ControlDrawData*>(oldData);
    if (!data) 
    {
        data = new ControlDrawData();
    }

    MObject node = objPath.node();
    MFnDependencyNode fnNode(node);
    RigControlNode* rigControlNode = dynamic_cast<RigControlNode*>(fnNode.userNode());

    if (rigControlNode && rigControlNode->isDrawDirty())
    {
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

        // Normal Vector
        MVector normalVector;
        MPlug pNormalVector = MPlug(node, RigControlNode::aNormalVector);
        if (!pNormalVector.isNull())
        {
            pNormalVector.child(0).getValue(normalVector.x);
            pNormalVector.child(1).getValue(normalVector.y);
            pNormalVector.child(2).getValue(normalVector.z);
        }
        data->normalVector = normalVector;

        // Up Vector
        MVector upVector;
        MPlug pUpVector = MPlug(node, RigControlNode::aUpVector);
        if (!pUpVector.isNull())
        {
            pUpVector.child(0).getValue(upVector.x);
            pUpVector.child(1).getValue(upVector.y);
            pUpVector.child(2).getValue(upVector.z);
        }
        data->upVector = upVector;

        // Base color from plugs. Other color is from selection highlighting.
        // Fetch dormant color (red, blue, etc)
        MPlug plugColor(node, RigControlNode::aWireColor);
        MColor color;
        float lineWidth = 1.0f;
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

        if (!plugColor.isNull())
        {
            plugColor.child(0).getValue(r);
            plugColor.child(1).getValue(g);
            plugColor.child(2).getValue(b);
        }

        MPlug plugAlpha(node, RigControlNode::aWireAlpha);
        if (!plugAlpha.isNull())
        {
            plugAlpha.getValue(a);
        }

        data->dormantColor = MColor(r, g, b, a);
        data->lineWidth = lineWidth;

        // Important! Set draw clean for the node so we can keep these cached plug reads until they dirty again.
        rigControlNode->setDrawClean();
    }
    
    // This part runs every frame update. It is very fast though as it is not reading plug values.
    // It wasn't dirty, but we still need to do selection color change updates.
    MHWRender::DisplayStatus displayStatus = MHWRender::MGeometryUtilities::displayStatus(objPath);
    if (displayStatus == MHWRender::kActive)
    {
        // Active selection (White)
        data->color = MColor(1.0f, 1.0f, 1.0f, data->dormantColor.a);
        data->lineWidth = 1.5f;
    }
    else if (displayStatus == MHWRender::kLead)
    {
        // Lead selection (Soft Green)
        data->color = MColor(0.26f, 1.0f, 0.64f, data->dormantColor.a);
        data->lineWidth = 2.0f;
    }
    else
    {
        // Dormant (Unselected) -> Use cached base color extracted from plugs
        data->color = data->dormantColor;
        data->lineWidth = 1.0f;
    }
    
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
    MPoint center(controlDrawData->centerOffset);
    MVector up(controlDrawData->upVector);
    MVector normal(controlDrawData->normalVector);
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
    case 3: // Capsule (Center, Up, Radius, Height, Subdivisions Width, Subdivisions Height, filled)
        drawManager.capsule(center, up, controlDrawData->width, controlDrawData->height, 6, 6, filled);
        break;
    default: // Default is Box / Cube
        drawManager.box(center, up, normal, controlDrawData->width, controlDrawData->height, controlDrawData->depth, filled);
        break;
    }

    drawManager.endDrawable();
}

// TODO: Method for wiring a control node to a module.