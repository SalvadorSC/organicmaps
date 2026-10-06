#include "testing/testing.hpp"

#include "bus_live/tmb_parser.hpp"

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
}  // namespace tmb_parser_tests
