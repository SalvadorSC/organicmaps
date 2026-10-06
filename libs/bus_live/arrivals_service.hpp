#pragma once

#include "bus_live/gtfs_rt.hpp"
#include "bus_live/types.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bus_live
{
inline std::string_view constexpr kBusArrivalsEnabledSetting = "BusArrivalsEnabled";
inline std::string_view constexpr kTmbAppIdSetting = "TmbAppId";
inline std::string_view constexpr kTmbAppKeySetting = "TmbAppKey";

inline std::string_view constexpr kAmbTripsUrl = "https://www.ambmobilitat.cat/transit/trips-updates/trips.bin";
inline std::string_view constexpr kAmbGtfsUrl = "https://www.ambmobilitat.cat/OpenData/google_transit.zip";
inline std::string_view constexpr kTmbStopUrlPrefix = "https://api.tmb.cat/v1/itransit/bus/parades/";
inline std::string_view constexpr kTmbCatalogUrl = "https://api.tmb.cat/v1/transit/parades";

bool InBarcelonaArea(ms::LatLon const & point);

class ArrivalsService
{
public:
  using HttpGet = std::function<std::optional<std::string>(std::string const & url)>;
  using WallClock = std::function<std::chrono::system_clock::time_point()>;
  using SteadyClock = std::function<std::chrono::steady_clock::time_point()>;
  using CredentialsFn = std::function<Credentials()>;
  using StaticLoader = std::function<std::optional<StaticIndex>()>;
  using CatalogLoader = std::function<std::optional<std::vector<TransitStop>>(Credentials const & credentials)>;

  // Does not read the settings toggle. Tests pass the loaders and the clock
  // so nothing here touches the network or the disk unless a loader does.
  ArrivalsService(HttpGet httpGet, WallClock wall, SteadyClock steady, CredentialsFn credentials,
                  StaticLoader loadStatic, CatalogLoader loadCatalog);

  ArrivalList Lookup(StopQuery const & query);

private:
  struct TmbCache
  {
    std::vector<Arrival> m_arrivals;
    int64_t m_updatedUnixSec = 0;
    bool m_ok = false;
  };

  HttpGet m_httpGet;
  WallClock m_wall;
  SteadyClock m_steady;
  CredentialsFn m_credentials;
  StaticLoader m_loadStatic;
  CatalogLoader m_loadCatalog;

  std::mutex m_mutex;
  std::optional<StaticIndex> m_static;
  std::chrono::steady_clock::time_point m_staticExpires{};
  std::optional<Feed> m_feed;
  std::chrono::steady_clock::time_point m_feedExpires{};
  std::optional<std::vector<TransitStop>> m_catalog;
  std::chrono::steady_clock::time_point m_catalogExpires{};
  std::uint64_t m_tmbSig = 0;
  std::unordered_map<std::string, std::pair<TmbCache, std::chrono::steady_clock::time_point>> m_tmbByStop;
};

bool IsBusArrivalsEnabled();
void SetBusArrivalsEnabled(bool enabled);

Credentials GetTmbCredentials();
void SetTmbCredentials(std::string appId, std::string appKey);

// NotApplicable when the setting is off, before any network.
ArrivalList LookupArrivals(StopQuery const & query);
}  // namespace bus_live
