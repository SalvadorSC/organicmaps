#include "commuter_live/service.hpp"

#include "commuter_live/names.hpp"
#include "commuter_live/parser.hpp"
#include "commuter_live/scheme_data.hpp"
#include "commuter_live/snap.hpp"

#include "platform/http_client.hpp"
#include "platform/settings.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/exception.hpp"
#include "base/logging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>

namespace commuter_live
{
namespace
{
double constexpr kTimeoutSec = 20.0;
std::string_view constexpr kUserAgent = "OrganicMaps";
std::chrono::seconds constexpr kLiveTtl{20};
std::chrono::seconds constexpr kRetryTtl{30};
int64_t constexpr kFgcStopSec = 120;
double constexpr kStationMatchM = 180.0;
double constexpr kNameMatchM = 700.0;
size_t constexpr kMaxArrivals = 8;

std::optional<std::string> ProductionGet(std::string const & url)
{
  try
  {
    platform::HttpClient request(url);
    request.SetTimeout(kTimeoutSec);
    request.SetRawHeader("User-Agent", std::string(kUserAgent));
    std::string body;
    if (!request.RunHttpRequest(body))
    {
      LOG(LINFO, ("Commuter request failed", url, request.ErrorCode()));
      return std::nullopt;
    }
    return body;
  }
  catch (RootException const & exception)
  {
    LOG(LINFO, ("Commuter request failed", url, exception.Msg()));
    return std::nullopt;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Commuter request failed", url, exception.what()));
    return std::nullopt;
  }
}

CommuterService & SharedService()
{
  static CommuterService service([](std::string const & url) { return ProductionGet(url); },
                                 [] { return std::chrono::steady_clock::now(); });
  return service;
}

std::vector<Train> Remembered(std::vector<Train> const & last, std::unordered_map<std::string, Fix> & nextFixes,
                              std::unordered_map<std::string, Fix> const & previous)
{
  std::vector<Train> trains;
  trains.reserve(last.size());
  for (Train train : last)
  {
    auto const it = previous.find(train.m_id);
    Fix const * prior = it == previous.end() ? nullptr : &it->second;
    auto const heading = HeadingFromMotion(prior, train.m_lat, train.m_lon, std::nullopt);
    train.m_directional = heading.has_value();
    train.m_headingDeg = heading.value_or(0);
    Fix fix;
    fix.m_lat = train.m_lat;
    fix.m_lon = train.m_lon;
    fix.m_headingDeg = heading;
    nextFixes.insert_or_assign(train.m_id, fix);
    trains.push_back(std::move(train));
  }
  return trains;
}
}  // namespace

CommuterService::CommuterService(HttpGet httpGet, SteadyClock steady)
  : m_httpGet(std::move(httpGet))
  , m_steady(std::move(steady))
{}

std::string FoldName(std::string_view text)
{
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();)
  {
    auto const c = static_cast<unsigned char>(text[i]);
    if (c < 0x80)
    {
      if (std::isalnum(c))
        out.push_back(static_cast<char>(std::tolower(c)));
      ++i;
      continue;
    }
    if (i + 1 < text.size() && c == 0xC3)
    {
      auto const next = static_cast<unsigned char>(text[i + 1]);
      char ascii = 0;
      switch (next)
      {
      case 0x80:
      case 0x81:
      case 0x82:
      case 0x83:
      case 0x84:
      case 0x85:
      case 0xA0:
      case 0xA1:
      case 0xA2:
      case 0xA3:
      case 0xA4:
      case 0xA5: ascii = 'a'; break;
      case 0x87:
      case 0xA7: ascii = 'c'; break;
      case 0x88:
      case 0x89:
      case 0x8A:
      case 0x8B:
      case 0xA8:
      case 0xA9:
      case 0xAA:
      case 0xAB: ascii = 'e'; break;
      case 0x8C:
      case 0x8D:
      case 0x8E:
      case 0x8F:
      case 0xAC:
      case 0xAD:
      case 0xAE:
      case 0xAF: ascii = 'i'; break;
      case 0x91:
      case 0xB1: ascii = 'n'; break;
      case 0x92:
      case 0x93:
      case 0x94:
      case 0x95:
      case 0x96:
      case 0xB2:
      case 0xB3:
      case 0xB4:
      case 0xB5:
      case 0xB6: ascii = 'o'; break;
      case 0x99:
      case 0x9A:
      case 0x9B:
      case 0x9C:
      case 0xB9:
      case 0xBA:
      case 0xBB:
      case 0xBC: ascii = 'u'; break;
      default: break;
      }
      if (ascii != 0)
        out.push_back(ascii);
      i += 2;
      continue;
    }
    ++i;
  }
  return out;
}

