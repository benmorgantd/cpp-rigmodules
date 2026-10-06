#include "AssetType.h"
#include <maya/MGlobal.h>
#include <string>

// Asset Type ---------------------------------------------------
AssetType getAssetTypeFromString(std::string& assetType)
{
	if (assetType == "Mesh") return AssetType::Mesh;
	else if (assetType == "Rig") return AssetType::Rig;
	else if (assetType == "Other") return AssetType::Other;
	else
	{
		MGlobal::displayError("Given asset type string is not valid, defaulting to Other");
		return AssetType::Other;
	}
}


AssetType getAssetTypeFromShort(const short assetType)
{
	if (assetType < 0 || assetType > static_cast<short>(AssetType::Other))
	{
		MGlobal::displayError("Short value out of range for AssetType, defaulting to Other.");
		return AssetType::Other;
	}
	return static_cast<AssetType>(assetType);
}

short getShortFromAssetType(const AssetType assetType)
{
	return static_cast<short>(assetType);
}
