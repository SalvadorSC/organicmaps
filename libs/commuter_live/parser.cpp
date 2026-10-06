#include "commuter_live/parser.hpp"

#include "coding/serdes_json.hpp"

#include "base/string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace commuter_live
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
  return {};
}

std::vector<std::string> AllParades(std::string_view text)
{
  std::vector<std::string> stops;
  size_t pos = 0;
  while (pos < text.size())
  {
    auto const key = text.find("\"parada\"", pos);
    if (key == std::string_view::npos)
      break;
    auto const colon = text.find(':', key);
    if (colon == std::string_view::npos)
      break;
    auto const open = text.find('"', colon + 1);
    if (open == std::string_view::npos)
      break;
    auto const close = text.find('"', open + 1);
    if (close == std::string_view::npos)
      break;
    if (close > open + 1)
      stops.emplace_back(text.substr(open + 1, close - open - 1));
    pos = close + 1;
  }
  return stops;
}

class Cursor
{
public:
  explicit Cursor(std::string_view data) : m_data(data) {}

  bool Eof() const { return m_pos >= m_data.size(); }

  bool ReadVarint(uint64_t & out)
  {
    uint64_t result = 0;
    for (int shift = 0; shift <= 63; shift += 7)
    {
      if (m_pos >= m_data.size())
        return false;
      auto const byte = static_cast<unsigned char>(m_data[m_pos++]);
      result |= static_cast<uint64_t>(byte & 0x7f) << shift;
      if ((byte & 0x80) == 0)
      {
        out = result;
        return true;
      }
    }
    return false;
  }

  bool ReadTag(uint32_t & field, uint32_t & wire)
  {
    uint64_t tag = 0;
    if (!ReadVarint(tag))
      return false;
    wire = static_cast<uint32_t>(tag & 0x7);
    field = static_cast<uint32_t>(tag >> 3);
    return field != 0;
  }

  bool ReadSlice(std::string_view & out)
  {
    uint64_t len = 0;
    if (!ReadVarint(len) || len > m_data.size() - m_pos)
      return false;
    out = m_data.substr(m_pos, static_cast<size_t>(len));
    m_pos += static_cast<size_t>(len);
    return true;
  }

  bool ReadString(std::string & out)
  {
    std::string_view slice;
    if (!ReadSlice(slice))
      return false;
    out.assign(slice);
    return true;
  }

  bool ReadFloat(float & out)
  {
    if (m_data.size() - m_pos < 4)
      return false;
    std::memcpy(&out, m_data.data() + m_pos, 4);
    m_pos += 4;
    return true;
  }

  bool Skip(uint32_t wire)
  {
    switch (wire)
    {
    case 0:
    {
      uint64_t unused = 0;
      return ReadVarint(unused);
    }
    case 1: return SkipBytes(8);
    case 5: return SkipBytes(4);
    case 2:
    {
      std::string_view unused;
      return ReadSlice(unused);
    }
    default: return false;
    }
  }

private:
  bool SkipBytes(size_t count)
  {
    if (count > m_data.size() - m_pos)
      return false;
    m_pos += count;
    return true;
  }

  std::string_view m_data;
  size_t m_pos = 0;
};

bool ParseMessage(std::string_view bytes, auto && onField)
{
  Cursor cursor(bytes);
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return false;
    if (!onField(cursor, field, wire))
      return false;
  }
  return true;
}

std::optional<std::string> FirstLine(std::string_view a, std::string_view b, std::string_view c, std::string_view d)
{
  for (std::string_view text : {a, b, c, d})
    if (auto line = RodaliesLine(text))
      return line;
  return std::nullopt;
}

bool ParsePosition(std::string_view bytes, RawTrain & train)
{
  return ParseMessage(bytes, [&](Cursor & cursor, uint32_t field, uint32_t wire)
  {
    // Position: latitude=1, longitude=2, bearing=3, speed=5. All float.
    if (wire == 5 && (field == 1 || field == 2 || field == 3))
    {
      float value = 0;
      if (!cursor.ReadFloat(value))
        return false;
      if (field == 1)
        train.m_lat = value;
      else if (field == 2)
        train.m_lon = value;
      else
        train.m_bearingDeg = value;
      return true;
    }
    return cursor.Skip(wire);
  });
}

bool ParseVehicle(std::string_view bytes, std::string & id, std::string & label)
{
  return ParseMessage(bytes, [&](Cursor & cursor, uint32_t field, uint32_t wire)
  {
    if (wire == 2 && (field == 1 || field == 2))
    {
      std::string text;
      if (!cursor.ReadString(text))
        return false;
      if (field == 1)
        id = std::move(text);
      else
        label = std::move(text);
      return true;
    }
    return cursor.Skip(wire);
  });
}