bool NamesMatch(std::string const & place, std::string_view stop)
{
  if (place.size() < 4 || stop.size() < 4)
    return false;
  std::string const foldedStop = FoldName(stop);
  if (foldedStop.size() < 4)
    return false;
  return place.find(foldedStop) != std::string::npos || foldedStop.find(place) != std::string::npos;
}

std::string StopTitle(std::string_view id)
{
  if (auto const * stop = FindStation(id))
    return std::string(stop->m_name);
  return StationName(id);
}

std::vector<Train> CommuterService::Build(std::vector<RawTrain> const & rows, std::string_view source,
                                          std::unordered_map<std::string, Fix> & nextFixes) const
{
  std::vector<Train> trains;
  trains.reserve(rows.size());
  for (auto const & row : rows)
  {
    if (row.m_line.empty() || !InBarcelona(row.m_lat, row.m_lon))
      continue;
    std::string const id = std::string(source) + ":" + row.m_id;
    auto const it = m_fixes.find(id);
    Fix const * prior = it == m_fixes.end() ? nullptr : &it->second;
    auto const heading = HeadingFromMotion(prior, row.m_lat, row.m_lon, row.m_bearingDeg);
    auto const snapped = SnapToTracks(row.m_lat, row.m_lon, row.m_line, m_tracks);
    Train train;
    train.m_id = id;
    train.m_line = row.m_line;
    train.m_color = LineColor(row.m_line);
    train.m_lat = snapped.m_lat;
    train.m_lon = snapped.m_lon;
    train.m_directional = heading.has_value();
    train.m_headingDeg = heading.value_or(0);
    train.m_destination = StationName(row.m_destination);
    train.m_nextStop = StationName(row.m_nextStop);
    train.m_key = id;
    train.m_upcoming = row.m_upcoming;
    train.m_parkedAt = row.m_parkedAt;
    Fix fix;
    fix.m_lat = row.m_lat;
    fix.m_lon = row.m_lon;
    fix.m_headingDeg = heading;
    nextFixes.insert_or_assign(id, fix);
    trains.push_back(std::move(train));
  }
  return trains;
}

void CommuterService::RefreshUnlocked()
{
  auto const now = m_steady ? m_steady() : std::chrono::steady_clock::now();
  std::optional<std::string> fgcBody;
  std::optional<std::string> renfeBody;
  std::optional<std::string> tripBody;
  if (m_httpGet)
  {
    fgcBody = m_httpGet(std::string(kGeotrenUrl));
    renfeBody = m_httpGet(std::string(kRenfeVehiclesUrl));
    tripBody = m_httpGet(std::string(kRenfeTripsUrl));
  }

  std::optional<std::vector<RawTrain>> fgcRows;
  std::optional<std::vector<RawTrain>> renfeRows;
  if (fgcBody)
    fgcRows = ParseGeotren(*fgcBody);
  if (renfeBody)
    renfeRows = ParseVehiclePositions(*renfeBody);
  if (tripBody)
  {
    if (auto trips = ParseTripUpdates(*tripBody))
    {
      m_trips = std::move(*trips);
      m_hasTrips = true;
    }
  }

  bool const fgcOk = fgcRows.has_value();
  bool const renfeOk = renfeRows.has_value();
  if (!fgcOk && !renfeOk)
  {
    if (m_has)
      m_expires = now + kRetryTtl;
    return;
  }

  std::unordered_map<std::string, Fix> nextFixes;
  std::vector<Train> trains;
  if (fgcOk)
  {
    m_lastFgc = Build(*fgcRows, "fgc", nextFixes);
    m_hasFgc = true;
  }
  else if (m_hasFgc)
    m_lastFgc = Remembered(m_lastFgc, nextFixes, m_fixes);
  if (renfeOk)
  {
    m_lastRenfe = Build(*renfeRows, "rodalies", nextFixes);
    m_hasRenfe = true;
  }
  else if (m_hasRenfe)
    m_lastRenfe = Remembered(m_lastRenfe, nextFixes, m_fixes);

  trains.reserve(m_lastFgc.size() + m_lastRenfe.size());
  trains.insert(trains.end(), m_lastFgc.begin(), m_lastFgc.end());
  trains.insert(trains.end(), m_lastRenfe.begin(), m_lastRenfe.end());
  m_fixes = std::move(nextFixes);
  m_trains = std::move(trains);
  m_has = true;
  m_expires = now + (fgcOk && renfeOk ? kLiveTtl : kRetryTtl);
}

