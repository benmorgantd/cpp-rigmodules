#include "AssetType.h"
#include <maya/MGlobal.h>

// Asset Type ---------------------------------------------------
AssetType getAssetTypeFromString(const char* assetType)
{
	if (assetType == "Prop") return AssetType::Prop;
	else if (assetType == "Character") return AssetType::Character;
	else if (assetType == "Vehicle") return AssetType::Vehicle;
	else if (assetType == "Head") return AssetType::Head;
	else if (assetType == "Weapon") return AssetType::Weapon;
	else if (assetType == "VFX") return AssetType::VFX;
	else if (assetType == "Light") return AssetType::Light;
	else if (assetType == "Environment") return AssetType::Environment;
	else if (assetType == "Other") return AssetType::Other;
	else
	{
		MGlobal::displayError("Given asset type is not valid, defaulting to Other");
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
