#pragma once
#include "core.hpp"
namespace quota {
// The plan contains only exact, hashed files in this executable's directory.
json portableUninstallPlan(const fs::path &app);
HANDLE startPortableUninstall(json plan, DWORD parentPid, bool userData, bool interactive = true);
bool removeUninstallUserData(bool whatIf);
int uninstallTests(std::ostream &log);
}
