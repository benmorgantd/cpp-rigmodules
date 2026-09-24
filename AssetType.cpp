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
	if (assetType == 0) return AssetType::Prop;
	else if (assetType == 1) return AssetType::Character;
	else if (assetType == 2) return AssetType::Vehicle;
	else if (assetType == 3) return AssetType::Head;
	else if (assetType == 4) return AssetType::Weapon;
	else if (assetType == 5) return AssetType::VFX;
	else if (assetType == 6) return AssetType::Light;
	else if (assetType == 7) return AssetType::Environment;
	else if (assetType == 8) return AssetType::Other;
	else
	{
		MGlobal::displayError("Given asset type is not valid, defaulting to Other");
		return AssetType::Other;
	}
}

short getShortFromAssetType(const AssetType assetType)
{
	switch (assetType)
	{
	case AssetType::Prop: return 0;
	case AssetType::Character: return 1;
	case AssetType::Vehicle: return 2;
	case AssetType::Head: return 3;
	case AssetType::Weapon: return 4;
	case AssetType::VFX: return 5;
	case AssetType::Light: return 6;
	case AssetType::Environment: return 7;
	case AssetType::Other: return 8;
	default: return 8;
	}
}