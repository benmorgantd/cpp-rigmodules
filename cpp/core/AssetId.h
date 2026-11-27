#pragma once

// Maya-free core: no Maya headers here.

#include <string>

// An asset id is the asset folder's path below RIGGING_PROJECT_DIR, '\' between folders, any depth:
// "shooter\main\characters\hero". Every token is lowercase, so ids compare exactly.
namespace AssetId
{
	// A folder name in the asset model: an id token, or a rig, skeleton or look name. Lowercase letters,
	// digits and '_', not empty. Uppercase is rejected, never converted, so no two names differ only in case.
	bool isValidName(const std::string& name);

	// Each '\'-separated token is a valid name.
	bool isValid(const std::string& assetId);

	// The last token, "hero" above. The default rig namespace.
	std::string name(const std::string& assetId);
}
