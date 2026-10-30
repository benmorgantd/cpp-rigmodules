#include "Side.h"
#include "Constants.h"

#include <string>

const char* getSideSuffix(const Side& sideEnum)
{
	switch (sideEnum)
	{
		case Side::Left: return "_l";
		case Side::Right: return "_r";
		case Side::Center: return "_c";
		default: return "_c";
	}
}

const char* getFullSideName(const Side& sideEnum)
{
	switch (sideEnum)
	{
		case Side::Left: return SideNames::Left;
		case Side::Right: return SideNames::Right;
		case Side::Center: return SideNames::Center;
		default: return SideNames::Center;
	}
}

Side getSideFromString(const std::string& side)
{
	if (side == "Center")
	{
		return Side::Center;
	}
	else if (side == "Left")
	{
		return Side::Left;
	}
	else if (side == "Right")
	{
		return Side::Right;
	}
	return Side::Center;
}

Side getSideFromSuffix(const std::string& suffix)
{
	if (suffix.length() == 0)
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

Side getSideFromShort(const short& side)
{
	return static_cast<Side>(side);
}

short getShortFromSide(const Side& side)
{
	return static_cast<short>(side);
}

MColor getColorFromSide(const Side& sideEnum)
{
	switch (sideEnum)
	{
		case Side::Center: return Colors::RigCenter;
		case Side::Left: return Colors::RigLeft;
		case Side::Right: return Colors::RigRight;
		default: return Colors::RigCenter;
	}
}