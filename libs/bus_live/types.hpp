#pragma once

#include "geometry/latlon.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bus_live
{
// A stop from static GTFS or from the TMB stop catalog.
// m_code is the canonical public code (leading zeros stripped when numeric).
struct TransitStop
{
  std::string m_id;
  std::string m_code;
  std::string m_name;
  ms::LatLon m_point;
};

struct GtfsRoute
{
  std::string m_id;
  std::string m_shortName;
  std::string m_longName;
};

struct GtfsTrip
{
  std::string m_id;
  std::string m_routeId;
  std::string m_headsign;
};

// stops.txt + routes.txt + trips.txt. stop_times.txt is intentionally absent:
// AMB TripUpdates carry absolute times, so the schedule file is not needed.
struct StaticIndex
{
  std::vector<TransitStop> m_stops;
  std::unordered_map<std::string, GtfsRoute> m_routes;
  std::unordered_map<std::string, GtfsTrip> m_trips;
};

struct Arrival
{
  std::string m_line;
  std::string m_destination;
  int64_t m_etaUnixSec = 0;
  bool m_fromTmb = false;
};

enum class LookupStatus : uint8_t
{
  // Outside the Barcelona service area, or the feature is not a lookup target.
  NotApplicable,
  // Inside the area, but no stop matched or the feeds had nothing to show.
  NoData,
  Ok
};

struct ArrivalList
{
  LookupStatus m_status = LookupStatus::NotApplicable;
  std::vector<Arrival> m_arrivals;
  int64_t m_updatedUnixSec = 0;
};

// OSM feature the user opened. m_ref is local_ref when the map has one.
// Generic OSM ref is not stored in mwm files; see stop_matcher.hpp.
struct StopQuery
{
  ms::LatLon m_point;
  std::string m_name;
  std::string m_ref;
};

// TMB developer credentials the user pasted. Never log these.
struct Credentials
{
  std::string m_appId;
  std::string m_appKey;

  bool Empty() const { return m_appId.empty() || m_appKey.empty(); }
};
}  // namespace bus_live
