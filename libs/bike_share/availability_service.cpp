#include "bike_share/availability_service.hpp"

#include "bike_share/gbfs_parser.hpp"
#include "bike_share/station_matcher.hpp"

#include "platform/http_client.hpp"
#include "platform/settings.hpp"

#include "base/exception.hpp"
#include "base/logging.hpp"

#include <utility>

namespace bike_share
{
namespace
{
// A fixed product name, not a device id. Some publishers reject an empty agent.
std::string_view constexpr kUserAgent = "OrganicMaps";
double constexpr kTimeoutSec = 15.0;
// Publishers that send ttl 0 ask clients not to cache. A short floor still
// avoids refetching when the place page refreshes the same station.
int constexpr kMinCacheSec = 30;

std::optional<std::string> HttpGet(std::string const & url)
{
  try
  {
    platform::HttpClient request(url);
    request.SetTimeout(kTimeoutSec);
    request.SetRawHeader("User-Agent", std::string(kUserAgent));
    request.SetRawHeader("Accept", "application/json");
    std::string body;
    if (!request.RunHttpRequest(body))
    {
      LOG(LINFO, ("Bike-share request failed", url, request.ErrorCode()));
      return std::nullopt;
    }
    return body;
  }
  catch (RootException const & exception)
  {
    LOG(LINFO, ("Bike-share request failed", url, exception.Msg()));
    return std::nullopt;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Bike-share request failed", url, exception.what()));
    return std::nullopt;
  }
}

Availability ToAvailability(StationMatch const & match, int64_t feedLastUpdated)
{
  Availability availability;
  availability.m_stationName = match.m_stationName;
  availability.m_feedName = match.m_feedName;
  availability.m_bikes = match.m_status.m_bikesAvailable;
  availability.m_ebikes = match.m_status.m_ebikesAvailable;
  availability.m_mechanical = match.m_status.m_mechanicalAvailable;
  availability.m_docks = match.m_status.m_docksAvailable;
  availability.m_lastUpdated = match.m_status.m_lastReported != 0 ? match.m_status.m_lastReported : feedLastUpdated;
  return availability;
}

AvailabilityService & SharedService()
{
  static AvailabilityService service(HttpGet, [] { return std::chrono::steady_clock::now(); }, Feeds());
  return service;
}
}  // namespace

AvailabilityService::AvailabilityService(HttpGet httpGet, Clock clock, std::vector<FeedDefinition> feeds)
  : m_httpGet(std::move(httpGet))
  , m_clock(std::move(clock))
  , m_feeds(std::move(feeds))
{}

std::optional<AvailabilityService::Snapshot> AvailabilityService::Load(FeedDefinition const & feed)
{
  auto const now = m_clock();
  if (auto const it = m_cache.find(feed.m_id); it != m_cache.end())
  {
    if (now < it->second.m_expires)
      return it->second;
    m_cache.erase(it);
  }
  if (!m_httpGet)
    return std::nullopt;

  auto const discoveryBody = m_httpGet(feed.m_discoveryUrl);
  if (!discoveryBody)
    return std::nullopt;
  auto const discovered = ParseDiscovery(*discoveryBody);
  if (!discovered)
  {
    LOG(LINFO, ("Bike-share discovery parse failed", feed.m_id));
    return std::nullopt;
  }

  auto const informationBody = m_httpGet(discovered->m_stationInformationUrl);
  auto const statusBody = m_httpGet(discovered->m_stationStatusUrl);
  if (!informationBody || !statusBody)
    return std::nullopt;

  auto information = ParseStationInformation(*informationBody);
  auto status = ParseStationStatus(*statusBody);
  if (!information || !status)
  {
    LOG(LINFO, ("Bike-share station feed parse failed", feed.m_id));
    return std::nullopt;
  }

  if (!discovered->m_vehicleTypesUrl.empty())
  {
    if (auto const typesBody = m_httpGet(discovered->m_vehicleTypesUrl))
    {
      if (auto types = ParseVehicleTypes(*typesBody))
        ApplyVehicleTypeSplit(*status, *types);
    }
  }

  Snapshot snapshot;
  snapshot.m_feedName = feed.m_name;
  snapshot.m_stations = std::move(information->m_stations);
  snapshot.m_statusById = std::move(status->m_byId);
  snapshot.m_feedLastUpdated = status->m_lastUpdated;
  int const ttlSec = status->m_ttlSec > 0 ? status->m_ttlSec : kMinCacheSec;
  snapshot.m_expires = now + std::chrono::seconds(ttlSec);
  m_cache.insert_or_assign(feed.m_id, snapshot);
  return snapshot;
}

std::optional<Availability> AvailabilityService::Lookup(MatchQuery const & query)
{
  if (!query.m_point.IsValid())
    return std::nullopt;

  std::vector<FeedDefinition const *> covering;
  for (auto const & feed : m_feeds)
    if (feed.m_bounds.Contains(query.m_point))
      covering.push_back(&feed);
  if (covering.empty())
    return std::nullopt;

  std::lock_guard<std::mutex> const lock(m_mutex);
  std::vector<Snapshot> snapshots;
  snapshots.reserve(covering.size());
  for (auto const * feed : covering)
  {
    try
    {
      if (auto snapshot = Load(*feed))
        snapshots.push_back(std::move(*snapshot));
    }
    catch (std::exception const & exception)
    {
      LOG(LINFO, ("Bike-share feed failed", feed->m_id, exception.what()));
    }
  }
  if (snapshots.empty())
    return std::nullopt;

  std::vector<FeedStations> views;
  views.reserve(snapshots.size());
  for (auto const & snapshot : snapshots)
  {
    FeedStations view;
    view.m_feedName = snapshot.m_feedName;
    view.m_stations = &snapshot.m_stations;
    view.m_statusById = &snapshot.m_statusById;
    views.push_back(view);
  }

  auto const match = MatchStation(views, query);
  if (!match)
    return std::nullopt;

  int64_t feedUpdated = 0;
  for (auto const & snapshot : snapshots)
  {
    if (snapshot.m_feedName == match->m_feedName)
    {
      feedUpdated = snapshot.m_feedLastUpdated;
      break;
    }
  }
  return ToAvailability(*match, feedUpdated);
}

bool IsAvailabilityEnabled()
{
  return settings::IsEnabled(kAvailabilityEnabledSetting);
}

void SetAvailabilityEnabled(bool enabled)
{
  settings::Set(kAvailabilityEnabledSetting, enabled);
}

std::optional<Availability> LookupAvailability(MatchQuery const & query)
{
  if (!IsAvailabilityEnabled())
    return std::nullopt;
  return SharedService().Lookup(query);
}
}  // namespace bike_share
