#include "testing/testing.hpp"

#include "metro_live/estimator.hpp"
#include "metro_live/parser.hpp"
#include "metro_live/service.hpp"

#include "coding/file_reader.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>

namespace metro_tests
{
std::string_view constexpr kStations = R"({
  "type":"FeatureCollection",
  "features":[
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.15,41.380]},
     "properties":{"CODI_ESTACIO":1,"NOM_ESTACIO":"Alpha","ORDRE_ESTACIO":1,"NOM_LINIA":"L1","COLOR_LINIA":"CE1126"}},
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.15,41.381]},
     "properties":{"CODI_ESTACIO":2,"NOM_ESTACIO":"Bravo","ORDRE_ESTACIO":2,"NOM_LINIA":"L1","COLOR_LINIA":"CE1126"}},
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.15,41.382]},
     "properties":{"CODI_ESTACIO":3,"NOM_ESTACIO":"Charlie","ORDRE_ESTACIO":3,"NOM_LINIA":"L1","COLOR_LINIA":"CE1126"}},
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.10,41.300]},
     "properties":{"CODI_ESTACIO":11,"NOM_ESTACIO":"South","ORDRE_ESTACIO":1,"NOM_LINIA":"L9S","COLOR_LINIA":"FB712B"}},
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.10,41.310]},
     "properties":{"CODI_ESTACIO":12,"NOM_ESTACIO":"Mid","ORDRE_ESTACIO":2,"NOM_LINIA":"L9S","COLOR_LINIA":"FB712B"}},
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.10,41.320]},
     "properties":{"CODI_ESTACIO":13,"NOM_ESTACIO":"Zona Universitaria","ORDRE_ESTACIO":3,"NOM_LINIA":"L9S","COLOR_LINIA":"FB712B"}},
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.20,41.450]},
     "properties":{"CODI_ESTACIO":21,"NOM_ESTACIO":"La Sagrera","ORDRE_ESTACIO":1,"NOM_LINIA":"L9N","COLOR_LINIA":"FB712B"}},
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.20,41.460]},
     "properties":{"CODI_ESTACIO":22,"NOM_ESTACIO":"Can Zam","ORDRE_ESTACIO":2,"NOM_LINIA":"L9N","COLOR_LINIA":"FB712B"}},
    {"type":"Feature","geometry":{"type":"Point","coordinates":[2.07,41.290]},
     "properties":{"CODI_ESTACIO":99,"NOM_ESTACIO":"Montjuic","ORDRE_ESTACIO":1,"NOM_LINIA":"FM","COLOR_LINIA":"004C38"}}
  ]
})";

std::string_view constexpr kLines = R"({
  "type":"FeatureCollection",
  "features":[
    {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.15,41.380],[2.15,41.381],[2.15,41.382]]]},
     "properties":{"NOM_LINIA":"L1","COLOR_LINIA":"CE1126","NOM_TIPUS_TRANSPORT":"METRO"}},
    {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.10,41.300],[2.10,41.310],[2.10,41.320]]]},
     "properties":{"NOM_LINIA":"L9S","COLOR_LINIA":"FB712B","NOM_TIPUS_TRANSPORT":"METRO"}},
    {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.20,41.450],[2.20,41.460]]]},
     "properties":{"NOM_LINIA":"L9N","COLOR_LINIA":"FB712B","NOM_TIPUS_TRANSPORT":"METRO"}},
    {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.07,41.290],[2.08,41.300]]]},
     "properties":{"NOM_LINIA":"FM","COLOR_LINIA":"004C38","NOM_TIPUS_TRANSPORT":"FUNICULAR"}}
  ]
})";

int64_t constexpr kNow = 1791287200;

metro_live::Network SampleNetwork()
{
  auto stations = metro_live::ParseStations(kStations);
  auto lines = metro_live::ParseLines(kLines);
  TEST(stations.has_value(), ());
  TEST(lines.has_value(), ());
  return metro_live::BuildNetwork(*lines, *stations);
}

metro_live::ArrivalObs Row(std::string line, std::string trajecte, std::string service, int station, int64_t eta,
                           std::string destination)
{
  metro_live::ArrivalObs row;
  row.m_line = std::move(line);
  row.m_trajecte = std::move(trajecte);
  row.m_service = std::move(service);
  row.m_station = station;
  row.m_etaUnixSec = eta;
  row.m_destination = std::move(destination);
  row.m_color = "CE1126";
  return row;
}