bool ParseTrip(std::string_view bytes, std::string & tripId)
{
  return ParseMessage(bytes, [&](Cursor & cursor, uint32_t field, uint32_t wire)
  {
    if (field == 1 && wire == 2)
      return cursor.ReadString(tripId);
    return cursor.Skip(wire);
  });
}

bool ParseVehiclePosition(std::string_view bytes, RawTrain & train, bool & hasPosition, std::string & vehicleId,
                          std::string & label, std::string & tripId)
{
  hasPosition = false;
  return ParseMessage(bytes, [&](Cursor & cursor, uint32_t field, uint32_t wire)
  {
    if (wire != 2)
      return cursor.Skip(wire);
    std::string_view slice;
    if (!cursor.ReadSlice(slice))
      return false;
    if (field == 1)
      return ParseTrip(slice, tripId);
    if (field == 2)
    {
      hasPosition = true;
      return ParsePosition(slice, train);
    }
    if (field == 8)
      return ParseVehicle(slice, vehicleId, label);
    return true;
  });
}

bool ParseStopEvent(std::string_view bytes, int64_t & unixSec)
{
  bool has = false;
  bool const ok = ParseMessage(bytes, [&](Cursor & cursor, uint32_t field, uint32_t wire)
  {
    if (field == 2 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      unixSec = static_cast<int64_t>(value);
      has = true;
      return true;
    }
    return cursor.Skip(wire);
  });
  return ok && has;
}

bool ParseStopVisit(std::string_view bytes, StopVisit & visit, bool & skipped)
{
  skipped = false;
  int64_t arrival = 0;
  int64_t departure = 0;
  bool hasArrival = false;
  bool hasDeparture = false;
  bool const ok = ParseMessage(bytes, [&](Cursor & cursor, uint32_t field, uint32_t wire)
  {
    if ((field == 2 || field == 3) && wire == 2)
    {
      std::string_view slice;
      if (!cursor.ReadSlice(slice))
        return false;
      int64_t unixSec = 0;
      if (!ParseStopEvent(slice, unixSec))
        return true;
      if (field == 2)
      {
        arrival = unixSec;
        hasArrival = true;
      }
      else
      {
        departure = unixSec;
        hasDeparture = true;
      }
      return true;
    }
    if (field == 4 && wire == 2)
      return cursor.ReadString(visit.m_stopId);
    if (field == 5 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      skipped = value == 1;
      return true;
    }
    return cursor.Skip(wire);
  });
  if (!ok)
    return false;
  if (hasArrival)
    visit.m_etaUnixSec = arrival;
  else if (hasDeparture)
    visit.m_etaUnixSec = departure;
  return true;
}

bool ParseTripDescriptor(std::string_view bytes, std::string & tripId, std::string & routeId, bool & canceled)
{
  canceled = false;
  return ParseMessage(bytes, [&](Cursor & cursor, uint32_t field, uint32_t wire)
  {
    if (field == 1 && wire == 2)
      return cursor.ReadString(tripId);
    if (field == 5 && wire == 2)
      return cursor.ReadString(routeId);
    if (field == 4 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      canceled = value == 3;
      return true;
    }
    return cursor.Skip(wire);
  });
}
}  // namespace

std::optional<std::string> RodaliesLine(std::string_view text)
{
  for (size_t i = 0; i < text.size(); ++i)
  {
    if (text[i] != 'R')
      continue;
    // A digit may precede the code, as in Renfe trip ids such as 5177M77552R4.
    if (i > 0 && std::isalpha(static_cast<unsigned char>(text[i - 1])))
      continue;
    size_t j = i + 1;
    if (j >= text.size() || !std::isdigit(static_cast<unsigned char>(text[j])))
      continue;
    while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])))
      ++j;
    while (j < text.size() && std::isupper(static_cast<unsigned char>(text[j])))
      ++j;
    if (j < text.size() && std::isalnum(static_cast<unsigned char>(text[j])))
      continue;
    return std::string(text.substr(i, j - i));
  }
  return std::nullopt;
}

