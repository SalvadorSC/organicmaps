#pragma once

#include "bus_live/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bus_live
{
std::optional<std::vector<TransitStop>> ParseStops(std::string_view csv);
std::optional<std::vector<GtfsRoute>> ParseRoutes(std::string_view csv);
std::optional<std::vector<GtfsTrip>> ParseTrips(std::string_view csv);

// Reads only stops.txt, routes.txt and trips.txt from a GTFS zip.
// Returns nullopt when those entries are missing or unreadable.
std::optional<StaticIndex> LoadStaticIndexFromZip(std::string const & zipPath);
}  // namespace bus_live
