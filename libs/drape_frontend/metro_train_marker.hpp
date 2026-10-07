#pragma once

#include "drape/color.hpp"

#include "geometry/point2d.hpp"

#include <string>
#include <vector>

namespace df
{
// One train drawn on the map. An empty m_label is a plain estimated-metro dot.
// A short line code (R4, S1, L6) is drawn inside a larger disc. Heading is not
// drawn: both layers are round markers.
struct MetroTrainMarker
{
  m2::PointD m_mercator;
  float m_headingRad = 0;
  dp::Color m_color;
  std::string m_label;
};

// One coloured piece of an FGC or Rodalies corridor. Shared corridors are split
// into equal bands before they reach the renderer.
struct MetroTrainStroke
{
  dp::Color m_color;
  std::vector<m2::PointD> m_mercator;
};
}  // namespace df
