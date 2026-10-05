#pragma once
#include <maya/MString.h>

enum class RigType : int
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

RigType getRigTypeFromString(const char* rigType);
RigType getRigTypeFromShort(const short rigType);
short getShortFromRigType(const RigType);