UNIT_TEST(Parse_RealSchemaSkipsFunicular)
{
  auto const stations = metro_live::ParseStations(kStations);
  auto const lines = metro_live::ParseLines(kLines);
  TEST(stations.has_value(), ());
  TEST_EQUAL(stations->size(), 9, ());
  TEST_EQUAL(stations->front().m_code, 1, ());
  TEST_ALMOST_EQUAL_ABS(stations->front().m_point.m_lat, 41.380, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(stations->front().m_point.m_lon, 2.15, 1e-9, ());
  TEST(lines.has_value(), ());
  TEST_EQUAL(lines->size(), 3, ());
  TEST_EQUAL(lines->front().m_name, "L1", ());
  TEST(!metro_live::ParseStations("not json").has_value(), ());

  std::string_view constexpr kArrivals = R"({
    "timestamp": 1791287200000,
    "linies": [{
      "nom_linia": "L1",
      "color_linia": "CE1126",
      "estacions": [{
        "codi_estacio": 2,
        "linies_trajectes": [{
          "codi_trajecte": "0011",
          "desti_trajecte": "Charlie",
          "propers_trens": [{"codi_servei": "116", "temps_arribada": 1791287240000}]
        }]
      }]
    }]
  })";
  auto const feed = metro_live::ParseArrivals(kArrivals);
  TEST(feed.has_value(), ());
  TEST_EQUAL(feed->m_updatedUnixSec, kNow, ());
  TEST_EQUAL(feed->m_rows.size(), 1, ());
  TEST_EQUAL(feed->m_rows[0].m_etaUnixSec, kNow + 40, ());
  TEST_EQUAL(feed->m_rows[0].m_service, "116", ());
}

UNIT_TEST(Estimate_InterpolatesIntoNextStation)
{
  auto const network = SampleNetwork();
  std::vector<metro_live::ArrivalObs> rows = {
      Row("L1", "0011", "116", 2, kNow + 40, "Charlie"),
      Row("L1", "0011", "116", 3, kNow + 130, "Charlie"),
  };
  auto const trains = metro_live::EstimateTrains(network, rows, kNow);
  TEST_EQUAL(trains.size(), 1, ());
  TEST_EQUAL(trains[0].m_line, "L1", ());
  TEST_EQUAL(trains[0].m_nextStop, "Bravo", ());
  TEST_EQUAL(trains[0].m_destination, "Charlie", ());
  TEST_GREATER(trains[0].m_lat, 41.3805, ());
  TEST_LESS(trains[0].m_lat, 41.381, ());
  TEST_ALMOST_EQUAL_ABS(trains[0].m_lon, 2.15, 1e-6, ());
}

UNIT_TEST(Estimate_TerminusStaysOnPlatform)
{
  auto const network = SampleNetwork();
  auto const trains = metro_live::EstimateTrains(network, {Row("L1", "0011", "100", 1, kNow + 30, "Charlie")}, kNow);
  TEST_EQUAL(trains.size(), 1, ());
  TEST_ALMOST_EQUAL_ABS(trains[0].m_lat, 41.380, 1e-6, ());
  TEST_EQUAL(trains[0].m_nextStop, "Alpha", ());
}

UNIT_TEST(Estimate_LongEtaSitsAtPreviousStation)
{
  auto const network = SampleNetwork();
  auto const trains = metro_live::EstimateTrains(network, {Row("L1", "0011", "100", 2, kNow + 200, "Charlie")}, kNow);
  TEST_EQUAL(trains.size(), 1, ());
  TEST_ALMOST_EQUAL_ABS(trains[0].m_lat, 41.380, 1e-4, ());
  TEST_EQUAL(trains[0].m_nextStop, "Bravo", ());
}

