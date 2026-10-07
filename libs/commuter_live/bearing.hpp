#pragma once

#include <optional>

namespace commuter_live
{
struct Fix
{
  double m_lat = 0;
  double m_lon = 0;
  std::optional<double> m_headingDeg;
};

// Degrees clockwise from north. Feed bearing wins. Otherwise the step from the
// previous fix, once it is at least 12 m. A shorter step keeps the last heading.
std::optional<double> HeadingFromMotion(Fix const * previous, double lat, double lon,
                                        std::optional<double> feedBearingDeg);
}  // namespace commuter_live
