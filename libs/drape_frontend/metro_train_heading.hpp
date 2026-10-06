#pragma once

#include "geometry/mercator.hpp"
#include "geometry/screenbase.hpp"

#include <cmath>

namespace df
{
// Shader u_azimut for a chevron whose tip sits at -Y. 0 keeps that tip up the
// screen. Positive values rotate it clockwise in pixel space (Y down), which is
// atan2(dx, -dy) of one step along the geographic heading through GtoP.
// GtoP already contains the map angle, so a rotated map is not applied again.
inline float ChevronScreenAzimuth(ScreenBase const & screen, m2::PointD const & mercator, float headingRad)
{
  ms::LatLon const ll = mercator::ToLatLon(mercator);
  double constexpr kDegToRad = 0.017453292519943295;
  // One metre. Long enough for a stable GtoP delta, short enough to be local.
  double constexpr kStep = 1.0 / 111320.0;
  double const cosLat = std::cos(ll.m_lat * kDegToRad);
  double const north = std::cos(static_cast<double>(headingRad)) * kStep;
  double const east = std::sin(static_cast<double>(headingRad)) * kStep;
  double const dLon = cosLat == 0 ? 0 : east / cosLat;
  m2::PointD const ahead = mercator::FromLatLon(ms::LatLon(ll.m_lat + north, ll.m_lon + dLon));
  m2::PointD const s0 = screen.GtoP(mercator);
  m2::PointD const s1 = screen.GtoP(ahead);
  double const dx = s1.x - s0.x;
  double const dy = s1.y - s0.y;
  if (dx * dx + dy * dy < 1e-12)
    return 0;
  return static_cast<float>(std::atan2(dx, -dy));
}
}  // namespace df
