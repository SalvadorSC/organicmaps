#include "testing/testing.hpp"

#include "bus_live/stop_matcher.hpp"
#include "bus_live/tmb_parser.hpp"

#include "coding/file_reader.hpp"

#include <cstdlib>
#include <string>

namespace tmb_parser_tests
{
std::string_view constexpr kStop = R"({
  "timestamp": 1791287276758,
  "parades": [{
    "codi_parada": "1720",
    "nom_parada": "Av Industria - Walden",
    "linies_trajectes": [{
      "transit_namespace": "bus",
      "nom_linia": "157",
      "desti_trajecte": "Sant Joan Despi",
      "propers_busos": [{"temps_arribada": 1791287778000}, {"temps_arribada": 1791289041000}]
    }, {
      "transit_namespace": "amb",
      "codi_linia": "L21",
      "nom_linia": "L21",
      "desti_trajecte": "St. Feliu L.",
      "propers_busos": [{"temps_arribada": 1791287837000}]
    }]
  }]
})";

UNIT_TEST(TmbArrivals_ReadsMillisecondsAndLineNames)
{
  auto const feed = bus_live::ParseTmbArrivals(kStop);
  TEST(feed.has_value(), ());
  TEST_EQUAL(feed->m_updatedUnixSec, 1791287276, ());
  TEST_EQUAL(feed->m_arrivals.size(), 3, ());
  TEST_EQUAL(feed->m_arrivals[0].m_line, "157", ());
  TEST_EQUAL(feed->m_arrivals[0].m_destination, "Sant Joan Despi", ());
  TEST_EQUAL(feed->m_arrivals[0].m_etaUnixSec, 1791287778, ());
  TEST(feed->m_arrivals[0].m_fromTmb, ());
  TEST_EQUAL(feed->m_arrivals[2].m_line, "L21", ());
  TEST(!bus_live::ParseTmbArrivals("{").has_value(), ());
}

UNIT_TEST(TmbCatalog_AcceptsGeoJsonAndParades)
{
  std::string_view constexpr kGeoJson =
      R"({"type":"FeatureCollection","features":[{"type":"Feature","geometry":{"type":"Point","coordinates":[2.0478329,41.3822242]},"properties":{"CODI_PARADA":"108","NOM_PARADA":"Pl Espanya"}}]})";
  auto const geo = bus_live::ParseTmbCatalog(kGeoJson);
  TEST(geo.has_value(), ());
  TEST_EQUAL(geo->size(), 1, ());
  TEST_EQUAL(geo->front().m_code, "108", ());
  TEST_ALMOST_EQUAL_ABS(geo->front().m_point.m_lat, 41.3822242, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(geo->front().m_point.m_lon, 2.0478329, 1e-6, ());

  auto const rows = bus_live::ParseTmbCatalog(
      R"({"parades":[{"codi_parada":"001720","nom_parada":"Walden","lat":"41.38","lon":"2.06"}]})");
  TEST(rows.has_value(), ());
  TEST_EQUAL(rows->front().m_code, "1720", ());
  auto const empty = bus_live::ParseTmbCatalog("{}");
  TEST(empty.has_value(), ());
  TEST(empty->empty(), ());
  TEST(!bus_live::ParseTmbCatalog("not json").has_value(), ());
}

bus_live::TransitStop const * FindCode(std::vector<bus_live::TransitStop> const & stops, std::string const & code)
{
  for (auto const & stop : stops)
    if (stop.m_code == code)
      return &stop;
  return nullptr;
}

// Real TMB catalog rows (integer CODI_PARADA, GeoJSON [lon, lat], null properties).
// ID_PARADA is the internal feature id and must not be used as the iTransit code.
UNIT_TEST(TmbCatalog_ResolvesCityStopsFromRealSchema)
{
  std::string json;
  FileReader(BUS_LIVE_TEST_DATA_DIR "/tmb_parades_sample.json").ReadAsString(json);
  auto const stops = bus_live::ParseTmbCatalog(json);
  TEST(stops.has_value(), ());
  TEST_EQUAL(stops->size(), 3, ());

  auto const * espanya = FindCode(*stops, "108");
  auto const * molina = FindCode(*stops, "559");
  auto const * walden = FindCode(*stops, "1720");
  TEST(espanya != nullptr, ());
  TEST(molina != nullptr, ());
  TEST(walden != nullptr, ());
  TEST_EQUAL(espanya->m_name, "Pl Espanya - FGC", ());
  TEST_EQUAL(molina->m_name, "Pl Molina", ());
  TEST_EQUAL(walden->m_name, "Av Indústria - Walden", ());
  TEST_NOT_EQUAL(espanya->m_code, "698907", ());
  TEST_NOT_EQUAL(molina->m_code, "692031", ());
  TEST_NOT_EQUAL(walden->m_code, "678982", ());
  TEST_ALMOST_EQUAL_ABS(espanya->m_point.m_lat, 41.37376223, 1e-7, ());
  TEST_ALMOST_EQUAL_ABS(espanya->m_point.m_lon, 2.1471762, 1e-7, ());
  TEST_ALMOST_EQUAL_ABS(molina->m_point.m_lat, 41.4014074, 1e-7, ());
  TEST_ALMOST_EQUAL_ABS(molina->m_point.m_lon, 2.14737677, 1e-7, ());
  TEST_ALMOST_EQUAL_ABS(walden->m_point.m_lat, 41.38049035, 1e-7, ());
  TEST_ALMOST_EQUAL_ABS(walden->m_point.m_lon, 2.06690935, 1e-7, ());

  auto const near = [](ms::LatLon const & point, std::string name)
  {
    bus_live::StopQuery query;
    query.m_point = ms::LatLon(point.m_lat + 0.000135, point.m_lon);
    query.m_name = std::move(name);
    return query;
  };
  auto const matched108 = bus_live::MatchStop(*stops, near(espanya->m_point, "Pl. Espanya"));
  auto const matched559 = bus_live::MatchStop(*stops, near(molina->m_point, "Pl. Molina"));
  TEST(matched108.has_value(), ());
  TEST(matched559.has_value(), ());
  TEST_EQUAL(matched108->m_stop.m_code, "108", ());
  TEST_EQUAL(matched559->m_stop.m_code, "559", ());
  TEST(matched108->m_name, ());
  TEST(matched559->m_name, ());
  TEST_LESS(matched108->m_distanceM, bus_live::kMatchRadiusM, ());
  TEST_LESS(matched559->m_distanceM, bus_live::kMatchRadiusM, ());
}

// Opt-in check of the full published catalog (not committed; it is ~2 MB).
//   BUS_LIVE_TMB_CATALOG=/path/to/tmb_parades_sample.json
UNIT_TEST(TmbCatalog_FullFileIfPresent)
{
  char const * path = std::getenv("BUS_LIVE_TMB_CATALOG");
  if (path == nullptr || path[0] == '\0')
    return;

  std::string json;
  FileReader(path).ReadAsString(json);
  auto const stops = bus_live::ParseTmbCatalog(json);
  TEST(stops.has_value(), ("TMB catalog did not parse"));
  TEST_EQUAL(stops->size(), 2728, ());
  TEST(FindCode(*stops, "108") != nullptr, ());
  TEST(FindCode(*stops, "559") != nullptr, ());
  TEST(FindCode(*stops, "1720") != nullptr, ());
}
}  // namespace tmb_parser_tests
