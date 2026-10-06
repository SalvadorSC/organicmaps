#pragma once

#include "drape/color.hpp"

#include "geometry/point2d.hpp"

namespace df
{
// One estimated train. Heading is radians, clockwise from north (0 is north).
struct MetroTrainMarker
{
  m2::PointD m_mercator;
  float m_headingRad = 0;
  dp::Color m_color;
};
}  // namespace df
