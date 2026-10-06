#pragma once

#include "drape/color.hpp"

#include "geometry/point2d.hpp"

namespace df
{
// One train drawn on the map. Estimated metro is a round dot and leaves m_directional
// false, so heading is ignored. A live train with a known bearing sets m_directional
// and m_headingRad (radians, clockwise from north; 0 is north).
struct MetroTrainMarker
{
  m2::PointD m_mercator;
  float m_headingRad = 0;
  dp::Color m_color;
  bool m_directional = false;
};
}  // namespace df
