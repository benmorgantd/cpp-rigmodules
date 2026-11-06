#pragma once

#include <maya/MColor.h>
#include <string>
#include <unordered_map>

enum class Side : int
{
	Center = 0,
	Left = 1,
	Right = 2,
};

const char* getSideSuffix(const Side& sideEnum);
const char* getFullSideName(const Side& sideEnum);
Side getSideFromSuffix(const std::string& suffix);
Side getSideFromShort(const short& side);
MColor getColorFromSide(const Side& sideEnum);
short getShortFromSide(const Side& side);
Side getSideFromString(const std::string& side);