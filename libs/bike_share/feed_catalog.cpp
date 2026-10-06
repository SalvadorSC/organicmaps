#include "bike_share/feed_catalog.hpp"

namespace bike_share
{
bool LatLonBox::Contains(ms::LatLon const & point) const
{
  return point.m_lat >= m_minLat && point.m_lat <= m_maxLat && point.m_lon >= m_minLon && point.m_lon <= m_maxLon;
}

std::vector<FeedDefinition> const & Feeds()
{
  // Boxes are the live station extents plus about 2 km, recorded from the
  // public feeds. A point inside several boxes fetches each of those feeds;
  // matching then keeps the nearest dock. To add a city, append a discovery
  // URL and the box that contains its stations.
  static std::vector<FeedDefinition> const kFeeds = {
      {"ambici", "AMBici", "https://gbfs.nextbike.net/maps/gbfs/v2/nextbike_bs/gbfs.json", {41.24, 1.92, 41.51, 2.29}},
      {"bicing",
       "Bicing",
       "https://barcelona.publicbikesystem.net/customer/gbfs/v2/gbfs.json",
       {41.32, 2.08, 41.49, 2.25}},
  };
  return kFeeds;
}

std::vector<FeedDefinition const *> FeedsCovering(ms::LatLon const & point)
{
  std::vector<FeedDefinition const *> covering;
  for (auto const & feed : Feeds())
    if (feed.m_bounds.Contains(point))
      covering.push_back(&feed);
  return covering;
}
}  // namespace bike_share
