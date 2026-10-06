#include "bike_share/gbfs_parser.hpp"

#include "coding/serdes_json.hpp"

#include "base/string_utils.hpp"

#include <string>

namespace bike_share
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
  if (!value.is_number())
    return std::nullopt;
  return value.as_number();
}

std::optional<bool> ReadBool(coding::JsonValue const & value)
{
  if (value.is_boolean())
    return value.get_boolean();
  if (auto const number = ReadInt(value))
    return *number != 0;
  return std::nullopt;
}

std::optional<std::string> ReadId(coding::JsonValue const & value)
{
  if (value.is_string())
  {
    if (value.get_string().empty())
      return std::nullopt;
    return value.get_string();
  }
  if (auto const number = ReadInt(value))
    return std::to_string(*number);
  return std::nullopt;
}

std::optional<std::string> ReadString(coding::JsonValue const & value)
{
  if (!value.is_string())
    return std::nullopt;
  return value.get_string();
}

int ReadTtl(coding::JsonValue const & root)
{
  if (auto const * ttl = Find(root, "ttl"))
  {
    if (auto const value = ReadInt(*ttl))
      return static_cast<int>(*value);
  }
  return 0;
}

int64_t ReadLastUpdated(coding::JsonValue const & root)
{
  if (auto const * updated = Find(root, "last_updated"))
  {
    if (auto const value = ReadInt(*updated))
      return *value;
  }
  return 0;
}

bool LooksElectric(std::string typeId)
{
  strings::MakeLowerCaseInplace(typeId);
  return typeId.find("ebike") != std::string::npos || typeId.find("e-bike") != std::string::npos ||
         typeId.find("electric") != std::string::npos || typeId.find("pedelec") != std::string::npos;
}

void ReadLegacySplit(coding::JsonValue const & types, StationStatus & status)
{
  if (!types.is_object())
    return;

  std::optional<int> mechanical;
  std::optional<int> ebikes;
  for (auto const & [key, value] : types.get_object())
  {
    auto const count = ReadInt(value);
    if (!count)
      continue;
    std::string lower = key;
    strings::MakeLowerCaseInplace(lower);
    if (lower == "mechanical" || lower == "human" || lower == "classic")
      mechanical = static_cast<int>(*count);
    else if (lower == "ebike" || lower == "e-bike" || lower == "electric" || lower == "electrical")
      ebikes = static_cast<int>(*count);
  }
  if (mechanical)
    status.m_mechanicalAvailable = *mechanical;
  if (ebikes)
    status.m_ebikesAvailable = *ebikes;
}

std::optional<StationStatus> ReadStatus(coding::JsonValue const & item)
{
  auto const * idValue = Find(item, "station_id");
  auto const * bikesValue = Find(item, "num_bikes_available");
  if (idValue == nullptr || bikesValue == nullptr)
    return std::nullopt;
  auto const id = ReadId(*idValue);
  auto const bikes = ReadInt(*bikesValue);
  if (!id || !bikes)
    return std::nullopt;

  StationStatus status;
  status.m_id = *id;
  status.m_bikesAvailable = static_cast<int>(*bikes);
  if (auto const * docks = Find(item, "num_docks_available"))
  {
    if (auto const value = ReadInt(*docks))
      status.m_docksAvailable = static_cast<int>(*value);
  }
  if (auto const * installed = Find(item, "is_installed"))
  {
    if (auto const value = ReadBool(*installed))
      status.m_isInstalled = *value;
  }
  if (auto const * renting = Find(item, "is_renting"))
  {
    if (auto const value = ReadBool(*renting))
      status.m_isRenting = *value;
  }
  if (auto const * reported = Find(item, "last_reported"))
  {
    if (auto const value = ReadInt(*reported))
      status.m_lastReported = *value;
  }
  if (auto const * vehicles = Find(item, "vehicle_types_available"); vehicles != nullptr && vehicles->is_array())
  {
    for (auto const & entry : vehicles->get_array())
    {
      auto const * typeId = Find(entry, "vehicle_type_id");
      auto const * count = Find(entry, "count");
      if (typeId == nullptr || count == nullptr)
        continue;
      auto const idText = ReadId(*typeId);
      auto const countValue = ReadInt(*count);
      if (!idText || !countValue)
        continue;
      status.m_vehicleTypes.push_back({*idText, static_cast<int>(*countValue)});
    }
  }
  if (auto const * legacy = Find(item, "num_bikes_available_types"))
    ReadLegacySplit(*legacy, status);
  return status;
}

DiscoveredFeeds ReadFeedList(coding::JsonValue const & feeds, int ttlSec)
{
  DiscoveredFeeds discovered;
  discovered.m_ttlSec = ttlSec;
  if (!feeds.is_array())
    return discovered;

  for (auto const & item : feeds.get_array())
  {
    auto const * name = Find(item, "name");
    auto const * url = Find(item, "url");
    if (name == nullptr || url == nullptr)
      continue;
    auto const feedName = ReadString(*name);
    auto const feedUrl = ReadString(*url);
    if (!feedName || !feedUrl || feedUrl->empty())
      continue;
    if (*feedName == "station_information")
      discovered.m_stationInformationUrl = *feedUrl;
    else if (*feedName == "station_status")
      discovered.m_stationStatusUrl = *feedUrl;
    else if (*feedName == "vehicle_types")
      discovered.m_vehicleTypesUrl = *feedUrl;
  }
  return discovered;
}

