#include "testing/testing.hpp"

#include "metro_live/estimator.hpp"
#include "metro_live/parser.hpp"
#include "metro_live/service.hpp"

#include "drape_frontend/metro_train_heading.hpp"

#include "geometry/mercator.hpp"
#include "geometry/screenbase.hpp"

#include "coding/file_reader.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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

double HeadingDelta(double a, double b)
{
  double d = std::fmod(std::abs(a - b), 360.0);
  if (d > 180.0)
    d = 360.0 - d;
  return d;
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
  TEST_LESS(HeadingDelta(trains[0].m_headingDeg, 0.0), 5.0, ());
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

double DistanceToLine(ms::LatLon const & point, std::vector<ms::LatLon> const & shape)
{
  if (shape.size() < 2)
    return 1e9;
  double constexpr kMetersPerDeg = 111320.0;
  double constexpr kDegToRad = 0.017453292519943295;
  double const cosLat = std::cos(point.m_lat * kDegToRad);
  double best = 1e9;
  for (size_t i = 0; i + 1 < shape.size(); ++i)
  {
    auto const & a = shape[i];
    auto const & b = shape[i + 1];
    double const bx = (b.m_lon - a.m_lon) * kMetersPerDeg * cosLat;
    double const by = (b.m_lat - a.m_lat) * kMetersPerDeg;
    double const px = (point.m_lon - a.m_lon) * kMetersPerDeg * cosLat;
    double const py = (point.m_lat - a.m_lat) * kMetersPerDeg;
    double const len2 = bx * bx + by * by;
    double t = 0;
    if (len2 > 1.0)
      t = std::clamp((px * bx + py * by) / len2, 0.0, 1.0);
    best = std::min(best, std::hypot(px - t * bx, py - t * by));
  }
  return best;
}

// TMB stations sit east of the curve Organic Maps draws. The halfway train must
// follow that curve, not the straight chord through the TMB coordinates.
UNIT_TEST(Estimate_FollowsInjectedMapLine)
{
  metro_live::Station south;
  south.m_code = 1;
  south.m_order = 1;
  south.m_line = "L3";
  south.m_name = "South";
  south.m_point = ms::LatLon(41.39000, 2.15200);
  metro_live::Station north;
  north.m_code = 2;
  north.m_order = 2;
  north.m_line = "L3";
  north.m_name = "North";
  north.m_point = ms::LatLon(41.40000, 2.15200);

  metro_live::LinePath path;
  path.m_name = "L3";
  path.m_color = "1EB53A";
  path.m_shape = {south.m_point, north.m_point};
  auto network = metro_live::BuildNetwork({path}, {south, north});

  metro_live::MapTrack track;
  track.m_ref = "l3";
  track.m_shape = {ms::LatLon(41.39000, 2.15000), ms::LatLon(41.39500, 2.14800), ms::LatLon(41.40000, 2.15000)};
  track.m_stops = {track.m_shape.front(), track.m_shape.back()};
  metro_live::ApplyMapTracks(network, {track});

  auto const trains = metro_live::EstimateTrains(network, {Row("L3", "1", "run", 2, kNow + 43, "North")}, kNow);
  TEST_EQUAL(trains.size(), 1, ());
  ms::LatLon const placed(trains[0].m_lat, trains[0].m_lon);
  TEST_LESS(DistanceToLine(placed, track.m_shape), 15.0, (placed));
  TEST_LESS(placed.m_lon, 2.1505, (placed));
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
  for (auto const & train : trains)
  {
    for (auto const & line : network.m_lines)
    {
      if (line.m_name != train.m_line)
        continue;
      TEST_LESS(DistanceToLine(ms::LatLon(train.m_lat, train.m_lon), line.m_shape), 15.0,
                (train.m_line, train.m_nextStop));
    }
  }
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
  int off120 = 0;
  int compared = 0;
  for (auto const & train : trains)
  {
    ms::LatLon next;
    bool found = false;
    for (auto const & line : network.m_lines)
    {
      if (line.m_name != train.m_line)
        continue;
      for (auto const & station : line.m_stations)
      {
        if (station.m_station.m_name == train.m_nextStop)
        {
          next = station.m_station.m_point;
          found = true;
        }
      }
    }
    if (!found)
      continue;
    double const east = (next.m_lon - train.m_lon) * std::cos(train.m_lat * 0.017453292519943295);
    double const north = next.m_lat - train.m_lat;
    if (east * east + north * north < 1e-12)
      continue;
    double deg = std::atan2(east, north) / 0.017453292519943295;
    if (deg < 0)
      deg += 360.0;
    ++compared;
    if (HeadingDelta(train.m_headingDeg, deg) > 120)
      ++off120;
  }
  // A curve can leave the chord to the next stop. It must not point backwards.
  TEST_GREATER(compared, 50, ());
  TEST_EQUAL(off120, 0, ());
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
UNIT_TEST(Estimate_HeadingFollowsTravel)
{
  auto const network = SampleNetwork();
  // Toward Alpha: Bravo is approached from Charlie, so the tangent points south.
  auto const south = metro_live::EstimateTrains(network, {Row("L1", "0012", "50", 2, kNow + 40, "Alpha")}, kNow);
  TEST_EQUAL(south.size(), 1, ());
  TEST_LESS(HeadingDelta(south[0].m_headingDeg, 180.0), 5.0, (south[0].m_headingDeg));

  // North, then east. The train is on the east leg, not on the chord through the corner.
  std::string_view constexpr kBendStations = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.150,41.400]},
       "properties":{"CODI_ESTACIO":1,"NOM_ESTACIO":"South","ORDRE_ESTACIO":1,"NOM_LINIA":"L4","COLOR_LINIA":"F7A30E"}},
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.150,41.401]},
       "properties":{"CODI_ESTACIO":2,"NOM_ESTACIO":"Bend","ORDRE_ESTACIO":2,"NOM_LINIA":"L4","COLOR_LINIA":"F7A30E"}},
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.152,41.401]},
       "properties":{"CODI_ESTACIO":3,"NOM_ESTACIO":"East","ORDRE_ESTACIO":3,"NOM_LINIA":"L4","COLOR_LINIA":"F7A30E"}}
    ]
  })";
  std::string_view constexpr kBendLines = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.150,41.400],[2.150,41.401],[2.152,41.401]]]},
       "properties":{"NOM_LINIA":"L4","COLOR_LINIA":"F7A30E","NOM_TIPUS_TRANSPORT":"METRO"}}
    ]
  })";
  auto stations = metro_live::ParseStations(kBendStations);
  auto lines = metro_live::ParseLines(kBendLines);
  TEST(stations.has_value(), ());
  TEST(lines.has_value(), ());
  auto const bend = metro_live::BuildNetwork(*lines, *stations);
  auto const east = metro_live::EstimateTrains(bend, {Row("L4", "1", "run", 3, kNow + 43, "East")}, kNow);
  TEST_EQUAL(east.size(), 1, ());
  TEST_LESS(HeadingDelta(east[0].m_headingDeg, 90.0), 15.0, (east[0].m_headingDeg));
  TEST_GREATER(east[0].m_lon, 2.1505, ());
}

