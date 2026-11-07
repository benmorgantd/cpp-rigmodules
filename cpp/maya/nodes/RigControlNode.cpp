#include "maya/nodes/RigControlNode.h"
#include "core/Constants.h"
#include "maya/shared/Colors.h"
#include "maya/shared/Utilities.h"

#include <string>

#include <maya/MDagModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MHWGeometryUtilities.h>
#include <maya/MNamespace.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>

namespace
{
	// Half extents of a primitive along the frame's axes, matching what addUIDrawables asks the draw manager for.
	// Measured against a +-1 cube in VP2 (2026-11-07): box, sphere, circle and rect sizes are half extents and radii,
	// while capsule and cylinder heights are full lengths. The round shapes don't scale linearly with the matrix
	// (a capsule's caps follow its radius, a sphere uses one radius), so the box is built from the sizes, not by
	// pushing a unit box through shapeTransform.
	MVector primitiveHalfExtents(ShapeType shapeType, const ShapeFrame& frame)
	{
		switch (shapeType)
		{
			case ShapeType::Sphere:
				return MVector(frame.sizeX, frame.sizeX, frame.sizeX);
			case ShapeType::Circle:
				return MVector(0.0, frame.sizeY, frame.sizeY);
			case ShapeType::Capsule:
				return MVector(0.5 * frame.sizeX + frame.sizeY, frame.sizeY, frame.sizeY);
			case ShapeType::Cylinder:
				return MVector(0.5 * frame.sizeX, frame.sizeY, frame.sizeY);
			case ShapeType::Square:
				return MVector(0.0, frame.sizeY, frame.sizeZ);
			default:
				return MVector(frame.sizeX, frame.sizeY, frame.sizeZ);
		}
	}

	MBoundingBox primitiveShapeBounds(ShapeType shapeType, const ShapeFrame& frame)
	{
		const MVector halfExtents = primitiveHalfExtents(shapeType, frame);
		MBoundingBox bounds;
		for (const double signX : { -1.0, 1.0 })
		{
			for (const double signY : { -1.0, 1.0 })
			{
				for (const double signZ : { -1.0, 1.0 })
				{
					bounds.expand(frame.center + frame.axisX * (signX * halfExtents.x) + frame.axisY * (signY * halfExtents.y)
						+ frame.axisZ * (signZ * halfExtents.z));
				}
			}
		}
		return bounds;
	}

