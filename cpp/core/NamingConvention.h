#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The name every joint, control, module, constraint and mesh in a rig carries:
//   <camelCaseName>_<##>_<L|R|M>_<JNT|CTRL|MOD|CON|GEO|MRK>  upperArm_00_L_JNT, upperArm_00_L_CTRL, fkChain_00_L_MOD, body_00_M_GEO, heel_00_L_MRK
// The base is camelCase with no underscore, so a name splits on its underscores. The index is two digits, or more
// when it needs them. Sides are the SideNames strings ("Left", "Right", "Center"), which markup also uses.

#include <string>

namespace NamingConvention
{
	enum class Kind
	{
		Joint,
		Control,
		Module,
		Constraint,
		Mesh,
		Marker,
	};

	struct Name
	{
		std::string base;
		int index{ 0 };
		std::string side;   // a SideNames string
		Kind kind{ Kind::Joint };
	};

	// The form, for error messages.
	inline constexpr const char* Form{ "<camelCaseName>_##_<L|R|M>_<JNT|CTRL|MOD|CON|GEO|MRK>" };

	// "L", "R" or "M" for a SideNames string. Empty for anything else.
	std::string sideToken(const std::string& side);

	// "JNT", "CTRL", "MOD", "CON", "GEO" or "MRK".
	const char* kindToken(Kind kind);

	// A lowercase letter, then letters and digits: "upperArm", "spineIK". No underscore.
	bool isValidBase(const std::string& base);

	// The name as text, always in canonical form: parse(format(n)) gives n back.
	std::string format(const Name& name);

	// Reads a name without a namespace. False when it is not in the form or not canonical ("arm_0_L_JNT" is not).
	bool parse(const std::string& text, Name& outName);

	// A name from a usage such as "layout_0" or "spineIK_02": a trailing _NN becomes the index, no suffix gives index 0.
	// False when what is left is not a valid base.
	bool fromUsage(const std::string& usage, const std::string& side, Kind kind, Name& outName);

	// The name a control takes. A joint name (namespace already stripped) gives the joint's base and index, a usage gives
	// fromUsage's. The side is the module's, so a control follows its module. False when source is neither.
	bool controlName(const std::string& source, const std::string& side, std::string& outName);

	// The same name on the other side, "arm_00_L_MOD" <-> "arm_00_R_MOD". Empty when the name does not parse or is Center.
	std::string swapSide(const std::string& text);
}
