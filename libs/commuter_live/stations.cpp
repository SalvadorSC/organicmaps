#include "commuter_live/scheme_data.hpp"

#include "geometry/distance_on_sphere.hpp"

namespace commuter_live
{
namespace
{
std::vector<StationStop> const & AllStops()
{
  static std::vector<StationStop> const stops = StationStops();
  return stops;
}
}  // namespace

StationStop const * FindStation(std::string_view id)
{
  if (id.empty())
    return nullptr;
  for (auto const & stop : AllStops())
    if (stop.m_id == id)
      return &stop;
  return nullptr;
}

std::vector<StationStop> StopsWithin(double lat, double lon, double limitM)
{
  std::vector<StationStop> found;
  for (auto const & stop : AllStops())
    if (ms::DistanceOnEarth(ms::LatLon(lat, lon), ms::LatLon(stop.m_lat, stop.m_lon)) <= limitM)
      found.push_back(stop);
  return found;
}
}  // namespace commuter_live
