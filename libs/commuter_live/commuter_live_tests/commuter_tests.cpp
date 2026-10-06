#include "testing/testing.hpp"

#include "commuter_live/bearing.hpp"
#include "commuter_live/names.hpp"
#include "commuter_live/parser.hpp"
#include "commuter_live/service.hpp"
#include "commuter_live/snap.hpp"

#include "geometry/distance_on_sphere.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace commuter_tests
{
void AppendVarint(std::string & out, uint64_t value)
{
  while (value > 0x7f)
  {
    out.push_back(static_cast<char>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<char>(value));
}

void AppendTag(std::string & out, uint32_t field, uint32_t wire)
{
  AppendVarint(out, (static_cast<uint64_t>(field) << 3) | wire);
}

void AppendFloat(std::string & out, uint32_t field, float value)
{
  AppendTag(out, field, 5);
  char bytes[4];
  std::memcpy(bytes, &value, 4);
  out.append(bytes, 4);
}

void AppendString(std::string & out, uint32_t field, std::string_view text)
{
  AppendTag(out, field, 2);
  AppendVarint(out, text.size());
  out.append(text);
}

void AppendMessage(std::string & out, uint32_t field, std::string const & message)
{
  AppendTag(out, field, 2);
  AppendVarint(out, message.size());
  out.append(message);
}

void AppendInt(std::string & out, uint32_t field, uint64_t value)
{
  AppendTag(out, field, 0);
  AppendVarint(out, value);
}

std::string TripFeed(uint64_t etaUnixSec)
{
  std::string event;
  AppendInt(event, 2, etaUnixSec);
  std::string stop;
  AppendMessage(stop, 2, event);
  AppendString(stop, 4, "71600");
  std::string descriptor;
  AppendString(descriptor, 1, "5177M77552R4");
  std::string update;
  AppendMessage(update, 1, descriptor);
  AppendMessage(update, 2, stop);
  std::string entity;
  AppendMessage(entity, 3, update);
  std::string feed;
  AppendMessage(feed, 2, entity);
  return feed;
}

std::string Vehicle(std::string_view entityId, std::string_view label, float lat, float lon,
                    std::optional<float> bearing)
{
  std::string position;
  AppendFloat(position, 1, lat);
  AppendFloat(position, 2, lon);
  if (bearing)
    AppendFloat(position, 3, *bearing);
  std::string descriptor;
  AppendString(descriptor, 2, label);
  std::string vehicle;
  AppendMessage(vehicle, 2, position);
  AppendMessage(vehicle, 8, descriptor);
  std::string entity;
  AppendString(entity, 1, entityId);
  AppendMessage(entity, 4, vehicle);
  std::string feed;
  AppendMessage(feed, 2, entity);
  return feed;
}

UNIT_TEST(RodaliesLine_ParsesLabel)
{
  TEST_EQUAL(*commuter_live::RodaliesLine("R4-77552"), std::string("R4"), ());
  TEST_EQUAL(*commuter_live::RodaliesLine("R2S-28378-PLATF.(2)"), std::string("R2S"), ());
  TEST_EQUAL(*commuter_live::RodaliesLine("VP_R4-77552"), std::string("R4"), ());
  auto const tripLine = commuter_live::RodaliesLine("5177M77552R4");
  TEST(tripLine.has_value(), ());
  TEST_EQUAL(*tripLine, std::string("R4"), ());
  TEST(!commuter_live::RodaliesLine("C5-23731").has_value(), ());
  TEST(!commuter_live::RodaliesLine("PLATF.(2)").has_value(), ());
}

UNIT_TEST(NamesAndColours_KnownLines)
{
  TEST_EQUAL(commuter_live::StationName("PC"), std::string("Barcelona - Plaça Catalunya"), ());
  TEST_EQUAL(commuter_live::StationName("ZZ"), std::string("ZZ"), ());
  TEST_EQUAL(commuter_live::LineColor("S1"), std::string("EF7900"), ());
  TEST_EQUAL(commuter_live::LineColor("R4"), std::string("FF9221"), ());
  TEST_EQUAL(commuter_live::LineColor("R5R"), std::string("3DBFC3"), ());
  TEST_EQUAL(commuter_live::LineColor("NOPE"), std::string("888888"), ());
  TEST(commuter_live::InBarcelona(41.47, 1.91), ());
  TEST(!commuter_live::InBarcelona(37.39, -5.98), ());
}

UNIT_TEST(Heading_FromConsecutivePositions)
{
  commuter_live::Fix previous;
  previous.m_lat = 41.39;
  previous.m_lon = 2.16;
  auto const east = commuter_live::HeadingFromMotion(&previous, 41.39, 2.161, std::nullopt);
  TEST(east.has_value(), ());
  TEST_ALMOST_EQUAL_ABS(*east, 90.0, 5.0, ());

  auto const west = commuter_live::HeadingFromMotion(&previous, 41.39, 2.159, std::nullopt);
  TEST(west.has_value(), ());
  TEST_ALMOST_EQUAL_ABS(*west, 270.0, 5.0, ());
  TEST_ALMOST_EQUAL_ABS(std::fabs(*east - *west), 180.0, 8.0, ());

  previous.m_headingDeg = 45.0;
  auto const held = commuter_live::HeadingFromMotion(&previous, 41.39001, 2.16001, std::nullopt);
  TEST(held.has_value(), ());
  TEST_ALMOST_EQUAL_ABS(*held, 45.0, 0.1, ());

  auto const fromFeed = commuter_live::HeadingFromMotion(&previous, 41.39, 2.17, 12.0);
  TEST(fromFeed.has_value(), ());
  TEST_ALMOST_EQUAL_ABS(*fromFeed, 12.0, 0.1, ());

  TEST(!commuter_live::HeadingFromMotion(nullptr, 41.39, 2.16, std::nullopt).has_value(), ());
}

UNIT_TEST(ParseGeotren_ReadsPositionAndStops)
{
  std::string_view constexpr kJson = R"({
    "total_count": 1,
    "results": [{
      "id": "train-1",
      "lin": "S1",
      "geo_point_2d": {"lon": 2.16, "lat": 41.39},
      "desti": "PC",
      "properes_parades": "{\"parada\": \"SG\"};{\"parada\": \"GR\"}"
    }, {
      "id": "no-point",
      "lin": "S2"
    }]
  })";
  auto const trains = commuter_live::ParseGeotren(kJson);
  TEST(trains.has_value(), ());
  TEST_EQUAL(trains->size(), 1, ());
  TEST_EQUAL((*trains)[0].m_line, std::string("S1"), ());
  TEST_EQUAL((*trains)[0].m_destination, std::string("PC"), ());
  TEST_EQUAL((*trains)[0].m_nextStop, std::string("SG"), ());
  TEST_ALMOST_EQUAL_ABS((*trains)[0].m_lat, 41.39, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS((*trains)[0].m_lon, 2.16, 1e-6, ());
  TEST(!commuter_live::ParseGeotren("not-json").has_value(), ());
}

UNIT_TEST(ParseVehiclePositions_LineAndBearing)
{
  std::string feed = Vehicle("VP_R4-77552", "R4-77552", 41.47f, 1.91f, 180.f);
  feed += Vehicle("VP_C5-23731", "C5-23731", 37.39f, -5.98f, std::nullopt);
  feed += Vehicle("VP_R2S-28378", "R2S-28378-PLATF.(2)", 41.27f, 1.96f, std::nullopt);
  auto const trains = commuter_live::ParseVehiclePositions(feed);
  TEST(trains.has_value(), ());
  TEST_EQUAL(trains->size(), 3, ());
  TEST_EQUAL((*trains)[0].m_line, std::string("R4"), ());
  TEST(trains->at(0).m_bearingDeg.has_value(), ());
  TEST_ALMOST_EQUAL_ABS(*trains->at(0).m_bearingDeg, 180.0, 0.1, ());
  TEST((*trains)[1].m_line.empty(), ());
  TEST_EQUAL((*trains)[2].m_line, std::string("R2S"), ());
  TEST(!trains->at(2).m_bearingDeg.has_value(), ());
  TEST(!commuter_live::ParseVehiclePositions(std::string("\x80", 1)).has_value(), ());
}

UNIT_TEST(Poll_BearingFromSecondFixAndDropsDistantRenfe)
{
  int step = 0;
  auto const clock = [&] { return std::chrono::steady_clock::time_point(std::chrono::seconds(step)); };
  auto const http = [&](std::string const & url) -> std::optional<std::string>
  {
    if (url.find("posicionament") != std::string::npos)
    {
      double const lon = step == 0 ? 2.16 : 2.161;
      return std::string("{\"results\":[{\"id\":\"t1\",\"lin\":\"S1\",\"geo_point_2d\":{\"lon\":") +
             std::to_string(lon) +
             ",\"lat\":41.39},\"desti\":\"PC\",\"properes_parades\":\"{\\\"parada\\\": \\\"SG\\\"}\"}]}";
    }
    if (step == 0)
      return Vehicle("VP_R4-1", "R4-1", 41.47f, 1.91f, std::nullopt) +
             Vehicle("VP_C5-1", "C5-1", 37.39f, -5.98f, std::nullopt);
    return Vehicle("VP_R4-1", "R4-1", 41.47f, 1.92f, std::nullopt);
  };
  commuter_live::CommuterService service(http, clock);
  auto const first = service.Poll();
  TEST(first.m_enabled, ());
  TEST_EQUAL(first.m_trains.size(), 2, ());
  bool sawS1 = false;
  bool sawR4 = false;
  for (auto const & train : first.m_trains)
  {
    if (train.m_line == "S1")
    {
      sawS1 = true;
      TEST(!train.m_directional, ());
      TEST_EQUAL(train.m_destination, std::string("Barcelona - Plaça Catalunya"), ());
      TEST_EQUAL(train.m_nextStop, std::string("Sant Gervasi"), ());
      TEST_EQUAL(train.m_color, std::string("EF7900"), ());
    }
    if (train.m_line == "R4")
    {
      sawR4 = true;
      TEST(!train.m_directional, ());
    }
    TEST(train.m_line != "C5", ());
  }
  TEST(sawS1, ());
  TEST(sawR4, ());

  step = 30;
  auto const second = service.Poll();
  TEST_EQUAL(second.m_trains.size(), 2, ());
  for (auto const & train : second.m_trains)
  {
    TEST(train.m_directional, ());
    if (train.m_line == "S1")
      TEST_ALMOST_EQUAL_ABS(train.m_headingDeg, 90.0, 8.0, ());
    if (train.m_line == "R4")
      TEST_ALMOST_EQUAL_ABS(train.m_headingDeg, 90.0, 8.0, ());
  }

  auto const cached = service.Poll();
  TEST_EQUAL(cached.m_trains.size(), 2, ());
}

UNIT_TEST(Snap_ProjectsOntoLine)
{
  commuter_live::RailTrack line;
  line.m_ref = "R4";
  line.m_shape = {{41.38, 2.15}, {41.42, 2.15}};
  double const metresPerDeg = 111320.0 * std::cos(41.40 * 0.017453292519943295);
  double const dlon = 33.0 / metresPerDeg;
  auto const snapped = commuter_live::SnapToTracks(41.40, 2.15 + dlon, "R4", {line});
  TEST(snapped.m_snapped, ());
  double const ontoLine = ms::DistanceOnEarth({snapped.m_lat, snapped.m_lon}, {41.40, 2.15});
  TEST_LESS(ontoLine, 15.0, ());
  double const moved = ms::DistanceOnEarth({41.40, 2.15 + dlon}, {snapped.m_lat, snapped.m_lon});
  TEST_GREATER(moved, 15.0, ());

  commuter_live::RailTrack anonymous;
  anonymous.m_shape = line.m_shape;
  auto const nearRail = commuter_live::SnapToTracks(41.40, 2.15 + 40.0 / metresPerDeg, "R4", {anonymous});
  TEST(nearRail.m_snapped, ());
  TEST_LESS(ms::DistanceOnEarth({nearRail.m_lat, nearRail.m_lon}, {41.40, 2.15}), 15.0, ());
  auto const farRail = commuter_live::SnapToTracks(41.40, 2.15 + 400.0 / metresPerDeg, "R4", {anonymous});
  TEST(!farRail.m_snapped, ());

  commuter_live::RailTrack other;
  other.m_ref = "S1";
  other.m_shape = line.m_shape;
  auto const wrong = commuter_live::SnapToTracks(41.40, 2.15 + dlon, "R4", {other});
  TEST(!wrong.m_snapped, ());

  commuter_live::RailTrack colored;
  colored.m_ref = "L6";
  colored.m_colored = true;
  colored.m_shape = {{41.0, 2.0}, {41.1, 2.0}};
  commuter_live::RailTrack grey = line;
  grey.m_colored = false;
  auto const strokes = commuter_live::StrokeTracks({colored, grey}, {"L6", "R4"});
  TEST_EQUAL(strokes.size(), 1, ());
  TEST_EQUAL(strokes[0].m_ref, std::string("R4"), ());
}

UNIT_TEST(Lookup_GeotrenStopAndRenfeTrip)
{
  uint64_t constexpr kEta = 2000000000;
  auto const parsed = commuter_live::ParseTripUpdates(TripFeed(kEta));
  TEST(parsed.has_value(), ());
  TEST_EQUAL(parsed->size(), 1, ());
  if (parsed && !parsed->empty())
  {
    TEST_EQUAL(parsed->front().m_line, std::string("R4"), ());
    TEST_EQUAL(parsed->front().m_stops.size(), 1, ());
    TEST_EQUAL(parsed->front().m_stops.front().m_stopId, std::string("71600"), ());
  }
  auto const http = [&](std::string const & url) -> std::optional<std::string>
  {
    if (url.find("posicionament") != std::string::npos)
    {
      return std::string(
          "{\"results\":[{\"id\":\"t1\",\"lin\":\"S1\",\"geo_point_2d\":{\"lon\":2.16,\"lat\":41.39},"
          "\"desti\":\"TR\",\"properes_parades\":\"{\\\"parada\\\": \\\"PC\\\"}\"}]}");
    }
    if (url.find("trip_updates") != std::string::npos)
      return TripFeed(kEta);
    return std::string();
  };
  commuter_live::CommuterService service(http, [] { return std::chrono::steady_clock::time_point{}; });
  auto const fgc = service.LookupArrivals(41.385632, 2.168720, "Placa Catalunya");
  TEST_EQUAL(static_cast<int>(fgc.m_status), static_cast<int>(commuter_live::ArrivalStatus::Ok), ());
  bool sawS1 = false;
  int64_t const now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  for (auto const & arrival : fgc.m_arrivals)
  {
    if (arrival.m_line != "S1")
      continue;
    sawS1 = true;
    TEST_EQUAL(arrival.m_destination, std::string("Terrassa - Rambla"), ());
    TEST_ALMOST_EQUAL_ABS(static_cast<double>(arrival.m_etaUnixSec), static_cast<double>(now + 120), 5.0, ());
  }
  TEST(sawS1, ());

  auto const rodalies = service.LookupArrivals(41.186210, 1.524804, "");
  TEST_EQUAL(static_cast<int>(rodalies.m_status), static_cast<int>(commuter_live::ArrivalStatus::Ok), ());
  bool sawR4 = false;
  for (auto const & arrival : rodalies.m_arrivals)
  {
    if (arrival.m_line != "R4")
      continue;
    sawR4 = true;
    TEST_EQUAL(arrival.m_destination, std::string("Sant Vicenç de Calders"), ());
    TEST_EQUAL(arrival.m_etaUnixSec, static_cast<int64_t>(kEta), ());
  }
  TEST(sawR4, ());

  auto const none = service.LookupArrivals(37.39, -5.98, "Sevilla");
  TEST_EQUAL(static_cast<int>(none.m_status), static_cast<int>(commuter_live::ArrivalStatus::NotApplicable), ());
}
}  // namespace commuter_tests
