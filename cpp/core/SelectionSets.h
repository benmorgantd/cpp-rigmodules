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

	// The sets for one kind (SelectionSetKind), in this order: the "all" set at index 0, a subset for each module
	// that owns an item (by module name), then Left, Right and Center. The "all" set nests the module subsets and
	// holds directly the items no module owns, so selecting it selects everything. The side sets are siblings of it,
	// and always exist, even empty, so a lookup never has to handle their absence.
	std::vector<Set> planSets(const std::string& kind, const std::vector<Item>& items);
}
