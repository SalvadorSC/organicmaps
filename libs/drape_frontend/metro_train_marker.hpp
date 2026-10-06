#pragma once

#include "drape/color.hpp"

#include "geometry/point2d.hpp"

namespace df
{
// One estimated metro train. The marker is a round dot in m_color; heading is unused.
struct MetroTrainMarker
{
  m2::PointD m_mercator;
  float m_headingRad = 0;
  dp::Color m_color;
};
}  // namespace df