// A few-metre out-and-back at a vertex used to become the arrow heading.
UNIT_TEST(Estimate_HeadingSkipsJoinSpur)
{
  std::string_view constexpr kStations = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.150,41.400]},
       "properties":{"CODI_ESTACIO":1,"NOM_ESTACIO":"South","ORDRE_ESTACIO":1,"NOM_LINIA":"L1","COLOR_LINIA":"CE1126"}},
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.150,41.401]},
       "properties":{"CODI_ESTACIO":2,"NOM_ESTACIO":"North","ORDRE_ESTACIO":2,"NOM_LINIA":"L1","COLOR_LINIA":"CE1126"}}
    ]
  })";
  // North, then 8 m east and back. The train has arrived at North.
  std::string_view constexpr kLines = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.150,41.400],[2.150,41.401],[2.1501,41.401],[2.150,41.401]]]},
       "properties":{"NOM_LINIA":"L1","COLOR_LINIA":"CE1126","NOM_TIPUS_TRANSPORT":"METRO"}}
    ]
  })";
  auto stations = metro_live::ParseStations(kStations);
  auto lines = metro_live::ParseLines(kLines);
  TEST(stations.has_value(), ());
  TEST(lines.has_value(), ());
  auto const network = metro_live::BuildNetwork(*lines, *stations);
  auto const arrived = metro_live::EstimateTrains(network, {Row("L1", "1", "run", 2, kNow, "North")}, kNow);
  TEST_EQUAL(arrived.size(), 1, ());
  TEST_LESS(HeadingDelta(arrived[0].m_headingDeg, 0.0), 15.0, (arrived[0].m_headingDeg));

  // Sitting on the south end of a spur that leaves the bend toward the west.
  std::string_view constexpr kBendStations = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.150,41.400]},
       "properties":{"CODI_ESTACIO":1,"NOM_ESTACIO":"South","ORDRE_ESTACIO":1,"NOM_LINIA":"L4","COLOR_LINIA":"F7A30E"}},
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.150,41.401]},
       "properties":{"CODI_ESTACIO":2,"NOM_ESTACIO":"Bend","ORDRE_ESTACIO":2,"NOM_LINIA":"L4","COLOR_LINIA":"F7A30E"}},
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.152,41.401]},
       "properties":{"CODI_ESTACIO":3,"NOM_ESTACIO":"East","ORDRE_ESTACIO":3,"NOM_LINIA":"L4","COLOR_LINIA":"F7A30E"}}
    ]
  })";
  std::string_view constexpr kBendLines = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.150,41.400],[2.150,41.401],[2.1499,41.401],[2.150,41.401],[2.152,41.401]]]},
       "properties":{"NOM_LINIA":"L4","COLOR_LINIA":"F7A30E","NOM_TIPUS_TRANSPORT":"METRO"}}
    ]
  })";
  auto bendStations = metro_live::ParseStations(kBendStations);
  auto bendLines = metro_live::ParseLines(kBendLines);
  TEST(bendStations.has_value(), ());
  TEST(bendLines.has_value(), ());
  auto const bend = metro_live::BuildNetwork(*bendLines, *bendStations);
  // ETA covers a full segment, so the train is still at Bend, on the spur vertex.
  auto const east = metro_live::EstimateTrains(bend, {Row("L4", "1", "run", 3, kNow + 87, "East")}, kNow);
  TEST_EQUAL(east.size(), 1, ());
  TEST_LESS(HeadingDelta(east[0].m_headingDeg, 90.0), 20.0, (east[0].m_headingDeg));
  auto const west = metro_live::EstimateTrains(bend, {Row("L4", "2", "back", 1, kNow + 87, "South")}, kNow);
  TEST_EQUAL(west.size(), 1, ());
  TEST_LESS(HeadingDelta(west[0].m_headingDeg, 180.0), 20.0, (west[0].m_headingDeg));
}

