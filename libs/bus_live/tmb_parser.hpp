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

// Accepts the live TMB catalog: a GeoJSON FeatureCollection whose features
// use geometry.coordinates [lon, lat] and integer properties.CODI_PARADA.
// NOM_PARADA is the stop name. A parades array with lat/lon is also accepted.
// Null properties are ignored. An empty vector means the JSON parsed but no
// stop was recognized.
std::optional<std::vector<TransitStop>> ParseTmbCatalog(std::string_view json);
}  // namespace bus_live