UNIT_TEST(Estimate_DropsStaleAndRecycledService)
{
  auto const network = SampleNetwork();
  std::vector<metro_live::ArrivalObs> rows = {
      Row("L1", "0011", "116", 2, kNow + 30, "Charlie"),
      Row("L1", "0011", "116", 3, kNow + 100, "Charlie"),
      Row("L1", "0011", "116", 1, kNow + 110, "Charlie"),
      Row("L1", "0011", "999", 3, kNow - 120, "Charlie"),
  };
  auto const trains = metro_live::EstimateTrains(network, rows, kNow);
  TEST_EQUAL(trains.size(), 1, ());
  TEST_EQUAL(trains[0].m_nextStop, "Bravo", ());
  TEST_GREATER(trains[0].m_lat, 41.380, ());
  TEST_LESS(trains[0].m_lat, 41.381, ());

  auto const gone = metro_live::EstimateTrains(network, {}, kNow);
  TEST(gone.empty(), ());
}

UNIT_TEST(Estimate_SeparateDirectionsAndL9Branches)
{
  auto const network = SampleNetwork();
  std::vector<metro_live::ArrivalObs> rows = {
      Row("L1", "0011", "50", 2, kNow + 40, "Charlie"),
      Row("L1", "0012", "50", 2, kNow + 40, "Alpha"),
      Row("L9S", "0911", "50", 12, kNow + 40, "Zona Universitaria"),
      Row("L9N", "0941", "50", 21, kNow + 40, "Can Zam"),
      Row("FM", "0991", "50", 99, kNow + 20, "Montjuic"),
  };
  auto const trains = metro_live::EstimateTrains(network, rows, kNow);
  TEST_EQUAL(trains.size(), 4, ());
  int l9s = 0;
  int l9n = 0;
  for (auto const & train : trains)
  {
    TEST_NOT_EQUAL(train.m_line, "FM", ());
    if (train.m_line == "L9S")
    {
      ++l9s;
      TEST_GREATER(train.m_lat, 41.30, ());
      TEST_LESS(train.m_lat, 41.32, ());
      TEST_LESS(train.m_lon, 2.15, ());
    }
    if (train.m_line == "L9N")
    {
      ++l9n;
      TEST_GREATER(train.m_lat, 41.44, ());
    }
  }
  TEST_EQUAL(l9s, 1, ());
  TEST_EQUAL(l9n, 1, ());
}

struct Env
{
  std::chrono::steady_clock::time_point m_steady{};
  std::chrono::system_clock::time_point m_wall{std::chrono::seconds{kNow}};
  metro_live::Credentials m_creds;
  int m_stations = 0;
  int m_lines = 0;
  int m_arrivals = 0;
  std::vector<std::string> m_urls;

  std::unique_ptr<metro_live::MetroService> Make()
  {
    return std::make_unique<metro_live::MetroService>([this](std::string const & url) -> std::optional<std::string>
    {
      m_urls.push_back(url);
      if (url.find("/transit/linies/metro/estacions") != std::string::npos)
      {
        ++m_stations;
        return std::string(kStations);
      }
      if (url.find("/transit/linies/metro") != std::string::npos)
      {
        ++m_lines;
        return std::string(kLines);
      }
      if (url.find("/itransit/metro/estacions") != std::string::npos)
      {
        ++m_arrivals;
        return std::string(R"({
          "timestamp": 1791287200000,
          "linies": [{"nom_linia":"L1","color_linia":"CE1126","estacions":[{
            "codi_estacio": 2,
            "linies_trajectes": [{"codi_trajecte":"0011","desti_trajecte":"Charlie",
              "propers_trens":[{"codi_servei":"116","temps_arribada":1791287240000},
                               {"codi_servei":"118","temps_arribada":1791287500000}]}
            ]}, {
            "codi_estacio": 3,
            "linies_trajectes": [{"codi_trajecte":"0011","desti_trajecte":"Charlie",
              "propers_trens":[{"codi_servei":"116","temps_arribada":1791287330000}]}]
          }]}]
        })");
      }
      return std::nullopt;
    }, [this] { return m_wall; }, [this] { return m_steady; }, [this] { return m_creds; });
  }
};

