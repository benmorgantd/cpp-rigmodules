#pragma once
#include <maya/MString.h>


enum class AssetType : int
{
	Prop = 0,
	Character = 1,
	Head = 2,
	Vehicle = 3,
	Weapon = 4,
	VFX = 5,
	Light = 6,
	Environment = 7,
	Other = 8
};

AssetType getAssetTypeFromString(const char* assetType);
AssetType getAssetTypeFromShort(const short assetType);
short getShortFromAssetType(const AssetType);