std::optional<std::vector<RawTrain>> ParseGeotren(std::string_view json)
{
  auto const root = ReadJson(json);
  if (!root)
    return std::nullopt;
  auto const * results = Find(*root, "results");
  if (results == nullptr || !results->is_array())
    return std::nullopt;
  std::vector<RawTrain> trains;
  for (auto const & row : results->get_array())
  {
    if (!row.is_object())
      continue;
    auto const * point = Find(row, "geo_point_2d");
    if (point == nullptr || !point->is_object())
      continue;
    auto const * latValue = Find(*point, "lat");
    auto const * lonValue = Find(*point, "lon");
    if (latValue == nullptr || lonValue == nullptr)
      continue;
    auto const lat = ReadDouble(*latValue);
    auto const lon = ReadDouble(*lonValue);
    if (!lat || !lon)
      continue;
    RawTrain train;
    train.m_id = ReadText(Find(row, "id"));
    train.m_line = ReadText(Find(row, "lin"));
    train.m_lat = *lat;
    train.m_lon = *lon;
    train.m_destination = ReadText(Find(row, "desti"));
    train.m_upcoming = AllParades(ReadText(Find(row, "properes_parades")));
    if (!train.m_destination.empty() &&
        std::find(train.m_upcoming.begin(), train.m_upcoming.end(), train.m_destination) == train.m_upcoming.end())
      train.m_upcoming.push_back(train.m_destination);
    if (!train.m_upcoming.empty())
      train.m_nextStop = train.m_upcoming.front();
    train.m_parkedAt = ReadText(Find(row, "estacionat_a"));
    trains.push_back(std::move(train));
  }
  return trains;
}

std::optional<std::vector<RawTrain>> ParseVehiclePositions(std::string_view bytes)
{
  std::vector<RawTrain> trains;
  Cursor cursor(bytes);
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return std::nullopt;
    if (field != 2 || wire != 2)
    {
      if (!cursor.Skip(wire))
        return std::nullopt;
      continue;
    }
    std::string_view entity;
    if (!cursor.ReadSlice(entity))
      return std::nullopt;
    RawTrain train;
    bool hasPosition = false;
    std::string entityId;
    std::string vehicleId;
    std::string label;
    std::string tripId;
    if (!ParseMessage(entity, [&](Cursor & inner, uint32_t entityField, uint32_t entityWire)
    {
      if (entityField == 1 && entityWire == 2)
        return inner.ReadString(entityId);
      if (entityField == 4 && entityWire == 2)
      {
        std::string_view slice;
        if (!inner.ReadSlice(slice))
          return false;
        return ParseVehiclePosition(slice, train, hasPosition, vehicleId, label, tripId);
      }
      return inner.Skip(entityWire);
    }))
      continue;
    if (!hasPosition)
      continue;
    train.m_id = !entityId.empty() ? entityId : vehicleId;
    if (auto const line = FirstLine(label, vehicleId, entityId, tripId))
      train.m_line = *line;
    trains.push_back(std::move(train));
  }
  return trains;
}

std::optional<std::vector<TripPass>> ParseTripUpdates(std::string_view bytes)
{
  std::vector<TripPass> trips;
  Cursor cursor(bytes);
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return std::nullopt;
    if (field != 2 || wire != 2)
    {
      if (!cursor.Skip(wire))
        return std::nullopt;
      continue;
    }
    std::string_view entity;
    if (!cursor.ReadSlice(entity))
      return std::nullopt;
    TripPass trip;
    bool canceled = false;
    bool sawUpdate = false;
    if (!ParseMessage(entity, [&](Cursor & inner, uint32_t entityField, uint32_t entityWire)
    {
      if (entityField != 3 || entityWire != 2)
        return inner.Skip(entityWire);
      std::string_view update;
      if (!inner.ReadSlice(update))
        return false;
      sawUpdate = true;
      return ParseMessage(update, [&](Cursor & updateCursor, uint32_t updateField, uint32_t updateWire)
      {
        if (updateField == 1 && updateWire == 2)
        {
          std::string_view slice;
          if (!updateCursor.ReadSlice(slice))
            return false;
          std::string tripId;
          std::string routeId;
          if (!ParseTripDescriptor(slice, tripId, routeId, canceled))
            return false;
          if (auto const line = FirstLine(tripId, routeId, {}, {}))
            trip.m_line = *line;
          return true;
        }
        if (updateField == 2 && updateWire == 2)
        {
          std::string_view slice;
          if (!updateCursor.ReadSlice(slice))
            return false;
          StopVisit visit;
          bool skipped = false;
          if (!ParseStopVisit(slice, visit, skipped))
            return false;
          if (!skipped && !visit.m_stopId.empty() && visit.m_etaUnixSec > 0)
            trip.m_stops.push_back(std::move(visit));
          return true;
        }
        return updateCursor.Skip(updateWire);
      });
    }))
      continue;
    if (!sawUpdate || canceled || trip.m_line.empty() || trip.m_stops.empty())
      continue;
    trips.push_back(std::move(trip));
  }
  return trips;
}
}  // namespace commuter_live
