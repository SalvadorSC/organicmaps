#pragma once

#include "bus_live/gtfs_rt.hpp"
#include "bus_live/types.hpp"

#include <cstdint>
#include <vector>

namespace bus_live
{
int64_t constexpr kStaleSec = 60;
int64_t constexpr kDedupeSec = 90;
size_t constexpr kMaxArrivals = 12;

// Absolute times only. Delay-only updates are ignored because stop_times.txt
// is not downloaded. Canceled trips and skipped stops are dropped.
std::vector<Arrival> CollectAmbArrivals(Feed const & feed, StaticIndex const & index, TransitStop const & stop,
                                        int64_t nowUnix);

// Same line (ignoring spaces and case) and an ETA within kDedupeSec: keep TMB.
// Result is sorted by ETA and capped at kMaxArrivals.
std::vector<Arrival> MergeArrivals(std::vector<Arrival> amb, std::vector<Arrival> tmb);
}  // namespace bus_live
