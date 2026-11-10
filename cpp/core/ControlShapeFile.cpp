#include "ControlShapeFile.h"
#include "Constants.h"
#include "ControlShapes.h"
#include "FormatUpdate.h"
#include "JointMarkup.h"

#include <cmath>

namespace
{
	// Version 1 had no alpha. 1.0 is the wireframeAlpha default, so an old file loads as it always looked.
	void addWireframeAlpha(nlohmann::json& data)
	{
		if (!data.is_object() || !data.contains(ControlShapeFileKey::Controls) || !data.at(ControlShapeFileKey::Controls).is_object())
		{
			return;
		}
		for (auto& side : data.at(ControlShapeFileKey::Controls))
		{
			if (!side.is_object())
			{
				continue;
			}
			for (auto& entry : side)
			{
				if (entry.is_object() && !entry.contains(RigControlNodeAttr::WireframeAlpha))
				{
					entry[RigControlNodeAttr::WireframeAlpha] = 1.0;
				}
			}
		}
	}

	// One control's entry. Reports problems under where and returns false if any.
	bool parseControlShape(const nlohmann::json& entry, const std::string& where, ControlShape& outShape, ValidationReport& report)
	{
		const std::size_t errorCount = report.errors.size();
		RigTemplateValidation::checkKeys(entry,
			{ RigControlNodeAttr::ShapeType, RigControlNodeAttr::ShapeTransform, RigControlNodeAttr::Filled, RigControlNodeAttr::WireframeAlpha },
			{}, where, report);
		if (report.errors.size() != errorCount)
		{
			return false;
		}

		const nlohmann::json& shapeType = entry.at(RigControlNodeAttr::ShapeType);
		ShapeType parsedType = ShapeType::Box;
		if (!shapeType.is_string() || !findShapeType(shapeType.get<std::string>(), parsedType))
		{
			report.addError(where + "." + RigControlNodeAttr::ShapeType, shapeType.dump() + " is not a shape type");
		}

		const nlohmann::json& shapeTransform = entry.at(RigControlNodeAttr::ShapeTransform);
		bool isMatrix = shapeTransform.is_array() && shapeTransform.size() == outShape.shapeTransform.size();
		for (std::size_t i = 0; isMatrix && i < shapeTransform.size(); ++i)
		{
			isMatrix = shapeTransform[i].is_number();
		}
		if (!isMatrix)
		{
			report.addError(where + "." + RigControlNodeAttr::ShapeTransform, "must be a list of 16 numbers");
		}

		const nlohmann::json& filled = entry.at(RigControlNodeAttr::Filled);
		if (!filled.is_boolean())
		{
			report.addError(where + "." + RigControlNodeAttr::Filled, "must be true or false");
		}

		const nlohmann::json& alpha = entry.at(RigControlNodeAttr::WireframeAlpha);
		if (!alpha.is_number() || alpha.get<double>() < 0.0 || alpha.get<double>() > 1.0)
		{
			report.addError(where + "." + RigControlNodeAttr::WireframeAlpha, "must be a number from 0 to 1");
		}

		if (report.errors.size() != errorCount)
		{
			return false;
		}

		outShape.shapeType = shapeType.get<std::string>();
		shapeTransform.get_to(outShape.shapeTransform);
		outShape.filled = filled.get<bool>();
		outShape.wireframeAlpha = alpha.get<double>();
		return true;
	}
}

const UpdateTable& controlShapeUpdateTable()
{
	// Add a step here, and nowhere else, when the shape file changes shape or meaning. Step 0 takes version 1 to 2.
	static const UpdateTable table{ 1, { &addWireframeAlpha } };
	return table;
}

// Written by hand instead of json::dump(4), which puts each of the 16 matrix values on its own line and sorts
// formatVersion after controls. Here a control's matrix is one line, so a shape edit is a one-line diff.
// Values and keys still go through json::dump, so escaping and number formatting match the parser.
std::string serializeControlShapes(const ControlShapeMap& shapes)
{
	const auto quoted = [](const std::string& text)
	{
		return nlohmann::json(text).dump();
	};
	const auto matrixLine = [](const std::array<double, 16>& values)
	{
		std::string line = "[";
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			line += (i == 0 ? "" : ", ") + nlohmann::json(values[i]).dump();
		}
		return line + "]";
	};

	std::string out = "{\n";
	out += "    " + quoted(ControlShapeFileKey::FormatVersion) + ": " + std::to_string(controlShapeUpdateTable().currentVersion()) + ",\n";
	out += "    " + quoted(ControlShapeFileKey::Controls) + ": {";

	bool isFirstSide = true;
	for (const auto& [side, usages] : shapes)
	{
		out += std::string(isFirstSide ? "" : ",") + "\n        " + quoted(side) + ": {";
		isFirstSide = false;

		bool isFirstUsage = true;
		for (const auto& [usage, shape] : usages)
		{
			out += std::string(isFirstUsage ? "" : ",") + "\n            " + quoted(usage) + ": {\n";
			isFirstUsage = false;

			out += "                " + quoted(RigControlNodeAttr::ShapeType) + ": " + quoted(shape.shapeType) + ",\n";
			out += "                " + quoted(RigControlNodeAttr::ShapeTransform) + ": " + matrixLine(shape.shapeTransform) + ",\n";
			out += "                " + quoted(RigControlNodeAttr::Filled) + ": " + (shape.filled ? "true" : "false") + ",\n";
			// The plug is a float, so its value read as a double is 0.800000011920929. Four places keep the file readable.
			out += "                " + quoted(RigControlNodeAttr::WireframeAlpha) + ": " + nlohmann::json(std::round(shape.wireframeAlpha * 1e4) / 1e4).dump() + "\n";
			out += "            }";
		}
		out += "\n        }";
	}
	out += std::string(shapes.empty() ? "" : "\n    ") + "}\n}\n";
	return out;
}

void deserializeControlShapes(const std::string& text, ControlShapeMap& outShapes, ValidationReport& report)
{
	const std::string where = "shape file";
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
	RigTemplateValidation::checkKeys(file, { ControlShapeFileKey::FormatVersion, ControlShapeFileKey::Controls }, {}, where, report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	const nlohmann::json& version = file.at(ControlShapeFileKey::FormatVersion);
	const std::string versionPath = where + "." + ControlShapeFileKey::FormatVersion;
	if (!version.is_number_integer())
	{
		report.addError(versionPath, "must be an integer, not " + version.dump());
		return;
	}
	// Bring an older file forward first (D33), so the checks below read only the current shape.
	if (!FormatUpdate::runUpdates(controlShapeUpdateTable(), version.get<int>(), file, versionPath, report))
	{
		return;
	}

	const nlohmann::json& controls = file.at(ControlShapeFileKey::Controls);
	const std::string controlsPath = where + "." + ControlShapeFileKey::Controls;
	if (!controls.is_object())
	{
		report.addError(controlsPath, "must be a JSON object of sides");
		return;
	}

	for (const auto& [side, usages] : controls.items())
	{
		const std::string sidePath = controlsPath + "." + side;
		if (!isValidSideName(side))
		{
			report.addError(sidePath, "'" + side + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right);
			continue;
		}
		if (!usages.is_object())
		{
			report.addError(sidePath, "must be a JSON object of usages");
			continue;
		}

		for (const auto& [usage, entry] : usages.items())
		{
			if (usage.empty())
			{
				report.addError(sidePath, "usage is empty");
				continue;
			}

			ControlShape shape;
			if (parseControlShape(entry, sidePath + "." + usage, shape, report))
			{
				outShapes[side][usage] = shape;
			}
		}
	}
}
