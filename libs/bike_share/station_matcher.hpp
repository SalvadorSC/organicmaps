#pragma once

#include "bike_share/types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bike_share
{
// OSM docks and the operator's pin are rarely the same point. 50 m covers
// typical GPS / mapping drift without jumping to the next dock.
double constexpr kMatchRadiusM = 50.0;

struct FeedStations
{
  std::string m_feedName;
  std::vector<Station> const * m_stations = nullptr;
  std::unordered_map<std::string, StationStatus> const * m_statusById = nullptr;
};

struct StationMatch
{
  std::string m_stationName;
  std::string m_feedName;
  StationStatus m_status;
  double m_distanceM = 0;
};

// Nearest installed station within radiusM. An exact ref (station id or
// short_name) wins over a closer dock; a name match wins over a closer
// unnamed one. Returns nullopt when nothing qualifies.
std::optional<StationMatch> MatchStation(std::vector<FeedStations> const & feeds, MatchQuery const & query,
                                         double radiusM = kMatchRadiusM);
}  // namespace bike_share
