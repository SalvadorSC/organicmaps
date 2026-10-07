#include "commuter_live/snap.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/point2d.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace commuter_live
{
namespace
{
double constexpr kRefLimitM = 3000.0;
double constexpr kRailLimitM = 150.0;

std::optional<std::pair<ms::LatLon, double>> Closest(ms::LatLon const & point, std::vector<ms::LatLon> const & shape)
{
  if (shape.size() < 2 || !point.IsValid())
    return std::nullopt;
  double const cosLat = std::cos(point.m_lat * 0.017453292519943295);
  if (std::abs(cosLat) < 1e-3)
    return std::nullopt;
  auto const xy = [cosLat](ms::LatLon const & ll) { return m2::PointD(ll.m_lon * cosLat, ll.m_lat); };
  m2::PointD const query = xy(point);
  double best2 = 1e100;
  m2::PointD best = query;
  for (size_t i = 0; i + 1 < shape.size(); ++i)
  {
    m2::PointD const a = xy(shape[i]);
    m2::PointD const b = xy(shape[i + 1]);
    m2::PointD const ab = b - a;
    double const len2 = ab.SquaredLength();
    double t = 0.0;
    if (len2 > 1e-18)
    {
      m2::PointD const aq = query - a;
      t = std::clamp((aq.x * ab.x + aq.y * ab.y) / len2, 0.0, 1.0);
    }
    m2::PointD const projected = a + ab * t;
    double const dist2 = query.SquaredLength(projected);
    if (dist2 < best2)
    {
      best2 = dist2;
      best = projected;
    }
  }
  ms::LatLon const snapped(best.y, best.x / cosLat);
  return std::pair{snapped, ms::DistanceOnEarth(point, snapped)};
}

bool SameLine(std::string_view a, std::string_view b)
{
  return !a.empty() && (a == b || CanonicalLine(a) == CanonicalLine(b));
}
}  // namespace

std::string CanonicalLine(std::string_view line)
{
  if (line == "R5R" || line == "R53")
    return "R5";
  if (line == "R6R" || line == "R60" || line == "R61" || line == "R62" || line == "R63")
    return "R6";
  return std::string(line);
}

SnapPoint SnapToTracks(double lat, double lon, std::string_view line, std::vector<RailTrack> const & tracks)
{
  SnapPoint result;
  result.m_lat = lat;
  result.m_lon = lon;
  ms::LatLon const gps(lat, lon);
  double bestRef = kRefLimitM;
  double bestRail = kRailLimitM;
  ms::LatLon refPoint;
  ms::LatLon railPoint;
  bool hasRef = false;
  bool hasRail = false;
  for (auto const & track : tracks)
  {
    auto const hit = Closest(gps, track.m_shape);
    if (!hit)
      continue;
    if (!track.m_ref.empty() && SameLine(track.m_ref, line))
    {
      if (hit->second <= bestRef)
      {
        bestRef = hit->second;
        refPoint = hit->first;
        hasRef = true;
      }
    }
    else if (track.m_ref.empty() && hit->second <= bestRail)
    {
      bestRail = hit->second;
      railPoint = hit->first;
      hasRail = true;
    }
  }
  if (hasRef)
    return {refPoint.m_lat, refPoint.m_lon, true};
  if (hasRail)
    return {railPoint.m_lat, railPoint.m_lon, true};
  return result;
}

namespace
{
struct Cell
{
  int m_lat = 0;
  int m_lon = 0;
  bool operator==(Cell const & rhs) const { return m_lat == rhs.m_lat && m_lon == rhs.m_lon; }
};

struct EdgeKey
{
  Cell m_a;
  Cell m_b;
  bool operator<(EdgeKey const & rhs) const
  {
    if (m_a.m_lat != rhs.m_a.m_lat)
      return m_a.m_lat < rhs.m_a.m_lat;
    if (m_a.m_lon != rhs.m_a.m_lon)
      return m_a.m_lon < rhs.m_a.m_lon;
    if (m_b.m_lat != rhs.m_b.m_lat)
      return m_b.m_lat < rhs.m_b.m_lat;
    return m_b.m_lon < rhs.m_b.m_lon;
  }
};

Cell Quantize(ms::LatLon const & point)
{
  // About 70 m. Parallel rails of one corridor fall in the same cell.
  return {static_cast<int>(std::lround(point.m_lat / 0.00063)), static_cast<int>(std::lround(point.m_lon / 0.00084))};
}

EdgeKey MakeEdge(Cell a, Cell b)
{
  if (b.m_lat < a.m_lat || (b.m_lat == a.m_lat && b.m_lon < a.m_lon))
    std::swap(a, b);
  return {a, b};
}

double ShapeLength(std::vector<ms::LatLon> const & shape)
{
  double length = 0;
  for (size_t i = 0; i + 1 < shape.size(); ++i)
    length += ms::DistanceOnEarth(shape[i], shape[i + 1]);
  return length;
}

// Points every ~40 m so two copies of a corridor land on the same cells
// even when their original vertices are out of phase.
std::vector<ms::LatLon> Resample(std::vector<ms::LatLon> const & shape)
{
  double constexpr kStepM = 40.0;
  std::vector<ms::LatLon> out;
  if (shape.empty())
    return out;
  out.push_back(shape.front());
  double carry = 0;
  for (size_t i = 0; i + 1 < shape.size(); ++i)
  {
    double const span = ms::DistanceOnEarth(shape[i], shape[i + 1]);
    if (span < 0.5)
      continue;
    double walked = 0;
    while (carry + (span - walked) >= kStepM)
    {
      double const need = kStepM - carry;
      double const t = (walked + need) / span;
      out.push_back({shape[i].m_lat + (shape[i + 1].m_lat - shape[i].m_lat) * t,
                     shape[i].m_lon + (shape[i + 1].m_lon - shape[i].m_lon) * t});
      walked += need;
      carry = 0;
    }
    carry += span - walked;
  }
  auto const & back = shape.back();
  if (out.back().m_lat != back.m_lat || out.back().m_lon != back.m_lon)
    out.push_back(back);
  return out;
}
}  // namespace

std::vector<CorridorStroke> SharedStrokes(std::vector<RailTrack> const & tracks, std::vector<std::string> const & lines)
{
  std::vector<std::string> wanted;
  for (auto const & line : lines)
  {
    auto const canon = CanonicalLine(line);
    if (canon.empty() || std::find(wanted.begin(), wanted.end(), canon) != wanted.end())
      continue;
    wanted.push_back(canon);
  }
  std::sort(wanted.begin(), wanted.end());

  struct Chosen
  {
    std::string m_line;
    std::vector<ms::LatLon> m_shape;
  };
  std::vector<Chosen> chosen;
  for (auto const & line : wanted)
  {
    RailTrack const * plain = nullptr;
    RailTrack const * colored = nullptr;
    double plainLen = -1;
    double coloredLen = -1;
    for (auto const & track : tracks)
    {
      if (track.m_ref.empty() || track.m_shape.size() < 2 || !SameLine(track.m_ref, line))
        continue;
      double const len = ShapeLength(track.m_shape);
      if (track.m_colored)
      {
        if (len > coloredLen)
        {
          coloredLen = len;
          colored = &track;
        }
      }
      else if (len > plainLen)
      {
        plainLen = len;
        plain = &track;
      }
    }
    // Scheme geometry wins when Organic Maps has not already claimed the line.
    // FGC subway lines only exist as coloured transit shapes, so use those.
    RailTrack const * best = plain != nullptr ? plain : colored;
    if (best != nullptr)
      chosen.push_back({line, best->m_shape});
  }

  struct EdgeLines
  {
    std::vector<int> m_lines;
  };
  std::map<EdgeKey, EdgeLines> edges;
  struct Sample
  {
    ms::LatLon m_point;
    Cell m_cell;
  };
  std::vector<std::vector<Sample>> samples(chosen.size());
  for (int i = 0; i < static_cast<int>(chosen.size()); ++i)
  {
    Cell previous{};
    bool hasPrevious = false;
    for (auto const & point : Resample(chosen[i].m_shape))
    {
      Cell const cell = Quantize(point);
      if (hasPrevious && !(cell == previous))
      {
        auto & slot = edges[MakeEdge(previous, cell)].m_lines;
        if (std::find(slot.begin(), slot.end(), i) == slot.end())
          slot.push_back(i);
      }
      if (!hasPrevious || !(cell == previous))
        samples[i].push_back({point, cell});
      previous = cell;
      hasPrevious = true;
    }
  }

  std::vector<CorridorStroke> strokes;
  auto flush = [&](std::vector<int> const & owners, std::vector<ms::LatLon> & shape)
  {
    if (owners.empty() || shape.size() < 2)
    {
      shape.clear();
      return;
    }
    CorridorStroke stroke;
    for (int const index : owners)
      stroke.m_lines.push_back(chosen[index].m_line);
    stroke.m_shape = std::move(shape);
    shape.clear();
    strokes.push_back(std::move(stroke));
  };

  for (int i = 0; i < static_cast<int>(chosen.size()); ++i)
  {
    std::vector<int> runLines;
    std::vector<ms::LatLon> run;
    for (size_t s = 0; s + 1 < samples[i].size(); ++s)
    {
      auto const found = edges.find(MakeEdge(samples[i][s].m_cell, samples[i][s + 1].m_cell));
      if (found == edges.end() || found->second.m_lines.empty() || found->second.m_lines.front() != i)
      {
        flush(runLines, run);
        runLines.clear();
        continue;
      }
      if (runLines != found->second.m_lines)
      {
        flush(runLines, run);
        runLines = found->second.m_lines;
      }
      if (run.empty())
        run.push_back(samples[i][s].m_point);
      run.push_back(samples[i][s + 1].m_point);
    }
    flush(runLines, run);
  }
  return strokes;
}
}  // namespace commuter_live
