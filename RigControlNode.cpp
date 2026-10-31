#include "RigControlNode.h"
#include "Side.h"
#include "Utilities.h"
#include "Constants.h"

#include <string>

#include <maya/MFnEnumAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MPlug.h>
#include <maya/MVector.h>
#include <maya/MUIDrawManager.h>
#include <maya/MHWGeometryUtilities.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MDagModifier.h>
#include <maya/MGlobal.h>
#include <maya/MFnMatrixData.h>

const MPointArray& CustomShapes::Triangle()
{
    // Lives for the duration of the Maya session.
    static const MPointArray points = []() 
    {
        MPointArray pArray;
        pArray.append(0.0, 0.0, 1.0);
        pArray.append(-1.0, 0.0, -1.0);
        pArray.append(1.0, 0.0, -1.0);
        pArray.append(0.0, 0.0, 1.0);
        return pArray;
    }();
    return points;
}

MPointArray CustomShapes::transformPointArray(const MPointArray& points, const MMatrix& matrix)
{
    MPointArray outputPoints;
    const unsigned int count = points.length();
    outputPoints.setLength(count);

    for (unsigned int i = 0; i < count; ++i)
    {
        // Row-major post-multiplication in Maya C++ API
        outputPoints[i] = points[i] * matrix;
    }

    return outputPoints;
}

MTypeId RigControlNode::id(0x0013B5C0);
MString RigControlNode::drawDbClassification("drawdb/geometry/rigControlNode");
MString RigControlNode::drawRegistrantId("RigControlNodePlugin");

MObject RigControlNode::aShapeType;
MObject RigControlNode::aWireColor;
MObject RigControlNode::aWireAlpha;
MObject RigControlNode::aShapeTransform;
MObject RigControlNode::aFilled;

MObject RigControlNode::aRigModule;

RigControlNode::RigControlNode() 
    : m_drawIsDirty(true) {}

RigControlNode::~RigControlNode() {}

void* RigControlNode::creator() 
{
    return new RigControlNode();
}

MStatus RigControlNode::setDependentsDirty(const MPlug& plugBeingDirtied, MPlugArray& affectedPlugs)
{
    // Check if the plug being dirtied matches any visual, transform, or matrix attributes
    MObject attr = plugBeingDirtied.attribute();
    MObject parentAttr = plugBeingDirtied.parent().attribute();

    // use this block for simple attributes that have no children (aren't vectors or colors)
    if (attr == aShapeType || attr == aShapeTransform || attr == aWireAlpha ||
        attr == aWireColor || parentAttr == aWireColor || attr == aFilled)
    {
        setDrawDirty(); // Flag for Viewport 2.0 MPxDrawOverride
        // Importantly, set this to be dirty so that the viewport redraws while we're changing attributes.
        MHWRender::MRenderer::setGeometryDrawDirty(thisMObject());
    }
 
    // Call base class MPxTransform implementation
    return MPxTransform::setDependentsDirty(plugBeingDirtied, affectedPlugs);
}

MStatus RigControlNode::initialize() {
    MFnEnumAttribute eAttr;
    MFnNumericAttribute nAttr;
    MFnMessageAttribute msgAttr;
    MFnMatrixAttribute mAttr;

    aShapeType = eAttr.create(RigControlNodeAttr::ShapeType, RigControlNodeAttr::ShapeType, 0);
    // TODO: the maya enum and cpp enum are not tied together. 
    eAttr.addField("Box", 0);
    eAttr.addField("Sphere", 1);
    eAttr.addField("Circle", 2);
    eAttr.addField("Capsule", 3);
    eAttr.addField("Triangle", 4);
    eAttr.addField("Cylinder", 5);
    eAttr.addField("Square", 6);
    eAttr.setKeyable(false);
    eAttr.setStorable(true);
    addAttribute(aShapeType);

    aWireColor = nAttr.createColor(RigControlNodeAttr::WireframeColor, RigControlNodeAttr::WireframeColor);
    nAttr.setDefault(1.0f, 0.0f, 0.0f);  // note that this was silently failing when trying to set alpha
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aWireColor);

    aWireAlpha = nAttr.create(RigControlNodeAttr::WireframeAlpha, RigControlNodeAttr::WireframeAlpha, MFnNumericData::kFloat, 1.0f);
    nAttr.setMin(0.0f);
    nAttr.setMax(1.0f);
    nAttr.setKeyable(false);
    nAttr.setStorable(true);
    addAttribute(aWireAlpha);

    aShapeTransform = mAttr.create(RigControlNodeAttr::ShapeTransform, RigControlNodeAttr::ShapeTransform, MFnMatrixAttribute::kDouble);
    mAttr.setWritable(true);
    mAttr.setStorable(true);
    addAttribute(aShapeTransform);

    aFilled = nAttr.create(RigControlNodeAttr::Filled, RigControlNodeAttr::Filled, MFnNumericData::kBoolean, false);
    nAttr.setWritable(true);
    nAttr.setStorable(true);
    addAttribute(aFilled);

    // Rig Attributes
    aRigModule = msgAttr.create(RigControlNodeAttr::RigModule, RigControlNodeAttr::RigModule);
    addAttribute(aRigModule);

    return MS::kSuccess;
}

