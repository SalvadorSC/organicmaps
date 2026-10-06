#pragma once

#include <optional>
#include <string>
#include <vector>

namespace commuter_live
{
// Barcelona commuter area: Rodalies de Barcelona plus FGC Vallès, Llobregat, and Montserrat.
// Drops Renfe vehicle positions elsewhere in Spain.
bool InBarcelona(double lat, double lon);

struct RawTrain
{
  std::string m_id;
  std::string m_line;
  double m_lat = 0;
  double m_lon = 0;
  std::optional<double> m_bearingDeg;
  std::string m_destination;
  std::string m_nextStop;
};

struct Train
{
  std::string m_id;
  std::string m_line;
  std::string m_color;
  double m_lat = 0;
  double m_lon = 0;
  bool m_directional = false;
  double m_headingDeg = 0;
  std::string m_destination;
  std::string m_nextStop;
  std::string m_key;
};

struct PollResult
{
  bool m_enabled = false;
  std::vector<Train> m_trains;
};
}  // namespace commuter_live
