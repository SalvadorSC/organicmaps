#include "bus_live/arrivals.hpp"

#include "bus_live/stop_matcher.hpp"

#include <algorithm>
#include <cstdlib>

namespace bus_live
{
namespace
{
bool SameStop(std::string const & rtStopId, TransitStop const & stop)
{
  auto const code = CanonicalCode(rtStopId);
  if (code.empty())
    return false;
  if (!stop.m_code.empty() && code == stop.m_code)
    return true;
  return code == CanonicalCode(stop.m_id);
}

std::optional<int64_t> EventTime(StopTimeUpdate const & update)
{
  if (update.m_arrival.m_time)
    return update.m_arrival.m_time;
  return update.m_departure.m_time;
}

std::string NormLine(std::string const & line)
{
  std::string out;
  out.reserve(line.size());
  for (unsigned char const ch : line)
  {
    if (ch == ' ' || ch == '\t')
      continue;
    if (ch >= 'a' && ch <= 'z')
      out.push_back(static_cast<char>(ch - 'a' + 'A'));
    else
      out.push_back(static_cast<char>(ch));
  }
  return out;
}

void SortAndCap(std::vector<Arrival> & arrivals)
{
  std::sort(arrivals.begin(), arrivals.end(), [](Arrival const & a, Arrival const & b)
  {
    if (a.m_etaUnixSec != b.m_etaUnixSec)
      return a.m_etaUnixSec < b.m_etaUnixSec;
    return a.m_line < b.m_line;
  });
  if (arrivals.size() > kMaxArrivals)
    arrivals.resize(kMaxArrivals);
}
}  // namespace

std::vector<Arrival> CollectAmbArrivals(Feed const & feed, StaticIndex const & index, TransitStop const & stop,
                                        int64_t nowUnix)
{
  std::vector<Arrival> arrivals;
  for (auto const & update : feed.m_updates)
  {
    if (update.m_trip.m_schedule == kTripCanceled)
      continue;

    std::string routeId = update.m_trip.m_routeId;
    std::string destination;
    if (auto const trip = index.m_trips.find(update.m_trip.m_tripId); trip != index.m_trips.end())
    {
      if (routeId.empty())
        routeId = trip->second.m_routeId;
      destination = trip->second.m_headsign;
    }
    std::string line = routeId;
    if (auto const route = index.m_routes.find(routeId); route != index.m_routes.end())
    {
      if (!route->second.m_shortName.empty())
        line = route->second.m_shortName;
    }
    if (line.empty())
      continue;

    for (auto const & stopTime : update.m_stops)
    {
      if (stopTime.m_schedule == kStopSkipped || !SameStop(stopTime.m_stopId, stop))
        continue;
      auto const when = EventTime(stopTime);
      if (!when || *when < nowUnix - kStaleSec)
        continue;
      Arrival arrival;
      arrival.m_line = line;
      arrival.m_destination = destination;
      arrival.m_etaUnixSec = *when;
      arrivals.push_back(std::move(arrival));
    }
  }
  return arrivals;
}

std::vector<Arrival> MergeArrivals(std::vector<Arrival> amb, std::vector<Arrival> tmb)
{
  std::vector<Arrival> kept;
  kept.reserve(amb.size() + tmb.size());
  for (auto & arrival : amb)
  {
    bool duplicated = false;
    auto const line = NormLine(arrival.m_line);
    for (auto const & city : tmb)
    {
      if (line == NormLine(city.m_line) && std::llabs(arrival.m_etaUnixSec - city.m_etaUnixSec) <= kDedupeSec)
      {
        duplicated = true;
        break;
      }
    }
    if (!duplicated)
      kept.push_back(std::move(arrival));
  }
  for (auto & arrival : tmb)
    kept.push_back(std::move(arrival));
  SortAndCap(kept);
  return kept;
}
}  // namespace bus_live
