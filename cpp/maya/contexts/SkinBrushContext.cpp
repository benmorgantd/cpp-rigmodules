#include "maya/contexts/SkinBrushContext.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>

#include <maya/M3dView.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MArgParser.h>
#include <maya/MColor.h>
#include <maya/MColorArray.h>
#include <maya/MDagPath.h>
#include <maya/MFnCamera.h>
#include <maya/MFrameContext.h>
#include <maya/MGlobal.h>
#include <maya/MHWGeometry.h>
#include <maya/MPointArray.h>
#include <maya/MSelectionList.h>
#include <maya/MTimerMessage.h>

namespace
{
	// Code reads flags by long name only. Short names appear only where a syntax is built (D14).
	constexpr const char* kOperationFlag{ "-operation" };
	constexpr const char* kRadiusFlag{ "-radius" };
	constexpr const char* kStrengthFlag{ "-strength" };
	constexpr const char* kThroughMeshFlag{ "-throughMesh" };
	constexpr const char* kFalloffFlag{ "-falloff" };
	constexpr const char* kDebugFlag{ "-debug" };
	// Not "-hit" or "-ray": Maya rejects a three-letter long name, and the flag silently never registers.
	constexpr const char* kHitFlag{ "-hitPoint" };
	constexpr const char* kMeshFlag{ "-mesh" };
	constexpr const char* kRayFlag{ "-viewRay" };

	// The brush may not shrink to nothing under the resize drag.
	constexpr double kMinimumRadius = 1.0e-3;
	// Each pixel of a Ctrl + drag grows or shrinks the radius by this factor.
	constexpr double kResizePerPixel = 1.01;
	// A dab lands only after the brush moved this far (in radii) from the last one, so tiny moves don't pile up.
	constexpr double kDabSpacing = 0.25;
	// The default radius, as a fraction of the diagonal of the box around the selected meshes: 0.05 is a hand's width on
	// a character, and it scales with whatever mesh is selected.
	constexpr double kAutoRadiusFraction = 0.05;
	// Dabs along a drag are never closer than this on screen, and one event lands at most this many.
	constexpr double kMinimumStepPixels = 2.0;
	constexpr int kMaxDabsPerEvent = 24;
		// One drag event spends at most this long dabbing before it skips ahead to the pointer.
		constexpr std::chrono::milliseconds kEventBudget{ 40 };
	// While the button is held and no drag lands a dab, one repeats this often (seconds). Maya skins the mesh after each
	// one, which sets the real rate when that takes longer.
	constexpr float kRepeatSeconds = 0.05f;   // 20 a second

	bool isValidRadius(double radius)
	{
		return radius >= kMinimumRadius;
	}

	bool isValidStrength(double strength)
	{
		return strength > 0.0 && strength <= 1.0;
	}
}

// skinBrushStroke --------------------------------------------------------------------------------
const char* SkinBrushStrokeCmd::aCommandString = "skinBrushStroke";

void* SkinBrushStrokeCmd::creator()
{
	return new SkinBrushStrokeCmd();
}

MSyntax SkinBrushStrokeCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-op", kOperationFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-rd", kRadiusFlag, MSyntax::kDouble));
	CHECK_MSTATUS(syntax.addFlag("-st", kStrengthFlag, MSyntax::kDouble));
	CHECK_MSTATUS(syntax.addFlag("-hp", kHitFlag, MSyntax::kString, MSyntax::kLong, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kHitFlag));
	CHECK_MSTATUS(syntax.addFlag("-ms", kMeshFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kMeshFlag));
	CHECK_MSTATUS(syntax.addFlag("-vr", kRayFlag, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kRayFlag));
	CHECK_MSTATUS(syntax.addFlag("-tm", kThroughMeshFlag, MSyntax::kBoolean));
	CHECK_MSTATUS(syntax.addFlag("-fo", kFalloffFlag, MSyntax::kString));
	syntax.enableQuery(false);
	syntax.enableEdit(false);
	return syntax;
}

bool SkinBrushStrokeCmd::isUndoable() const
{
	return true;
}

