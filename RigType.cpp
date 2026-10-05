#include "RigType.h"
#include <maya/MGlobal.h>

// Asset Type ---------------------------------------------------
RigType getRigTypeFromString(const char* rigType)
{
	if (rigType == "Prop") return RigType::Prop;
	else if (rigType == "Character") return RigType::Character;
	else if (rigType == "Vehicle") return RigType::Vehicle;
	else if (rigType == "Head") return RigType::Head;
	else if (rigType == "Weapon") return RigType::Weapon;
	else if (rigType == "VFX") return RigType::VFX;
	else if (rigType == "Light") return RigType::Light;
	else if (rigType == "Environment") return RigType::Environment;
	else if (rigType == "Other") return RigType::Other;
	else
	{
		MGlobal::displayError("Given asset type is not valid, defaulting to Other");
		return RigType::Other;
	}
}


RigType getRigTypeFromShort(const short rigType)
{
	if (rigType < 0 || rigType > static_cast<short>(RigType::Other))
	{
		MGlobal::displayError("Short value out of range for RigType, defaulting to Other.");
		return RigType::Other;
	}
	return static_cast<RigType>(rigType);
}

short getShortFromRigType(const RigType rigType)
{
	return static_cast<short>(rigType);
}
