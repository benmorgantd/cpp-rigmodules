#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Which selection sets a rig gets (D39), planned from plain data so the layout is testable without a scene.

#include <cstddef>
#include <string>
#include <vector>

namespace SelectionSets
{
	// The setMarkup format version, so the schema can grow (D16).
	inline constexpr int MarkupVersion{ 1 };

	// One control or joint, described by what the sets split on.
	struct Item
	{
		std::string module;   // short name of the module that owns it, empty when none does
		std::string side;     // one of SideNames
	};

	// One set to create. items and children index the caller's item list and this plan.
	struct Set
	{
		std::string name;     // short name, without a namespace
		std::string markup;   // the setMarkup JSON
		std::vector<std::size_t> items;      // direct members
		std::vector<std::size_t> children;   // nested sets, by index into the plan
	};

	// The sets for one kind (SelectionSetKind), as a hierarchy:
	//   <kind>_set: <kind>_left_set, <kind>_right_set, <kind>_center_set, <kind>_modules_set
	//   <kind>_modules_set: <module>_<kind>_set, one per module that owns an item, by module name
	// Index 0 is the top set, then the three side sets, the modules set, and the module sets. Only the side and
	// module sets hold items directly: an item is in the one set for its side and the one for its module, and
	// selecting the top set selects it once. An item no module owns is in its side set only. The side sets always
	// exist, even empty, so a lookup never has to handle their absence.
	std::vector<Set> planSets(const std::string& kind, const std::vector<Item>& items);
}
