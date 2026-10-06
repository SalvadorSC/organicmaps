#pragma once

#include "bus_live/types.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace bus_live
{
struct TmbFeed
{
  std::vector<Arrival> m_arrivals;
  int64_t m_updatedUnixSec = 0;
};

// iTransit bus stop payload: parades[].linies_trajectes[].propers_busos[].
// Times in the payload are milliseconds. Arrivals are not filtered by age here.
std::optional<TmbFeed> ParseTmbArrivals(std::string_view json);

// Accepts a GeoJSON FeatureCollection (CODI_PARADA / NOM_PARADA) or a
// parades/stops array. An empty vector means the JSON parsed but no stop
// was recognized, so callers fall back to AMB matching.
std::optional<std::vector<TransitStop>> ParseTmbCatalog(std::string_view json);
}  // namespace bus_live
