#include "bus_live/tmb_parser.hpp"

#include "bus_live/stop_matcher.hpp"

#include "coding/serdes_json.hpp"

#include "base/string_utils.hpp"

#include <initializer_list>

namespace bus_live
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
  if (value == nullptr)
    return {};
  if (value->is_string())
    return value->get_string();
  if (auto const number = ReadInt(*value))
    return std::to_string(*number);
  return {};
}

int64_t ToUnixSeconds(int64_t raw)
{
  // TMB temps_arribada is an epoch in milliseconds. A 10-digit value is already seconds.
  if (raw > 100000000000LL)
    return raw / 1000;
  return raw;
}

std::optional<ms::LatLon> ReadPoint(coding::JsonValue const & object)
{
  if (!object.is_object())
    return std::nullopt;
  if (auto const * lat = FindAny(object, {"lat", "stop_lat", "LATITUD"}))
  {
    if (auto const * lon = FindAny(object, {"lon", "stop_lon", "LONGITUD"}))
    {
      auto const latValue = ReadDouble(*lat);
      auto const lonValue = ReadDouble(*lon);
      if (latValue && lonValue)
        return ms::LatLon(*latValue, *lonValue);
    }
  }

  coding::JsonValue const * coordinates = Find(object, "coordinates");
  if (coordinates == nullptr)
  {
    if (auto const * geometry = Find(object, "geometry"))
      coordinates = Find(*geometry, "coordinates");
  }
  if (coordinates != nullptr && coordinates->is_array() && coordinates->get_array().size() >= 2)
  {
    auto const & pair = coordinates->get_array();
    auto const lon = ReadDouble(pair[0]);
    auto const lat = ReadDouble(pair[1]);
    if (lat && lon)
      return ms::LatLon(*lat, *lon);
  }
  return std::nullopt;
}

void ReadIdentity(coding::JsonValue const & object, TransitStop & stop)
{
  if (!object.is_object() || !stop.m_code.empty())
    return;
  stop.m_code = CanonicalCode(ReadText(FindAny(object, {"CODI_PARADA", "codi_parada", "stop_code", "CODI"})));
  stop.m_id = stop.m_code;
  stop.m_name = ReadText(FindAny(object, {"NOM_PARADA", "nom_parada", "stop_name", "NOM", "name"}));
}

void CollectStops(coding::JsonValue const & node, std::vector<TransitStop> & stops)
{
  if (node.is_array())
  {
    for (auto const & item : node.get_array())
      CollectStops(item, stops);
    return;
  }
  if (!node.is_object())
    return;

  TransitStop stop;
  if (auto const * properties = Find(node, "properties"))
    ReadIdentity(*properties, stop);
  ReadIdentity(node, stop);
  if (auto const point = ReadPoint(node))
    stop.m_point = *point;
  if (!stop.m_code.empty() && stop.m_point.IsValid())
  {
    stops.push_back(std::move(stop));
    return;
  }

  for (auto const & [key, value] : node.get_object())
    if (key == "features" || key == "parades" || key == "stops" || key == "data")
      CollectStops(value, stops);
}
}  // namespace

std::optional<TmbFeed> ParseTmbArrivals(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root || !root->is_object())
    return std::nullopt;

  TmbFeed feed;
  if (auto const * timestamp = Find(*root, "timestamp"))
  {
    if (auto const value = ReadInt(*timestamp))
      feed.m_updatedUnixSec = ToUnixSeconds(*value);
  }

  auto const * parades = Find(*root, "parades");
  if (parades == nullptr || !parades->is_array())
    return feed;

  for (auto const & parada : parades->get_array())
  {
    auto const * lines = Find(parada, "linies_trajectes");
    if (lines == nullptr || !lines->is_array())
      continue;
    for (auto const & line : lines->get_array())
    {
      std::string name = ReadText(Find(line, "nom_linia"));
      if (name.empty())
        name = ReadText(Find(line, "codi_linia"));
      if (name.empty())
        continue;
      std::string const destination = ReadText(Find(line, "desti_trajecte"));
      auto const * buses = Find(line, "propers_busos");
      if (buses == nullptr || !buses->is_array())
        continue;
      for (auto const & bus : buses->get_array())
      {
        auto const * when = Find(bus, "temps_arribada");
        if (when == nullptr)
          continue;
        auto const raw = ReadInt(*when);
        if (!raw)
          continue;
        Arrival arrival;
        arrival.m_line = name;
        arrival.m_destination = destination;
        arrival.m_etaUnixSec = ToUnixSeconds(*raw);
        arrival.m_fromTmb = true;
        feed.m_arrivals.push_back(std::move(arrival));
      }
    }
  }
  return feed;
}

std::optional<std::vector<TransitStop>> ParseTmbCatalog(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root)
    return std::nullopt;
  std::vector<TransitStop> stops;
  CollectStops(*root, stops);
  return stops;
}
}  // namespace bus_live
