#include "testing/testing.hpp"

#include "bus_live/arrivals.hpp"
#include "bus_live/gtfs_csv.hpp"
#include "bus_live/gtfs_rt.hpp"

#include "coding/file_reader.hpp"

#include <cstdlib>
#include <string>

namespace live_feed_tests
{
// Opt-in smoke against the public AMB feeds. Unit tests stay offline.
//   BUS_LIVE_TRIPS=/tmp/amb/trips.bin BUS_LIVE_GTFS=/tmp/amb/google_transit.zip
UNIT_TEST(AmbFeeds_LiveFilesIfPresent)
{
  char const * tripsPath = std::getenv("BUS_LIVE_TRIPS");
  char const * gtfsPath = std::getenv("BUS_LIVE_GTFS");
  if (tripsPath == nullptr || tripsPath[0] == '\0' || gtfsPath == nullptr || gtfsPath[0] == '\0')
    return;

  FileReader tripsReader(tripsPath);
  std::string bytes;
  tripsReader.ReadAsString(bytes);
  auto const feed = bus_live::ParseFeed(bytes);
  TEST(feed.has_value(), ("AMB trips.bin did not parse"));
  TEST_GREATER(feed->m_updates.size(), 0, ());

  int withRoute = 0;
  int withTime = 0;
  for (auto const & update : feed->m_updates)
  {
    if (!update.m_trip.m_routeId.empty())
      ++withRoute;
    for (auto const & stop : update.m_stops)
      if (stop.m_arrival.m_time || stop.m_departure.m_time)
        ++withTime;
  }
  // The published TripUpdates omit route_id; the line name comes from static GTFS.
  TEST_EQUAL(withRoute, 0, ());
  TEST_GREATER(withTime, 0, ());

  auto const index = bus_live::LoadStaticIndexFromZip(gtfsPath);
  TEST(index.has_value(), ("AMB GTFS zip did not yield stops, routes and trips"));
  TEST_GREATER(index->m_stops.size(), 1000, ());
  TEST_GREATER(index->m_routes.size(), 10, ());
  TEST_GREATER(index->m_trips.size(), 1000, ());

  bool sawEstacio = false;
  bool saw1720 = false;
  for (auto const & stop : index->m_stops)
  {
    if (stop.m_id == "100037")
      sawEstacio = true;
    if (stop.m_code == "1720" && stop.m_id == "001720")
      saw1720 = true;
  }
  TEST(sawEstacio, ());
  TEST(saw1720, ());

  int joined = 0;
  for (auto const & update : feed->m_updates)
    if (index->m_trips.find(update.m_trip.m_tripId) != index->m_trips.end())
      ++joined;
  TEST_GREATER(joined, 100, ());

  int shown = 0;
  for (auto const & update : feed->m_updates)
  {
    if (update.m_stops.empty())
      continue;
    for (auto const & stop : index->m_stops)
    {
      if (stop.m_id != update.m_stops.front().m_stopId && stop.m_code != update.m_stops.front().m_stopId)
        continue;
      auto const arrivals = bus_live::CollectAmbArrivals(*feed, *index, stop, feed->m_headerTimestamp);
      if (!arrivals.empty() && !arrivals.front().m_line.empty())
        ++shown;
      break;
    }
    if (shown > 0)
      break;
  }
  TEST_GREATER(shown, 0, ());
}
}  // namespace live_feed_tests
