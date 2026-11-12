#include "JointClipFile.h"
#include "Constants.h"
#include "FormatUpdate.h"
#include "JointMarkup.h"

#include <cmath>
#include <cstddef>
#include <set>
#include <utility>

namespace
{
	// A quaternion that is further from unit length than this is a corrupt file, not rounding.
	constexpr double kQuaternionTolerance{ 1e-3 };

	// A JSON array of exactly N numbers into an array. Reports and returns false otherwise.
	template <std::size_t N>
	bool readNumbers(const nlohmann::json& value, const std::string& where, std::array<double, N>& outNumbers, ValidationReport& report)
	{
		if (!value.is_array() || value.size() != N)
		{
			report.addError(where, "must be an array of " + std::to_string(N) + " numbers");
			return false;
		}
		for (std::size_t i = 0; i < N; ++i)
		{
			if (!value[i].is_number())
			{
				report.addError(where + "[" + std::to_string(i) + "]", "must be a number, not " + value[i].dump());
				return false;
			}
			outNumbers[i] = value[i].get<double>();
		}
		return true;
	}

	bool readTransform(const nlohmann::json& value, const std::string& where, JointTransform& outTransform, ValidationReport& report)
	{
		const std::size_t errorCount = report.errors.size();
		RigTemplateValidation::checkKeys(
			value,
			{ JointClipFileKey::Translate, JointClipFileKey::Rotate, JointClipFileKey::Scale },
			{},
			where,
			report);
		if (report.errors.size() != errorCount)
		{
			return false;
		}

		bool isValid = readNumbers(value.at(JointClipFileKey::Translate), where + "." + JointClipFileKey::Translate, outTransform.translate, report);
		isValid = readNumbers(value.at(JointClipFileKey::Rotate), where + "." + JointClipFileKey::Rotate, outTransform.rotate, report) && isValid;
		isValid = readNumbers(value.at(JointClipFileKey::Scale), where + "." + JointClipFileKey::Scale, outTransform.scale, report) && isValid;
		if (!isValid)
		{
			return false;
		}

		double lengthSquared = 0.0;
		for (const double component : outTransform.rotate)
		{
			lengthSquared += component * component;
		}
		if (std::abs(std::sqrt(lengthSquared) - 1.0) > kQuaternionTolerance)
		{
			report.addError(where + "." + JointClipFileKey::Rotate, "must be a unit quaternion, not " + value.at(JointClipFileKey::Rotate).dump());
			return false;
		}
		return true;
	}

	// The path above a joint's own, "" for a root.
	std::string parentPath(const std::string& path)
	{
		const std::size_t slash = path.rfind('/');
		return slash == std::string::npos ? std::string() : path.substr(0, slash);
	}

	void readJoints(const nlohmann::json& joints, const std::string& jointsPath, JointClip& outClip, ValidationReport& report)
	{
		if (!joints.is_array() || joints.empty())
		{
			report.addError(jointsPath, "must be a non-empty array of joints");
			return;
		}

		std::set<std::string> paths;
		std::set<std::pair<std::string, std::string>> markedJoints;
		for (std::size_t i = 0; i < joints.size(); ++i)
		{
			const std::string where = jointsPath + "[" + std::to_string(i) + "]";
			const nlohmann::json& joint = joints[i];
			const std::size_t errorCount = report.errors.size();
			RigTemplateValidation::checkKeys(
				joint,
				{ JointClipFileKey::Path, JointClipFileKey::Markup, JointClipFileKey::Rest },
				{},
				where,
				report);
			if (report.errors.size() != errorCount)
			{
				continue;
			}

			JointClipJoint clipJoint;
			const nlohmann::json& path = joint.at(JointClipFileKey::Path);
			const nlohmann::json& markup = joint.at(JointClipFileKey::Markup);
			if (!path.is_string() || path.get<std::string>().empty())
			{
				report.addError(where + "." + JointClipFileKey::Path, "must be a non-empty string, not " + path.dump());
				continue;
			}
			clipJoint.path = path.get<std::string>();
			if (!markup.is_string())
			{
				report.addError(where + "." + JointClipFileKey::Markup, "must be a string, not " + markup.dump());
				continue;
			}
			clipJoint.markup = markup.get<std::string>();

			if (!paths.insert(clipJoint.path).second)
			{
				report.addError(where + "." + JointClipFileKey::Path, "'" + clipJoint.path + "' is already used by an earlier joint");
				continue;
			}
			// The hierarchy is the path, and a parent comes before its children.
			const std::string parent = parentPath(clipJoint.path);
			if (!parent.empty() && paths.find(parent) == paths.end())
			{
				report.addError(where + "." + JointClipFileKey::Path, "'" + clipJoint.path + "' has no joint '" + parent + "' before it");
				continue;
			}

			if (!clipJoint.markup.empty())
			{
				JointMarkup parsed;
				std::string error;
				if (!parseJointMarkup(clipJoint.markup, parsed, error))
				{
					report.addError(where + "." + JointClipFileKey::Markup, error);
					continue;
				}
				if (!markedJoints.insert({ parsed.side, parsed.usage }).second)
				{
					report.addError(where + "." + JointClipFileKey::Markup, "another joint already has " + formatJointSelector({ parsed.side, parsed.usage }));
					continue;
				}
			}

			if (readTransform(joint.at(JointClipFileKey::Rest), where + "." + JointClipFileKey::Rest, clipJoint.rest, report))
			{
				outClip.joints.push_back(std::move(clipJoint));
			}
		}
	}

