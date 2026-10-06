#include "testing/testing.hpp"

#include "bus_live/arrivals.hpp"
#include "bus_live/arrivals_service.hpp"
#include "bus_live/gtfs_rt.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace arrivals_tests
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

  void Int(uint32_t field, uint64_t value)
  {
    Varint((static_cast<uint64_t>(field) << 3) | 0);
    Varint(value);
  }

  void Str(uint32_t field, std::string_view value)
  {
    Varint((static_cast<uint64_t>(field) << 3) | 2);
    Varint(value.size());
    m_bytes.append(value);
  }

  void Msg(uint32_t field, Proto const & child) { Str(field, child.m_bytes); }

  std::string m_bytes;
};

void AddUpdate(Proto & feed, std::string const & tripId, int schedule, std::string const & stopId, bool withTime,
               uint64_t when)
{
  Proto trip;
  trip.Str(1, tripId);
  if (schedule != 0)
    trip.Int(4, static_cast<uint64_t>(schedule));
  Proto event;
  if (withTime)
    event.Int(2, when);
  else
    event.Int(1, 30);
  Proto stop;
  stop.Msg(2, event);
  stop.Str(4, stopId);
  Proto update;
  update.Msg(1, trip);
  update.Msg(2, stop);
  Proto entity;
  entity.Msg(3, update);
  feed.Msg(2, entity);
}

std::string SampleTrips()
{
  Proto header;
  header.Int(3, 1791287276);
  Proto feed;
  feed.Msg(1, header);
  AddUpdate(feed, "TRIP157", 0, "100037", true, 1791287400);
  AddUpdate(feed, "TRIP63", 0, "100037", true, 1791287500);
  AddUpdate(feed, "CANCELED", bus_live::kTripCanceled, "100037", true, 1791287300);
  AddUpdate(feed, "DELAY", 0, "100037", false, 0);
  AddUpdate(feed, "OLD", 0, "100037", true, 1791287000);
  return feed.m_bytes;
}

bus_live::StaticIndex SampleIndex()
{
  bus_live::StaticIndex index;
  bus_live::TransitStop stop;
  stop.m_id = "100037";
  stop.m_code = "100037";
  stop.m_name = "Pl Estacio Bertrand";
  stop.m_point = ms::LatLon(41.3822242, 2.0478329);
  index.m_stops.push_back(stop);
  index.m_routes.emplace("R157", bus_live::GtfsRoute{"R157", "157", "Line 157"});
  index.m_routes.emplace("R63", bus_live::GtfsRoute{"R63", "63", "Line 63"});
  index.m_trips.emplace("TRIP157", bus_live::GtfsTrip{"TRIP157", "R157", "Sant Joan"});
  index.m_trips.emplace("TRIP63", bus_live::GtfsTrip{"TRIP63", "R63", "Via Augusta"});
  index.m_trips.emplace("CANCELED", bus_live::GtfsTrip{"CANCELED", "R157", "Nope"});
  index.m_trips.emplace("OLD", bus_live::GtfsTrip{"OLD", "R157", "Old"});
  index.m_trips.emplace("DELAY", bus_live::GtfsTrip{"DELAY", "R157", "Delay"});
  return index;
}

std::string_view constexpr kTmb = R"({
  "timestamp": 1791287276000,
  "parades": [{
    "codi_parada": "100037",
    "linies_trajectes": [{
      "nom_linia": "157",
      "desti_trajecte": "Sant Joan Despi",
      "propers_busos": [{"temps_arribada": 1791287430000}]
    }, {
      "nom_linia": "L21",
      "desti_trajecte": "St. Feliu L.",
      "propers_busos": [{"temps_arribada": 1791288000000}]
    }]
  }]
})";

struct Env
{
  std::chrono::steady_clock::time_point m_steady{};
  std::chrono::system_clock::time_point m_wall{std::chrono::seconds{1791287200}};
  int m_http = 0;
  int m_staticLoads = 0;
  int m_catalogLoads = 0;
  bus_live::Credentials m_creds;
  std::optional<bus_live::StaticIndex> m_index;
  std::optional<std::vector<bus_live::TransitStop>> m_catalog;
  std::string m_trips;
  std::string m_tmb;
  std::vector<std::string> m_urls;

  std::unique_ptr<bus_live::ArrivalsService> Make()
  {
    return std::make_unique<bus_live::ArrivalsService>(
        [this](std::string const & url) -> std::optional<std::string>
    {
      ++m_http;
      m_urls.push_back(url);
      if (url.find("trips.bin") != std::string::npos)
        return m_trips;
      if (url.find("/itransit/bus/parades/") != std::string::npos)
        return m_tmb;
      return std::nullopt;
    }, [this] { return m_wall; }, [this] { return m_steady; }, [this] { return m_creds; },
        [this]() -> std::optional<bus_live::StaticIndex>
    {
      ++m_staticLoads;
      return m_index;
    }, [this](bus_live::Credentials const &) -> std::optional<std::vector<bus_live::TransitStop>>
    {
      ++m_catalogLoads;
      return m_catalog;
    });
  }
};

bus_live::StopQuery Barcelona()
{
  bus_live::StopQuery query;
  query.m_point = ms::LatLon(41.3822242, 2.0478329);
  query.m_name = "Pl Estacio Bertrand";
  query.m_ref = "100037";
  return query;
}