PollResult CommuterService::Poll(std::vector<RailTrack> const & tracks)
{
  std::lock_guard<std::mutex> const lock(m_mutex);
  m_tracks = tracks;
  auto const now = m_steady ? m_steady() : std::chrono::steady_clock::now();
  if (!(m_has && now < m_expires))
    RefreshUnlocked();
  PollResult result;
  result.m_enabled = true;
  result.m_trains = m_trains;
  return result;
}

ArrivalList CommuterService::LookupArrivals(double lat, double lon, std::string const & name)
{
  std::lock_guard<std::mutex> const lock(m_mutex);
  auto const nowSteady = m_steady ? m_steady() : std::chrono::steady_clock::now();
  if (!m_has || nowSteady >= m_expires)
    RefreshUnlocked();

  auto stops = StopsWithin(lat, lon, kStationMatchM);
  std::string const folded = FoldName(name);
  if (!folded.empty())
  {
    for (auto const & stop : StationStops())
    {
      if (ms::DistanceOnEarth(ms::LatLon(lat, lon), ms::LatLon(stop.m_lat, stop.m_lon)) > kNameMatchM)
        continue;
      if (!NamesMatch(folded, stop.m_name) && !NamesMatch(folded, stop.m_id))
        continue;
      bool const already = std::any_of(stops.begin(), stops.end(), [&](StationStop const & known)
      { return known.m_id == stop.m_id && known.m_fgc == stop.m_fgc; });
      if (!already)
        stops.push_back(stop);
    }
  }
  if (stops.empty())
    return {};

  int64_t const nowUnix =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::vector<Arrival> arrivals;
  for (auto const & stop : stops)
  {
    if (stop.m_fgc)
    {
      for (auto const & train : m_trains)
      {
        if (train.m_id.rfind("fgc:", 0) != 0)
          continue;
        if (train.m_parkedAt == stop.m_id)
        {
          Arrival arrival;
          arrival.m_line = train.m_line;
          arrival.m_destination = train.m_destination;
          arrival.m_etaUnixSec = nowUnix;
          arrivals.push_back(std::move(arrival));
          continue;
        }
        auto const it = std::find(train.m_upcoming.begin(), train.m_upcoming.end(), stop.m_id);
        if (it == train.m_upcoming.end())
          continue;
        auto const index = static_cast<int64_t>(it - train.m_upcoming.begin());
        Arrival arrival;
        arrival.m_line = train.m_line;
        arrival.m_destination = train.m_destination;
        arrival.m_etaUnixSec = nowUnix + (index + 1) * kFgcStopSec;
        arrivals.push_back(std::move(arrival));
      }
    }
    else
    {
      for (auto const & trip : m_trips)
      {
        auto const visit = std::find_if(trip.m_stops.begin(), trip.m_stops.end(), [&](StopVisit const & item)
        { return item.m_stopId == stop.m_id && item.m_etaUnixSec >= nowUnix - 30; });
        if (visit == trip.m_stops.end())
          continue;
        Arrival arrival;
        arrival.m_line = trip.m_line;
        arrival.m_destination = StopTitle(trip.m_stops.back().m_stopId);
        arrival.m_etaUnixSec = visit->m_etaUnixSec;
        arrivals.push_back(std::move(arrival));
      }
    }
  }

  std::sort(arrivals.begin(), arrivals.end(), [](Arrival const & a, Arrival const & b)
  {
    if (a.m_etaUnixSec != b.m_etaUnixSec)
      return a.m_etaUnixSec < b.m_etaUnixSec;
    return a.m_line < b.m_line;
  });
  if (arrivals.size() > kMaxArrivals)
    arrivals.resize(kMaxArrivals);
  ArrivalList list;
  list.m_status = arrivals.empty() ? ArrivalStatus::NoData : ArrivalStatus::Ok;
  list.m_arrivals = std::move(arrivals);
  return list;
}

bool IsCommuterLiveEnabled()
{
  return settings::IsEnabled(kCommuterLiveEnabledSetting);
}

void SetCommuterLiveEnabled(bool enabled)
{
  settings::Set(kCommuterLiveEnabledSetting, enabled);
}

PollResult PollCommuter(std::vector<RailTrack> const & tracks)
{
  if (!IsCommuterLiveEnabled())
    return {};
  return SharedService().Poll(tracks);
}

ArrivalList LookupCommuterArrivals(double lat, double lon, std::string const & name)
{
  if (!IsCommuterLiveEnabled())
    return {};
  return SharedService().LookupArrivals(lat, lon, name);
}
}  // namespace commuter_live
