#include "metro_live/estimator.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/string_utils.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

double SnapAlong(IndexedLine const & line, ms::LatLon const & point, double limitM)
{
  if (line.m_shape.size() < 2)
    return -1;
  double best = limitM;
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

// Clockwise from north. A zero-length step has no direction.
double BearingDeg(ms::LatLon const & from, ms::LatLon const & to)
{
  double constexpr kDegToRad = 0.017453292519943295;
  double const cosLat = std::cos(from.m_lat * kDegToRad);
  double const east = (to.m_lon - from.m_lon) * cosLat;
  double const north = to.m_lat - from.m_lat;
  if (east * east + north * north < 1e-16)
    return 0;
  double deg = std::atan2(east, north) / kDegToRad;
  if (deg < 0)
    deg += 360.0;
  return deg;
}

double AngleDelta(double a, double b)
{
  double d = std::fmod(std::abs(a - b), 360.0);
  if (d > 180.0)
    d = 360.0 - d;
  return d;
}

struct AlongPoint
{
  double m_along = 0;
  ms::LatLon m_point;
};

// Points from `fromAlong` to `toAlong`, with out-and-back joins removed.
// A join longer than the 25 m sample used to become the arrow: both services
// then face each other along the join instead of along the track.
std::vector<AlongPoint> CleanArc(IndexedLine const & line, double fromAlong, double toAlong)
{
  double constexpr kStepM = 8.0;
  double constexpr kReturnM = 20.0;
  double constexpr kMinLoopM = 12.0;
  double constexpr kMaxLoopM = 150.0;
  std::vector<AlongPoint> raw;
  double const sign = toAlong >= fromAlong ? 1.0 : -1.0;
  raw.push_back({fromAlong, PointAt(line, fromAlong)});
  double pos = fromAlong;
  for (int n = 0; n < 4000; ++n)
  {
    if ((toAlong - pos) * sign <= 0.5)
      break;
    double next = pos + sign * kStepM;
    if ((next - toAlong) * sign > 0)
      next = toAlong;
    if (std::abs(next - pos) < 0.05)
      break;
    pos = next;
    raw.push_back({pos, PointAt(line, pos)});
  }

  std::vector<AlongPoint> clean;
  clean.reserve(raw.size());
  for (auto const & sample : raw)
  {
    clean.push_back(sample);
    for (int i = static_cast<int>(clean.size()) - 2; i >= 0; --i)
    {
      double const loop = std::abs(clean.back().m_along - clean[static_cast<size_t>(i)].m_along);
      if (loop > kMaxLoopM)
        break;
      if (loop >= kMinLoopM &&
          ms::DistanceOnEarth(clean[static_cast<size_t>(i)].m_point, clean.back().m_point) <= kReturnM)
      {
        clean.resize(static_cast<size_t>(i) + 1);
        break;
      }
    }
  }
  return clean;
}

size_t NearestSample(std::vector<AlongPoint> const & arc, double along)
{
  size_t best = 0;
  double bestDistance = 1e100;
  for (size_t i = 0; i < arc.size(); ++i)
  {
    double const distance = std::abs(arc[i].m_along - along);
    if (distance < bestDistance)
    {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

bool ChordBearing(std::vector<AlongPoint> const & arc, size_t from, size_t to, double & bearing)
{
  if (from >= arc.size() || to >= arc.size() || from == to)
    return false;
  if (ms::DistanceOnEarth(arc[from].m_point, arc[to].m_point) < 1.0)
    return false;
  bearing = BearingDeg(arc[from].m_point, arc[to].m_point);
  return true;
}

// Step along the cleaned arc until the geographic chord is at least 25 m.
size_t WalkNet(std::vector<AlongPoint> const & arc, size_t index, int step)
{
  double constexpr kWantM = 25.0;
  size_t pos = index;
  while (step > 0 ? pos + 1 < arc.size() : pos > 0)
  {
    size_t const next = static_cast<size_t>(static_cast<int>(pos) + step);
    if (ms::DistanceOnEarth(arc[index].m_point, arc[next].m_point) >= kWantM)
      return next;
    pos = next;
  }
  return pos;
}

// Direction of the station-to-station arc away from its vertices.
double MidlineBearing(std::vector<AlongPoint> const & arc)
{
  if (arc.size() < 2)
    return 0;
  size_t const begin = arc.size() / 4;
  size_t end = (arc.size() * 3) / 4;
  if (end <= begin)
    end = arc.size() - 1;
  if (ms::DistanceOnEarth(arc[begin].m_point, arc[end].m_point) < 25.0)
    end = arc.size() - 1;
  double bearing = 0;
  size_t const from = begin == end ? 0 : begin;
  if (!ChordBearing(arc, from, end, bearing))
    ChordBearing(arc, 0, arc.size() - 1, bearing);
  return bearing;
}

double HeadingDeg(IndexedLine const & line, int fromIndex, int toIndex, double along)
{
  if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex || fromIndex >= static_cast<int>(line.m_stations.size()) ||
      toIndex >= static_cast<int>(line.m_stations.size()))
    return 0;
  auto const & from = line.m_stations[static_cast<size_t>(fromIndex)];
  auto const & to = line.m_stations[static_cast<size_t>(toIndex)];
  if (from.m_alongM < 0 || to.m_alongM < 0 || line.m_shape.size() < 2 || line.m_alongM.size() != line.m_shape.size())
    return BearingDeg(from.m_station.m_point, to.m_station.m_point);

  // Previous station → next station only. The sample never follows a join that
  // lives on another part of a doubled polyline.
  auto const arc = CleanArc(line, from.m_alongM, to.m_alongM);
  if (arc.size() < 2)
    return BearingDeg(from.m_station.m_point, to.m_station.m_point);

  double const mid = MidlineBearing(arc);
  size_t const at = NearestSample(arc, along);
  size_t const ahead = WalkNet(arc, at, +1);
  double local = 0;
  bool haveLocal = ChordBearing(arc, at, ahead, local);
  if (!haveLocal)
  {
    // At the next-stop end of the arc (platform or terminus): face along the
    // inbound approach. Outbound is the forward chord above.
    size_t const behind = WalkNet(arc, at, -1);
    haveLocal = ChordBearing(arc, behind, at, local);
  }
  if (!haveLocal)
    return mid;

  // A vertex lead-in can run tens of metres off the segment the eye follows.
  // Near either station, keep the arc's midline when the local step disagrees.
  double constexpr kVertexM = 100.0;
  double const fromEnd =
      std::min(std::abs(arc[at].m_along - arc.front().m_along), std::abs(arc[at].m_along - arc.back().m_along));
  if (fromEnd <= kVertexM && AngleDelta(local, mid) > 40.0)
    return mid;
  return local;
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
      station.m_alongM = SnapAlong(line, station.m_station.m_point, kSnapLimitM);
    network.m_lines.push_back(std::move(line));
    (void)name;
  }
  std::sort(network.m_lines.begin(), network.m_lines.end(),
            [](IndexedLine const & a, IndexedLine const & b) { return EarlierLine(a.m_name, b.m_name); });
  return network;
}

namespace
{
std::string LineKey(std::string_view name)
{
  std::string key;
  key.reserve(name.size());
  for (char const ch : name)
  {
    unsigned char const c = static_cast<unsigned char>(ch);
    if (c == ' ' || c == '-' || c == '_')
      continue;
    key.push_back(static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c));
  }
  return key;
}
}  // namespace

void ApplyMapTracks(Network & network, std::vector<MapTrack> const & tracks)
{
  // A matched line may be stored once per direction. The longer polyline is the
  // one that still has the station-to-station curves.
  double constexpr kStationMatchM = 400.0;
  double constexpr kMapSnapLimitM = 3000.0;
  std::unordered_map<std::string, MapTrack const *> best;
  for (auto const & track : tracks)
  {
    if (track.m_ref.empty() || track.m_shape.size() < 2)
      continue;
    MapTrack const *& slot = best[LineKey(track.m_ref)];
    if (slot == nullptr || track.m_shape.size() > slot->m_shape.size())
      slot = &track;
  }
  if (best.empty())
    return;

  for (auto & line : network.m_lines)
  {
    auto const found = best.find(LineKey(line.m_name));
    if (found == best.end())
      continue;
    MapTrack const & track = *found->second;
    line.m_shape = track.m_shape;
    BuildAlong(line);
    for (auto & station : line.m_stations)
    {
      ms::LatLon anchor = station.m_station.m_point;
      double nearest = kStationMatchM;
      for (auto const & stop : track.m_stops)
      {
        if (!stop.IsValid())
          continue;
        double const distance = ms::DistanceOnEarth(station.m_station.m_point, stop);
        if (distance < nearest)
        {
          nearest = distance;
          anchor = stop;
        }
      }
      station.m_alongM = SnapAlong(line, anchor, kMapSnapLimitM);
    }
  }
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
    int fromIndex = -1;
    int toIndex = -1;
    if (!onPlatform)
    {
      fromIndex = previousIndex;
      toIndex = nextIndex;
    }
    else
    {
      int const count = static_cast<int>(line.m_stations.size());
      int const step = direction == 0 ? 1 : direction;
      int const ahead = nextIndex + step;
      int const behind = nextIndex - step;
      if (ahead >= 0 && ahead < count)
      {
        fromIndex = nextIndex;
        toIndex = ahead;
      }
      else if (behind >= 0 && behind < count)
      {
        fromIndex = behind;
        toIndex = nextIndex;
      }
    }
    double along = 0;
    if (fromIndex >= 0 && toIndex >= 0)
    {
      if (!onPlatform)
      {
        auto const & from = line.m_stations[static_cast<size_t>(fromIndex)];
        auto const & to = line.m_stations[static_cast<size_t>(toIndex)];
        along = from.m_alongM + (to.m_alongM - from.m_alongM) * progress;
      }
      else
        along = line.m_stations[static_cast<size_t>(nextIndex)].m_alongM;
    }
    train.m_headingDeg = HeadingDeg(line, fromIndex, toIndex, along);
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
