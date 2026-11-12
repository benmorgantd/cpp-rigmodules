#pragma once

// Maya-free core: no Maya headers here.

#include <string>

// An asset id is the asset folder's path below RIGGING_PROJECT_DIR, '\' between folders, any depth (D29):
// "shooter\main\characters\hero". Ids compare without case (StringUtils::equalsIgnoreCase).
namespace AssetId
{
	// Each token becomes a folder name: letters, digits and '_', none empty.
	bool isValid(const std::string& assetId);

	// The last token, "hero" above. The default rig namespace.
	std::string name(const std::string& assetId);
}