coding::JsonValue const * LanguageFeeds(coding::JsonValue const & data)
{
  if (auto const * english = Find(data, "en"))
  {
    if (auto const * feeds = Find(*english, "feeds"))
      return feeds;
  }
  if (!data.is_object())
    return nullptr;
  for (auto const & [key, value] : data.get_object())
  {
    (void)key;
    if (auto const * feeds = Find(value, "feeds"))
      return feeds;
  }
  return nullptr;
}
}  // namespace

std::optional<DiscoveredFeeds> ParseDiscovery(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root)
    return std::nullopt;
  auto const * data = Find(*root, "data");
  if (data == nullptr)
    return std::nullopt;

  coding::JsonValue const * feeds = nullptr;
  if (auto const * flat = Find(*data, "feeds"))
    feeds = flat;
  else
    feeds = LanguageFeeds(*data);
  if (feeds == nullptr)
    return std::nullopt;

  auto discovered = ReadFeedList(*feeds, ReadTtl(*root));
  if (discovered.m_stationInformationUrl.empty() || discovered.m_stationStatusUrl.empty())
    return std::nullopt;
  return discovered;
}

std::optional<StationInformationFeed> ParseStationInformation(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root)
    return std::nullopt;
  auto const * data = Find(*root, "data");
  auto const * stations = data == nullptr ? nullptr : Find(*data, "stations");
  if (stations == nullptr || !stations->is_array())
    return std::nullopt;

  StationInformationFeed feed;
  feed.m_ttlSec = ReadTtl(*root);
  feed.m_lastUpdated = ReadLastUpdated(*root);
  for (auto const & item : stations->get_array())
  {
    auto const * idValue = Find(item, "station_id");
    auto const * latValue = Find(item, "lat");
    auto const * lonValue = Find(item, "lon");
    if (idValue == nullptr || latValue == nullptr || lonValue == nullptr)
      continue;
    auto const id = ReadId(*idValue);
    auto const lat = ReadDouble(*latValue);
    auto const lon = ReadDouble(*lonValue);
    if (!id || !lat || !lon)
      continue;
    if (*lat < ms::LatLon::kMinLat || *lat > ms::LatLon::kMaxLat || *lon < ms::LatLon::kMinLon ||
        *lon > ms::LatLon::kMaxLon)
      continue;

    Station station;
    station.m_id = *id;
    station.m_point = ms::LatLon(*lat, *lon);
    if (auto const * name = Find(item, "name"))
    {
      if (auto const text = ReadString(*name))
        station.m_name = *text;
    }
    if (auto const * shortName = Find(item, "short_name"))
    {
      if (auto const text = ReadId(*shortName))
        station.m_shortName = *text;
    }
    feed.m_stations.push_back(std::move(station));
  }
  return feed;
}

std::optional<StationStatusFeed> ParseStationStatus(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root)
    return std::nullopt;
  auto const * data = Find(*root, "data");
  auto const * stations = data == nullptr ? nullptr : Find(*data, "stations");
  if (stations == nullptr || !stations->is_array())
    return std::nullopt;

  StationStatusFeed feed;
  feed.m_ttlSec = ReadTtl(*root);
  feed.m_lastUpdated = ReadLastUpdated(*root);
  for (auto const & item : stations->get_array())
  {
    if (auto status = ReadStatus(item))
    {
      auto const id = status->m_id;
      feed.m_byId.emplace(id, std::move(*status));
    }
  }
  return feed;
}

std::optional<EbikeByType> ParseVehicleTypes(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root)
    return std::nullopt;
  auto const * data = Find(*root, "data");
  auto const * types = data == nullptr ? nullptr : Find(*data, "vehicle_types");
  if (types == nullptr || !types->is_array())
    return std::nullopt;

  EbikeByType result;
  for (auto const & item : types->get_array())
  {
    auto const * idValue = Find(item, "vehicle_type_id");
    if (idValue == nullptr)
      continue;
    auto const id = ReadId(*idValue);
    if (!id)
      continue;
    bool electric = LooksElectric(*id);
    if (auto const * propulsion = Find(item, "propulsion_type"))
    {
      if (auto const text = ReadString(*propulsion))
      {
        std::string lower = *text;
        strings::MakeLowerCaseInplace(lower);
        electric = lower == "electric_assist" || lower == "electric";
      }
    }
    else if (auto const * name = Find(item, "name"))
    {
      if (auto const text = ReadString(*name))
        electric = LooksElectric(*text);
    }
    result.emplace(*id, electric);
  }
  return result;
}

void ApplyVehicleTypeSplit(StationStatusFeed & status, EbikeByType const & isEbikeByType)
{
  for (auto & [id, station] : status.m_byId)
  {
    (void)id;
    if (station.m_ebikesAvailable || station.m_mechanicalAvailable || station.m_vehicleTypes.empty())
      continue;

    int ebikes = 0;
    int mechanical = 0;
    bool classified = true;
    for (auto const & type : station.m_vehicleTypes)
    {
      auto const it = isEbikeByType.find(type.m_typeId);
      if (it != isEbikeByType.end())
        if (it->second)
          ebikes += type.m_count;
        else
          mechanical += type.m_count;
      else if (LooksElectric(type.m_typeId))
        ebikes += type.m_count;
      else
        classified = false;
    }
    if (!classified)
      continue;
    station.m_ebikesAvailable = ebikes;
    station.m_mechanicalAvailable = mechanical;
  }
}
}  // namespace bike_share