// Two services on one NW–SE segment, with an east-west lead-in at the vertex.
// Stopping the 25 m walk inside that lead-in made them face each other
// horizontally (the Horta bowtie) instead of opposite ways along the segment.
UNIT_TEST(Estimate_HeadingHortaBowtie)
{
  std::string_view constexpr kStations = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.16000,41.43000]},
       "properties":{"CODI_ESTACIO":1,"NOM_ESTACIO":"Horta","ORDRE_ESTACIO":1,"NOM_LINIA":"L5","COLOR_LINIA":"005A97"}},
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.16400,41.42600]},
       "properties":{"CODI_ESTACIO":2,"NOM_ESTACIO":"Carmel","ORDRE_ESTACIO":2,"NOM_LINIA":"L5","COLOR_LINIA":"005A97"}},
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.15600,41.43400]},
       "properties":{"CODI_ESTACIO":3,"NOM_ESTACIO":"Vilapicina","ORDRE_ESTACIO":0,"NOM_LINIA":"L5","COLOR_LINIA":"005A97"}}
    ]
  })";
  // Horta, 75 m east, then the NW–SE run to Carmel. The lead-in is long enough
  // that a 25 m walk stops inside it.
  std::string_view constexpr kLines = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.15600,41.43400],[2.16000,41.43000],[2.16090,41.43000],[2.16400,41.42600]]]},
       "properties":{"NOM_LINIA":"L5","COLOR_LINIA":"005A97","NOM_TIPUS_TRANSPORT":"METRO"}}
    ]
  })";
  auto stations = metro_live::ParseStations(kStations);
  auto lines = metro_live::ParseLines(kLines);
  TEST(stations.has_value(), ());
  TEST(lines.has_value(), ());
  auto const network = metro_live::BuildNetwork(*lines, *stations);
  // Full segment still ahead: the train is at Horta, outbound toward Carmel.
  auto const outbound = metro_live::EstimateTrains(network, {Row("L5", "1", "se", 2, kNow + 87, "Carmel")}, kNow);
  // Arrived at Horta from Carmel, so the approach is the same segment inbound.
  auto const inbound = metro_live::EstimateTrains(network, {Row("L5", "2", "nw", 1, kNow, "Vilapicina")}, kNow);
  TEST_EQUAL(outbound.size(), 1, ());
  TEST_EQUAL(inbound.size(), 1, ());
  // Station chord Horta → Carmel is about 143° (NW–SE), not east-west.
  double constexpr kAxis = 143.0;
  TEST_LESS(HeadingDelta(outbound[0].m_headingDeg, kAxis), 20.0, (outbound[0].m_headingDeg));
  TEST_LESS(HeadingDelta(inbound[0].m_headingDeg, kAxis + 180.0), 20.0, (inbound[0].m_headingDeg));
  TEST_LESS(std::abs(HeadingDelta(outbound[0].m_headingDeg, inbound[0].m_headingDeg) - 180.0), 20.0,
            (outbound[0].m_headingDeg, inbound[0].m_headingDeg));
  TEST_GREATER(HeadingDelta(outbound[0].m_headingDeg, 90.0), 30.0, (outbound[0].m_headingDeg));
  TEST_GREATER(HeadingDelta(inbound[0].m_headingDeg, 270.0), 30.0, (inbound[0].m_headingDeg));
  // Between the stations the same arc is used, so the pair stays on that axis.
  auto const between = metro_live::EstimateTrains(network, {Row("L5", "1", "se", 2, kNow + 43, "Carmel")}, kNow);
  auto const betweenBack =
      metro_live::EstimateTrains(network, {Row("L5", "2", "nw", 1, kNow + 43, "Vilapicina")}, kNow);
  TEST_EQUAL(between.size(), 1, ());
  TEST_EQUAL(betweenBack.size(), 1, ());
  TEST_LESS(HeadingDelta(between[0].m_headingDeg, kAxis), 20.0, (between[0].m_headingDeg));
  TEST_LESS(HeadingDelta(betweenBack[0].m_headingDeg, kAxis + 180.0), 20.0, (betweenBack[0].m_headingDeg));
}

