#pragma once

#include "commuter_live/bearing.hpp"
#include "commuter_live/types.hpp"

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace commuter_live
{
inline std::string_view constexpr kCommuterLiveEnabledSetting = "CommuterLiveEnabled";

// FGC Geotren and Renfe GTFS-RT VehiclePositions. Both are CC BY 4.0.
inline std::string_view constexpr kGeotrenUrl =
    "https://fgc.opendatasoft.com/api/explore/v2.1/catalog/datasets/posicionament-dels-trens/records?limit=200";
inline std::string_view constexpr kRenfeVehiclesUrl = "https://gtfsrt.renfe.com/vehicle_positions.pb";

class CommuterService
{
public:
  using HttpGet = std::function<std::optional<std::string>(std::string const & url)>;
  using SteadyClock = std::function<std::chrono::steady_clock::time_point()>;

  // Does not read the settings toggle. Callers that should honor it use PollCommuter.
  CommuterService(HttpGet httpGet, SteadyClock steady);

  PollResult Poll();

private:
  std::vector<Train> Build(std::vector<RawTrain> const & rows, std::string_view source,
                           std::unordered_map<std::string, Fix> & nextFixes) const;

  HttpGet m_httpGet;
  SteadyClock m_steady;
  std::mutex m_mutex;
  bool m_has = false;
  std::chrono::steady_clock::time_point m_expires{};
  std::vector<Train> m_trains;
  std::unordered_map<std::string, Fix> m_fixes;
  std::vector<Train> m_lastFgc;
  std::vector<Train> m_lastRenfe;
  bool m_hasFgc = false;
  bool m_hasRenfe = false;
};

bool IsCommuterLiveEnabled();
void SetCommuterLiveEnabled(bool enabled);

PollResult PollCommuter();
}  // namespace commuter_live
