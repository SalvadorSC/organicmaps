#pragma once

#include "metro_live/estimator.hpp"
#include "metro_live/types.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace metro_live
{
inline std::string_view constexpr kMetroLiveEnabledSetting = "MetroLiveEnabled";
inline std::string_view constexpr kTmbAppIdSetting = "TmbAppId";
inline std::string_view constexpr kTmbAppKeySetting = "TmbAppKey";

inline std::string_view constexpr kStationsUrl = "https://api.tmb.cat/v1/transit/linies/metro/estacions";
inline std::string_view constexpr kLinesUrl = "https://api.tmb.cat/v1/transit/linies/metro";
inline std::string_view constexpr kArrivalsUrl = "https://api.tmb.cat/v1/itransit/metro/estacions";

class MetroService
{
public:
  using HttpGet = std::function<std::optional<std::string>(std::string const & url)>;
  using WallClock = std::function<std::chrono::system_clock::time_point()>;
  using SteadyClock = std::function<std::chrono::steady_clock::time_point()>;
  using CredentialsFn = std::function<Credentials()>;

  // Does not read the settings toggle. Callers that should honor it use PollMetro.
  MetroService(HttpGet httpGet, WallClock wall, SteadyClock steady, CredentialsFn credentials);

  PollResult Poll(std::vector<MapTrack> const & tracks = {});

private:
  HttpGet m_httpGet;
  WallClock m_wall;
  SteadyClock m_steady;
  CredentialsFn m_credentials;

  std::mutex m_mutex;
  Network m_network;
  bool m_hasNetwork = false;
  std::chrono::steady_clock::time_point m_networkExpires{};
  std::vector<ArrivalObs> m_rows;
  bool m_hasRows = false;
  std::chrono::steady_clock::time_point m_rowsExpires{};
  std::uint64_t m_signature = 0;
};

bool IsMetroLiveEnabled();
void SetMetroLiveEnabled(bool enabled);

// Same TMB fields as live buses. Never logged.
Credentials GetTmbCredentials();

// Not enabled: no network. Enabled without a key: m_needsKey, no network.
PollResult PollMetro(std::vector<MapTrack> const & tracks = {});
}  // namespace metro_live
