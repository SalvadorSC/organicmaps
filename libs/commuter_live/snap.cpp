#include "commuter_live/snap.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/point2d.hpp"

#include <algorithm>
#include <cmath>
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

std::vector<RailTrack> StrokeTracks(std::vector<RailTrack> const & tracks, std::vector<std::string> const & lines)
{
  std::vector<RailTrack> strokes;
  for (auto const & line : lines)
  {
    RailTrack const * best = nullptr;
    for (auto const & track : tracks)
    {
      if (track.m_colored || track.m_ref.empty() || track.m_shape.size() < 2 || !SameLine(track.m_ref, line))
        continue;
      if (best == nullptr || track.m_shape.size() > best->m_shape.size())
        best = &track;
    }
    if (best == nullptr)
      continue;
    bool const already = std::any_of(strokes.begin(), strokes.end(),
                                     [&](RailTrack const & stroke) { return SameLine(stroke.m_ref, best->m_ref); });
    if (!already)
      strokes.push_back(*best);
  }
  return strokes;
}
}  // namespace commuter_live
