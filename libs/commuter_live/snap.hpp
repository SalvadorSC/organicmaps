#pragma once

#include "geometry/latlon.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace commuter_live
{
// One polyline a train can sit on. An empty m_ref is anonymous rail from the map:
// it is only a GPS fallback. m_colored means Organic Maps already draws the line
// in a line colour, so the overlay does not stroke it again.
struct RailTrack
{
  std::string m_ref;
  std::vector<ms::LatLon> m_shape;
  bool m_colored = false;
};

// R5R and R61 share a timetable line with R5 and R6.
std::string CanonicalLine(std::string_view line);

struct SnapPoint
{
  double m_lat = 0;
  double m_lon = 0;
  bool m_snapped = false;
};

// Prefer a polyline whose ref is this line, within 3 km. Otherwise the nearest
// anonymous rail within 150 m. Otherwise the GPS point.
SnapPoint SnapToTracks(double lat, double lon, std::string_view line, std::vector<RailTrack> const & tracks);

// One uncoloured polyline per requested line. The longest shape wins.
std::vector<RailTrack> StrokeTracks(std::vector<RailTrack> const & tracks, std::vector<std::string> const & lines);
}  // namespace commuter_live
