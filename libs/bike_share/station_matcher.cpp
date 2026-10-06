#include "bike_share/station_matcher.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/string_utils.hpp"

namespace bike_share
{
namespace
{
std::string CanonicalRef(std::string text)
{
  strings::Trim(text);
  strings::MakeLowerCaseInplace(text);
  if (text.empty())
    return text;

  bool digits = true;
  for (char const ch : text)
    if (ch < '0' || ch > '9')
      digits = false;
  if (!digits)
    return text;

  auto const first = text.find_first_not_of('0');
  if (first == std::string::npos)
    return "0";
  return text.substr(first);
}

std::string NormalizeName(std::string_view text)
{
  std::string lower = strings::ToUtf8(strings::MakeLowerCase(strings::MakeUniString(text)));
  std::string normalized;
  normalized.reserve(lower.size());
  bool pendingSpace = false;
  for (char const ch : lower)
  {
    auto const c = static_cast<unsigned char>(ch);
    bool const separator = c <= 0x20 || c == '-' || c == '_' || c == ',' || c == '.' || c == '/' || c == '\'' ||
                           c == '"' || c == '(' || c == ')' || c == ':';
    if (separator)
    {
      pendingSpace = !normalized.empty();
      continue;
    }
    if (pendingSpace)
    {
      normalized.push_back(' ');
      pendingSpace = false;
    }
    normalized.push_back(ch);
  }
  return normalized;
}

bool NamesMatch(std::string const & query, std::string const & station)
{
  if (query.empty() || station.empty())
    return false;
  if (query == station)
    return true;
  // "Sant Feliu de Llobregat" should hit "RENFE Sant Feliu de Llobregat",
  // but a very short token would match too many docks.
  size_t constexpr kMinLength = 8;
  auto const & shorter = query.size() < station.size() ? query : station;
  auto const & longer = query.size() < station.size() ? station : query;
  return shorter.size() >= kMinLength && longer.find(shorter) != std::string::npos;
}

bool RefsMatch(std::string const & queryRef, Station const & station)
{
  if (queryRef.empty())
    return false;
  return queryRef == CanonicalRef(station.m_id) || queryRef == CanonicalRef(station.m_shortName);
}

struct Scored
{
  Station const * m_station = nullptr;
  StationStatus const * m_status = nullptr;
  std::string m_feedName;
  bool m_ref = false;
  bool m_name = false;
  double m_distanceM = 0;
};

bool Better(Scored const & candidate, Scored const & current)
{
  if (candidate.m_ref != current.m_ref)
    return candidate.m_ref;
  if (candidate.m_name != current.m_name)
    return candidate.m_name;
  return candidate.m_distanceM < current.m_distanceM;
}
}  // namespace

std::optional<StationMatch> MatchStation(std::vector<FeedStations> const & feeds, MatchQuery const & query,
                                         double radiusM)
{
  if (!query.m_point.IsValid() || radiusM <= 0)
    return std::nullopt;

  auto const queryRef = CanonicalRef(query.m_ref);
  auto const queryName = NormalizeName(query.m_name);
  std::optional<Scored> best;

  for (auto const & feed : feeds)
  {
    if (feed.m_stations == nullptr || feed.m_statusById == nullptr)
      continue;
    for (auto const & station : *feed.m_stations)
    {
      auto const statusIt = feed.m_statusById->find(station.m_id);
      if (statusIt == feed.m_statusById->end() || !statusIt->second.m_isInstalled)
        continue;

      double const distanceM = ms::DistanceOnEarth(query.m_point, station.m_point);
      if (distanceM > radiusM)
        continue;

      Scored scored;
      scored.m_station = &station;
      scored.m_status = &statusIt->second;
      scored.m_feedName = feed.m_feedName;
      scored.m_ref = RefsMatch(queryRef, station);
      scored.m_name = NamesMatch(queryName, NormalizeName(station.m_name));
      scored.m_distanceM = distanceM;
      if (!best || Better(scored, *best))
        best = scored;
    }
  }

  if (!best)
    return std::nullopt;

  StationMatch match;
  match.m_stationName = best->m_station->m_name;
  match.m_feedName = best->m_feedName;
  match.m_status = *best->m_status;
  match.m_distanceM = best->m_distanceM;
  return match;
}
}  // namespace bike_share
