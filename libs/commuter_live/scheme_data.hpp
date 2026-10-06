#pragma once

#include "commuter_live/snap.hpp"

#include <string_view>
#include <vector>

namespace commuter_live
{
// A station from the FGC or Renfe Cercanías GTFS stop list. Views point at
// static storage owned by this library.
struct StationStop
{
  std::string_view m_id;
  std::string_view m_name;
  double m_lat = 0;
  double m_lon = 0;
  bool m_fgc = false;
};

std::vector<StationStop> StationStops();

// Rodalies shapes from the Renfe Cercanías GTFS. Organic Maps draws these
// corridors as grey rail, so the overlay can stroke them in the line colour.
std::vector<RailTrack> SchemeTracks();

StationStop const * FindStation(std::string_view id);
std::vector<StationStop> StopsWithin(double lat, double lon, double limitM);
}  // namespace commuter_live