double RadiansDelta(double a, double b)
{
  double constexpr kPi = 3.141592653589793;
  double d = std::fmod(std::abs(a - b), 2.0 * kPi);
  if (d > kPi)
    d = 2.0 * kPi - d;
  return d;
}

// Tip (0, -1) rotated by theta lands on (sin theta, -cos theta), Y down.
void TipOffset(float theta, double & x, double & y)
{
  x = std::sin(theta);
  y = -std::cos(theta);
}

UNIT_TEST(ChevronScreenAngle_MatchesTravel)
{
  std::string_view constexpr kStations = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.150,41.400]},
       "properties":{"CODI_ESTACIO":1,"NOM_ESTACIO":"West","ORDRE_ESTACIO":1,"NOM_LINIA":"L1","COLOR_LINIA":"CE1126"}},
      {"type":"Feature","geometry":{"type":"Point","coordinates":[2.152,41.400]},
       "properties":{"CODI_ESTACIO":2,"NOM_ESTACIO":"East","ORDRE_ESTACIO":2,"NOM_LINIA":"L1","COLOR_LINIA":"CE1126"}}
    ]
  })";
  // Stored west-to-east. The opposite direction must flip, not follow storage order.
  std::string_view constexpr kLines = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","geometry":{"type":"MultiLineString","coordinates":[[[2.150,41.400],[2.152,41.400]]]},
       "properties":{"NOM_LINIA":"L1","COLOR_LINIA":"CE1126","NOM_TIPUS_TRANSPORT":"METRO"}}
    ]
  })";
  auto stations = metro_live::ParseStations(kStations);
  auto lines = metro_live::ParseLines(kLines);
  TEST(stations.has_value(), ());
  TEST(lines.has_value(), ());
  auto const network = metro_live::BuildNetwork(*lines, *stations);
  auto const east = metro_live::EstimateTrains(network, {Row("L1", "1", "go", 2, kNow + 40, "East")}, kNow);
  auto const west = metro_live::EstimateTrains(network, {Row("L1", "2", "back", 1, kNow + 40, "West")}, kNow);
  TEST_EQUAL(east.size(), 1, ());
  TEST_EQUAL(west.size(), 1, ());
  TEST_LESS(HeadingDelta(east[0].m_headingDeg, 90.0), 5.0, (east[0].m_headingDeg));
  TEST_LESS(HeadingDelta(west[0].m_headingDeg, 270.0), 5.0, (west[0].m_headingDeg));

  double constexpr kPi = 3.141592653589793;
  double constexpr kFew = 5.0 * kPi / 180.0;
  ScreenBase screen;
  screen.OnSize(0, 0, 1000, 2000);
  m2::PointD const merc = mercator::FromLatLon(east[0].m_lat, east[0].m_lon);
  screen.SetFromParams(merc, 0.0, 1e-5);
  auto const headingRad = [](double deg) { return static_cast<float>(deg * kPi / 180.0); };
  float const eastAngle = df::ChevronScreenAzimuth(screen, merc, headingRad(east[0].m_headingDeg));
  float const westAngle = df::ChevronScreenAzimuth(screen, merc, headingRad(west[0].m_headingDeg));
  // North-up: east travel puts the tip to the right, west travel to the left.
  // North and northwest lock the -Y tip against a reflection, which would
  // leave east-west alone and turn a diagonal track into a bowtie.
  TEST_LESS(RadiansDelta(eastAngle, kPi / 2.0), kFew, (eastAngle));
  TEST_LESS(RadiansDelta(westAngle, -kPi / 2.0), kFew, (westAngle));
  TEST_LESS(RadiansDelta(eastAngle - westAngle, kPi), kFew, (eastAngle, westAngle));
  float const northAngle = df::ChevronScreenAzimuth(screen, merc, headingRad(0.0));
  float const nwAngle = df::ChevronScreenAzimuth(screen, merc, headingRad(315.0));
  TEST_LESS(RadiansDelta(northAngle, 0.0), kFew, (northAngle));
  TEST_LESS(RadiansDelta(nwAngle, -kPi / 4.0), kFew, (nwAngle));

  screen.SetAngle(kPi / 2.0);
  float const rotated = df::ChevronScreenAzimuth(screen, merc, headingRad(east[0].m_headingDeg));
  ms::LatLon const ll(east[0].m_lat, east[0].m_lon);
  double const cosLat = std::cos(ll.m_lat * kPi / 180.0);
  m2::PointD const ahead = mercator::FromLatLon(ms::LatLon(ll.m_lat, ll.m_lon + (1.0 / 111320.0) / cosLat));
  m2::PointD const s0 = screen.GtoP(merc);
  m2::PointD const s1 = screen.GtoP(ahead);
  double const dx = s1.x - s0.x;
  double const dy = s1.y - s0.y;
  double tx = 0;
  double ty = 0;
  TipOffset(rotated, tx, ty);
  double const len = std::sqrt(dx * dx + dy * dy);
  TEST_GREATER(len, 0.1, ());
  double const dot = (tx * dx + ty * dy) / len;
  TEST_GREATER(dot, std::cos(kFew), (rotated, dot));
  // The map angle moved the tip. It is not left at the north-up value.
  TEST_GREATER(RadiansDelta(rotated, eastAngle), kPi / 4.0, (rotated, eastAngle));
}

}  // namespace metro_tests