	void readPoses(const nlohmann::json& poses, const std::string& posesPath, JointClip& outClip, ValidationReport& report)
	{
		if (!poses.is_array() || poses.empty())
		{
			report.addError(posesPath, "must be a non-empty array of poses");
			return;
		}

		for (std::size_t p = 0; p < poses.size(); ++p)
		{
			const std::string poseWhere = posesPath + "[" + std::to_string(p) + "]";
			const nlohmann::json& pose = poses[p];
			if (!pose.is_array() || pose.size() != outClip.joints.size())
			{
				report.addError(poseWhere, "must be an array with one transform per joint (" + std::to_string(outClip.joints.size()) + ")");
				continue;
			}

			JointPose clipPose(pose.size());
			bool isValid = true;
			for (std::size_t j = 0; j < pose.size(); ++j)
			{
				isValid = readTransform(pose[j], poseWhere + "[" + std::to_string(j) + "]", clipPose[j], report) && isValid;
			}
			if (isValid)
			{
				outClip.poses.push_back(std::move(clipPose));
			}
		}
	}
}

const UpdateTable& jointClipUpdateTable()
{
	// Add a step here, and nowhere else, when the JointClip file changes shape or meaning.
	static const UpdateTable table{ 1, {} };
	return table;
}

void deserializeJointClip(const std::string& text, JointClip& outClip, ValidationReport& report)
{
	const std::string where = "JointClip file";
	nlohmann::json file;
	try
	{
		file = nlohmann::json::parse(text);
	}
	catch (const nlohmann::json::exception& e)
	{
		report.addError(where, e.what());
		return;
	}

	const std::size_t errorCount = report.errors.size();
	RigTemplateValidation::checkKeys(
		file,
		{ JointClipFileKey::FormatVersion, JointClipFileKey::FrameRate, JointClipFileKey::StartFrame, JointClipFileKey::Joints, JointClipFileKey::Poses },
		{},
		where,
		report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	const nlohmann::json& version = file.at(JointClipFileKey::FormatVersion);
	const std::string versionPath = where + "." + JointClipFileKey::FormatVersion;
	if (!version.is_number_integer())
	{
		report.addError(versionPath, "must be an integer, not " + version.dump());
		return;
	}
	if (!FormatUpdate::runUpdates(jointClipUpdateTable(), version.get<int>(), file, versionPath, report))
	{
		return;
	}

	JointClip clip;
	const nlohmann::json& frameRate = file.at(JointClipFileKey::FrameRate);
	if (!frameRate.is_number() || frameRate.get<double>() <= 0.0)
	{
		report.addError(where + "." + JointClipFileKey::FrameRate, "must be a number above 0, not " + frameRate.dump());
		return;
	}
	clip.frameRate = frameRate.get<double>();

	const nlohmann::json& startFrame = file.at(JointClipFileKey::StartFrame);
	if (!startFrame.is_number_integer())
	{
		report.addError(where + "." + JointClipFileKey::StartFrame, "must be an integer, not " + startFrame.dump());
		return;
	}
	clip.startFrame = startFrame.get<int>();

	readJoints(file.at(JointClipFileKey::Joints), where + "." + JointClipFileKey::Joints, clip, report);
	if (report.errors.size() != errorCount)
	{
		return;
	}
	readPoses(file.at(JointClipFileKey::Poses), where + "." + JointClipFileKey::Poses, clip, report);
	if (report.errors.size() == errorCount)
	{
		outClip = std::move(clip);
	}
}
