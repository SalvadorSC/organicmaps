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

// Rodalies shapes from the Renfe Cercanías GTFS, plus FGC corridors.
// Organic Maps already paints many of these, and the overlay still strokes them.
std::vector<RailTrack> SchemeTracks();
void AppendFgcSchemeTracks(std::vector<RailTrack> & tracks);

StationStop const * FindStation(std::string_view id);
std::vector<StationStop> StopsWithin(double lat, double lon, double limitM);
}  // namespace commuter_live