	MBoundingBox curveShapeBounds(const std::vector<ShapePoint>& points)
	{
		MBoundingBox bounds;
		for (const ShapePoint& point : points)
		{
			bounds.expand(MPoint(point.x, point.y, point.z));
		}
		return bounds;
	}
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
MObject RigControlNode::aControlMarkup;

RigControlNode::RigControlNode()
{
}

RigControlNode::~RigControlNode()
{
}

void* RigControlNode::creator()
{
	return new RigControlNode();
}

MStatus RigControlNode::setDependentsDirty(const MPlug& plugBeingDirtied, MPlugArray& affectedPlugs)
{
	const MObject attr = plugBeingDirtied.attribute();
	// wireframeColor is a compound, so a change to one channel arrives on a child plug.
	const bool isColorChannel = plugBeingDirtied.isChild() && plugBeingDirtied.parent().attribute() == aWireColor;

	if (attr == aShapeType || attr == aShapeTransform || attr == aWireAlpha || attr == aWireColor || attr == aFilled || isColorChannel)
	{
		m_drawIsDirty = true;
		// Redraw while the attribute is being changed, not only on the next unrelated refresh.
		MHWRender::MRenderer::setGeometryDrawDirty(thisMObject());
	}

	return MPxTransform::setDependentsDirty(plugBeingDirtied, affectedPlugs);
}

bool RigControlNode::isBounded() const
{
	return true;
}

MBoundingBox RigControlNode::boundingBox() const
{
	const MObject oControl = thisMObject();
	const ShapeType shapeType = getShapeType(oControl);
	const MMatrix mShapeTransform = getShapeTransform(oControl);
	const std::vector<ShapePoint>* pCurvePoints = getCurveShapePoints(shapeType);
	if (pCurvePoints == nullptr)
	{
		return primitiveShapeBounds(shapeType, getShapeFrame(mShapeTransform));
	}

	MBoundingBox bounds = curveShapeBounds(*pCurvePoints);
	bounds.transformUsing(mShapeTransform);
	return bounds;
}

ShapeFrame RigControlNode::getShapeFrame(const MMatrix& mShapeTransform)
{
	const MVector rowX(mShapeTransform[0][0], mShapeTransform[0][1], mShapeTransform[0][2]);
	const MVector rowY(mShapeTransform[1][0], mShapeTransform[1][1], mShapeTransform[1][2]);
	const MVector rowZ(mShapeTransform[2][0], mShapeTransform[2][1], mShapeTransform[2][2]);

	ShapeFrame frame;
	frame.center = MPoint(mShapeTransform[3][0], mShapeTransform[3][1], mShapeTransform[3][2]);
	frame.sizeX = rowX.length();
	frame.sizeY = rowY.length();
	frame.sizeZ = rowZ.length();
	frame.axisX = frame.sizeX > 1e-6 ? rowX / frame.sizeX : MVector::xAxis;
	frame.axisY = frame.sizeY > 1e-6 ? rowY / frame.sizeY : MVector::yAxis;
	frame.axisZ = frame.sizeZ > 1e-6 ? rowZ / frame.sizeZ : MVector::zAxis;
	return frame;
}

MMatrix RigControlNode::getShapeTransform(const MObject& oControl)
{
	MPlug pShapeTransform(oControl, aShapeTransform);
	MObject oShapeTransform;
	if (pShapeTransform.getValue(oShapeTransform) != MStatus::kSuccess || oShapeTransform.isNull())
	{
		return MMatrix::identity;
	}
	return MFnMatrixData(oShapeTransform).matrix();
}

ShapeType RigControlNode::getShapeType(const MObject& oControl)
{
	const short value = MPlug(oControl, aShapeType).asShort();
	return (value >= 0 && value < ShapeType::Count) ? static_cast<ShapeType>(value) : ShapeType::Box;
}

MStatus RigControlNode::initialize()
{
	MStatus status;
	MFnEnumAttribute eAttr;
	MFnNumericAttribute nAttr;
	MFnMessageAttribute msgAttr;
	MFnMatrixAttribute mAttr;
	MFnTypedAttribute tAttr;

	aShapeType = eAttr.create(RigControlNodeAttr::ShapeType, RigControlNodeAttr::ShapeType, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (std::size_t i = 0; i < RigControlNodeAttr::ShapeTypes::All.size(); ++i)
	{
		CHECK_MSTATUS_AND_RETURN_IT(eAttr.addField(RigControlNodeAttr::ShapeTypes::All[i], static_cast<short>(i)));
	}
	eAttr.setKeyable(false);
	eAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aShapeType));

	aWireColor = nAttr.createColor(RigControlNodeAttr::WireframeColor, RigControlNodeAttr::WireframeColor, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	nAttr.setDefault(1.0f, 0.0f, 0.0f);   // RGB only. Alpha is wireframeAlpha.
	nAttr.setKeyable(false);
	nAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aWireColor));

	aWireAlpha = nAttr.create(RigControlNodeAttr::WireframeAlpha, RigControlNodeAttr::WireframeAlpha, MFnNumericData::kFloat, 1.0f, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	nAttr.setMin(0.0f);
	nAttr.setMax(1.0f);
	nAttr.setKeyable(false);
	nAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aWireAlpha));

	aShapeTransform = mAttr.create(RigControlNodeAttr::ShapeTransform, RigControlNodeAttr::ShapeTransform, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setWritable(true);
	mAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aShapeTransform));

	aFilled = nAttr.create(RigControlNodeAttr::Filled, RigControlNodeAttr::Filled, MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	nAttr.setKeyable(false);
	nAttr.setWritable(true);
	nAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aFilled));

	// Rig attributes. Neither affects an output: the module reads the control, never these.
	aRigModule = msgAttr.create(RigControlNodeAttr::RigModule, RigControlNodeAttr::RigModule, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRigModule));

	aControlMarkup = tAttr.create(RigControlNodeAttr::ControlMarkup, RigControlNodeAttr::ControlMarkup, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	tAttr.setKeyable(false);
	tAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aControlMarkup));

	return MS::kSuccess;
}

