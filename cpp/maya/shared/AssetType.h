#pragma once
#include <string>

enum class AssetType : int
{
	Mesh = 0,
	Rig = 1,
	Other = 2
};

AssetType getAssetTypeFromString(std::string& assetType);
AssetType getAssetTypeFromShort(const short assetType);
short getShortFromAssetType(const AssetType);