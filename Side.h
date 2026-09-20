#pragma once

#include <maya/MColor.h>

enum class Side : int
{
	Center = 0,
	Left = 1,
	Right = 2,
};

const char* getSideSuffix(const Side sideEnum);
const char* getFullSideName(const Side sideEnum);
Side getSideFromSuffix(const char* suffix);
Side getSideFromInt(unsigned int side);
MColor getColorFromSide(Side sideEnum);