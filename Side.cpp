#include "Side.h"

const char* getSideSuffix(Side sideEnum)
{
	switch (sideEnum)
	{
		case Side::Left: return "_l";
		case Side::Right: return "_r";
		case Side::Center: return "_c";
		default: return "_c";
	}
}

const char* getFullSideName(const Side sideEnum)
{
	switch (sideEnum)
	{
		case Side::Left: return "Left";
		case Side::Right: return "Right";
		case Side::Center: return "Center";
		default: return "Center";
	}
}

Side getSideFromSuffix(const char* suffix)
{
	if (!suffix)
	{
		return Side::Center;
	}

	if (suffix == "_l" || suffix == "l")
	{
		return Side::Left;
	}
	else if (suffix == "_r" || suffix == "r")
	{
		return Side::Right;
	}
	return Side::Center;
}

Side getSideFromInt(unsigned int side)
{
	switch (side)
	{
		case 0: return Side::Center;
		case 1: return Side::Left;
		case 2: return Side::Right;
		default: return Side::Center;
	}
}

MColor getColorFromSide(Side sideEnum)
{
	switch (sideEnum)
	{
		case Side::Left: return MColor(0.0f, 1.0f, 0.0f);  // TODO: pull this out of a constants file
		case Side::Right: return MColor(1.0f, 0.0f, 0.0f);
		case Side::Center: return MColor(1.0f, 1.0f, 0.0f);
		default: return MColor(1.0f, 1.0f, 0.0f);
	}
}