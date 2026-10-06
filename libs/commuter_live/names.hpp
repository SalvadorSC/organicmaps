#pragma once

#include <string>
#include <string_view>

namespace commuter_live
{
// Catalan station name from an FGC code. Unknown codes are returned unchanged.
std::string StationName(std::string_view code);

// RRGGBB without a leading '#'. Unknown lines are grey.
std::string LineColor(std::string_view line);
}  // namespace commuter_live
