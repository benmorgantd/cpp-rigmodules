#pragma once

// Split from Constants.h so Constants.h stays free of Maya headers.
#include <maya/MColor.h>

namespace Colors
{
	// Control Colors
	inline MColor RigLeft(0.0f, 1.0f, 0.0f);
	inline MColor RigRight(1.0f, 0.0f, 0.0f);
	inline MColor RigCenter(1.0f, 1.0f, 0.0f);

	// Selection Colors
	inline MColor ActiveLead(1.0f, 1.0f, 1.0f);  // White
	inline MColor ActiveSelected(0.0f, 1.0f, 0.64f);  // Green
}