MHWRender::MPxDrawOverride* RigControlDrawOverride::Creator(const MObject& obj)
{
	return new RigControlDrawOverride(obj);
}

// isAlwaysDirty is false so viewport tumbles don't trigger MPlug reads.
RigControlDrawOverride::RigControlDrawOverride(const MObject& obj)
	: MHWRender::MPxDrawOverride(obj, nullptr, false)
{
}

MBoundingBox RigControlDrawOverride::boundingBox(const MDagPath& objPath, const MDagPath& cameraPath) const
{
	const RigControlNode* pControl = dynamic_cast<const RigControlNode*>(MFnDependencyNode(objPath.node()).userNode());
	return pControl ? pControl->boundingBox() : MBoundingBox(MPoint(-1.0, -1.0, -1.0), MPoint(1.0, 1.0, 1.0));
}

// Reads plugs only when the shape changed, or when VP2 hands over no cached data.
MUserData* RigControlDrawOverride::prepareForDraw(
	const MDagPath& objPath,
	const MDagPath& cameraPath,
	const MHWRender::MFrameContext& frameContext,
	MUserData* oldData)
{
	ControlDrawData* data = dynamic_cast<ControlDrawData*>(oldData);
	const bool isNewData = data == nullptr;
	if (isNewData)
	{
		data = new ControlDrawData();
	}

	MObject oControl = objPath.node();
	RigControlNode* pControl = dynamic_cast<RigControlNode*>(MFnDependencyNode(oControl).userNode());

	// The dirty flag lives on the node, but the cache lives on this data. A new draw override (new panel, instance,
	// display mode change) starts with no data while the node is already clean, so new data always reads the plugs.
	if (pControl && (isNewData || pControl->isDrawDirty()))
	{
		data->shapeType = RigControlNode::getShapeType(oControl);

		const MMatrix mShapeTransform = RigControlNode::getShapeTransform(oControl);
		data->frame = RigControlNode::getShapeFrame(mShapeTransform);

		data->curvePoints.clear();
		if (const std::vector<ShapePoint>* pCurvePoints = getCurveShapePoints(data->shapeType))
		{
			data->curvePoints.setLength(static_cast<unsigned int>(pCurvePoints->size()));
			for (unsigned int i = 0; i < data->curvePoints.length(); ++i)
			{
				const ShapePoint& point = (*pCurvePoints)[i];
				data->curvePoints[i] = MPoint(point.x, point.y, point.z) * mShapeTransform;
			}
		}

		MPlug pColor(oControl, RigControlNode::aWireColor);
		const float r = pColor.child(0).asFloat();
		const float g = pColor.child(1).asFloat();
		const float b = pColor.child(2).asFloat();
		const float a = MPlug(oControl, RigControlNode::aWireAlpha).asFloat();
		data->dormantColor = MColor(r, g, b, a);

		data->filled = MPlug(oControl, RigControlNode::aFilled).asBool();

		pControl->setDrawClean();
	}

	// Selection color runs on every call, from the cached dormant color.
	const MHWRender::DisplayStatus displayStatus = MHWRender::MGeometryUtilities::displayStatus(objPath);

	// A control that is selected looks different from one that is only highlighted because a parent is selected.
	MSelectionList selList;
	MGlobal::getActiveSelectionList(selList);
	const bool isObjectSelected = selList.hasItem(objPath);
	const unsigned int selLen = selList.length();

	data->lineWidth = 1.0f;

	if (displayStatus == MHWRender::kLead && isObjectSelected && selLen != 1)
	{
		data->color = MColor(Colors::ActiveLead.r, Colors::ActiveLead.g, Colors::ActiveLead.b, data->dormantColor.a);
		data->lineWidth = 2.0f;
	}
	else if ((displayStatus == MHWRender::kActive || displayStatus == MHWRender::kLead) && isObjectSelected)
	{
		data->color = MColor(Colors::ActiveSelected.r, Colors::ActiveSelected.g, Colors::ActiveSelected.b, data->dormantColor.a);
		data->lineWidth = 2.0f;
	}
	else if ((displayStatus == MHWRender::kActiveAffected) || (!isObjectSelected && displayStatus != MHWRender::kDormant))
	{
		data->color = MColor(Colors::ActiveSelected.r, Colors::ActiveSelected.g, Colors::ActiveSelected.b, data->dormantColor.a * 0.75f);
	}
	else
	{
		data->color = data->dormantColor;
	}

	return data;
}

