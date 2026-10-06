#include "testing/testing.hpp"

#include "bike_share/availability_service.hpp"

#include "bike_share/bike_share_tests/fixture.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

namespace availability_service_tests
{
std::unordered_map<std::string, std::string> AmbiciBodies()
{
  return {
      {"https://gbfs.nextbike.net/maps/gbfs/v2/nextbike_bs/gbfs.json", ReadBikeShareFixture("gbfs.json")},
      {"https://gbfs.nextbike.net/maps/gbfs/v2/nextbike_bs/en/station_information.json",
       ReadBikeShareFixture("station_information.json")},
      {"https://gbfs.nextbike.net/maps/gbfs/v2/nextbike_bs/en/station_status.json",
       ReadBikeShareFixture("station_status.json")},
      {"https://gbfs.nextbike.net/maps/gbfs/v2/nextbike_bs/en/vehicle_types.json",
       ReadBikeShareFixture("vehicle_types.json")},
  };
}

UNIT_TEST(AvailabilityService_MatchesAmbiciAndCachesForTtl)
{
  auto const bodies = AmbiciBodies();
  int calls = 0;
  auto const http = [&](std::string const & url) -> std::optional<std::string>
  {
    ++calls;
    auto const it = bodies.find(url);
    if (it == bodies.end())
      return std::nullopt;
    return it->second;
  };
  auto now = std::chrono::steady_clock::time_point{std::chrono::seconds(1000)};
  bike_share::AvailabilityService service(http, [&now] { return now; }, bike_share::Feeds());

  bike_share::MatchQuery const query{ms::LatLon(41.384018, 2.047004), "RENFE Sant Feliu de Llobregat", "34920"};
  auto const first = service.Lookup(query);
  TEST(first.has_value(), ());
  TEST_EQUAL(first->m_stationName, "RENFE Sant Feliu de Llobregat", ());
  TEST_EQUAL(first->m_feedName, "AMBici", ());
  TEST_EQUAL(first->m_bikes, 3, ());
  TEST_EQUAL(*first->m_ebikes, 3, ());
  TEST_EQUAL(*first->m_mechanical, 0, ());
  TEST_EQUAL(*first->m_docks, 12, ());
  TEST_EQUAL(first->m_lastUpdated, 1791274659, ());
  TEST_EQUAL(calls, 4, ());

  auto const second = service.Lookup(query);
  TEST(second.has_value(), ());
  TEST_EQUAL(calls, 4, ());

  now += std::chrono::seconds(61);
  auto const third = service.Lookup(query);
  TEST(third.has_value(), ());
  TEST_EQUAL(calls, 8, ());
}

UNIT_TEST(AvailabilityService_NoMatchInsideFeedStillCaches)
{
  auto const bodies = AmbiciBodies();
  int calls = 0;
  auto const http = [&](std::string const & url) -> std::optional<std::string>
  {
    ++calls;
    auto const it = bodies.find(url);
    if (it == bodies.end())
      return std::nullopt;
    return it->second;
  };
  auto const now = std::chrono::steady_clock::time_point{std::chrono::seconds(1000)};
  bike_share::AvailabilityService service(http, [&now] { return now; }, bike_share::Feeds());

  // Inside the AMBici box and outside Bicing, away from the four recorded docks.
  bike_share::MatchQuery const query{ms::LatLon(41.40, 2.00), "Nowhere", ""};
  TEST(!service.Lookup(query).has_value(), ());
  TEST_EQUAL(calls, 4, ());
  TEST(!service.Lookup(query).has_value(), ());
  TEST_EQUAL(calls, 4, ());
}

UNIT_TEST(AvailabilityService_OutsideEveryFeedDoesNotFetch)
{
  int calls = 0;
  auto const http = [&](std::string const &) -> std::optional<std::string>
  {
    ++calls;
    return std::nullopt;
  };
  auto const now = std::chrono::steady_clock::time_point{};
  bike_share::AvailabilityService service(http, [&now] { return now; }, bike_share::Feeds());
  TEST(!service.Lookup({ms::LatLon(40.4, -3.7), "", ""}).has_value(), ());
  TEST_EQUAL(calls, 0, ());
}

UNIT_TEST(AvailabilityService_HttpFailureIsSilentAndNotCached)
{
  int calls = 0;
  auto const http = [&](std::string const &) -> std::optional<std::string>
  {
    ++calls;
    return std::nullopt;
  };
  auto const now = std::chrono::steady_clock::time_point{};
  bike_share::AvailabilityService service(http, [&now] { return now; }, bike_share::Feeds());
  bike_share::MatchQuery const query{ms::LatLon(41.384018, 2.047004), "", ""};
  TEST(!service.Lookup(query).has_value(), ());
  TEST_EQUAL(calls, 1, ());
  TEST(!service.Lookup(query).has_value(), ());
  TEST_EQUAL(calls, 2, ());
}

UNIT_TEST(AvailabilityService_ZeroTtlRefetches)
{
  std::string const discovery = R"({
    "ttl": 0,
    "data": {"feeds": [
      {"name": "station_information", "url": "https://ttl0.test/info"},
      {"name": "station_status", "url": "https://ttl0.test/status"}
    ]}
  })";
  std::string const info = R"({
    "ttl": 0,
    "data": {"stations": [{"station_id": "1", "name": "Dock", "lat": 10.0, "lon": 10.0}]}
  })";
  std::string const status = R"({
    "ttl": 0,
    "last_updated": 50,
    "data": {"stations": [{"station_id": "1", "num_bikes_available": 2, "num_docks_available": 3, "is_installed": true}]}
  })";
  int calls = 0;
  auto const http = [&](std::string const & url) -> std::optional<std::string>
  {
    ++calls;
    if (url.find("gbfs.json") != std::string::npos)
      return discovery;
    if (url.find("info") != std::string::npos)
      return info;
    if (url.find("status") != std::string::npos)
      return status;
    return std::nullopt;
  };
  auto const now = std::chrono::steady_clock::time_point{};
  std::vector<bike_share::FeedDefinition> feeds = {
      {"ttl0", "Test", "https://ttl0.test/gbfs.json", {9.0, 9.0, 11.0, 11.0}}};
  bike_share::AvailabilityService service(http, [&now] { return now; }, feeds);

  auto const first = service.Lookup({ms::LatLon(10.0, 10.0), "Dock", ""});
  TEST(first.has_value(), ());
  TEST_EQUAL(first->m_bikes, 2, ());
  TEST_EQUAL(*first->m_docks, 3, ());
  TEST(!first->m_ebikes.has_value(), ());
  TEST_EQUAL(calls, 3, ());

  auto const second = service.Lookup({ms::LatLon(10.0, 10.0), "Dock", ""});
  TEST(second.has_value(), ());
  TEST_EQUAL(calls, 6, ());
}
}  // namespace availability_service_tests
