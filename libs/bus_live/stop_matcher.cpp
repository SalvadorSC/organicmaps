#include "bus_live/stop_matcher.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/string_utils.hpp"

namespace bus_live
{
namespace
{
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

bool NamesMatch(std::string const & query, std::string const & stop)
{
  if (query.empty() || stop.empty())
    return false;
  if (query == stop)
    return true;
  size_t constexpr kMinLength = 8;
  auto const & shorter = query.size() < stop.size() ? query : stop;
  auto const & longer = query.size() < stop.size() ? stop : query;
  return shorter.size() >= kMinLength && longer.find(shorter) != std::string::npos;
}

bool CodesMatch(std::string const & queryCode, TransitStop const & stop)
{
  if (queryCode.empty())
    return false;
  if (!stop.m_code.empty() && queryCode == stop.m_code)
    return true;
  return queryCode == CanonicalCode(stop.m_id);
}

struct Scored
{
  TransitStop const * m_stop = nullptr;
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

std::string CanonicalCode(std::string text)
{
  strings::Trim(text);
  strings::MakeLowerCaseInplace(text);
  if (text.empty())
    return text;

  bool digits = true;
  for (unsigned char const ch : text)
    if (ch < '0' || ch > '9')
      digits = false;
  if (!digits)
    return text;

  auto const first = text.find_first_not_of('0');
  if (first == std::string::npos)
    return "0";
  return text.substr(first);
}

std::optional<StopMatch> MatchStop(std::vector<TransitStop> const & stops, StopQuery const & query)
{
  if (!query.m_point.IsValid())
    return std::nullopt;

  auto const queryCode = CanonicalCode(query.m_ref);
  auto const queryName = NormalizeName(query.m_name);
  std::optional<Scored> best;

  for (auto const & stop : stops)
  {
    if (!stop.m_point.IsValid())
      continue;
    double const distanceM = ms::DistanceOnEarth(query.m_point, stop.m_point);
    bool const ref = CodesMatch(queryCode, stop) && distanceM <= kRefRadiusM;
    bool const geo = distanceM <= kMatchRadiusM;
    if (!ref && !geo)
      continue;

    Scored scored;
    scored.m_stop = &stop;
    scored.m_ref = ref;
    scored.m_name = geo && NamesMatch(queryName, NormalizeName(stop.m_name));
    scored.m_distanceM = distanceM;
    if (!best || Better(scored, *best))
      best = scored;
  }

  if (!best)
    return std::nullopt;

  StopMatch match;
  match.m_stop = *best->m_stop;
  match.m_ref = best->m_ref;
  match.m_name = best->m_name;
  match.m_distanceM = best->m_distanceM;
  return match;
}
}  // namespace bus_live
