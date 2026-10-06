#include "commuter_live/service.hpp"

#include "commuter_live/names.hpp"
#include "commuter_live/parser.hpp"

#include "platform/http_client.hpp"
#include "platform/settings.hpp"

#include "base/exception.hpp"
#include "base/logging.hpp"

#include <utility>

namespace commuter_live
{
namespace
{
double constexpr kTimeoutSec = 20.0;
std::string_view constexpr kUserAgent = "OrganicMaps";
std::chrono::seconds constexpr kLiveTtl{20};
std::chrono::seconds constexpr kRetryTtl{30};

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
    Train train;
    train.m_id = id;
    train.m_line = row.m_line;
    train.m_color = LineColor(row.m_line);
    train.m_lat = row.m_lat;
    train.m_lon = row.m_lon;
    train.m_directional = heading.has_value();
    train.m_headingDeg = heading.value_or(0);
    train.m_destination = StationName(row.m_destination);
    train.m_nextStop = StationName(row.m_nextStop);
    train.m_key = id;
    Fix fix;
    fix.m_lat = row.m_lat;
    fix.m_lon = row.m_lon;
    fix.m_headingDeg = heading;
    nextFixes.insert_or_assign(id, fix);
    trains.push_back(std::move(train));
  }
  return trains;
}

PollResult CommuterService::Poll()
{
  std::lock_guard<std::mutex> const lock(m_mutex);
  auto const now = m_steady ? m_steady() : std::chrono::steady_clock::now();
  if (m_has && now < m_expires)
  {
    PollResult cached;
    cached.m_enabled = true;
    cached.m_trains = m_trains;
    return cached;
  }

  std::optional<std::string> fgcBody;
  std::optional<std::string> renfeBody;
  if (m_httpGet)
  {
    fgcBody = m_httpGet(std::string(kGeotrenUrl));
    renfeBody = m_httpGet(std::string(kRenfeVehiclesUrl));
  }

  std::optional<std::vector<RawTrain>> fgcRows;
  std::optional<std::vector<RawTrain>> renfeRows;
  if (fgcBody)
    fgcRows = ParseGeotren(*fgcBody);
  if (renfeBody)
    renfeRows = ParseVehiclePositions(*renfeBody);

  bool const fgcOk = fgcRows.has_value();
  bool const renfeOk = renfeRows.has_value();
  if (!fgcOk && !renfeOk)
  {
    if (m_has)
      m_expires = now + kRetryTtl;
    PollResult result;
    result.m_enabled = true;
    result.m_trains = m_trains;
    return result;
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

  PollResult result;
  result.m_enabled = true;
  result.m_trains = m_trains;
  return result;
}

bool IsCommuterLiveEnabled()
{
  return settings::IsEnabled(kCommuterLiveEnabledSetting);
}

void SetCommuterLiveEnabled(bool enabled)
{
  settings::Set(kCommuterLiveEnabledSetting, enabled);
}

PollResult PollCommuter()
{
  if (!IsCommuterLiveEnabled())
    return {};
  return SharedService().Poll();
}
}  // namespace commuter_live