UNIT_TEST(MetroService_CachesStaticAndRefetchesLive)
{
  Env env;
  env.m_creds = {"id", "key"};
  auto service = env.Make();
  auto const first = service->Poll();
  TEST(!first.m_needsKey, ());
  TEST_EQUAL(first.m_trains.size(), 2, ());
  TEST_EQUAL(first.m_lines.size(), 3, ());
  TEST_EQUAL(first.m_lines[0].m_name, "L1", ());
  TEST_EQUAL(first.m_lines[1].m_name, "L9N", ());
  TEST_EQUAL(first.m_lines[2].m_name, "L9S", ());
  TEST_EQUAL(env.m_stations, 1, ());
  TEST_EQUAL(env.m_lines, 1, ());
  TEST_EQUAL(env.m_arrivals, 1, ());
  TEST(env.m_urls[0].find("app_id=id") != std::string::npos, ());
  TEST(env.m_urls[0].find("app_key=key") != std::string::npos, ());
  TEST(env.m_urls[2].find("/itransit/metro/estacions?") != std::string::npos, ());

  service->Poll();
  TEST_EQUAL(env.m_arrivals, 1, ());
  TEST_EQUAL(env.m_stations, 1, ());

  env.m_steady += std::chrono::seconds{21};
  service->Poll();
  TEST_EQUAL(env.m_arrivals, 2, ());
  TEST_EQUAL(env.m_stations, 1, ());
  TEST_EQUAL(env.m_lines, 1, ());

  env.m_steady += std::chrono::seconds{7 * 24 * 3600};
  service->Poll();
  TEST_EQUAL(env.m_stations, 2, ());
  TEST_EQUAL(env.m_lines, 2, ());
}

UNIT_TEST(MetroService_MissingKeyDoesNotFetch)
{
  Env env;
  auto service = env.Make();
  auto const result = service->Poll();
  TEST(result.m_needsKey, ());
  TEST(result.m_trains.empty(), ());
  TEST_EQUAL(env.m_stations, 0, ());
  TEST_EQUAL(env.m_arrivals, 0, ());
}

// Opt-in check of the recorded TMB responses (not committed).
//   METRO_LINES=... METRO_STATIONS=... METRO_ARRIVALS=...
UNIT_TEST(Estimate_RecordedFeedsIfPresent)
{
  char const * linesPath = std::getenv("METRO_LINES");
  char const * stationsPath = std::getenv("METRO_STATIONS");
  char const * arrivalsPath = std::getenv("METRO_ARRIVALS");
  if (linesPath == nullptr || stationsPath == nullptr || arrivalsPath == nullptr)
    return;
  std::string linesText;
  std::string stationsText;
  std::string arrivalsText;
  FileReader(linesPath).ReadAsString(linesText);
  FileReader(stationsPath).ReadAsString(stationsText);
  FileReader(arrivalsPath).ReadAsString(arrivalsText);
  auto const lines = metro_live::ParseLines(linesText);
  auto const stations = metro_live::ParseStations(stationsText);
  auto const feed = metro_live::ParseArrivals(arrivalsText);
  TEST(lines.has_value(), ());
  TEST(stations.has_value(), ());
  TEST(feed.has_value(), ());
  TEST_GREATER(lines->size(), 5, ());
  TEST_GREATER(stations->size(), 100, ());
  TEST_GREATER(feed->m_rows.size(), 100, ());
  auto const network = metro_live::BuildNetwork(*lines, *stations);
  int64_t const now = feed->m_updatedUnixSec == 0 ? kNow : feed->m_updatedUnixSec;
  auto const trains = metro_live::EstimateTrains(network, feed->m_rows, now);
  TEST_GREATER(trains.size(), 40, ());
  bool sawL1 = false;
  for (auto const & train : trains)
  {
    TEST_NOT_EQUAL(train.m_line, "FM", ());
    TEST_GREATER(train.m_lat, 41.2, ());
    TEST_LESS(train.m_lat, 41.6, ());
    TEST_GREATER(train.m_lon, 1.9, ());
    TEST_LESS(train.m_lon, 2.4, ());
    if (train.m_line == "L1")
      sawL1 = true;
  }
  TEST(sawL1, ());
  // This recording has no L9/L10 departures. When those rows exist they must place.
  bool feedL9 = false;
  for (auto const & row : feed->m_rows)
    if (row.m_line == "L9S" || row.m_line == "L9N")
      feedL9 = true;
  if (feedL9)
  {
    bool sawL9 = false;
    for (auto const & train : trains)
      if (train.m_line == "L9S" || train.m_line == "L9N")
        sawL9 = true;
    TEST(sawL9, ());
  }
}
}  // namespace metro_tests