UNIT_TEST(MergeArrivals_PrefersTmbAndCaps)
{
  std::vector<bus_live::Arrival> amb = {{"157", "Sant Joan", 1000, false}, {"63", "Via", 1010, false}};
  std::vector<bus_live::Arrival> tmb = {{"157", "Sant Joan Despi", 1030, true}};
  auto const merged = bus_live::MergeArrivals(amb, tmb);
  TEST_EQUAL(merged.size(), 2, ());
  TEST_EQUAL(merged[0].m_line, "63", ());
  TEST_EQUAL(merged[1].m_line, "157", ());
  TEST_EQUAL(merged[1].m_destination, "Sant Joan Despi", ());
  TEST(merged[1].m_fromTmb, ());

  std::vector<bus_live::Arrival> many;
  for (int i = 0; i < 13; ++i)
    many.push_back(bus_live::Arrival{"L" + std::to_string(i), "", 2000 - i, false});
  auto const capped = bus_live::MergeArrivals(many, {});
  TEST_EQUAL(capped.size(), bus_live::kMaxArrivals, ());
  TEST_EQUAL(capped.front().m_etaUnixSec, 2000 - 12, ());
}

UNIT_TEST(CollectAmbArrivals_JoinsTripIdWhenRouteIsMissing)
{
  auto const feed = bus_live::ParseFeed(SampleTrips());
  TEST(feed.has_value(), ());
  auto const arrivals = bus_live::CollectAmbArrivals(*feed, SampleIndex(), SampleIndex().m_stops.front(), 1791287200);
  TEST_EQUAL(arrivals.size(), 2, ());
  TEST_EQUAL(arrivals[0].m_line, "157", ());
  TEST_EQUAL(arrivals[0].m_destination, "Sant Joan", ());
  TEST_EQUAL(arrivals[1].m_line, "63", ());
}

UNIT_TEST(ArrivalsService_OutsideAreaDoesNotFetch)
{
  Env env;
  env.m_index = SampleIndex();
  env.m_trips = SampleTrips();
  auto service = env.Make();
  bus_live::StopQuery query;
  query.m_point = ms::LatLon(40.4168, -3.7038);
  auto const list = service->Lookup(query);
  TEST_EQUAL(static_cast<int>(list.m_status), static_cast<int>(bus_live::LookupStatus::NotApplicable), ());
  TEST_EQUAL(env.m_http, 0, ());
  TEST_EQUAL(env.m_staticLoads, 0, ());
}

UNIT_TEST(ArrivalsService_MergesCachesAndRefetches)
{
  Env env;
  env.m_index = SampleIndex();
  env.m_trips = SampleTrips();
  env.m_tmb = std::string(kTmb);
  env.m_creds = {"a b", "k/y"};
  env.m_catalog = std::vector<bus_live::TransitStop>{};
  auto service = env.Make();
  auto const first = service->Lookup(Barcelona());
  TEST_EQUAL(static_cast<int>(first.m_status), static_cast<int>(bus_live::LookupStatus::Ok), ());
  TEST_EQUAL(first.m_arrivals.size(), 3, ());
  TEST_EQUAL(first.m_arrivals[0].m_line, "157", ());
  TEST_EQUAL(first.m_arrivals[0].m_destination, "Sant Joan Despi", ());
  TEST(first.m_arrivals[0].m_fromTmb, ());
  TEST_EQUAL(first.m_arrivals[1].m_line, "63", ());
  TEST(!first.m_arrivals[1].m_fromTmb, ());
  TEST_EQUAL(first.m_arrivals[2].m_line, "L21", ());
  TEST_EQUAL(env.m_staticLoads, 1, ());
  TEST_EQUAL(env.m_catalogLoads, 1, ());
  TEST_EQUAL(env.m_http, 2, ());
  TEST(env.m_urls[1].find("parades/100037?") != std::string::npos, ());
  TEST(env.m_urls[1].find("app_id=a%20b") != std::string::npos, ());
  TEST(env.m_urls[1].find("app_key=k%2Fy") != std::string::npos, ());
  TEST(env.m_urls[1].find("a b") == std::string::npos, ());

  auto const second = service->Lookup(Barcelona());
  TEST_EQUAL(second.m_arrivals.size(), 3, ());
  TEST_EQUAL(env.m_http, 2, ());
  TEST_EQUAL(env.m_staticLoads, 1, ());
  TEST_EQUAL(env.m_catalogLoads, 1, ());

  env.m_steady += std::chrono::seconds{31};
  service->Lookup(Barcelona());
  TEST_EQUAL(env.m_http, 4, ());
  TEST_EQUAL(env.m_staticLoads, 1, ());
  TEST_EQUAL(env.m_catalogLoads, 1, ());
}

UNIT_TEST(ArrivalsService_NoCredentialsSkipsTmb)
{
  Env env;
  env.m_index = SampleIndex();
  env.m_trips = SampleTrips();
  env.m_tmb = std::string(kTmb);
  auto service = env.Make();
  auto const list = service->Lookup(Barcelona());
  TEST_EQUAL(list.m_arrivals.size(), 2, ());
  TEST_EQUAL(env.m_catalogLoads, 0, ());
  TEST_EQUAL(env.m_http, 1, ());
  TEST(env.m_urls[0].find("trips.bin") != std::string::npos, ());
}

UNIT_TEST(ArrivalsService_InAreaWithoutData)
{
  Env env;
  auto service = env.Make();
  auto const list = service->Lookup(Barcelona());
  TEST_EQUAL(static_cast<int>(list.m_status), static_cast<int>(bus_live::LookupStatus::NoData), ());
  TEST_EQUAL(env.m_http, 0, ());
  TEST_EQUAL(env.m_staticLoads, 1, ());
}

UNIT_TEST(InBarcelonaArea_Bounds)
{
  TEST(bus_live::InBarcelonaArea(ms::LatLon(41.3822242, 2.0478329)), ());
  TEST(!bus_live::InBarcelonaArea(ms::LatLon(40.4168, -3.7038)), ());
}
}  // namespace arrivals_tests
