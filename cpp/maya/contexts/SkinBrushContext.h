#pragma once

#include "maya/shared/SkinBrushStroke.h"

#include <string>
#include <vector>

#include <maya/MEvent.h>
#include <maya/MMessage.h>
#include <maya/MPoint.h>
#include <maya/MPxContext.h>
#include <maya/MPxContextCommand.h>
#include <maya/MPxToolCommand.h>
#include <maya/MSyntax.h>
#include <maya/MUIDrawManager.h>

// The skin weight brush (D65): drag over the selected skinned meshes to paint weights under the brush. What a dab does
// is a SkinBrushOperation, so this context serves every skin painting command: it picks the hit (the closest surface
// across the selected meshes, or every surface the ray crosses), reaches over the surface from it, draws the reach, and
// hands the stroke to an undoable tool command. Ctrl + drag sideways resizes the brush. Holding the button still
// repeats the dab, a timer ticking while the stroke is down.

// One stroke as a command: undoable, and scriptable by replaying its hits, which is how the stroke engine is tested.
//   cmds.skinBrushStroke(operation="smooth", radius=5.0, strength=0.5, hitPoint=[("plane_geo", 5, 0.0, 0.0, 0.0)])
// returns the number of vertices it changed. Each hitPoint is one dab, and each viewRay (origin and direction, with the meshes it may hit named by mesh and
// throughMesh to cross them all) is one dab at what it picks, after the hitPoints.
class SkinBrushStrokeCmd : public MPxToolCommand
{
public:
	static const char* aCommandString;
	static void* creator();
	static MSyntax newSyntax();

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;
	MStatus finalize() override;
	MStatus cancel() override;

	// What the context hands over when a stroke ends.
	void setStroke(SkinBrushEdit&& edit, const std::string& operation, double radius, double strength);

private:
	SkinBrushEdit fEdit;
	std::string fOperation;
	double fRadius = 0.0;
	double fStrength = 0.0;
};

class SkinBrushContext : public MPxContext
{
public:
	SkinBrushContext();
	~SkinBrushContext() override;

	void toolOnSetup(MEvent& event) override;
	void toolOffCleanup() override;
	MStatus doPress(MEvent& event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override;
	MStatus doDrag(MEvent& event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override;
	MStatus doRelease(MEvent& event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override;
	MStatus doPtrMoved(MEvent& event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override;
	MStatus drawFeedback(MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override;
	MStatus doEnterRegion(MEvent& event) override;
	MPxToolCommand* newToolCommand() override;

	const std::string& operation() const;
	double radius() const;
	double strength() const;
	bool throughMesh() const;
	// Each returns false (and keeps the old value) for one that is not valid.
	bool setOperation(const std::string& operation);
	bool setRadius(double radius);
	bool setStrength(double strength);
	void setThroughMesh(bool throughMesh);

private:
	// The selected skinned meshes. A selection of vertices or faces limits the brush to them. Rebuilds only what the
	// selection changed; the vertex positions are read again every time.
	void gather();
	bool pickAt(short x, short y);
	// isRepeat: a hold, which lands whatever the brush moved since the last dab.
	void dabAt(short x, short y, bool isRepeat);
	void endStroke();
	void startRepeat();
	void stopRepeat();
	static void repeatTick(float elapsedTime, float lastTime, void* clientData);
	void draw(MHWRender::MUIDrawManager& drawMgr);

	std::string fOperation = "smooth";
	double fRadius = 5.0;
	double fStrength = 0.5;
	bool fThroughMesh = false;

	SkinBrushStroke fStroke;
	std::string fSelectionKey;              // what the stroke was built from
	std::vector<BrushHit> fHits;            // under the pointer
	std::vector<BrushReach> fReaches;       // what those hits reach, drawn
	bool fIsStroking = false;
	bool fIsResizing = false;
	short fResizeFromX = 0;
	bool fHasLastDab = false;
	short fPointerX = 0;                    // where the pointer last was while stroking
	short fPointerY = 0;
	bool fMovedSinceTick = false;           // a drag landed since the last tick, so the brush is not being held
	MCallbackId fRepeatCallback = 0;
	MPoint fLastDabPoint;
};

// skinBrushCtx: creates the context and edits and queries its settings.
//   ctx = cmds.skinBrushCtx(); cmds.skinBrushCtx(ctx, edit=True, operation="smooth", radius=8.0, strength=0.4)
//   cmds.setToolTo(ctx)
class SkinBrushCtxCmd : public MPxContextCommand
{
public:
	static const char* aCommandString;
	static void* creator();

	MPxContext* makeObj() override;
	MStatus appendSyntax() override;
	MStatus doEditFlags() override;
	MStatus doQueryFlags() override;

private:
	SkinBrushContext* fContext = nullptr;
};
