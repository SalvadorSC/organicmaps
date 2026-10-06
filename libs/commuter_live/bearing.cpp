#include "commuter_live/bearing.hpp"

#include <cmath>

namespace commuter_live
{
namespace
{
double constexpr kMinMoveM = 12.0;
double constexpr kDegToRad = 0.017453292519943295;
double constexpr kRadToDeg = 57.29577951308232;

double Wrap360(double deg)
{
  while (deg < 0)
    deg += 360.0;
  while (deg >= 360.0)
    deg -= 360.0;
  return deg;
}
}  // namespace

std::optional<double> HeadingFromMotion(Fix const * previous, double lat, double lon,
                                        std::optional<double> feedBearingDeg)
{
  if (feedBearingDeg)
    return Wrap360(*feedBearingDeg);
  if (previous == nullptr)
    return std::nullopt;
  double const dNorth = (lat - previous->m_lat) * 111320.0;
  double const cosLat = std::cos(previous->m_lat * kDegToRad);
  double const dEast = (lon - previous->m_lon) * 111320.0 * cosLat;
  if (dNorth * dNorth + dEast * dEast < kMinMoveM * kMinMoveM)
    return previous->m_headingDeg;
  return Wrap360(std::atan2(dEast, dNorth) * kRadToDeg);
}
}  // namespace commuter_live
