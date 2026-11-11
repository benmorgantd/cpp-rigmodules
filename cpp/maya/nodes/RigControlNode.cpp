#include "maya/nodes/RigControlNode.h"
#include "core/Constants.h"
#include "maya/shared/Colors.h"
#include "maya/shared/Utilities.h"

#include <string>

#include <maya/MDagModifier.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MHWGeometryUtilities.h>
#include <maya/MNamespace.h>
#include <maya/MPlug.h>
#include <maya/MProfiler.h>

MTypeId RigControlNode::id(0x0013B5C0);
MString RigControlNode::drawDbClassification("drawdb/geometry/rigControlNode");
MString RigControlNode::drawRegistrantId("RigControlNodePlugin");

MObject RigControlNode::aShapeType;
MObject RigControlNode::aWireColor;
MObject RigControlNode::aWireColorR;
MObject RigControlNode::aWireColorG;
MObject RigControlNode::aWireColorB;
MObject RigControlNode::aWireAlpha;
MObject RigControlNode::aShapeTransform;
MObject RigControlNode::aFilled;
MObject RigControlNode::aXray;

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

namespace
{
	// Profiler event names, in ProfilerUtils' category.
	constexpr const char* ProfileSetDependentsDirty{ "rigControl setDependentsDirty" };
	constexpr const char* ProfileBoundingBox{ "rigControl boundingBox" };
	constexpr const char* ProfileBoundingBoxRebuild{ "rigControl boundingBox rebuild" };
	constexpr const char* ProfileDrawOverrideBoundingBox{ "rigControl drawOverride boundingBox" };
	constexpr const char* ProfilePrepareForDraw{ "rigControl prepareForDraw" };
	constexpr const char* ProfilePrepareForDrawRead{ "rigControl prepareForDraw read" };
	constexpr const char* ProfileAddUIDrawables{ "rigControl addUIDrawables" };
}

MStatus RigControlNode::setDependentsDirty(const MPlug& plugBeingDirtied, MPlugArray& affectedPlugs)
{
	MProfilingScope profile(ProfilerUtils::category(), MProfiler::kColorG_L1, ProfileSetDependentsDirty);

	// Compared by attribute only: this runs for every dirtied plug, including the matrices on every edit.
	// wireframeColor is a compound, so a change to one channel arrives on a child plug.
	const MObject attr = plugBeingDirtied.attribute();
	const bool isBoundsAttr = attr == aShapeType || attr == aShapeTransform;
	const bool isColorAttr = attr == aWireColor || attr == aWireColorR || attr == aWireColorG || attr == aWireColorB;

	if (isBoundsAttr || isColorAttr || attr == aWireAlpha || attr == aFilled || attr == aXray)
	{
		if (isBoundsAttr)
		{
			m_boundsAreDirty.store(true);
		}
		++m_drawGeneration;
		if (m_drawGeneration == 0)
		{
			m_drawGeneration = 1;
		}
		// Redraw while the attribute is being changed, not only on the next unrelated refresh.
		MHWRender::MRenderer::setGeometryDrawDirty(thisMObject());
	}

	return MPxTransform::setDependentsDirty(plugBeingDirtied, affectedPlugs);
}

bool RigControlNode::isBounded() const
{
	return true;
}

