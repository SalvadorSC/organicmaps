#pragma once

#include "metro_live/types.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace metro_live
{
// GeoJSON FeatureCollection from /v1/transit/linies/metro/estacions.
// CODI_ESTACIO, NOM_ESTACIO, ORDRE_ESTACIO, NOM_LINIA and a Point [lon, lat].
// An empty vector means the JSON parsed but no station was recognized.
std::optional<std::vector<Station>> ParseStations(std::string_view json);

// GeoJSON FeatureCollection from /v1/transit/linies/metro.
// MultiLineString coordinates are [lon, lat]. Funicular rows are skipped.
std::optional<std::vector<LinePath>> ParseLines(std::string_view json);

// /v1/itransit/metro/estacions. temps_arribada is milliseconds.
// Returns the feed timestamp in unix seconds (0 if absent) and the rows.
struct ArrivalFeed
{
  int64_t m_updatedUnixSec = 0;
  std::vector<ArrivalObs> m_rows;
};

std::optional<ArrivalFeed> ParseArrivals(std::string_view json);
}  // namespace metro_live
