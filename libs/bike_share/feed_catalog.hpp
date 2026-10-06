#pragma once

#include "geometry/latlon.hpp"

#include <string>
#include <vector>

namespace bike_share
{
struct LatLonBox
{
  double m_minLat = 0;
  double m_minLon = 0;
  double m_maxLat = 0;
  double m_maxLon = 0;

  bool Contains(ms::LatLon const & point) const;
};

// One public GBFS system. Add a city by appending an entry in feed_catalog.cpp.
struct FeedDefinition
{
  std::string m_id;
  std::string m_name;
  std::string m_discoveryUrl;
  LatLonBox m_bounds;
};

std::vector<FeedDefinition> const & Feeds();
std::vector<FeedDefinition const *> FeedsCovering(ms::LatLon const & point);
}  // namespace bike_share
