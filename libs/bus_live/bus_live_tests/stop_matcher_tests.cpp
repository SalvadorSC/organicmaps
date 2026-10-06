#include "testing/testing.hpp"

#include "bus_live/stop_matcher.hpp"

#include <string>
#include <vector>

namespace stop_matcher_tests
{
double constexpr kLat = 41.3822242;
double constexpr kLon = 2.0478329;

bus_live::TransitStop MakeStop(std::string id, std::string code, std::string name, double lat, double lon)
{
  bus_live::TransitStop stop;
  stop.m_id = std::move(id);
  stop.m_code = std::move(code);
  stop.m_name = std::move(name);
  stop.m_point = ms::LatLon(lat, lon);
  return stop;
}

UNIT_TEST(CanonicalCode_StripsLeadingZeros)
{
  TEST_EQUAL(bus_live::CanonicalCode("001720"), "1720", ());
  TEST_EQUAL(bus_live::CanonicalCode("100037"), "100037", ());
  TEST_EQUAL(bus_live::CanonicalCode("L21"), "l21", ());
  TEST_EQUAL(bus_live::CanonicalCode("000"), "0", ());
}

UNIT_TEST(MatchStop_GeoNameAndRef)
{
  // ~5 m, ~33 m, ~89 m and ~156 m north of the query. 1 degree latitude is ~111317 m.
  auto const near = MakeStop("1", "1", "Other", kLat + 0.00005, kLon);
  auto const named = MakeStop("2", "2", "Pl Estacio Bertrand", kLat + 0.0003, kLon);
  auto const coded = MakeStop("001720", "1720", "Walden", kLat + 0.0008, kLon);
  auto const far = MakeStop("9", "9", "Far", kLat + 0.0014, kLon);
  std::vector<bus_live::TransitStop> const stops = {near, named, coded, far};

  bus_live::StopQuery query;
  query.m_point = ms::LatLon(kLat, kLon);
  query.m_name = "Pl. Estacio - Bertrand";
  auto const byName = bus_live::MatchStop(stops, query);
  TEST(byName.has_value(), ());
  TEST_EQUAL(byName->m_stop.m_id, "2", ());
  TEST(byName->m_name, ());
  TEST(!byName->m_ref, ());

  query.m_ref = "001720";
  auto const byRef = bus_live::MatchStop(stops, query);
  TEST(byRef.has_value(), ());
  TEST_EQUAL(byRef->m_stop.m_code, "1720", ());
  TEST(byRef->m_ref, ());

  query.m_ref = "9";
  auto const outside = bus_live::MatchStop(stops, query);
  TEST(outside.has_value(), ());
  TEST_EQUAL(outside->m_stop.m_id, "2", ());
  TEST(!outside->m_ref, ());

  bus_live::StopQuery madrid;
  madrid.m_point = ms::LatLon(40.4, -3.7);
  madrid.m_ref = "1720";
  TEST(!bus_live::MatchStop(stops, madrid).has_value(), ());
}
}  // namespace stop_matcher_tests
