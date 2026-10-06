#pragma once

#include "commuter_live/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace commuter_live
{
// Line code from a Renfe id or label such as "R4-77552" or "R2S-28378-PLATF.(2)".
std::optional<std::string> RodaliesLine(std::string_view text);

// nullopt when the document is not the Geotren records payload.
std::optional<std::vector<RawTrain>> ParseGeotren(std::string_view json);

// GTFS-RT FeedMessage of VehiclePositions. nullopt when the bytes are truncated.
// Vehicles without a position are omitted. Line may be empty when the id has no R-code.
std::optional<std::vector<RawTrain>> ParseVehiclePositions(std::string_view bytes);

// One predicted call from a GTFS-RT TripUpdate.
struct StopVisit
{
  std::string m_stopId;
  int64_t m_etaUnixSec = 0;
};

// A Rodalies trip that is not cancelled. Stops without an absolute time are omitted.
struct TripPass
{
  std::string m_line;
  std::vector<StopVisit> m_stops;
};

// GTFS-RT FeedMessage of TripUpdates. nullopt when the bytes are truncated.
std::optional<std::vector<TripPass>> ParseTripUpdates(std::string_view bytes);
}  // namespace commuter_live
