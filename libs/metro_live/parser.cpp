#include "metro_live/parser.hpp"

#include "coding/serdes_json.hpp"

#include "base/string_utils.hpp"

#include <initializer_list>

namespace metro_live
{
namespace
{
std::optional<coding::JsonValue> ReadJson(std::string_view json)
{
  coding::JsonValue root;
  if (glz::read_json(root, json))
    return std::nullopt;
  return root;
}

coding::JsonValue const * Find(coding::JsonValue const & object, char const * key)
{
  if (!object.is_object())
    return nullptr;
  auto const & fields = object.get_object();
  auto const it = fields.find(key);
  if (it == fields.end())
    return nullptr;
  return &it->second;
}

coding::JsonValue const * FindAny(coding::JsonValue const & object, std::initializer_list<char const *> keys)
{
  for (char const * key : keys)
    if (auto const * value = Find(object, key))
      return value;
  return nullptr;
}

std::optional<int64_t> ReadInt(coding::JsonValue const & value)
{
  if (value.is_number())
    return value.as<int64_t>();
  if (value.is_string())
  {
    int64_t parsed = 0;
    if (strings::to_int(value.get_string(), parsed))
      return parsed;
  }
  return std::nullopt;
}

std::optional<double> ReadDouble(coding::JsonValue const & value)
{
  if (value.is_number())
    return value.as_number();
  if (value.is_string())
  {
    double parsed = 0;
    if (strings::to_double(value.get_string(), parsed))
      return parsed;
  }
  return std::nullopt;
}

std::string ReadText(coding::JsonValue const * value)
{
  if (value == nullptr || value->is_null())
    return {};
  if (value->is_string())
    return value->get_string();
  if (auto const number = ReadInt(*value))
    return std::to_string(*number);
  return {};
}

int64_t ToUnixSeconds(int64_t raw)
{
  if (raw > 100000000000LL)
    return raw / 1000;
  return raw;
}

std::optional<ms::LatLon> ReadPair(coding::JsonValue const & pair)
{
  if (!pair.is_array() || pair.get_array().size() < 2)
    return std::nullopt;
  auto const & values = pair.get_array();
  auto const lon = ReadDouble(values[0]);
  auto const lat = ReadDouble(values[1]);
  if (!lat || !lon)
    return std::nullopt;
  return ms::LatLon(*lat, *lon);
}

bool IsMetro(coding::JsonValue const & properties)
{
  std::string const kind = ReadText(FindAny(properties, {"NOM_TIPUS_TRANSPORT", "nom_tipus_transport"}));
  if (kind.empty())
    return true;
  std::string lower = kind;
  strings::MakeLowerCaseInplace(lower);
  return lower == "metro";
}

void ReadShape(coding::JsonValue const & coordinates, std::vector<ms::LatLon> & shape)
{
  if (!coordinates.is_array() || coordinates.get_array().empty())
    return;
  auto const & outer = coordinates.get_array();
  // MultiLineString: [ [ [lon,lat], ... ], ... ]. A bare [lon,lat] pair is one point.
  if (outer.front().is_number())
  {
    if (auto const point = ReadPair(coordinates))
      shape.push_back(*point);
    return;
  }
  for (auto const & part : outer)
  {
    if (!part.is_array() || part.get_array().empty())
      continue;
    if (part.get_array().front().is_number())
    {
      if (auto const point = ReadPair(part))
        shape.push_back(*point);
      continue;
    }
    for (auto const & pair : part.get_array())
      if (auto const point = ReadPair(pair))
        shape.push_back(*point);
  }
}
}  // namespace

std::optional<std::vector<Station>> ParseStations(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root)
    return std::nullopt;
  auto const * features = Find(*root, "features");
  if (features == nullptr || !features->is_array())
    return std::vector<Station>{};

