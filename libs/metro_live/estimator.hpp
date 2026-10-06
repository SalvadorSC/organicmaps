#pragma once

#include "metro_live/types.hpp"

#include <cstdint>
#include <vector>

namespace metro_live
{
// Median gap between successive station ETAs in the 2026-10-06 snapshot.
int64_t constexpr kTypicalSegmentSec = 87;
// Drop a prediction that is already this far in the past.
int64_t constexpr kStaleSec = 60;

// Stations ordered along each line, with distance-along the line geometry.
struct IndexedStation
{
  Station m_station;
  // Meters from the start of the line shape. Negative when the stop did not snap.
  double m_alongM = -1;
};

struct IndexedLine
{
  std::string m_name;
  std::string m_color;
  std::vector<ms::LatLon> m_shape;
  std::vector<double> m_alongM;
  std::vector<IndexedStation> m_stations;
};

struct Network
{
  std::vector<IndexedLine> m_lines;
};

// Joins station order to line geometry. A line with no shape uses the station
// points in ORDRE_ESTACIO order, so a missing line file still places trains.
Network BuildNetwork(std::vector<LinePath> const & lines, std::vector<Station> const & stations);

// Group rows by (line, codi_trajecte, codi_servei). The next station is the
// soonest ETA. The train sits on the segment into that station: fraction
// remaining/segment-time, where the segment time is the gap to the following
// reported station, or kTypicalSegmentSec. A run that has already passed the
// first station of this direction stays on the platform (terminus turnaround).
// Station order that goes backwards is a recycled codi_servei and is ignored.
// L9/L10 north and south are different line names, so they never share a shape.
std::vector<TrainEstimate> EstimateTrains(Network const & network, std::vector<ArrivalObs> const & rows,
                                          int64_t nowUnix);
}  // namespace metro_live