MHWRender::MPxDrawOverride* RigControlDrawOverride::Creator(const MObject& obj) {
    return new RigControlDrawOverride(obj);
}

//The last bool is important for efficiency. Sets it to not be always dirty so viewport tumbles don't trigger MPlug reads
RigControlDrawOverride::RigControlDrawOverride(const MObject& obj)
    : MHWRender::MPxDrawOverride(obj, nullptr, false) {}

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

        // Get the shape transformation
        MPlug pShapeTransform(node, RigControlNode::aShapeTransform);
        MObject oShapeTransformObj;
        pShapeTransform.getValue(oShapeTransformObj);
        MFnMatrixData fnShapeTransform(oShapeTransformObj);
        MMatrix mShapeTransform = fnShapeTransform.matrix();
        data->shapeTransform = mShapeTransform;

        MTransformationMatrix tMatrix(mShapeTransform);

        // Scale
        double scale[3];
        tMatrix.getScale(scale, MSpace::kObject);
        data->width = scale[0];
        data->height = scale[1];
        data->depth = scale[2];

        // Center offset
        MPoint centerOffset(tMatrix.getTranslation(MSpace::kObject));
        data->centerOffset = centerOffset;

        MMatrix rMatrix = tMatrix.asRotateMatrix();

        // Normal Vector is the matrix X axis
        MVector normalVector;
        normalVector = MVector(rMatrix[0][0], rMatrix[0][1], rMatrix[0][2]);
        normalVector.normalize();

        if (normalVector.length() < 0.001)
        {
            normalVector = MVector(1, 0, 0);
        }
        data->normalVector = normalVector;

        // Up Vector is the Y axis
        MVector upVector;
        upVector = MVector(rMatrix[1][0], rMatrix[1][1], rMatrix[1][2]);
        upVector.normalize();

        if (upVector.length() < 0.001)
        {
            upVector = MVector(0, 0, 1);
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

        // Filled
        MPlug plugFilled(node, RigControlNode::aFilled);
        bool filled = false;
        if (!plugFilled.isNull())
        {
            filled = plugFilled.asBool();
        }
        data->filled = filled;

        // Important! Set draw clean for the node so we can keep these cached plug reads until they dirty again.
        rigControlNode->setDrawClean();
    }
    
    // This part runs every frame update. It is very fast though as it is not reading plug values.
    // It wasn't dirty, but we still need to do selection color change updates.
    MHWRender::DisplayStatus displayStatus = MHWRender::MGeometryUtilities::displayStatus(objPath);

    // For selection, do some fancy stuff to make it so that child selected controls that are not explicitely selected look different.
    MSelectionList selList;
    MGlobal::getActiveSelectionList(selList);
    bool isObjectSelected = selList.hasItem(objPath);
    unsigned int selLen = selList.length();

    MColor newColor = data->dormantColor;
    data->lineWidth = 1.0f;

    if (displayStatus == MHWRender::kLead && isObjectSelected && selLen != 1)
    {
        // Active selection (White)
        data->color = MColor(Colors::ActiveLead.r, Colors::ActiveLead.g, Colors::ActiveLead.b, data->dormantColor.a);
        data->lineWidth = 2.0f;
    }
    else if ((displayStatus == MHWRender::kActive || displayStatus == MHWRender::kLead) && isObjectSelected)
    {
        // Affected selection (Soft Green)
        data->color = MColor(Colors::ActiveSelected.r, Colors::ActiveSelected.g, Colors::ActiveSelected.b, data->dormantColor.a);
        data->lineWidth = 2.0f;
    }
    else if ((displayStatus == MHWRender::kActiveAffected) || (!isObjectSelected && displayStatus != MHWRender::kDormant))
    {
        // Affected selection (Soft Green but less alpha)
        data->color = MColor(Colors::ActiveSelected.r, Colors::ActiveSelected.g, Colors::ActiveSelected.b, data->dormantColor.a * 0.75);
    }
    else
    {
        // Dormant (Unselected) -> Use cached base color extracted from plugs
        data->color = newColor;
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
    // TODO: restore this again, where we are pulling from the matrix to build these.
    MPoint center(controlDrawData->centerOffset);
    MVector up(controlDrawData->upVector);
    MVector normal(controlDrawData->normalVector);
    double width = controlDrawData->width;
    double height = controlDrawData->height;
    double depth = controlDrawData->depth;
    MMatrix mShapeTransform(controlDrawData->shapeTransform);
    bool filled = controlDrawData->filled;

    switch (controlDrawData->shapeType) 
    {
        case ShapeType::Box: // Box / Cube (Center, Up, Normal, ScaleX, ScaleY, ScaleZ)
            drawManager.box(center, up, normal, width, height, depth, filled);
            break;
        case ShapeType::Sphere: // Sphere (Center, Radius)
            drawManager.sphere(center, width, filled);
            break;
        case ShapeType::Circle: // Circle (Center, Normal, Radius)
            drawManager.circle(center, normal, width, filled);
            break;
        case ShapeType::Capsule: // Capsule (Center, Up, Radius, Height, Subdivisions Width, Subdivisions Height, filled)
            drawManager.capsule(center, up, width, height, 6, 6, filled);
            break;
        case ShapeType::Triangle:
            drawManager.lineStrip(CustomShapes::transformPointArray(CustomShapes::Triangle(), mShapeTransform), false);
            break;
        case ShapeType::Cylinder:
            drawManager.cylinder(center, up, width, height, 8, filled);
            break;
        case ShapeType::Square:
            drawManager.rect(center, up, normal, width, height, filled);
            break;
        default: // Default is Box
            drawManager.box(center, up, normal, width, height, depth, filled);
            break;
    }
    drawManager.endDrawable();
}