  std::vector<Station> stations;
  for (auto const & feature : features->get_array())
  {
    auto const * properties = Find(feature, "properties");
    if (properties == nullptr)
      continue;
    auto const * codeNode = FindAny(*properties, {"CODI_ESTACIO", "codi_estacio"});
    if (codeNode == nullptr)
      continue;
    auto const codeValue = ReadInt(*codeNode);
    if (!codeValue)
      continue;
    Station station;
    station.m_code = static_cast<int>(*codeValue);
    if (auto const * orderValue = FindAny(*properties, {"ORDRE_ESTACIO", "ordre_estacio"}))
      if (auto const parsed = ReadInt(*orderValue))
        station.m_order = static_cast<int>(*parsed);
    station.m_line = ReadText(FindAny(*properties, {"NOM_LINIA", "nom_linia"}));
    station.m_name = ReadText(FindAny(*properties, {"NOM_ESTACIO", "nom_estacio"}));
    station.m_color = ReadText(FindAny(*properties, {"COLOR_LINIA", "color_linia"}));
    auto const * geometry = Find(feature, "geometry");
    if (geometry == nullptr)
      continue;
    auto const * coordinates = Find(*geometry, "coordinates");
    if (coordinates == nullptr)
      continue;
    auto const point = ReadPair(*coordinates);
    if (!point || station.m_line.empty())
      continue;
    station.m_point = *point;
    stations.push_back(std::move(station));
  }
  return stations;
}

std::optional<std::vector<LinePath>> ParseLines(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root)
    return std::nullopt;
  auto const * features = Find(*root, "features");
  if (features == nullptr || !features->is_array())
    return std::vector<LinePath>{};

  std::vector<LinePath> lines;
  for (auto const & feature : features->get_array())
  {
    auto const * properties = Find(feature, "properties");
    if (properties == nullptr || !IsMetro(*properties))
      continue;
    LinePath line;
    line.m_name = ReadText(FindAny(*properties, {"NOM_LINIA", "nom_linia"}));
    line.m_color = ReadText(FindAny(*properties, {"COLOR_LINIA", "color_linia"}));
    if (line.m_name.empty())
      continue;
    if (auto const * geometry = Find(feature, "geometry"))
      if (auto const * coordinates = Find(*geometry, "coordinates"))
        ReadShape(*coordinates, line.m_shape);
    lines.push_back(std::move(line));
  }
  return lines;
}

std::optional<ArrivalFeed> ParseArrivals(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root || !root->is_object())
    return std::nullopt;

  ArrivalFeed feed;
  if (auto const * timestamp = Find(*root, "timestamp"))
    if (auto const value = ReadInt(*timestamp))
      feed.m_updatedUnixSec = ToUnixSeconds(*value);

  auto const * lines = Find(*root, "linies");
  if (lines == nullptr || !lines->is_array())
    return feed;

  for (auto const & line : lines->get_array())
  {
    std::string const lineName = ReadText(FindAny(line, {"nom_linia", "NOM_LINIA"}));
    std::string const color = ReadText(FindAny(line, {"color_linia", "COLOR_LINIA"}));
    auto const * stations = Find(line, "estacions");
    if (lineName.empty() || stations == nullptr || !stations->is_array())
      continue;
    for (auto const & station : stations->get_array())
    {
      auto const * codeValue = FindAny(station, {"codi_estacio", "CODI_ESTACIO"});
      if (codeValue == nullptr)
        continue;
      auto const code = ReadInt(*codeValue);
      if (!code)
        continue;
      auto const * trips = Find(station, "linies_trajectes");
      if (trips == nullptr || !trips->is_array())
        continue;
      for (auto const & trip : trips->get_array())
      {
        std::string const trajecte = ReadText(FindAny(trip, {"codi_trajecte", "CODI_TRAJECTE"}));
        std::string const destination = ReadText(FindAny(trip, {"desti_trajecte", "DESTI_TRAJECTE"}));
        std::string tripLine = ReadText(FindAny(trip, {"nom_linia", "NOM_LINIA"}));
        if (tripLine.empty())
          tripLine = lineName;
        std::string tripColor = ReadText(FindAny(trip, {"color_linia", "COLOR_LINIA"}));
        if (tripColor.empty())
          tripColor = color;
        auto const * trains = Find(trip, "propers_trens");
        if (trains == nullptr || !trains->is_array())
          continue;
        for (auto const & train : trains->get_array())
        {
          auto const * when = FindAny(train, {"temps_arribada", "TEMPS_ARRIBADA"});
          if (when == nullptr)
            continue;
          auto const raw = ReadInt(*when);
          if (!raw)
            continue;
          ArrivalObs row;
          row.m_line = tripLine;
          row.m_color = tripColor;
          row.m_trajecte = trajecte;
          row.m_destination = destination;
          row.m_service = ReadText(FindAny(train, {"codi_servei", "CODI_SERVEI"}));
          row.m_station = static_cast<int>(*code);
          row.m_etaUnixSec = ToUnixSeconds(*raw);
          if (row.m_service.empty())
            continue;
          feed.m_rows.push_back(std::move(row));
        }
      }
    }
  }
  return feed;
}
}  // namespace metro_live
