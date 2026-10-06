#include "metro_live/estimator.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/string_utils.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace metro_live
{
namespace
{
int64_t constexpr kMinSegmentSec = 40;
int64_t constexpr kMaxSegmentSec = 200;
double constexpr kSnapLimitM = 500.0;
double constexpr kMetersPerDeg = 111320.0;

std::string Normalize(std::string_view text)
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

struct LineRank
{
  int m_number = 1000;
  std::string m_suffix;
};

LineRank RankOf(std::string const & name)
{
  LineRank rank;
  size_t i = 0;
  if (!name.empty() && (name[0] == 'L' || name[0] == 'l'))
    i = 1;
  int number = 0;
  bool any = false;
  while (i < name.size() && name[i] >= '0' && name[i] <= '9')
  {
    any = true;
    number = number * 10 + (name[i] - '0');
    ++i;
  }
  if (any)
    rank.m_number = number;
  rank.m_suffix = name.substr(i);
  return rank;
}

bool EarlierLine(std::string const & a, std::string const & b)
{
  auto const left = RankOf(a);
  auto const right = RankOf(b);
  if (left.m_number != right.m_number)
    return left.m_number < right.m_number;
  if (left.m_suffix != right.m_suffix)
    return left.m_suffix < right.m_suffix;
  return a < b;
}

void BuildAlong(IndexedLine & line)
{
  line.m_alongM.assign(line.m_shape.size(), 0);
  for (size_t i = 1; i < line.m_shape.size(); ++i)
    line.m_alongM[i] = line.m_alongM[i - 1] + ms::DistanceOnEarth(line.m_shape[i - 1], line.m_shape[i]);
}

double SnapAlong(IndexedLine const & line, ms::LatLon const & point)
{
  if (line.m_shape.size() < 2)
    return -1;
  double best = kSnapLimitM;
  double along = -1;
  double constexpr kDegToRad = 0.017453292519943295;
  double const latRad = point.m_lat * kDegToRad;
  double const cosLat = std::cos(latRad);
  for (size_t i = 0; i + 1 < line.m_shape.size(); ++i)
  {
    auto const & a = line.m_shape[i];
    auto const & b = line.m_shape[i + 1];
    double const bx = (b.m_lon - a.m_lon) * kMetersPerDeg * cosLat;
    double const by = (b.m_lat - a.m_lat) * kMetersPerDeg;
    double const px = (point.m_lon - a.m_lon) * kMetersPerDeg * cosLat;
    double const py = (point.m_lat - a.m_lat) * kMetersPerDeg;
    double const len2 = bx * bx + by * by;
    double t = 0;
    if (len2 > 1.0)
      t = std::clamp((px * bx + py * by) / len2, 0.0, 1.0);
    double const dx = px - t * bx;
    double const dy = py - t * by;
    double const dist = std::sqrt(dx * dx + dy * dy);
    if (dist < best)
    {
      best = dist;
      along = line.m_alongM[i] + t * (line.m_alongM[i + 1] - line.m_alongM[i]);
    }
  }
  return along;
}

ms::LatLon PointAt(IndexedLine const & line, double along)
{
  if (line.m_shape.empty())
    return {};
  if (line.m_shape.size() == 1 || along <= line.m_alongM.front())
    return line.m_shape.front();
  if (along >= line.m_alongM.back())
    return line.m_shape.back();
  auto const it = std::lower_bound(line.m_alongM.begin(), line.m_alongM.end(), along);
  size_t const i = static_cast<size_t>(it - line.m_alongM.begin());
  if (i == 0)
    return line.m_shape.front();
  double const span = line.m_alongM[i] - line.m_alongM[i - 1];
  double const t = span <= 1.0 ? 0 : (along - line.m_alongM[i - 1]) / span;
  auto const & a = line.m_shape[i - 1];
  auto const & b = line.m_shape[i];
  return ms::LatLon(a.m_lat + (b.m_lat - a.m_lat) * t, a.m_lon + (b.m_lon - a.m_lon) * t);
}

ms::LatLon Between(IndexedLine const & line, IndexedStation const & from, IndexedStation const & to, double progress)
{
  progress = std::clamp(progress, 0.0, 1.0);
  if (from.m_alongM >= 0 && to.m_alongM >= 0 && line.m_shape.size() >= 2)
    return PointAt(line, from.m_alongM + (to.m_alongM - from.m_alongM) * progress);
  auto const & a = from.m_station.m_point;
  auto const & b = to.m_station.m_point;
  return ms::LatLon(a.m_lat + (b.m_lat - a.m_lat) * progress, a.m_lon + (b.m_lon - a.m_lon) * progress);
}

int DirectionFromDestination(IndexedLine const & line, std::string const & destination)
{
  if (line.m_stations.size() < 2)
    return 0;
  auto const dest = Normalize(destination);
  auto const first = Normalize(line.m_stations.front().m_station.m_name);
  auto const last = Normalize(line.m_stations.back().m_station.m_name);
  if (NamesMatch(dest, last) && !NamesMatch(dest, first))
    return 1;
  if (NamesMatch(dest, first) && !NamesMatch(dest, last))
    return -1;
  return 0;
}

struct Hit
{
  int m_index = 0;
  int64_t m_eta = 0;
};

int64_t SegmentSeconds(std::vector<Hit> const & chain)
{
  if (chain.size() < 2)
    return kTypicalSegmentSec;
  int64_t const gap = chain[1].m_eta - chain[0].m_eta;
  if (gap < kMinSegmentSec || gap > kMaxSegmentSec)
    return kTypicalSegmentSec;
  return gap;
}
}  // namespace

Network BuildNetwork(std::vector<LinePath> const & lines, std::vector<Station> const & stations)
{
  std::unordered_map<std::string, IndexedLine> byName;
  for (auto const & path : lines)
  {
    if (path.m_name.empty())
      continue;
    IndexedLine & line = byName[path.m_name];
    line.m_name = path.m_name;
    if (!path.m_color.empty())
      line.m_color = path.m_color;
    if (line.m_shape.empty())
      line.m_shape = path.m_shape;
  }
  for (auto const & station : stations)
  {
    if (station.m_line.empty() || !station.m_point.IsValid())
      continue;
    // Stations whose line was rejected (funicular) are not placed on a metro shape.
    if (!lines.empty() && byName.find(station.m_line) == byName.end())
      continue;
    IndexedLine & line = byName[station.m_line];
    line.m_name = station.m_line;
    if (line.m_color.empty())
      line.m_color = station.m_color;
    IndexedStation indexed;
    indexed.m_station = station;
    line.m_stations.push_back(std::move(indexed));
  }

  Network network;
  for (auto & [name, line] : byName)
  {
    std::sort(line.m_stations.begin(), line.m_stations.end(), [](IndexedStation const & a, IndexedStation const & b)
    {
      if (a.m_station.m_order != b.m_station.m_order)
        return a.m_station.m_order < b.m_station.m_order;
      return a.m_station.m_code < b.m_station.m_code;
    });
    line.m_stations.erase(std::unique(line.m_stations.begin(), line.m_stations.end(),
                                      [](IndexedStation const & a, IndexedStation const & b)
    { return a.m_station.m_code == b.m_station.m_code; }),
                          line.m_stations.end());
    if (line.m_stations.empty())
      continue;
    if (line.m_shape.size() < 2)
    {
      line.m_shape.clear();
      for (auto const & station : line.m_stations)
        line.m_shape.push_back(station.m_station.m_point);
    }
    BuildAlong(line);
    for (auto & station : line.m_stations)
      station.m_alongM = SnapAlong(line, station.m_station.m_point);
    network.m_lines.push_back(std::move(line));
    (void)name;
  }
  std::sort(network.m_lines.begin(), network.m_lines.end(),
            [](IndexedLine const & a, IndexedLine const & b) { return EarlierLine(a.m_name, b.m_name); });
  return network;
}

std::vector<TrainEstimate> EstimateTrains(Network const & network, std::vector<ArrivalObs> const & rows,
                                          int64_t nowUnix)
{
  std::unordered_map<std::string, IndexedLine const *> lines;
  for (auto const & line : network.m_lines)
    lines.emplace(line.m_name, &line);

  std::unordered_map<std::string, std::vector<ArrivalObs const *>> groups;
  for (auto const & row : rows)
  {
    if (row.m_service.empty() || row.m_line.empty() || row.m_etaUnixSec < nowUnix - kStaleSec)
      continue;
    groups[row.m_line + "\n" + row.m_trajecte + "\n" + row.m_service].push_back(&row);
  }

  std::vector<TrainEstimate> trains;
  for (auto & [key, group] : groups)
  {
    std::sort(group.begin(), group.end(), [](ArrivalObs const * a, ArrivalObs const * b)
    {
      if (a->m_etaUnixSec != b->m_etaUnixSec)
        return a->m_etaUnixSec < b->m_etaUnixSec;
      return a->m_station < b->m_station;
    });
    ArrivalObs const & first = *group.front();
    auto const lineIt = lines.find(first.m_line);
    if (lineIt == lines.end())
      continue;
    IndexedLine const & line = *lineIt->second;

    std::unordered_map<int, int> indexByCode;
    for (size_t i = 0; i < line.m_stations.size(); ++i)
      indexByCode.emplace(line.m_stations[i].m_station.m_code, static_cast<int>(i));

    std::vector<Hit> hits;
    for (auto const * row : group)
    {
      auto const found = indexByCode.find(row->m_station);
      if (found == indexByCode.end())
        continue;
      if (!hits.empty() && hits.back().m_index == found->second)
        continue;
      hits.push_back(Hit{found->second, row->m_etaUnixSec});
    }
    if (hits.empty())
      continue;

    int direction = 0;
    for (size_t i = 1; i < hits.size(); ++i)
    {
      if (hits[i].m_index == hits[0].m_index)
        continue;
      direction = hits[i].m_index > hits[0].m_index ? 1 : -1;
      break;
    }
    if (direction == 0)
      direction = DirectionFromDestination(line, first.m_destination);

    std::vector<Hit> chain;
    chain.push_back(hits.front());
    if (direction != 0)
    {
      for (size_t i = 1; i < hits.size(); ++i)
      {
        int const step = (hits[i].m_index - chain.back().m_index) * direction;
        if (step > 0)
          chain.push_back(hits[i]);
        else if (step < 0)
          break;
      }
    }

    int const nextIndex = chain.front().m_index;
    int const previousIndex = direction == 0 ? -1 : nextIndex - direction;
    bool const onPlatform = previousIndex < 0 || previousIndex >= static_cast<int>(line.m_stations.size());
    double progress = 1;
    if (!onPlatform)
    {
      int64_t const remaining = chain.front().m_eta - nowUnix;
      if (remaining <= 0)
        progress = 1;
      else
        progress = 1.0 - std::min(1.0, static_cast<double>(remaining) / static_cast<double>(SegmentSeconds(chain)));
    }

    TrainEstimate train;
    train.m_line = line.m_name;
    train.m_color = first.m_color.empty() ? line.m_color : first.m_color;
    train.m_destination = first.m_destination;
    train.m_nextStop = line.m_stations[static_cast<size_t>(nextIndex)].m_station.m_name;
    train.m_key = key;
    ms::LatLon point = line.m_stations[static_cast<size_t>(nextIndex)].m_station.m_point;
    if (!onPlatform)
      point = Between(line, line.m_stations[static_cast<size_t>(previousIndex)],
                      line.m_stations[static_cast<size_t>(nextIndex)], progress);
    train.m_lat = point.m_lat;
    train.m_lon = point.m_lon;
    trains.push_back(std::move(train));
    (void)key;
  }

  std::sort(trains.begin(), trains.end(), [](TrainEstimate const & a, TrainEstimate const & b)
  {
    if (a.m_line != b.m_line)
      return EarlierLine(a.m_line, b.m_line);
    return a.m_key < b.m_key;
  });
  return trains;
}
}  // namespace metro_live