// Creates a rig control and wires it to the module
MObject RigControlNode::createRigControl(MObject& moduleNode, MDagModifier& dagMod, const MString& jointName, const ShapeType& shapeType, const Side& side)
{
    MStatus status;

    // Create and name the control transform
    MObject oControl = dagMod.createNode(RigControlNode::id);

    // All transforms on rig controls get ignored and calculated by modules instead
    DagUtils::setInheritsTransforms(oControl, false, dagMod);

    MString ctrlName = jointName;
    ctrlName.substitute("_jnt", "");  // TODO: global var for jnt suffix
    ctrlName += "_ctrl";  // TODO: global naming method, global var for ctrl suffix
    ctrlName += getSideSuffix(side);
    dagMod.renameNode(oControl, ctrlName);

    // TODO: set rig control side color based on module side attr.
    MPlug controlColor(oControl, RigControlNode::aWireColor);
    MColor sideColor = getColorFromSide(side);

    if (!controlColor.isNull())
    {
        dagMod.newPlugValueFloat(controlColor.child(0), sideColor.r);
        dagMod.newPlugValueFloat(controlColor.child(1), sideColor.g);
        dagMod.newPlugValueFloat(controlColor.child(2), sideColor.b);
    }

    MPlug pShapeType(oControl, aShapeType);
    short shapeTypeShort = static_cast<short>(shapeType);
    dagMod.newPlugValueShort(pShapeType, shapeTypeShort);

    return oControl;
}

ShapeType getShapeTypeFromString(const std::string& value)
{
    auto shapeTypeIt = stringToShapeType.find(value);

    if (shapeTypeIt != stringToShapeType.end())
    {
        return shapeTypeIt->second;
    }
    return ShapeType::Box;
}