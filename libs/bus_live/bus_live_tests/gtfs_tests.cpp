#include "testing/testing.hpp"

#include "bus_live/gtfs_csv.hpp"
#include "bus_live/gtfs_rt.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace gtfs_tests
{
class Proto
{
public:
  void Varint(uint64_t value)
  {
    while (value > 0x7f)
    {
      m_bytes.push_back(static_cast<char>((value & 0x7f) | 0x80));
      value >>= 7;
    }
    m_bytes.push_back(static_cast<char>(value));
  }

  void Key(uint32_t field, uint32_t wire) { Varint((static_cast<uint64_t>(field) << 3) | wire); }

  void Int(uint32_t field, uint64_t value)
  {
    Key(field, 0);
    Varint(value);
  }

  void Str(uint32_t field, std::string_view value)
  {
    Key(field, 2);
    Varint(value.size());
    m_bytes.append(value);
  }

  void Msg(uint32_t field, Proto const & child) { Str(field, child.m_bytes); }

  std::string m_bytes;
};

std::string SampleFeed()
{
  Proto header;
  header.Int(3, 1791287276);

  Proto trip;
  trip.Str(1, "TRIP157");

  Proto arrival;
  arrival.Int(2, 1791287400);
  Proto stopTime;
  stopTime.Msg(2, arrival);
  stopTime.Str(4, "001720");

  Proto update;
  update.Msg(1, trip);
  update.Msg(2, stopTime);

  Proto entity;
  entity.Str(1, "e1");
  entity.Msg(3, update);

  Proto canceledTrip;
  canceledTrip.Str(1, "GONE");
  canceledTrip.Int(4, 3);
  Proto canceledStop;
  canceledStop.Msg(2, arrival);
  canceledStop.Str(4, "001720");
  Proto canceledUpdate;
  canceledUpdate.Msg(1, canceledTrip);
  canceledUpdate.Msg(2, canceledStop);
  Proto canceledEntity;
  canceledEntity.Msg(3, canceledUpdate);

  Proto delayOnly;
  delayOnly.Int(1, 40);
  Proto delayStop;
  delayStop.Msg(2, delayOnly);
  delayStop.Str(4, "001720");
  Proto delayTrip;
  delayTrip.Str(1, "DELAY");
  Proto delayUpdate;
  delayUpdate.Msg(1, delayTrip);
  delayUpdate.Msg(2, delayStop);
  Proto delayEntity;
  delayEntity.Msg(3, delayUpdate);

  Proto skipped;
  skipped.Msg(2, arrival);
  skipped.Str(4, "001720");
  skipped.Int(5, 1);
  Proto skippedTrip;
  skippedTrip.Str(1, "SKIP");
  Proto skippedUpdate;
  skippedUpdate.Msg(1, skippedTrip);
  skippedUpdate.Msg(2, skipped);
  Proto skippedEntity;
  skippedEntity.Msg(3, skippedUpdate);

  Proto feed;
  feed.Msg(1, header);
  feed.Msg(2, entity);
  feed.Msg(2, canceledEntity);
  feed.Msg(2, delayEntity);
  feed.Msg(2, skippedEntity);
  return feed.m_bytes;
}

UNIT_TEST(GtfsRt_ParsesTripUpdateAndIgnoresNonTimes)
{
  auto const feed = bus_live::ParseFeed(SampleFeed());
  TEST(feed.has_value(), ());
  TEST_EQUAL(feed->m_headerTimestamp, 1791287276, ());
  TEST_EQUAL(feed->m_updates.size(), 4, ());
  TEST_EQUAL(feed->m_updates[0].m_trip.m_tripId, "TRIP157", ());
  TEST(feed->m_updates[0].m_trip.m_routeId.empty(), ());
  TEST(feed->m_updates[0].m_stops[0].m_arrival.m_time.has_value(), ());
  TEST_EQUAL(*feed->m_updates[0].m_stops[0].m_arrival.m_time, 1791287400, ());
  TEST_EQUAL(feed->m_updates[0].m_stops[0].m_stopId, "001720", ());
  TEST_EQUAL(feed->m_updates[1].m_trip.m_schedule, bus_live::kTripCanceled, ());
  TEST(feed->m_updates[2].m_stops[0].m_arrival.m_delay.has_value(), ());
  TEST(!feed->m_updates[2].m_stops[0].m_arrival.m_time.has_value(), ());
  TEST_EQUAL(feed->m_updates[3].m_stops[0].m_schedule, bus_live::kStopSkipped, ());
}

UNIT_TEST(GtfsRt_RejectsHtmlAndTruncation)
{
  TEST(!bus_live::ParseFeed("<html>").has_value(), ());
  TEST(!bus_live::ParseFeed("").has_value(), ());
  std::string truncated;
  truncated.push_back(static_cast<char>(0x12));
  truncated.push_back(static_cast<char>(5));
  truncated.push_back('x');
  TEST(!bus_live::ParseFeed(truncated).has_value(), ());
}

UNIT_TEST(GtfsCsv_ParsesQuotedStopsRoutesAndTrips)
{
  std::string const stops =
      "stop_id,stop_code,stop_name,stop_lat,stop_lon\n"
      "001720,1720,\"Av. Industria, Walden\",41.38049,2.066909\n"
      "100037,100037,Pl. Estacio,41.38262094,2.04777003\n";
  auto const parsedStops = bus_live::ParseStops(stops);
  TEST(parsedStops.has_value(), ());
  TEST_EQUAL(parsedStops->size(), 2, ());
  TEST_EQUAL((*parsedStops)[0].m_code, "1720", ());
  TEST_EQUAL((*parsedStops)[0].m_id, "001720", ());
  TEST_EQUAL((*parsedStops)[0].m_name, "Av. Industria, Walden", ());

  auto const routes = bus_live::ParseRoutes("route_id,route_short_name,route_long_name\nR1,157,Line 157\n");
  TEST(routes.has_value(), ());
  TEST_EQUAL(routes->front().m_shortName, "157", ());

  auto const trips = bus_live::ParseTrips("route_id,service_id,trip_id,trip_headsign\nR1,S,TRIP157,Sant Joan\n");
  TEST(trips.has_value(), ());
  TEST_EQUAL(trips->front().m_headsign, "Sant Joan", ());
  TEST(!bus_live::ParseStops("name,lat\nA,1\n").has_value(), ());
}
}  // namespace gtfs_tests
