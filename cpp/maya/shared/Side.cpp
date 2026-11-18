#include "maya/shared/Side.h"
#include "core/Constants.h"
#include "maya/shared/Colors.h"

#include <string>

#include <maya/MColor.h>

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
	if (side == SideNames::Center)
	{
		return Side::Center;
	}
	else if (side == SideNames::Left)
	{
		return Side::Left;
	}
	else if (side == SideNames::Right)
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