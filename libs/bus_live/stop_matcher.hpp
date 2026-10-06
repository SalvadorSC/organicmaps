#pragma once

#include "bus_live/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace bus_live
{
// Installed maps do not carry a generic OSM ref (FMD_REF). Only local_ref is
// stored, as FMD_LOCAL_REF. Matching therefore uses coordinates, the feature
// name, and local_ref when present. Persisting ref from the generator would
// be a metadata format change and is intentionally not part of this feature.

double constexpr kMatchRadiusM = 50.0;
// An exact public code may win a bit beyond the geo radius (001720 vs 1720).
double constexpr kRefRadiusM = 120.0;

// Lowercase, and strip leading zeros when the whole token is numeric.
std::string CanonicalCode(std::string text);

struct StopMatch
{
  TransitStop m_stop;
  bool m_ref = false;
  bool m_name = false;
  double m_distanceM = 0;
};

// Nearest stop inside kMatchRadiusM. An exact code inside kRefRadiusM beats a
// closer stop, and a name match beats a closer unnamed stop.
std::optional<StopMatch> MatchStop(std::vector<TransitStop> const & stops, StopQuery const & query);
}  // namespace bus_live
