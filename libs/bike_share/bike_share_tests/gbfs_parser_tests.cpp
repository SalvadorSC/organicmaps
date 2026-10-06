#include "testing/testing.hpp"

#include "bike_share/gbfs_parser.hpp"

#include "bike_share/bike_share_tests/fixture.hpp"

#include <string>

namespace gbfs_parser_tests
{
UNIT_TEST(ParseDiscovery_PrefersEnglishFeeds)
{
  auto const discovered = bike_share::ParseDiscovery(ReadBikeShareFixture("gbfs.json"));
  TEST(discovered.has_value(), ());
  TEST_EQUAL(discovered->m_ttlSec, 60, ());
  TEST(discovered->m_stationInformationUrl.find("/en/station_information.json") != std::string::npos, ());
  TEST(discovered->m_stationStatusUrl.find("/en/station_status.json") != std::string::npos, ());
  TEST(discovered->m_vehicleTypesUrl.find("/en/vehicle_types.json") != std::string::npos, ());
}

UNIT_TEST(ParseDiscovery_FlatFeedList)
{
  std::string const json = R"({
    "ttl": 15,
    "data": {
      "feeds": [
        {"name": "station_information", "url": "https://example.test/info"},
        {"name": "station_status", "url": "https://example.test/status"}
      ]
    }
  })";
  auto const discovered = bike_share::ParseDiscovery(json);
  TEST(discovered.has_value(), ());
  TEST_EQUAL(discovered->m_ttlSec, 15, ());
  TEST_EQUAL(discovered->m_stationInformationUrl, "https://example.test/info", ());
  TEST_EQUAL(discovered->m_stationStatusUrl, "https://example.test/status", ());
  TEST(discovered->m_vehicleTypesUrl.empty(), ());
}

UNIT_TEST(ParseDiscovery_InvalidJson)
{
  TEST(!bike_share::ParseDiscovery("{").has_value(), ());
  TEST(!bike_share::ParseDiscovery(R"({"data":{}})").has_value(), ());
}

UNIT_TEST(ParseStationInformation_AmbiciFixture)
{
  auto const feed = bike_share::ParseStationInformation(ReadBikeShareFixture("station_information.json"));
  TEST(feed.has_value(), ());
  TEST_EQUAL(feed->m_ttlSec, 60, ());
  TEST_EQUAL(feed->m_lastUpdated, 1791274675, ());
  TEST_EQUAL(feed->m_stations.size(), 4, ());

  bike_share::Station const * santFeliu = nullptr;
  for (auto const & station : feed->m_stations)
    if (station.m_id == "135355081")
      santFeliu = &station;
  TEST(santFeliu != nullptr, ());
  TEST_EQUAL(santFeliu->m_name, "RENFE Sant Feliu de Llobregat", ());
  TEST_EQUAL(santFeliu->m_shortName, "34920", ());
  TEST_ALMOST_EQUAL_ABS(santFeliu->m_point.m_lat, 41.384018, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(santFeliu->m_point.m_lon, 2.047004, 1e-6, ());
}

UNIT_TEST(ParseStationStatus_AmbiciEbikeSplit)
{
  auto status = bike_share::ParseStationStatus(ReadBikeShareFixture("station_status.json"));
  auto const types = bike_share::ParseVehicleTypes(ReadBikeShareFixture("vehicle_types.json"));
  TEST(status.has_value(), ());
  TEST(types.has_value(), ());
  TEST_EQUAL(types->at("204"), true, ());
  TEST_EQUAL(types->at("196"), false, ());

  bike_share::ApplyVehicleTypeSplit(*status, *types);

  auto const & mixed = status->m_byId.at("342754906");
  TEST_EQUAL(mixed.m_bikesAvailable, 19, ());
  TEST_EQUAL(*mixed.m_docksAvailable, 1, ());
  TEST_EQUAL(*mixed.m_ebikesAvailable, 18, ());
  TEST_EQUAL(*mixed.m_mechanicalAvailable, 1, ());
  TEST_EQUAL(mixed.m_lastReported, 1791274659, ());

  auto const & electricOnly = status->m_byId.at("162830923");
  TEST_EQUAL(electricOnly.m_bikesAvailable, 7, ());
  TEST_EQUAL(*electricOnly.m_ebikesAvailable, 7, ());
  TEST_EQUAL(*electricOnly.m_mechanicalAvailable, 0, ());
  TEST_EQUAL(*electricOnly.m_docksAvailable, 12, ());

  // Empty vehicle_types_available leaves the split unset.
  auto const & empty = status->m_byId.at("135349994");
  TEST_EQUAL(empty.m_bikesAvailable, 0, ());
  TEST(!empty.m_ebikesAvailable.has_value(), ());
  TEST(!empty.m_mechanicalAvailable.has_value(), ());
  TEST_EQUAL(*empty.m_docksAvailable, 19, ());
}

UNIT_TEST(ParseStationStatus_LegacySplitAndNumericId)
{
  std::string const json = R"({
    "last_updated": 100,
    "ttl": 30,
    "data": {
      "stations": [{
        "station_id": 42,
        "num_bikes_available": 13,
        "num_docks_available": 7,
        "is_installed": 1,
        "is_renting": false,
        "last_reported": 99,
        "num_bikes_available_types": {"mechanical": 10, "ebike": 3}
      }]
    }
  })";
  auto const status = bike_share::ParseStationStatus(json);
  TEST(status.has_value(), ());
  auto const & station = status->m_byId.at("42");
  TEST_EQUAL(station.m_bikesAvailable, 13, ());
  TEST_EQUAL(*station.m_docksAvailable, 7, ());
  TEST_EQUAL(*station.m_mechanicalAvailable, 10, ());
  TEST_EQUAL(*station.m_ebikesAvailable, 3, ());
  TEST(station.m_isInstalled, ());
  TEST(!station.m_isRenting, ());
  TEST_EQUAL(station.m_lastReported, 99, ());
  TEST_EQUAL(status->m_ttlSec, 30, ());
}
}  // namespace gbfs_parser_tests