MStatus SkinBrushStrokeCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	fOperation = "smooth";
	fRadius = 5.0;
	fStrength = 0.5;
	if (argData.isFlagSet(kOperationFlag))
	{
		MString operation;
		argData.getFlagArgument(kOperationFlag, 0, operation);
		fOperation = operation.asChar();
	}
	if (argData.isFlagSet(kRadiusFlag))
	{
		argData.getFlagArgument(kRadiusFlag, 0, fRadius);
	}
	if (argData.isFlagSet(kStrengthFlag))
	{
		argData.getFlagArgument(kStrengthFlag, 0, fStrength);
	}
	fFalloff = FalloffMode::Surface;
	if (argData.isFlagSet(kFalloffFlag))
	{
		MString falloff;
		argData.getFlagArgument(kFalloffFlag, 0, falloff);
		if (!parseFalloff(falloff.asChar(), fFalloff))
		{
			MGlobal::displayError(falloff + " is not a falloff: surface or volume.");
			return MStatus::kInvalidParameter;
		}
	}
	if (!isValidRadius(fRadius) || !isValidStrength(fStrength))
	{
		MGlobal::displayError(MString(aCommandString) + " needs -radius of at least 0.001 and -strength above 0 and at most 1.");
		return MStatus::kInvalidParameter;
	}

	// Each hit names its mesh, so the meshes are the distinct names, in the order they first appear.
	struct NamedHit
	{
		std::string mesh;
		int face;
		MPoint point;
	};
	std::vector<NamedHit> hits;
	const unsigned int hitCount = argData.numberOfFlagUses(kHitFlag);
	for (unsigned int i = 0; i < hitCount; ++i)
	{
		MArgList hitArgs;
		status = argData.getFlagArgumentList(kHitFlag, i, hitArgs);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		hits.push_back({ hitArgs.asString(0).asChar(), hitArgs.asInt(1), MPoint(hitArgs.asDouble(2), hitArgs.asDouble(3), hitArgs.asDouble(4)) });
	}
	const unsigned int rayCount = argData.numberOfFlagUses(kRayFlag);
	if (hits.empty() && rayCount == 0)
	{
		MGlobal::displayError(MString(aCommandString) + " needs at least one -hitPoint (mesh, face and the point on it) or -viewRay (origin and direction).");
		return MStatus::kFailure;
	}

	// The meshes a ray may hit are the -mesh names, then the distinct hit names, in the order they first appear.
	std::vector<std::string> names;
	const auto addMesh = [&names](const std::string& name)
	{
		if (std::find(names.begin(), names.end(), name) == names.end())
		{
			names.push_back(name);
		}
	};
	for (unsigned int i = 0; i < argData.numberOfFlagUses(kMeshFlag); ++i)
	{
		MArgList meshArgs;
		status = argData.getFlagArgumentList(kMeshFlag, i, meshArgs);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		addMesh(meshArgs.asString(0).asChar());
	}
	for (const NamedHit& hit : hits)
	{
		addMesh(hit.mesh);
	}
	MDagPathArray meshes;
	for (const std::string& name : names)
	{
		MDagPath mesh;
		status = DagUtils::getDagPathFromName(name.c_str(), mesh, DagUtils::NodeKind::kMesh);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		meshes.append(mesh);
	}

	bool throughMesh = false;
	if (argData.isFlagSet(kThroughMeshFlag))
	{
		argData.getFlagArgument(kThroughMeshFlag, 0, throughMesh);
	}

	SkinBrushStroke stroke;
	status = stroke.begin(meshes, fOperation);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (const NamedHit& hit : hits)
	{
		BrushHit brushHit;
		brushHit.mesh = static_cast<unsigned int>(std::find(names.begin(), names.end(), hit.mesh) - names.begin());
		brushHit.face = hit.face;
		brushHit.point = hit.point;
		status = stroke.dab({ brushHit }, fRadius, fStrength, fFalloff);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	for (unsigned int i = 0; i < rayCount; ++i)
	{
		MArgList rayArgs;
		status = argData.getFlagArgumentList(kRayFlag, i, rayArgs);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		std::vector<BrushHit> picked;
		stroke.pick(MPoint(rayArgs.asDouble(0), rayArgs.asDouble(1), rayArgs.asDouble(2)), MVector(rayArgs.asDouble(3), rayArgs.asDouble(4), rayArgs.asDouble(5)), throughMesh, picked);
		if (picked.empty())
		{
			continue;
		}
		status = stroke.dab(picked, fRadius, fStrength, fFalloff);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	status = stroke.finish(fEdit);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	setResult(static_cast<int>(fEdit.vertexCount()));
	return MStatus::kSuccess;
}

MStatus SkinBrushStrokeCmd::redoIt()
{
	return fEdit.apply();
}

MStatus SkinBrushStrokeCmd::undoIt()
{
	return fEdit.undo();
}

void SkinBrushStrokeCmd::setStroke(SkinBrushEdit&& edit, const std::string& operation, double radius, double strength, FalloffMode falloff)
{
	fFalloff = falloff;
	fEdit = std::move(edit);
	fOperation = operation;
	fRadius = radius;
	fStrength = strength;
}

MStatus SkinBrushStrokeCmd::finalize()
{
	// The line the script editor and the journal show for the stroke: the same command, with every dab as a hit.
	MArgList command;
	command.addArg(MString(aCommandString));
	command.addArg(MString(kOperationFlag));
	command.addArg(MString(fOperation.c_str()));
	command.addArg(MString(kRadiusFlag));
	command.addArg(fRadius);
	command.addArg(MString(kStrengthFlag));
	command.addArg(fStrength);
	command.addArg(MString(kFalloffFlag));
	command.addArg(MString(falloffName(fFalloff)));
	for (const BrushDab& dab : fEdit.dabs)
	{
		command.addArg(MString(kHitFlag));
		command.addArg(MString(dab.mesh.c_str()));
		command.addArg(dab.face);
		command.addArg(dab.x);
		command.addArg(dab.y);
		command.addArg(dab.z);
	}
	return doFinalize(command);
}

MStatus SkinBrushStrokeCmd::cancel()
{
	// The stroke was written as it went, so a cancelled one is put back.
	return fEdit.undo();
}

// SkinBrushContext -------------------------------------------------------------------------------
SkinBrushContext::SkinBrushContext()
{
	setTitleString("Skin Brush");
}

const std::string& SkinBrushContext::operation() const
{
	return fOperation;
}

double SkinBrushContext::radius() const
{
	return fRadius;
}

double SkinBrushContext::strength() const
{
	return fStrength;
}

bool SkinBrushContext::throughMesh() const
{
	return fThroughMesh;
}

bool SkinBrushContext::debug() const
{
	return fDebug;
}

void SkinBrushContext::setDebug(bool debug)
{
	fDebug = debug;
}

void SkinBrushContext::debugLog(const MString& message) const
{
	if (fDebug)
	{
		MGlobal::displayInfo(MString("[skinBrush] ") + message);
	}
}

FalloffMode SkinBrushContext::falloff() const
{
	return fFalloff;
}

bool SkinBrushContext::setFalloff(const std::string& name)
{
	return parseFalloff(name, fFalloff);
}

bool SkinBrushContext::setOperation(const std::string& operation)
{
	const std::vector<std::string>& names = brushOperationNames();
	if (std::find(names.begin(), names.end(), operation) == names.end())
	{
		return false;
	}
	fOperation = operation;
	fSelectionKey.clear();
	return true;
}

bool SkinBrushContext::setRadius(double radius)
{
	if (!isValidRadius(radius))
	{
		return false;
	}
	fRadius = radius;
	fRadiusIsAuto = false;
	return true;
}

bool SkinBrushContext::setStrength(double strength)
{
	if (!isValidStrength(strength))
	{
		return false;
	}
	fStrength = strength;
	return true;
}

void SkinBrushContext::setThroughMesh(bool throughMesh)
{
	fThroughMesh = throughMesh;
}

void SkinBrushContext::gather()
{
	MSelectionList selection;
	MGlobal::getActiveSelectionList(selection);
	std::vector<MeshVertexTarget> targets;
	MeshUtils::getVertexTargets(selection, targets);

	MDagPathArray meshes;
	std::vector<const MeshVertexTarget*> kept;
	std::string key = fOperation;
	for (const MeshVertexTarget& target : targets)
	{
		if (!SkinWeightRows::isSkinned(target.mesh))
		{
			continue;
		}
		meshes.append(target.mesh);
		kept.push_back(&target);
		key += "|";
		key += target.mesh.fullPathName().asChar();
		if (!target.allVertices)
		{
			std::size_t hash = target.vertices.size();
			for (const unsigned int v : target.vertices)
			{
				hash = hash * 1099511628211ull ^ v;
			}
			key += ":" + std::to_string(hash);
		}
	}

	if (key == fSelectionKey)
	{
		fStroke.refresh();
		applyAutoRadius();
		return;
	}
	if (!fStroke.begin(meshes, fOperation))
	{
		fSelectionKey.clear();
		return;
	}
	fSelectionKey = key;
	for (unsigned int i = 0; i < kept.size(); ++i)
	{
		if (!kept[i]->allVertices)
		{
			fStroke.restrict(i, kept[i]->vertices);
		}
	}
	applyAutoRadius();
	if (kept.empty())
	{
		MGlobal::displayWarning("Skin brush: select one or more skinned meshes.");
	}
}

void SkinBrushContext::applyAutoRadius()
{
	const double diagonal = fStroke.boundsDiagonal();
	if (fRadiusIsAuto && diagonal > 0.0)
	{
		fRadius = std::max(kMinimumRadius, kAutoRadiusFraction * diagonal);
	}
}

void SkinBrushContext::requestRedraw()
{
	M3dView::active3dView().scheduleRefresh();
}

void SkinBrushContext::toolOnSetup(MEvent& event)
{
	setHelpString("Drag over the selected skinned meshes to paint. Ctrl + drag sideways resizes the brush.");
	gather();
}

void SkinBrushContext::toolOffCleanup()
{
	// Drops the meshes, their neighbor maps and positions.
	fStroke.begin(MDagPathArray(), fOperation);
	fSelectionKey.clear();
	fHits.clear();
	fReaches.clear();
	stopRepeat();
	fIsStroking = false;
	fIsResizing = false;
	MPxContext::toolOffCleanup();
}

MStatus SkinBrushContext::doEnterRegion(MEvent& event)
{
	gather();
	return MStatus::kSuccess;
}

MPxToolCommand* SkinBrushContext::newToolCommand()
{
	return new SkinBrushStrokeCmd();
}

SkinBrushContext::~SkinBrushContext()
{
	stopRepeat();
}

void SkinBrushContext::startRepeat()
{
	if (fRepeatCallback == 0)
	{
		MStatus status;
		fRepeatCallback = MTimerMessage::addTimerCallback(kRepeatSeconds, repeatTick, this, &status);
		debugLog(MString("timer added, status ok = ") + (status ? "yes" : "NO") + ", period " + kRepeatSeconds + " s");
		if (!status)
		{
			fRepeatCallback = 0;
		}
	}
	fTicks = 0;
	fDragEvents = 0;
	fDragEventsAtLastTick = 0;
	fDabs = 0;
	fDabSinceTick = true;   // the press dabbed, so the first repeat is a tick later
}

void SkinBrushContext::stopRepeat()
{
	if (fRepeatCallback != 0)
	{
		MMessage::removeCallback(fRepeatCallback);
		fRepeatCallback = 0;
	}
}

// Runs on Maya's main thread, so it is safe to touch the scene. A dab that landed since the last tick means
// the brush is moving, so it is not repeated. Otherwise it repeats, however the pointer jitters: a held mouse or
// trackpad sends small drag events that land no dab, and those must not stop the repeat.
void SkinBrushContext::repeatTick(float elapsedTime, float lastTime, void* clientData)
{
	SkinBrushContext* context = static_cast<SkinBrushContext*>(clientData);
	++context->fTicks;
	const int dragsSinceTick = context->fDragEvents - context->fDragEventsAtLastTick;
	context->fDragEventsAtLastTick = context->fDragEvents;
	context->debugLog(MString("tick ") + context->fTicks + ": stroking " + (context->fIsStroking ? "yes" : "no") + ", resizing " + (context->fIsResizing ? "yes" : "no") +
		", dab since last tick " + (context->fDabSinceTick ? "yes" : "no") + ", drag events since last tick " + dragsSinceTick +
		", pointer " + context->fPointerX + "," + context->fPointerY);
	if (!context->fIsStroking || context->fIsResizing)
	{
		return;
	}
	if (context->fDabSinceTick)
	{
		context->fDabSinceTick = false;
		return;
	}
	const int dabsBefore = context->fDabs;
	context->dabAt(context->fPointerX, context->fPointerY, true);
	context->debugLog(MString("  repeat dab: ") + (context->fDabs > dabsBefore ? "landed" : "MISSED (no hit under the pointer, or the dab failed)") +
		", hits under pointer " + static_cast<int>(context->fHits.size()));
	context->fDabSinceTick = false;   // a repeat is not a drag's dab: the next tick repeats again
	if (context->fDabs > dabsBefore)
	{
		M3dView::active3dView().refresh(false, true);   // show the repeated dab now
	}
	else
	{
		context->requestRedraw();
	}
}

bool SkinBrushContext::pickAt(short x, short y)
{
	M3dView view = M3dView::active3dView();
	MPoint origin;
	MVector direction;
	fHits.clear();
	fReaches.clear();
	if (!view.viewToWorld(x, y, origin, direction))
	{
		return false;
	}
	fStroke.pick(origin, direction, fThroughMesh, fHits);
	fStroke.reach(fHits, fRadius, fFalloff, fReaches);
	return !fHits.empty();
}

bool SkinBrushContext::dabOnce(short x, short y)
{
	if (!pickAt(x, y))
	{
		return false;
	}
	const BrushHit& hit = fHits.front();
	M3dView view = M3dView::active3dView();
	MDagPath camera;
	short x1 = 0;
	short y1 = 0;
	short x2 = 0;
	short y2 = 0;
	if (view.getCamera(camera) && view.worldToView(hit.point, x1, y1))
	{
		MFnCamera fnCamera(camera);
		const MPoint edge = hit.point + fnCamera.rightDirection(MSpace::kWorld) * fRadius;
		if (view.worldToView(edge, x2, y2))
		{
			fPixelRadius = std::hypot(static_cast<double>(x2 - x1), static_cast<double>(y2 - y1));
		}
	}
	if (!fStroke.dab(fHits, fRadius, fStrength, fFalloff))
	{
		return false;
	}
	fHasLastDab = true;
	fDabSinceTick = true;
	++fDabs;
	debugLog(MString("  dab at ") + x + "," + y + ": radius " + fRadius + " (" + fPixelRadius + " px), strength " + fStrength + ", reached " +
		fStroke.lastReachedCount() + " vertices, changed " + fStroke.lastChangedCount());
	fLastDabX = x;
	fLastDabY = y;
	return true;
}

void SkinBrushContext::dabAt(short x, short y, bool isRepeat)
{
	if (isRepeat || !fHasLastDab)
	{
		dabOnce(x, y);
		return;
	}
	const double step = std::max(kMinimumStepPixels, kDabSpacing * fPixelRadius);
	const double startX = fLastDabX;
	const double startY = fLastDabY;
	const double dx = x - startX;
	const double dy = y - startY;
	const double distance = std::hypot(dx, dy);
	if (distance < step)
	{
		return;
	}
	const int count = std::min(static_cast<int>(distance / step), kMaxDabsPerEvent);
	const auto begin = std::chrono::steady_clock::now();
	for (int i = 1; i <= count; ++i)
	{
		const double t = i * step / distance;
		dabOnce(static_cast<short>(std::lround(startX + dx * t)), static_cast<short>(std::lround(startY + dy * t)));
		// Each dab costs a skin evaluation. When the budget is spent the brush skips to the pointer instead of catching
		// up, which leaves a gap on a fast drag but keeps the stroke under the pointer, as Maya's own brushes do.
		if (std::chrono::steady_clock::now() - begin > kEventBudget)
		{
			if (i < count)
			{
				fLastDabX = x;
				fLastDabY = y;
			}
			break;
		}
	}
}

void SkinBrushContext::endStroke()
{
	stopRepeat();
	fIsStroking = false;
	fHasLastDab = false;
	SkinBrushEdit edit;
	if (!fStroke.finish(edit) || edit.empty())
	{
		return;
	}
	// Maya owns the command from here: it sits on the undo queue.
	SkinBrushStrokeCmd* command = static_cast<SkinBrushStrokeCmd*>(newToolCommand());
	command->setStroke(std::move(edit), fOperation, fRadius, fStrength, fFalloff);
	command->finalize();
}

MStatus SkinBrushContext::doPress(MEvent& event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context)
{
	debugLog(MString("press: button ") + static_cast<int>(event.mouseButton()) + ", ctrl " + (event.isModifierControl() ? "yes" : "no") + ", meshes " + fStroke.meshCount());
	// Not the middle button, which pans. A trackpad click may not report as the left button, so anything else paints.
	if (event.mouseButton() == MEvent::kMiddleMouse)
	{
		return MStatus::kSuccess;
	}
	if (event.isModifierControl())
	{
		short y = 0;
		event.getPosition(fResizeFromX, y);
		fIsResizing = true;
		return MStatus::kSuccess;
	}
	fStroke.refresh();
	fIsStroking = true;
	fHasLastDab = false;
	event.getPosition(fPointerX, fPointerY);
	dabAt(fPointerX, fPointerY, false);
	debugLog(MString("press dab at ") + fPointerX + "," + fPointerY + ": hits " + static_cast<int>(fHits.size()) + ", dabs " + fDabs);
	startRepeat();
	requestRedraw();
	return MStatus::kSuccess;
}

MStatus SkinBrushContext::doDrag(MEvent& event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context)
{
	if (fIsResizing)
	{
		short x = 0;
		short y = 0;
		event.getPosition(x, y);
		fRadius = std::max(kMinimumRadius, fRadius * std::pow(kResizePerPixel, static_cast<double>(x - fResizeFromX)));
		fRadiusIsAuto = false;
		fResizeFromX = x;
		fStroke.reach(fHits, fRadius, fFalloff, fReaches);
	}
	else if (fIsStroking)
	{
		event.getPosition(fPointerX, fPointerY);
		++fDragEvents;
		const int dabsBefore = fDabs;
		dabAt(fPointerX, fPointerY, false);
		// The ring follows the pointer, not the last dab, and stays at the last surface it was on when the pointer
		// leaves the mesh mid-stroke, so it does not blink out.
		if (fLastDabX != fPointerX || fLastDabY != fPointerY)
		{
			const std::vector<BrushHit> lastHits = fHits;
			const std::vector<BrushReach> lastReaches = fReaches;
			if (!pickAt(fPointerX, fPointerY))
			{
				fHits = lastHits;
				fReaches = lastReaches;
			}
		}
		if (fDabs > dabsBefore)
		{
			// A dab changed the skin: redraw now so the weights show under the pointer, not a frame behind.
			M3dView::active3dView().refresh(false, true);
			return MStatus::kSuccess;
		}
	}
	requestRedraw();
	return MStatus::kSuccess;
}

MStatus SkinBrushContext::doRelease(MEvent& event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context)
{
	fIsResizing = false;
	debugLog(MString("release: stroking ") + (fIsStroking ? "yes" : "no") + ", ticks " + fTicks + ", drag events " + fDragEvents + ", dabs " + fDabs);
	if (fIsStroking)
	{
		endStroke();
	}
	requestRedraw();
	return MStatus::kSuccess;
}

MStatus SkinBrushContext::doPtrMoved(MEvent& event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context)
{
	short x = 0;
	short y = 0;
	event.getPosition(x, y);
	pickAt(x, y);
	requestRedraw();
	return MStatus::kSuccess;
}

MStatus SkinBrushContext::drawFeedback(MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context)
{
	draw(drawMgr);
	return MStatus::kSuccess;
}

// The visualizer: a ring of the brush radius at the hit, and a point on every vertex the brush reaches, coloured by the
// falloff there (blue at the edge, yellow at the center). The reach is the one a dab would use, so it follows the
// surface, the restriction and the through-mesh setting. It is drawn on top of the surface in one call.
void SkinBrushContext::draw(MHWRender::MUIDrawManager& drawMgr)
{
	if (fHits.empty())
	{
		return;
	}
	drawMgr.beginDrawable();
	drawMgr.setDepthPriority(MHWRender::MRenderItem::sActivePointDepthPriority);

	MPointArray positions;
	MColorArray colors;
	for (const BrushReach& reach : fReaches)
	{
		for (std::size_t i = 0; i < reach.vertices.size(); ++i)
		{
			const double t = reach.scales[i];
			positions.append(fStroke.vertexPosition(reach.mesh, reach.vertices[i]));
			colors.append(MColor(static_cast<float>(0.1 + 0.9 * t), static_cast<float>(0.4 + 0.5 * t), static_cast<float>(1.0 - 0.8 * t), 1.0f));
		}
	}
	if (positions.length() > 0)
	{
		drawMgr.setPointSize(5.0f);
		drawMgr.mesh(MHWRender::MUIDrawManager::kPoints, positions, nullptr, &colors);
	}

	drawMgr.setColor(MColor(1.0f, 1.0f, 1.0f, 1.0f));
	drawMgr.setLineWidth(2.0f);
	for (const BrushHit& hit : fHits)
	{
		drawMgr.circle(hit.point, hit.normal, fRadius, false);
	}
	drawMgr.endDrawable();
}

// skinBrushCtx -----------------------------------------------------------------------------------
const char* SkinBrushCtxCmd::aCommandString = "skinBrushCtx";

void* SkinBrushCtxCmd::creator()
{
	return new SkinBrushCtxCmd();
}

MPxContext* SkinBrushCtxCmd::makeObj()
{
	fContext = new SkinBrushContext();
	return fContext;
}

MStatus SkinBrushCtxCmd::appendSyntax()
{
	MSyntax contextSyntax = syntax();
	CHECK_MSTATUS(contextSyntax.addFlag("-op", kOperationFlag, MSyntax::kString));
	CHECK_MSTATUS(contextSyntax.addFlag("-rd", kRadiusFlag, MSyntax::kDouble));
	CHECK_MSTATUS(contextSyntax.addFlag("-st", kStrengthFlag, MSyntax::kDouble));
	CHECK_MSTATUS(contextSyntax.addFlag("-tm", kThroughMeshFlag, MSyntax::kBoolean));
	CHECK_MSTATUS(contextSyntax.addFlag("-fo", kFalloffFlag, MSyntax::kString));
	CHECK_MSTATUS(contextSyntax.addFlag("-db", kDebugFlag, MSyntax::kBoolean));
	return MStatus::kSuccess;
}

MStatus SkinBrushCtxCmd::doEditFlags()
{
	MArgParser argData = parser();
	if (argData.isFlagSet(kOperationFlag))
	{
		MString operation;
		argData.getFlagArgument(kOperationFlag, 0, operation);
		if (!fContext->setOperation(operation.asChar()))
		{
			MGlobal::displayError(operation + " is not a brush operation.");
			return MStatus::kInvalidParameter;
		}
	}
	if (argData.isFlagSet(kRadiusFlag))
	{
		double radius = 0.0;
		argData.getFlagArgument(kRadiusFlag, 0, radius);
		if (!fContext->setRadius(radius))
		{
			MGlobal::displayError("The brush -radius must be at least 0.001.");
			return MStatus::kInvalidParameter;
		}
	}
	if (argData.isFlagSet(kStrengthFlag))
	{
		double strength = 0.0;
		argData.getFlagArgument(kStrengthFlag, 0, strength);
		if (!fContext->setStrength(strength))
		{
			MGlobal::displayError("The brush -strength must be above 0 and at most 1.");
			return MStatus::kInvalidParameter;
		}
	}
	if (argData.isFlagSet(kFalloffFlag))
	{
		MString falloff;
		argData.getFlagArgument(kFalloffFlag, 0, falloff);
		if (!fContext->setFalloff(falloff.asChar()))
		{
			MGlobal::displayError(falloff + " is not a falloff: surface or volume.");
			return MStatus::kInvalidParameter;
		}
	}
	if (argData.isFlagSet(kDebugFlag))
	{
		bool debug = false;
		argData.getFlagArgument(kDebugFlag, 0, debug);
		fContext->setDebug(debug);
	}
	if (argData.isFlagSet(kThroughMeshFlag))
	{
		bool throughMesh = false;
		argData.getFlagArgument(kThroughMeshFlag, 0, throughMesh);
		fContext->setThroughMesh(throughMesh);
	}
	return MStatus::kSuccess;
}

MStatus SkinBrushCtxCmd::doQueryFlags()
{
	MArgParser argData = parser();
	if (argData.isFlagSet(kOperationFlag))
	{
		setResult(MString(fContext->operation().c_str()));
	}
	if (argData.isFlagSet(kRadiusFlag))
	{
		setResult(fContext->radius());
	}
	if (argData.isFlagSet(kStrengthFlag))
	{
		setResult(fContext->strength());
	}
	if (argData.isFlagSet(kFalloffFlag))
	{
		setResult(MString(falloffName(fContext->falloff())));
	}
	if (argData.isFlagSet(kDebugFlag))
	{
		setResult(fContext->debug());
	}
	if (argData.isFlagSet(kThroughMeshFlag))
	{
		setResult(fContext->throughMesh());
	}
	return MStatus::kSuccess;
}
