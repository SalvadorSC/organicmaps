#pragma once

#include "geometry/latlon.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace metro_live
{
// One stop on one line. L9N and L9S are separate lines, matching the TMB catalog.
struct Station
{
  int m_code = 0;
  int m_order = 0;
  std::string m_line;
  std::string m_name;
  std::string m_color;
  ms::LatLon m_point;
};

// Polyline in travel order. Points are WGS84. Empty when the line file had no geometry.
struct LinePath
{
  std::string m_name;
  std::string m_color;
  std::vector<ms::LatLon> m_shape;
};

// One upcoming stop of one train, from itransit/metro/estacions.
struct ArrivalObs
{
  std::string m_line;
  std::string m_color;
  std::string m_trajecte;
  std::string m_destination;
  std::string m_service;
  int m_station = 0;
  int64_t m_etaUnixSec = 0;
};

struct TrainEstimate
{
  std::string m_line;
  std::string m_color;
  std::string m_destination;
  std::string m_nextStop;
  // line + trajecte + service. Stable while the same run is in the feed.
  std::string m_key;
  double m_lat = 0;
  double m_lon = 0;
};

struct LineSummary
{
  std::string m_name;
  std::string m_color;
};

struct Credentials
{
  std::string m_appId;
  std::string m_appKey;

  bool Empty() const { return m_appId.empty() || m_appKey.empty(); }
};

struct PollResult
{
  bool m_enabled = false;
  bool m_needsKey = false;
  std::vector<LineSummary> m_lines;
  std::vector<TrainEstimate> m_trains;
};
}  // namespace metro_live
