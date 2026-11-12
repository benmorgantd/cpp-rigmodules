#include "maya/shared/RigInverse.h"
#include "core/Constants.h"
#include "core/ControlMarkup.h"
#include "core/JointMarkup.h"
#include "maya/ModuleRegistry.h"
#include "maya/nodes/Constraints.h"
#include "maya/shared/JointMarkupScene.h"
#include "maya/shared/ModuleInverse.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MEulerRotation.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MPxTransform.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MUuid.h>
#include <maya/MVector.h>

namespace
{
	// A rig joint and its source joint that sit further apart than this at rest (centimeters) make the source a
	// different skeleton, and `match` can't reproduce its pose (inverse-solve.md section 5).
	constexpr double kRestTolerance{ 1e-2 };
	constexpr double kShearTolerance{ 1e-6 };

	// kDefaultTransformValues order: translate 0-2, rotate 3-5, scale 6-8.
	constexpr int kTransformChannelCount{ 9 };
	constexpr double kTransformRest[kTransformChannelCount] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0 };

	// One socket of one module in the sweep, by index into the solver's modules. module -1 is "nothing".
	struct SocketRef
	{
		int module = -1;
		unsigned int socket = 0;
	};

	struct ConstraintDriverState
	{
		SocketRef socket;
		MMatrix mRest;
	};

	struct ConstraintState
	{
		int type = 0;
		MMatrix mChildRest;
		MMatrix mParentRest;
		SocketRef parent;
		std::map<int, ConstraintDriverState> drivers;   // by the constraint's logical driver index
		int currentDriver = 0;                          // the active driver now, used when the sidecar has no space
		int hostControl = -1;                           // the control whose `space` channel picks the driver, -1 for a fixed one
	};

	// What feeds one child plug: nothing, a socket, or a space-switch constraint.
	struct PlugState
	{
		enum class Kind
		{
			kNone,
			kSocket,
			kConstraint
		};

		Kind kind = Kind::kNone;
		SocketRef socket;
		MMatrix mParentRest;   // kSocket: the socket's rest, stored on the plug
		ConstraintState constraint;
	};

	struct ControlState
	{
		MObject oControl;
		ControlMarkup markup;
		std::vector<RigPoseScene::Channel> channels;
		MEulerRotation::RotationOrder rotateOrder = MEulerRotation::kXYZ;
		MEulerRotation previousRotation;
		int spaceChannel = -1;   // index into channels of the `space` enum, -1 for a control without one
		std::string label;
	};

	struct ModuleState
	{
		MObject oModule;
		std::string name;
		ModuleInverseFn inverse = nullptr;
		ModuleInverseContext context;
		std::vector<int> controlOfSlot;                       // index into the solver's controls, -1 for none
		std::vector<int> clipJointOfSlot;                     // index into the clip's joints, -1 when the slot has no rig joint or the source lacks it
		std::vector<std::string> missingJoints;               // the rig's joints the source lacks, as "[side, usage]"
		std::vector<PlugState> plugs;                         // by slot
		std::vector<MMatrix> socketWorld;                     // this frame's result
	};

	// A unique, stable key for a node: names can repeat across namespaces.
	std::string nodeKey(const MObject& oNode)
	{
		return MFnDependencyNode(oNode).uuid().asString().asChar();
	}

	// The logical indices of an array attribute's elements, in order.
	std::vector<unsigned int> elementIndices(const MPlug& pArray)
	{
		std::vector<unsigned int> indices;
		for (unsigned int e = 0; e < pArray.numElements(); ++e)
		{
			indices.push_back(pArray.elementByPhysicalIndex(e).logicalIndex());
		}
		return indices;
	}

	MMatrix matrixFromTransform(const JointTransform& transform)
	{
		MTransformationMatrix t;
		t.setTranslation(MVector(transform.translate[0], transform.translate[1], transform.translate[2]), MSpace::kTransform);
		t.setRotationQuaternion(transform.rotate[0], transform.rotate[1], transform.rotate[2], transform.rotate[3]);
		t.setScale(transform.scale.data(), MSpace::kTransform);
		return t.asMatrix();
	}

	// Each joint's world matrix through the clip's hierarchy: a joint's parent is the path above it, and comes first.
	std::vector<MMatrix> composeWorlds(const std::vector<int>& parents, const JointPose& pose)
	{
		std::vector<MMatrix> worlds(pose.size());
		for (std::size_t j = 0; j < pose.size(); ++j)
		{
			const MMatrix mLocal = matrixFromTransform(pose[j]);
			worlds[j] = parents[j] < 0 ? mLocal : mLocal * worlds[parents[j]];
		}
		return worlds;
	}

	std::vector<int> findParents(const JointClip& clip)
	{
		std::map<std::string, int> indexByPath;
		std::vector<int> parents;
		for (std::size_t j = 0; j < clip.joints.size(); ++j)
		{
			const std::string& path = clip.joints[j].path;
			const std::size_t slash = path.rfind('/');
			const auto it = slash == std::string::npos ? indexByPath.end() : indexByPath.find(path.substr(0, slash));
			parents.push_back(it == indexByPath.end() ? -1 : it->second);
			indexByPath[path] = static_cast<int>(j);
		}
		return parents;
	}

	// A control's local matrix from its channel values (internal units). A transform channel the control doesn't have
	// (locked or driven) is at rest.
	MMatrix localFromValues(const ControlState& control, const std::vector<double>& values)
	{
		double t[kTransformChannelCount];
		std::copy(std::begin(kTransformRest), std::end(kTransformRest), t);
		for (std::size_t i = 0; i < control.channels.size(); ++i)
		{
			if (control.channels[i].transformIndex >= 0)
			{
				t[control.channels[i].transformIndex] = values[i];
			}
		}

		MTransformationMatrix matrix;
		matrix.setTranslation(MVector(t[0], t[1], t[2]), MSpace::kTransform);
		matrix.rotateTo(MEulerRotation(t[3], t[4], t[5], control.rotateOrder));
		matrix.setScale(t + 6, MSpace::kTransform);
		return matrix.asMatrix();
	}

	// Writes a derived local matrix into the control's transform channels, with the euler nearest the previous frame's
	// so a curve doesn't flip. Warns about what the channels can't hold.
	void applyLocal(ControlState& control, const MMatrix& mLocal, std::vector<double>& values, std::vector<char>& written, std::set<std::string>& warnings)
	{
		const MTransformationMatrix matrix(mLocal);
		const MVector translation = matrix.getTranslation(MSpace::kTransform);
		double scale[3];
		double shear[3];
		matrix.getScale(scale, MSpace::kTransform);
		matrix.getShear(shear, MSpace::kTransform);
		MEulerRotation rotation = matrix.eulerRotation();
		rotation.reorderIt(control.rotateOrder);
		rotation.setToClosestSolution(control.previousRotation);
		control.previousRotation = rotation;

		if (std::abs(shear[0]) > kShearTolerance || std::abs(shear[1]) > kShearTolerance || std::abs(shear[2]) > kShearTolerance)
		{
			warnings.insert(control.label + ": the source joint has shear, which the control's channels can't hold. Position and rotation match, the shape may not.");
		}

		const double derived[kTransformChannelCount] =
		{
			translation.x, translation.y, translation.z,
			rotation.x, rotation.y, rotation.z,
			scale[0], scale[1], scale[2]
		};
		int missingChannels = 0;
		for (int t = 0; t < kTransformChannelCount; ++t)
		{
			const auto it = std::find_if(control.channels.begin(), control.channels.end(), [t](const RigPoseScene::Channel& channel)
			{
				return channel.transformIndex == t;
			});
			if (it == control.channels.end())
			{
				++missingChannels;
				continue;
			}
			const std::size_t i = static_cast<std::size_t>(it - control.channels.begin());
			values[i] = derived[t];
			written[i] = 1;
		}
		if (missingChannels > 0)
		{
			warnings.insert(control.label + ": locked or driven channels kept their values, so the control may not match its joint.");
		}
	}

	const PoseChannels* findSidecar(const PoseMap& sidecar, const ControlMarkup& markup)
	{
		const auto itSide = sidecar.find(markup.side);
		if (itSide == sidecar.end())
		{
			return nullptr;
		}
		const auto itUsage = itSide->second.find(markup.usage);
		return itUsage == itSide->second.end() ? nullptr : &itUsage->second;
	}

	// The whole rig, read once: modules in sweep order, their controls, rests and what feeds each child plug.
	class Solver
	{
	public:
		MStatus read(const MObject& oRigRoot, const JointClip& clip);
		MStatus solveFrame(const JointPose& pose, const PoseMap& sidecar, PoseMap& outPose);
		void showWarnings() const;

	private:
		MStatus readControls(ModuleState& module, std::map<std::string, int>& controlByKey);
		MStatus readJoints(ModuleState& module, std::size_t slotCount, const std::map<std::pair<std::string, std::string>, int>& clipJointByMarkup);
		SocketRef readSocketSource(const MPlug& pDestination, const std::map<std::string, int>& moduleByKey) const;
		void readPlugs(ModuleState& module, std::size_t slotCount, const std::map<std::string, int>& moduleByKey, const std::map<std::string, int>& controlByKey);
		MStatus sortModules();
		MMatrix socketMotion(const SocketRef& socket, const MMatrix& mRest) const;
		MMatrix plugDelta(const PlugState& plug, const std::vector<std::vector<char>>& written, const std::vector<std::vector<double>>& values) const;

		std::vector<ModuleState> fModules;
		std::vector<int> fOrder;   // fModules indices, parents first
		std::vector<ControlState> fControls;
		std::vector<int> fClipParents;
		std::vector<MMatrix> fClipRestWorld;
		mutable std::set<std::string> fWarnings;
	};

	SocketRef Solver::readSocketSource(const MPlug& pDestination, const std::map<std::string, int>& moduleByKey) const
	{
		SocketRef ref;
		const MPlug pSource = pDestination.source();
		if (pSource.isNull())
		{
			return ref;
		}
		const auto it = moduleByKey.find(nodeKey(pSource.node()));
		if (it != moduleByKey.end())
		{
			ref.module = it->second;
			ref.socket = pSource.logicalIndex();
		}
		return ref;
	}

	MStatus Solver::readControls(ModuleState& module, std::map<std::string, int>& controlByKey)
	{
		MStatus status;
		const MFnDependencyNode fnModule(module.oModule);
		const MPlug pControlMessage = fnModule.findPlug(RigAttr::ControlMessage, false, &status);
		if (!status)
		{
			return MStatus::kSuccess;   // a module type without controls
		}

		for (unsigned int e = 0; e < pControlMessage.numElements(); ++e)
		{
			const MPlug pElement = pControlMessage.elementByPhysicalIndex(e);
			MPlugArray pDestinations;
			pElement.connectedTo(pDestinations, false, true);
			if (pDestinations.length() == 0)
			{
				continue;
			}

			ControlState control;
			control.oControl = pDestinations[0].node();
			status = RigTraversal::getControlMarkup(control.oControl, control.markup);
			if (status)
			{
				status = RigPoseScene::getControlChannels(control.oControl, control.channels);
			}
			if (!status)
			{
				return status;
			}

			control.label = DagUtils::getPartialPathName(control.oControl).asChar();
			control.rotateOrder = static_cast<MEulerRotation::RotationOrder>(MPlug(control.oControl, MPxTransform::rotateOrder).asShort());
			control.previousRotation = MEulerRotation(
				MPlug(control.oControl, MPxTransform::rotateX).asMAngle().asRadians(),
				MPlug(control.oControl, MPxTransform::rotateY).asMAngle().asRadians(),
				MPlug(control.oControl, MPxTransform::rotateZ).asMAngle().asRadians(),
				control.rotateOrder);
			for (std::size_t i = 0; i < control.channels.size(); ++i)
			{
				if (control.channels[i].info.name == RigAttr::ControlSpace)
				{
					control.spaceChannel = static_cast<int>(i);
				}
			}

			const unsigned int slot = pElement.logicalIndex();
			if (module.controlOfSlot.size() <= slot)
			{
				module.controlOfSlot.resize(slot + 1, -1);
			}
			module.controlOfSlot[slot] = static_cast<int>(fControls.size());
			controlByKey[nodeKey(control.oControl)] = static_cast<int>(fControls.size());
			fControls.push_back(std::move(control));
		}
		return MStatus::kSuccess;
	}

	// Maps each of the module's rig joints to the source's joint with the same markup.
	MStatus Solver::readJoints(ModuleState& module, std::size_t slotCount, const std::map<std::pair<std::string, std::string>, int>& clipJointByMarkup)
	{
		module.clipJointOfSlot.assign(slotCount, -1);

		MStatus status;
		const MFnDependencyNode fnModule(module.oModule);
		const MPlug pJointOPM = fnModule.findPlug(RigAttr::JointOPM, false, &status);
		if (!status)
		{
			return MStatus::kSuccess;   // Layout has no joints
		}
		const MPlug pJointRest = fnModule.findPlug(RigAttr::JointRestWorld, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		for (unsigned int e = 0; e < pJointOPM.numElements(); ++e)
		{
			const MPlug pElement = pJointOPM.elementByPhysicalIndex(e);
			MPlugArray pDestinations;
			pElement.connectedTo(pDestinations, false, true);
			if (pDestinations.length() == 0)
			{
				continue;
			}

			bool hasMarkup = false;
			JointMarkup markup;
			status = JointMarkupScene::readMarkup(pDestinations[0].node(), hasMarkup, markup);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			if (!hasMarkup)
			{
				continue;
			}

			const unsigned int slot = pElement.logicalIndex();
			const std::string label = formatJointSelector({ markup.side, markup.usage });
			const auto it = clipJointByMarkup.find({ markup.side, markup.usage });
			if (it == clipJointByMarkup.end())
			{
				module.missingJoints.push_back(label);
				continue;
			}
			module.clipJointOfSlot[slot] = it->second;

			// The source check: the same joint should be at the same place at rest.
			const MMatrix mRigRest = DagUtils::getMatrixFromPlug(pJointRest.elementByLogicalIndex(slot));
			const MMatrix& mSourceRest = fClipRestWorld[it->second];
			const MVector difference(
				mRigRest[3][0] - mSourceRest[3][0], mRigRest[3][1] - mSourceRest[3][1], mRigRest[3][2] - mSourceRest[3][2]);
			if (difference.length() > kRestTolerance)
			{
				fWarnings.insert("The source's rest pose differs from the rig's at " + label + " by " + std::to_string(difference.length())
					+ " cm, so its bone lengths differ and the result will not reproduce the source's pose. That needs a retarget.");
			}
		}
		return MStatus::kSuccess;
	}

	void Solver::readPlugs(ModuleState& module, std::size_t slotCount, const std::map<std::string, int>& moduleByKey, const std::map<std::string, int>& controlByKey)
	{
		module.plugs.assign(slotCount, PlugState());

		MStatus status;
		const MFnDependencyNode fnModule(module.oModule);
		const MPlug pChildPlugs = fnModule.findPlug(RigAttr::ChildPlugs, false, &status);
		if (!status)
		{
			return;
		}
		MObject oParentMatrix = fnModule.attribute(RigAttr::ChildPlugParentMatrix);
		MObject oParentRest = fnModule.attribute(RigAttr::ChildPlugParentRestMatrix);

		for (unsigned int e = 0; e < pChildPlugs.numElements(); ++e)
		{
			const MPlug pElement = pChildPlugs.elementByPhysicalIndex(e);
			const unsigned int slot = pElement.logicalIndex();
			PlugState& plug = module.plugs[slot];
			const MPlug pParentMatrix = pElement.child(oParentMatrix);
			plug.mParentRest = DagUtils::getMatrixFromPlug(pElement.child(oParentRest));

			const MPlug pSource = pParentMatrix.source();
			if (pSource.isNull())
			{
				continue;
			}

			if (MFnDependencyNode(pSource.node()).typeId() != RigConstraintNode::id)
			{
				plug.kind = PlugState::Kind::kSocket;
				plug.socket = readSocketSource(pParentMatrix, moduleByKey);
				continue;
			}

			// A constraint: its output is already the delta, so the plug's own rest is identity (createConstraint).
			const MObject oConstraint = pSource.node();
			plug.kind = PlugState::Kind::kConstraint;
			ConstraintState& constraint = plug.constraint;
			constraint.type = MPlug(oConstraint, RigConstraintNode::aConstraintType).asShort();
			constraint.mChildRest = DagUtils::getMatrixFromPlug(MPlug(oConstraint, RigConstraintNode::aChildRest));
			constraint.mParentRest = DagUtils::getMatrixFromPlug(MPlug(oConstraint, RigConstraintNode::aParentRest));
			constraint.parent = readSocketSource(MPlug(oConstraint, RigConstraintNode::aParentMatrix), moduleByKey);

			const MPlug pDrivers(oConstraint, RigConstraintNode::aDrivers);
			for (unsigned int d = 0; d < pDrivers.numElements(); ++d)
			{
				const MPlug pDriver = pDrivers.elementByPhysicalIndex(d);
				ConstraintDriverState driver;
				driver.socket = readSocketSource(pDriver.child(RigConstraintNode::aDriverMatrix), moduleByKey);
				driver.mRest = DagUtils::getMatrixFromPlug(pDriver.child(RigConstraintNode::aDriverRest));
				constraint.drivers[static_cast<int>(pDriver.logicalIndex())] = driver;
			}

			const MPlug pActive(oConstraint, RigConstraintNode::aActiveDriver);
			constraint.currentDriver = pActive.asInt();
			const MPlug pSpace = pActive.source();
			if (!pSpace.isNull())
			{
				const auto it = controlByKey.find(nodeKey(pSpace.node()));
				constraint.hostControl = it == controlByKey.end() ? -1 : it->second;
			}
		}
	}

	// Parents first: a module comes after every module that feeds one of its child plugs.
	MStatus Solver::sortModules()
	{
		const std::size_t count = fModules.size();
		std::vector<std::set<int>> dependencies(count);
		for (std::size_t m = 0; m < count; ++m)
		{
			for (const PlugState& plug : fModules[m].plugs)
			{
				if (plug.kind == PlugState::Kind::kSocket)
				{
					dependencies[m].insert(plug.socket.module);
				}
				else if (plug.kind == PlugState::Kind::kConstraint)
				{
					dependencies[m].insert(plug.constraint.parent.module);
					for (const auto& driver : plug.constraint.drivers)
					{
						dependencies[m].insert(driver.second.socket.module);
					}
				}
			}
			dependencies[m].erase(-1);
			dependencies[m].erase(static_cast<int>(m));
		}

		std::vector<char> placed(count, 0);
		while (fOrder.size() < count)
		{
			bool progressed = false;
			for (std::size_t m = 0; m < count; ++m)
			{
				if (placed[m])
				{
					continue;
				}
				const bool ready = std::all_of(dependencies[m].begin(), dependencies[m].end(), [&placed](int dependency)
				{
					return placed[dependency] != 0;
				});
				if (ready)
				{
					placed[m] = 1;
					fOrder.push_back(static_cast<int>(m));
					progressed = true;
				}
			}
			if (!progressed)
			{
				MGlobal::displayError("The rig's modules feed each other in a cycle, so there is no order to solve them in.");
				return MStatus::kFailure;
			}
		}
		return MStatus::kSuccess;
	}

	MStatus Solver::read(const MObject& oRigRoot, const JointClip& clip)
	{
		// The source: world matrices of its rest pose, and its joints by markup.
		fClipParents = findParents(clip);
		JointPose restPose;
		std::map<std::pair<std::string, std::string>, int> clipJointByMarkup;
		for (std::size_t j = 0; j < clip.joints.size(); ++j)
		{
			restPose.push_back(clip.joints[j].rest);
			JointMarkup markup;
			std::string error;
			if (!clip.joints[j].markup.empty() && parseJointMarkup(clip.joints[j].markup, markup, error))
			{
				clipJointByMarkup[{ markup.side, markup.usage }] = static_cast<int>(j);
			}
		}
		fClipRestWorld = composeWorlds(fClipParents, restPose);

		MObjectArray oModules;
		MStatus status = RigTraversal::getRigModules(oRigRoot, oModules);
		if (!status)
		{
			return status;
		}

		std::map<std::string, int> moduleByKey;
		std::map<std::string, int> controlByKey;
		fModules.resize(oModules.length());
		for (unsigned int m = 0; m < oModules.length(); ++m)
		{
			ModuleState& module = fModules[m];
			module.oModule = oModules[m];
			module.name = MFnDependencyNode(oModules[m]).name().asChar();
			moduleByKey[nodeKey(oModules[m])] = static_cast<int>(m);

			const ModuleDescriptor* pDescriptor = ModuleRegistry::find(RigTraversal::getModuleType(oModules[m]));
			if (pDescriptor == nullptr || pDescriptor->inverse == nullptr)
			{
				MGlobal::displayError(MString(module.name.c_str()) + " has no inverse, so the rig can't be restored from joints.");
				return MStatus::kFailure;
			}
			module.inverse = pDescriptor->inverse;

			status = readControls(module, controlByKey);
			if (!status)
			{
				return status;
			}
		}

		for (ModuleState& module : fModules)
		{
			// Every array a module's slots share holds its rest or its name, which a build always stores.
			std::size_t slotCount = module.controlOfSlot.size();
			const MFnDependencyNode fnModule(module.oModule);
			for (const char* name : { RigAttr::ControlInputData, RigAttr::JointRestWorld, RigAttr::ChildPlugs, RigAttr::SocketNames })
			{
				MPlug pArray = fnModule.findPlug(name, false, &status);
				if (status)
				{
					for (const unsigned int index : elementIndices(pArray))
					{
						slotCount = std::max<std::size_t>(slotCount, index + 1);
					}
				}
			}
			module.controlOfSlot.resize(slotCount, -1);

			MPlug pControlData = fnModule.findPlug(RigAttr::ControlInputData, false, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			module.context.controlRestWorld.assign(slotCount, MMatrix::identity);
			MObject oRestWorld = fnModule.attribute(RigAttr::ControlRestWorld);
			for (const unsigned int slot : elementIndices(pControlData))
			{
				module.context.controlRestWorld[slot] = DagUtils::getMatrixFromPlug(pControlData.elementByLogicalIndex(slot).child(oRestWorld));
			}

			status = readJoints(module, slotCount, clipJointByMarkup);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			readPlugs(module, slotCount, moduleByKey, controlByKey);
			module.socketWorld.assign(slotCount, MMatrix::identity);

			if (!module.missingJoints.empty())
			{
				fWarnings.insert(module.name + ": the source lacks " + StringUtils::joinNames(module.missingJoints)
					+ ", so those controls come from the sidecar, or stay at rest.");
			}
		}

		return sortModules();
	}

	MMatrix Solver::socketMotion(const SocketRef& socket, const MMatrix& mRest) const
	{
		if (socket.module < 0 || socket.socket >= fModules[socket.module].socketWorld.size())
		{
			return MMatrix::identity;
		}
		return MathUtils::computeDeltaFromRest(mRest, fModules[socket.module].socketWorld[socket.socket]);
	}

	// The motion a child plug's module receives: its parent socket's, or what its space switch makes of it.
	MMatrix Solver::plugDelta(const PlugState& plug, const std::vector<std::vector<char>>& written, const std::vector<std::vector<double>>& values) const
	{
		if (plug.kind == PlugState::Kind::kSocket)
		{
			return socketMotion(plug.socket, plug.mParentRest);
		}
		if (plug.kind != PlugState::Kind::kConstraint)
		{
			return MMatrix::identity;
		}

		const ConstraintState& constraint = plug.constraint;
		int driverIndex = constraint.currentDriver;
		if (constraint.hostControl >= 0)
		{
			const int space = fControls[constraint.hostControl].spaceChannel;
			if (space >= 0 && written[constraint.hostControl][space])
			{
				driverIndex = static_cast<int>(std::lround(values[constraint.hostControl][space]));
			}
		}

		MMatrix mDriverDelta = MMatrix::identity;
		const auto itDriver = constraint.drivers.find(driverIndex);
		if (itDriver != constraint.drivers.end())
		{
			mDriverDelta = socketMotion(itDriver->second.socket, itDriver->second.mRest);
		}
		return RigConstraintNode::computeDelta(constraint.type, constraint.mChildRest, socketMotion(constraint.parent, constraint.mParentRest), mDriverDelta);
	}

	MStatus Solver::solveFrame(const JointPose& pose, const PoseMap& sidecar, PoseMap& outPose)
	{
		const std::vector<MMatrix> clipWorlds = composeWorlds(fClipParents, pose);

		// Every control starts at rest for its transform and keeps nothing else, then the sidecar overlays it.
		std::vector<std::vector<double>> values(fControls.size());
		std::vector<std::vector<char>> written(fControls.size());
		for (std::size_t c = 0; c < fControls.size(); ++c)
		{
			const ControlState& control = fControls[c];
			values[c].resize(control.channels.size());
			written[c].assign(control.channels.size(), 0);
			for (std::size_t i = 0; i < control.channels.size(); ++i)
			{
				values[c][i] = control.channels[i].rest;
				written[c][i] = control.channels[i].transformIndex >= 0 ? 1 : 0;
			}

			const PoseChannels* pChannels = findSidecar(sidecar, control.markup);
			if (pChannels == nullptr)
			{
				continue;
			}
			for (const auto& [name, value] : *pChannels)
			{
				const auto it = std::find_if(control.channels.begin(), control.channels.end(), [&name](const RigPoseScene::Channel& channel)
				{
					return channel.info.name == name;
				});
				if (it == control.channels.end())
				{
					fWarnings.insert(control.label + " has no writable channel " + name + ", so the sidecar's value is skipped.");
					continue;
				}
				const std::size_t i = static_cast<std::size_t>(it - control.channels.begin());
				values[c][i] = RigPoseScene::fromUiUnits(*it, value);
				written[c][i] = 1;
			}
		}

		for (const int m : fOrder)
		{
			ModuleState& module = fModules[m];
			const std::size_t slotCount = module.context.controlRestWorld.size();

			ModuleInverseInput input;
			input.jointWorld.resize(slotCount);
			input.parentDelta.resize(slotCount);
			input.fallbackLocal.assign(slotCount, MMatrix::identity);
			for (std::size_t slot = 0; slot < slotCount; ++slot)
			{
				if (module.clipJointOfSlot[slot] >= 0)
				{
					input.jointWorld[slot] = clipWorlds[module.clipJointOfSlot[slot]];
				}
				input.parentDelta[slot] = plugDelta(module.plugs[slot], written, values);
				const int control = module.controlOfSlot[slot];
				if (control >= 0)
				{
					input.fallbackLocal[slot] = localFromValues(fControls[control], values[control]);
				}
			}

			const ModuleInverseResult result = module.inverse(module.context, input);
			for (const std::string& warning : result.warnings)
			{
				fWarnings.insert(module.name + ": " + warning);
			}
			if (!result.solved && module.missingJoints.empty())
			{
				fWarnings.insert(module.name + ": " + result.reason);
			}

			module.socketWorld = result.socketWorld;
			for (std::size_t slot = 0; slot < slotCount && slot < result.controlLocal.size(); ++slot)
			{
				const int control = module.controlOfSlot[slot];
				if (control >= 0 && result.controlLocal[slot].has_value())
				{
					applyLocal(fControls[control], *result.controlLocal[slot], values[control], written[control], fWarnings);
				}
			}
		}

		for (std::size_t c = 0; c < fControls.size(); ++c)
		{
			const ControlState& control = fControls[c];
			PoseChannels channels;
			for (std::size_t i = 0; i < control.channels.size(); ++i)
			{
				if (written[c][i])
				{
					channels.emplace_back(control.channels[i].info.name, RigPoseScene::toUiUnits(control.channels[i], values[c][i]));
				}
			}
			outPose[control.markup.side][control.markup.usage] = channels;
		}
		return MStatus::kSuccess;
	}

	void Solver::showWarnings() const
	{
		for (const std::string& warning : fWarnings)
		{
			MGlobal::displayWarning(warning.c_str());
		}
		fWarnings.clear();
	}
}

MStatus RigInverse::solve(const MObject& oRigRoot, const JointClip& clip, const std::vector<PoseMap>& sidecars, std::vector<PoseMap>& outPoses)
{
	outPoses.clear();
	if (sidecars.size() != clip.poses.size())
	{
		MGlobal::displayError("The solve needs one sidecar entry for every JointPose.");
		return MStatus::kFailure;
	}

	Solver solver;
	MStatus status = solver.read(oRigRoot, clip);
	if (!status)
	{
		return status;
	}

	std::vector<PoseMap> poses(clip.poses.size());
	for (std::size_t p = 0; p < clip.poses.size(); ++p)
	{
		status = solver.solveFrame(clip.poses[p], sidecars[p], poses[p]);
		if (!status)
		{
			return status;
		}
	}

	solver.showWarnings();
	outPoses = std::move(poses);
	return MStatus::kSuccess;
}