// Exact: every library point through shapeTransform. Rebuilt only after a shape change.
MBoundingBox RigControlNode::boundingBox() const
{
	MProfilingScope profile(ProfilerUtils::category(), MProfiler::kColorG_L2, ProfileBoundingBox);

	if (m_boundsAreDirty.load())
	{
		std::lock_guard<std::mutex> lock(m_boundsMutex);
		// Another thread may have rebuilt it while this one waited.
		if (m_boundsAreDirty.load())
		{
			MProfilingScope rebuildProfile(ProfilerUtils::category(), MProfiler::kColorG_L3, ProfileBoundingBoxRebuild);
			const MObject oControl = thisMObject();
			const ShapeMesh& mesh = getShapeMesh(getShapeType(oControl));
			const MMatrix mShapeTransform = getShapeTransform(oControl);

			MBoundingBox bounds;
			for (const ShapePoint& point : mesh.points)
			{
				bounds.expand(MPoint(point.x, point.y, point.z) * mShapeTransform);
			}
			m_bounds = bounds;
			m_boundsAreDirty.store(false);
		}
		return m_bounds;
	}
	return m_bounds;
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
	MFnCompoundAttribute fnWireColor(aWireColor, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	aWireColorR = fnWireColor.child(0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	aWireColorG = fnWireColor.child(1, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	aWireColorB = fnWireColor.child(2, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

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

	aFilled = nAttr.create(RigControlNodeAttr::Filled, RigControlNodeAttr::Filled, MFnNumericData::kBoolean, true, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	nAttr.setKeyable(false);
	nAttr.setWritable(true);
	nAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aFilled));

	aXray = nAttr.create(RigControlNodeAttr::Xray, RigControlNodeAttr::Xray, MFnNumericData::kBoolean, true, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	nAttr.setKeyable(false);
	nAttr.setWritable(true);
	nAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aXray));

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
	MProfilingScope profile(ProfilerUtils::category(), MProfiler::kColorG_L1, ProfileDrawOverrideBoundingBox);
	const RigControlNode* pControl = dynamic_cast<const RigControlNode*>(MFnDependencyNode(objPath.node()).userNode());
	return pControl ? pControl->boundingBox() : MBoundingBox(MPoint(-1.0, -1.0, -1.0), MPoint(1.0, 1.0, 1.0));
}

// Reads plugs only when this data is older than the node's draw generation. New data (a new panel, instance or
// display mode) starts at generation 0, so it always reads.
MUserData* RigControlDrawOverride::prepareForDraw(
	const MDagPath& objPath,
	const MDagPath& cameraPath,
	const MHWRender::MFrameContext& frameContext,
	MUserData* oldData)
{
	MObject oControl = objPath.node();
	MProfilingScope profile(ProfilerUtils::category(), MProfiler::kColorC_L1, ProfilePrepareForDraw, nullptr, oControl);

	ControlDrawData* data = dynamic_cast<ControlDrawData*>(oldData);
	if (data == nullptr)
	{
		data = new ControlDrawData();
	}

	RigControlNode* pControl = dynamic_cast<RigControlNode*>(MFnDependencyNode(oControl).userNode());

	if (pControl && data->drawGeneration != pControl->drawGeneration())
	{
		MProfilingScope readProfile(ProfilerUtils::category(), MProfiler::kColorC_L2, ProfilePrepareForDrawRead, nullptr, oControl);
		data->shapeType = RigControlNode::getShapeType(oControl);

		const MMatrix mShapeTransform = RigControlNode::getShapeTransform(oControl);
		const ShapeMesh& mesh = getShapeMesh(data->shapeType);
		data->points.setLength(static_cast<unsigned int>(mesh.points.size()));
		for (unsigned int i = 0; i < data->points.length(); ++i)
		{
			const ShapePoint& point = mesh.points[i];
			data->points[i] = MPoint(point.x, point.y, point.z) * mShapeTransform;
		}
		data->wireIndices = MUintArray(mesh.wireIndices.data(), static_cast<unsigned int>(mesh.wireIndices.size()));
		data->triangleIndices = MUintArray(mesh.triangleIndices.data(), static_cast<unsigned int>(mesh.triangleIndices.size()));

		MPlug pColor(oControl, RigControlNode::aWireColor);
		const float r = pColor.child(0).asFloat();
		const float g = pColor.child(1).asFloat();
		const float b = pColor.child(2).asFloat();
		const float a = MPlug(oControl, RigControlNode::aWireAlpha).asFloat();
		data->dormantColor = MColor(r, g, b, a);

		data->filled = MPlug(oControl, RigControlNode::aFilled).asBool();
		data->xray = MPlug(oControl, RigControlNode::aXray).asBool();

		data->drawGeneration = pControl->drawGeneration();
	}

	// Selection color runs on every call, from the cached dormant color.
	const MHWRender::DisplayStatus displayStatus = MHWRender::MGeometryUtilities::displayStatus(objPath);

	// A control that is selected looks different from one that is only highlighted because a parent is selected.
	const bool isObjectSelected = ActiveSelection::contains(oControl);
	const unsigned int selLen = ActiveSelection::length();

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
// The fill goes first at 0.75 of the wire's alpha, with the wire on top. With xray the whole drawable draws in
// front of meshes and other geometry.
void RigControlDrawOverride::addUIDrawables(
	const MDagPath& objPath,
	MHWRender::MUIDrawManager& drawManager,
	const MHWRender::MFrameContext& frameContext,
	const MUserData* data)
{
	MProfilingScope profile(ProfilerUtils::category(), MProfiler::kColorC_L3, ProfileAddUIDrawables);

	const ControlDrawData* pData = dynamic_cast<const ControlDrawData*>(data);
	if (!pData || pData->points.length() == 0)
	{
		return;
	}

	drawManager.beginDrawable();
	if (pData->xray)
	{
		drawManager.beginDrawInXray();
	}
	drawManager.setDepthPriority(100);

	if (pData->filled && pData->triangleIndices.length() > 0)
	{
		MColor fillColor = pData->color;
		fillColor.a *= 0.75f;
		drawManager.setColor(fillColor);
		drawManager.mesh(MHWRender::MUIDrawManager::kTriangles, pData->points, nullptr, nullptr, &pData->triangleIndices);
	}

	drawManager.setColor(pData->color);
	drawManager.setLineWidth(pData->lineWidth);
	drawManager.mesh(MHWRender::MUIDrawManager::kLines, pData->points, nullptr, nullptr, &pData->wireIndices);
	if (pData->xray)
	{
		drawManager.endDrawInXray();
	}
	drawManager.endDrawable();
}

MObject RigControlNode::createRigControl(
	MDagModifier& dagMod,
	const MString& baseName,
	ShapeType shapeType,
	Side side,
	const ControlMarkup& markup,
	MEulerRotation::RotationOrder rotateOrder)
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

	MPlug pRotateOrder(oControl, MPxTransform::rotateOrder);
	dagMod.newPlugValueShort(pRotateOrder, static_cast<short>(rotateOrder));

	return oControl;
}