// Draws in the control's object space. Maya applies the control's world matrix.
// +X is the normal of flat shapes and the long axis of Capsule and Cylinder. Scale X sizes along that axis,
// scale Y and Z size across it.
void RigControlDrawOverride::addUIDrawables(
	const MDagPath& objPath,
	MHWRender::MUIDrawManager& drawManager,
	const MHWRender::MFrameContext& frameContext,
	const MUserData* data)
{
	const ControlDrawData* pData = dynamic_cast<const ControlDrawData*>(data);
	if (!pData)
	{
		return;
	}

	drawManager.beginDrawable();
	drawManager.setColor(pData->color);
	drawManager.setLineWidth(pData->lineWidth);
	drawManager.setDepthPriority(100);

	// Sizes: box and rect take half extents, sphere and circle a radius, capsule and cylinder a radius and a full
	// length. primitiveHalfExtents mirrors this for the bounds.
	const ShapeFrame& frame = pData->frame;
	switch (pData->shapeType)
	{
		case ShapeType::Box:
			drawManager.box(frame.center, frame.axisY, frame.axisX, frame.sizeX, frame.sizeY, frame.sizeZ, pData->filled);
			break;
		case ShapeType::Sphere:
			drawManager.sphere(frame.center, frame.sizeX, pData->filled);
			break;
		case ShapeType::Circle:
			drawManager.circle(frame.center, frame.axisX, frame.sizeY, pData->filled);
			break;
		case ShapeType::Capsule:
			drawManager.capsule(frame.center, frame.axisX, frame.sizeY, frame.sizeX, 6, 6, pData->filled);
			break;
		case ShapeType::Cylinder:
			drawManager.cylinder(frame.center, frame.axisX, frame.sizeY, frame.sizeX, 8, pData->filled);
			break;
		case ShapeType::Square:
			// rect's scaleX runs along up x normal, which is the shape's Z axis.
			drawManager.rect(frame.center, frame.axisY, frame.axisX, frame.sizeZ, frame.sizeY, pData->filled);
			break;
		default:
			// Curve shapes. filled does not apply to a line strip.
			drawManager.lineStrip(pData->curvePoints, false);
			break;
	}
	drawManager.endDrawable();
}

MObject RigControlNode::createRigControl(
	MDagModifier& dagMod,
	const MString& baseName,
	ShapeType shapeType,
	Side side,
	const ControlMarkup& markup)
{
	MObject oControl = dagMod.createNode(RigControlNode::id);

	// All transforms on rig controls get ignored and calculated by modules instead
	DagUtils::setInheritsTransform(oControl, false, dagMod);

	// Input joints can sit in another namespace when modules are built. The control belongs in the current (rig) namespace.
	MString ctrlName = MNamespace::stripNamespaceFromName(baseName);
	ctrlName.substitute(RigNaming::JointSuffix, "");
	ctrlName += RigNaming::ControlSuffix;
	ctrlName += getSideSuffix(side);
	dagMod.renameNode(oControl, ctrlName);

	// Color follows the module's side.
	MPlug pColor(oControl, RigControlNode::aWireColor);
	const MColor sideColor = getColorFromSide(side);
	dagMod.newPlugValueFloat(pColor.child(0), sideColor.r);
	dagMod.newPlugValueFloat(pColor.child(1), sideColor.g);
	dagMod.newPlugValueFloat(pColor.child(2), sideColor.b);

	MPlug pShapeType(oControl, aShapeType);
	dagMod.newPlugValueShort(pShapeType, static_cast<short>(shapeType));

	MPlug pControlMarkup(oControl, aControlMarkup);
	dagMod.newPlugValueString(pControlMarkup, serializeControlMarkup(markup).c_str());

	return oControl;
}
