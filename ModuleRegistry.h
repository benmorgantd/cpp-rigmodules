#pragma once
#include "RigModule.h"
#include <vector>
#include <string>

class ModuleRegistry
{
public:
    static const std::vector<ModuleDescriptor>& all();
    static const ModuleDescriptor* find(const std::string& moduleType);
};