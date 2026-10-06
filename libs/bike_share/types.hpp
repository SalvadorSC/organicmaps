#pragma once

#include "geometry/latlon.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bike_share
{
// A dock from station_information. Coordinates are WGS84 degrees.
struct Station
{
  std::string m_id;
  std::string m_name;
  std::string m_shortName;
  ms::LatLon m_point;
};

struct VehicleCount
{
  std::string m_typeId;
  int m_count = 0;
};

// Live counts from station_status. E-bike fields stay empty when the feed
// does not say how the bikes are split.
struct StationStatus
{
  std::string m_id;
  int m_bikesAvailable = 0;
  std::optional<int> m_docksAvailable;
  std::optional<int> m_ebikesAvailable;
  std::optional<int> m_mechanicalAvailable;
  std::vector<VehicleCount> m_vehicleTypes;
  bool m_isInstalled = true;
  bool m_isRenting = true;
  int64_t m_lastReported = 0;
};

// What the place page shows after a successful match.
struct Availability
{
  std::string m_stationName;
  std::string m_feedName;
  int m_bikes = 0;
  std::optional<int> m_ebikes;
  std::optional<int> m_mechanical;
  std::optional<int> m_docks;
  int64_t m_lastUpdated = 0;
};

// OSM feature the user opened. ref is local_ref / station id when the map has one.
struct MatchQuery
{
  ms::LatLon m_point;
  std::string m_name;
  std::string m_ref;
};
}  // namespace bike_share
