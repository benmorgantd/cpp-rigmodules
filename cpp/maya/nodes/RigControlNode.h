#pragma once
#include "core/ControlMarkup.h"
#include "core/ControlShapes.h"
#include "maya/shared/Side.h"

#include <atomic>
#include <mutex>

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
#include <maya/MUintArray.h>
#include <maya/MUserData.h>

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

	// The node's draw generation these values were read at. 0 never matches a node, so new data always reads.
	unsigned int drawGeneration{ 0 };

	ShapeType shapeType{ ShapeType::Box };
	MColor color{ 1.0f, 0.0f, 0.0f, 1.0f };
	MColor dormantColor{ 1.0f, 0.0f, 0.0f, 1.0f };
	float lineWidth{ 1.0f };
	bool filled{ true };
	bool xray{ true };

	// The shape's library mesh, points through shapeTransform. Rebuilt only when the shape changes.
	MPointArray points;
	MUintArray wireIndices;       // Pairs, drawn as kLines
	MUintArray triangleIndices;   // Triples, drawn as kTriangles when filled
};

// Custom transform that draws its own control shape through a VP2 draw override.
class RigControlNode : public MPxTransform
{
public:
	RigControlNode();
	~RigControlNode() override;

	static void* creator();
	static MStatus initialize();

	// Bumps the draw generation only when a shape attribute changes, so a redraw doesn't trigger MPlug reads.
	MStatus setDependentsDirty(const MPlug& plugBeingDirtied, MPlugArray& affectedPlugs) override;

	// The shape's extent in object space, so framing and culling see the shapeTransform scale and offset.
	// Cached until shapeType or shapeTransform changes.
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
	static MObject aWireColorR;   // The color's channels, so setDependentsDirty can match a channel edit by attribute
	static MObject aWireColorG;
	static MObject aWireColorB;
	static MObject aWireAlpha;
	static MObject aShapeTransform;
	static MObject aFilled;
	static MObject aXray;

	// Changes whenever a shape attribute changes. Each draw override keeps the generation it last read in its
	// ControlDrawData, so every viewport refreshes, not only the first to draw after the change.
	unsigned int drawGeneration() const
	{
		return m_drawGeneration;
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

private:
	// Starts at 1, so it never equals a new ControlDrawData's 0. Skips 0 when it wraps.
	unsigned int m_drawGeneration{ 1 };

	// boundingBox() is const and may be called from evaluation threads, so the cache is guarded. Only
	// setDependentsDirty invalidates it, on the main thread, during an edit. This assumes the shape attributes
	// are set, not driven by an animated connection: parallel evaluation doesn't call setDependentsDirty.
	mutable std::mutex m_boundsMutex;
	mutable std::atomic<bool> m_boundsAreDirty{ true };
	mutable MBoundingBox m_bounds;
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
