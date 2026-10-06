#pragma once

#include "bike_share/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bike_share
{
struct DiscoveredFeeds
{
  std::string m_stationInformationUrl;
  std::string m_stationStatusUrl;
  std::string m_vehicleTypesUrl;
  int m_ttlSec = 0;
};

struct StationInformationFeed
{
  std::vector<Station> m_stations;
  int m_ttlSec = 0;
  int64_t m_lastUpdated = 0;
};

struct StationStatusFeed
{
  std::unordered_map<std::string, StationStatus> m_byId;
  int m_ttlSec = 0;
  int64_t m_lastUpdated = 0;
};

// vehicle_type_id -> true when propulsion is electric or electric_assist.
using EbikeByType = std::unordered_map<std::string, bool>;

// GBFS 2.x auto-discovery (language map) and the flat data.feeds shape.
// Prefers the "en" feeds when several languages are published.
std::optional<DiscoveredFeeds> ParseDiscovery(std::string_view json);

std::optional<StationInformationFeed> ParseStationInformation(std::string_view json);
std::optional<StationStatusFeed> ParseStationStatus(std::string_view json);
std::optional<EbikeByType> ParseVehicleTypes(std::string_view json);

// Fills e-bike / mechanical counts from vehicle_types_available when the
// status document did not already provide num_bikes_available_types.
void ApplyVehicleTypeSplit(StationStatusFeed & status, EbikeByType const & isEbikeByType);
}  // namespace bike_share
