#pragma once

#include "bike_share/feed_catalog.hpp"
#include "bike_share/types.hpp"

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bike_share
{
// Persisted with the rest of the app settings. Absent means off.
inline std::string_view constexpr kAvailabilityEnabledSetting = "BikeShareAvailabilityEnabled";

class AvailabilityService
{
public:
  using HttpGet = std::function<std::optional<std::string>(std::string const & url)>;
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  // |feeds| defaults to the built-in catalog. Tests pass a fake HTTP getter,
  // a clock, and a small catalog so nothing touches the network.
  AvailabilityService(HttpGet httpGet, Clock clock, std::vector<FeedDefinition> feeds);

  // nullopt on any failure, a disabled path is not this method's concern.
  // Network and parse errors are swallowed.
  std::optional<Availability> Lookup(MatchQuery const & query);

private:
  struct Snapshot
  {
    std::string m_feedName;
    std::vector<Station> m_stations;
    std::unordered_map<std::string, StationStatus> m_statusById;
    int64_t m_feedLastUpdated = 0;
    std::chrono::steady_clock::time_point m_expires{};
  };

  std::optional<Snapshot> Load(FeedDefinition const & feed);

  HttpGet m_httpGet;
  Clock m_clock;
  std::vector<FeedDefinition> m_feeds;
  std::mutex m_mutex;
  std::unordered_map<std::string, Snapshot> m_cache;
};

bool IsAvailabilityEnabled();
void SetAvailabilityEnabled(bool enabled);

// No network when the setting is off. Safe to call from a background thread.
std::optional<Availability> LookupAvailability(MatchQuery const & query);
}  // namespace bike_share
