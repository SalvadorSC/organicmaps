#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bus_live
{
// GTFS-RT TripUpdates subset. Field numbers follow the public gtfs-realtime.proto.
// The repository has no protobuf library, so this is a small hand-rolled decoder.

int constexpr kTripCanceled = 3;
int constexpr kStopSkipped = 1;

struct StopTimeEvent
{
  std::optional<int64_t> m_time;
  std::optional<int32_t> m_delay;
};

struct StopTimeUpdate
{
  StopTimeEvent m_arrival;
  StopTimeEvent m_departure;
  std::string m_stopId;
  int m_schedule = 0;
};

struct TripDescriptor
{
  std::string m_tripId;
  std::string m_routeId;
  int m_schedule = 0;
};

struct TripUpdate
{
  TripDescriptor m_trip;
  std::vector<StopTimeUpdate> m_stops;
  int64_t m_timestamp = 0;
};

struct Feed
{
  int64_t m_headerTimestamp = 0;
  std::vector<TripUpdate> m_updates;
};

// nullopt when the bytes are not a FeedMessage. An empty entity list is valid.
std::optional<Feed> ParseFeed(std::string_view bytes);
}  // namespace bus_live
