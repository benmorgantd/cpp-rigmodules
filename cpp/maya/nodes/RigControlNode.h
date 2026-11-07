#pragma once
#include "core/ControlMarkup.h"
#include "core/ControlShapes.h"
#include "maya/shared/Side.h"

#include <maya/MBoundingBox.h>
#include <maya/MColor.h>
#include <maya/MDagModifier.h>
#include <maya/MFrameContext.h>
#include <maya/MMatrix.h>
#include <maya/MPoint.h>
#include <maya/MPointArray.h>
#include <maya/MPxDrawOverride.h>
#include <maya/MPxTransform.h>
#include <maya/MUIDrawManager.h>
#include <maya/MUserData.h>
#include <maya/MVector.h>

// shapeTransform split for the draw manager primitives: the origin, unit axes and axis lengths.
// Lengths stay positive under a mirrored (negative) scale. axisX is the normal of flat shapes and the long axis
// of Capsule and Cylinder.
struct ShapeFrame
{
	MPoint center;
	MVector axisX{ 1.0, 0.0, 0.0 };
	MVector axisY{ 0.0, 1.0, 0.0 };
	MVector axisZ{ 0.0, 0.0, 1.0 };
	double sizeX{ 1.0 };
	double sizeY{ 1.0 };
	double sizeZ{ 1.0 };
};

// Data payload passed to the Viewport 2.0 draw thread.
// prepareForDraw reads the plugs into it, addUIDrawables draws from it.
class ControlDrawData : public MUserData
{
public:
	ControlDrawData()
		: MUserData()
	{
	}
	~ControlDrawData() override = default;

	ShapeType shapeType{ ShapeType::Box };
	MColor color{ 1.0f, 0.0f, 0.0f, 1.0f };
	MColor dormantColor{ 1.0f, 0.0f, 0.0f, 1.0f };
	float lineWidth{ 1.0f };
	bool filled{ false };

	ShapeFrame frame;

	// Curve shapes only: the library points through the full shapeTransform. Rebuilt only when the shape changes.
	MPointArray curvePoints;
};

// Custom transform that draws its own control shape through a VP2 draw override.
class RigControlNode : public MPxTransform
{
public:
	RigControlNode();
	~RigControlNode() override;

	static void* creator();
	static MStatus initialize();

	// Flags the draw data dirty only when a shape attribute changes, so a redraw doesn't trigger MPlug reads.
	MStatus setDependentsDirty(const MPlug& plugBeingDirtied, MPlugArray& affectedPlugs) override;

	// The shape's extent in object space, so framing and culling see the shapeTransform scale and offset.
	bool isBounded() const override;
	MBoundingBox boundingBox() const override;

	static MTypeId id;
	static MString drawDbClassification;
	static MString drawRegistrantId;

	// Rig related attributes
	static MObject aRigModule;       // Connected from its module's controlMessage element
	static MObject aControlMarkup;   // JSON (side, usage), see ControlMarkup.h

	// Shape attributes
	static MObject aShapeType;
	static MObject aWireColor;
	static MObject aWireAlpha;
	static MObject aShapeTransform;
	static MObject aFilled;

	bool isDrawDirty() const
	{
		return m_drawIsDirty;
	}
	void setDrawClean()
	{
		m_drawIsDirty = false;
	}

	// Creates the control, names it from baseName and side, colors it by side, and writes its markup.
	// The caller wires it to its module.
	static MObject createRigControl(
		MDagModifier& dagMod,
		const MString& baseName,
		ShapeType shapeType,
		Side side,
		const ControlMarkup& markup
	);

	static MMatrix getShapeTransform(const MObject& oControl);
	static ShapeType getShapeType(const MObject& oControl);
	static ShapeFrame getShapeFrame(const MMatrix& mShapeTransform);

private:
	bool m_drawIsDirty{ true };
};

// Viewport 2.0 Draw Override
class RigControlDrawOverride : public MHWRender::MPxDrawOverride
{
public:
	static MHWRender::MPxDrawOverride* Creator(const MObject& obj);

	RigControlDrawOverride(const MObject& obj);
	~RigControlDrawOverride() override = default;

	MHWRender::DrawAPI supportedDrawAPIs() const override
	{
		return (MHWRender::kOpenGL | MHWRender::kDirectX11 | MHWRender::kOpenGLCoreProfile);
	}

	bool isBounded(const MDagPath& objPath, const MDagPath& cameraPath) const override
	{
		return true;
	}

	MBoundingBox boundingBox(const MDagPath& objPath, const MDagPath& cameraPath) const override;

	// Tell VP2 to place this drawable in the transparent render queue, so wireframeAlpha works.
	bool isTransparent() const override
	{
		return true;
	}

	bool hasUIDrawables() const override
	{
		return true;
	}

	MUserData* prepareForDraw(
		const MDagPath& objPath,
		const MDagPath& cameraPath,
		const MHWRender::MFrameContext& frameContext,
		MUserData* oldData
	) override;

	void addUIDrawables(
		const MDagPath& objPath,
		MHWRender::MUIDrawManager& drawManager,
		const MHWRender::MFrameContext& frameContext,
		const MUserData* data
	) override;
};
