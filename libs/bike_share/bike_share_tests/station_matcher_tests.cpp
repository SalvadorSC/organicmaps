#include "testing/testing.hpp"

#include "bike_share/feed_catalog.hpp"
#include "bike_share/station_matcher.hpp"

#include "geometry/distance_on_sphere.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace station_matcher_tests
{
using bike_share::FeedStations;
using bike_share::MatchQuery;
using bike_share::Station;
using bike_share::StationStatus;

double constexpr kLat = 41.384018;
double constexpr kLon = 2.047004;

Station MakeStation(std::string id, std::string name, std::string shortName, double lat, double lon)
{
  Station station;
  station.m_id = std::move(id);
  station.m_name = std::move(name);
  station.m_shortName = std::move(shortName);
  station.m_point = ms::LatLon(lat, lon);
  return station;
}

StationStatus MakeStatus(std::string id, int bikes, int docks, bool installed = true)
{
  StationStatus status;
  status.m_id = std::move(id);
  status.m_bikesAvailable = bikes;
  status.m_docksAvailable = docks;
  status.m_isInstalled = installed;
  return status;
}

double NorthOf(double meters)
{
  return kLat + meters / 111320.0;
}

UNIT_TEST(MatchStation_NearestWithinRadius)
{
  std::vector<Station> stations = {
      MakeStation("near", "Near dock", "1", NorthOf(10), kLon),
      MakeStation("far", "Far dock", "2", NorthOf(40), kLon),
  };
  std::unordered_map<std::string, StationStatus> status = {
      {"near", MakeStatus("near", 4, 1)},
      {"far", MakeStatus("far", 9, 2)},
  };
  std::vector<FeedStations> feeds = {{"AMBici", &stations, &status}};

  auto const match = bike_share::MatchStation(feeds, MatchQuery{ms::LatLon(kLat, kLon), "", ""});
  TEST(match.has_value(), ());
  TEST_EQUAL(match->m_stationName, "Near dock", ());
  TEST_EQUAL(match->m_status.m_bikesAvailable, 4, ());
  TEST_LESS(match->m_distanceM, 20.0, ());
}

UNIT_TEST(MatchStation_PrefersRefOverCloserDock)
{
  std::vector<Station> stations = {
      MakeStation("close", "Close", "1", NorthOf(5), kLon),
      MakeStation("135355081", "RENFE Sant Feliu de Llobregat", "34920", NorthOf(30), kLon),
  };
  std::unordered_map<std::string, StationStatus> status = {
      {"close", MakeStatus("close", 1, 1)},
      {"135355081", MakeStatus("135355081", 3, 12)},
  };
  std::vector<FeedStations> feeds = {{"AMBici", &stations, &status}};

  auto const match = bike_share::MatchStation(feeds, MatchQuery{ms::LatLon(kLat, kLon), "Something else", "034920"});
  TEST(match.has_value(), ());
  TEST_EQUAL(match->m_stationName, "RENFE Sant Feliu de Llobregat", ());
  TEST_EQUAL(match->m_status.m_bikesAvailable, 3, ());
}

UNIT_TEST(MatchStation_PrefersNameOverCloserDock)
{
  std::vector<Station> stations = {
      MakeStation("close", "Other", "1", NorthOf(5), kLon),
      MakeStation("135355081", "RENFE Sant Feliu de Llobregat", "34920", NorthOf(30), kLon),
  };
  std::unordered_map<std::string, StationStatus> status = {
      {"close", MakeStatus("close", 1, 1)},
      {"135355081", MakeStatus("135355081", 3, 12)},
  };
  std::vector<FeedStations> feeds = {{"AMBici", &stations, &status}};

  auto const match = bike_share::MatchStation(feeds, MatchQuery{ms::LatLon(kLat, kLon), "Sant Feliu de Llobregat", ""});
  TEST(match.has_value(), ());
  TEST_EQUAL(match->m_stationName, "RENFE Sant Feliu de Llobregat", ());
}

UNIT_TEST(MatchStation_OutsideRadiusReturnsNothing)
{
  std::vector<Station> stations = {MakeStation("only", "Only", "1", NorthOf(80), kLon)};
  std::unordered_map<std::string, StationStatus> status = {{"only", MakeStatus("only", 2, 2)}};
  std::vector<FeedStations> feeds = {{"AMBici", &stations, &status}};

  double const distanceM = ms::DistanceOnEarth(ms::LatLon(kLat, kLon), stations.front().m_point);
  TEST_GREATER(distanceM, bike_share::kMatchRadiusM, ());
  auto const match = bike_share::MatchStation(feeds, MatchQuery{ms::LatLon(kLat, kLon), "Only", "1"});
  TEST(!match.has_value(), ());
}

UNIT_TEST(MatchStation_SkipsStationThatIsNotInstalled)
{
  std::vector<Station> stations = {MakeStation("gone", "Gone", "1", kLat, kLon)};
  std::unordered_map<std::string, StationStatus> status = {{"gone", MakeStatus("gone", 2, 2, false)}};
  std::vector<FeedStations> feeds = {{"AMBici", &stations, &status}};

  auto const match = bike_share::MatchStation(feeds, MatchQuery{ms::LatLon(kLat, kLon), "Gone", "1"});
  TEST(!match.has_value(), ());
}

UNIT_TEST(FeedCatalog_SantFeliuIsAmbiciOnly)
{
  auto const feeds = bike_share::FeedsCovering(ms::LatLon(41.384018, 2.047004));
  TEST_EQUAL(feeds.size(), 1, ());
  TEST_EQUAL(feeds.front()->m_id, "ambici", ());

  auto const molins = bike_share::FeedsCovering(ms::LatLon(41.409661, 2.02074));
  TEST_EQUAL(molins.size(), 1, ());
  TEST_EQUAL(molins.front()->m_id, "ambici", ());
}

UNIT_TEST(FeedCatalog_CentralBarcelonaOverlaps)
{
  auto const feeds = bike_share::FeedsCovering(ms::LatLon(41.387, 2.17));
  TEST_EQUAL(feeds.size(), 2, ());
  TEST(bike_share::FeedsCovering(ms::LatLon(40.4, -3.7)).empty(), ());
}
}  // namespace station_matcher_tests
