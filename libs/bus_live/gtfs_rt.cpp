#include "bus_live/gtfs_rt.hpp"

#include <cstddef>

namespace bus_live
{
namespace
{
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

bool ParseEvent(Cursor & cursor, StopTimeEvent & event)
{
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return false;
    if (field == 1 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      event.m_delay = static_cast<int32_t>(value);
    }
    else if (field == 2 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      event.m_time = static_cast<int64_t>(value);
    }
    else if (!cursor.Skip(wire))
      return false;
  }
  return true;
}

bool ParseStopTime(Cursor & cursor, StopTimeUpdate & update)
{
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return false;
    if ((field == 2 || field == 3) && wire == 2)
    {
      std::string_view slice;
      if (!cursor.ReadSlice(slice))
        return false;
      Cursor nested(slice);
      StopTimeEvent & event = field == 2 ? update.m_arrival : update.m_departure;
      if (!ParseEvent(nested, event))
        return false;
    }
    else if (field == 4 && wire == 2)
    {
      if (!cursor.ReadString(update.m_stopId))
        return false;
    }
    else if (field == 5 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      update.m_schedule = static_cast<int>(value);
    }
    else if (!cursor.Skip(wire))
      return false;
  }
  return true;
}

bool ParseTrip(Cursor & cursor, TripDescriptor & trip)
{
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return false;
    if (field == 1 && wire == 2)
    {
      if (!cursor.ReadString(trip.m_tripId))
        return false;
    }
    else if (field == 4 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      trip.m_schedule = static_cast<int>(value);
    }
    else if (field == 5 && wire == 2)
    {
      if (!cursor.ReadString(trip.m_routeId))
        return false;
    }
    else if (!cursor.Skip(wire))
      return false;
  }
  return true;
}

bool ParseTripUpdate(Cursor & cursor, TripUpdate & update)
{
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return false;
    if (field == 1 && wire == 2)
    {
      std::string_view slice;
      if (!cursor.ReadSlice(slice))
        return false;
      Cursor nested(slice);
      if (!ParseTrip(nested, update.m_trip))
        return false;
    }
    else if (field == 2 && wire == 2)
    {
      std::string_view slice;
      if (!cursor.ReadSlice(slice))
        return false;
      Cursor nested(slice);
      StopTimeUpdate stop;
      if (!ParseStopTime(nested, stop))
        return false;
      update.m_stops.push_back(std::move(stop));
    }
    else if (field == 4 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      update.m_timestamp = static_cast<int64_t>(value);
    }
    else if (!cursor.Skip(wire))
      return false;
  }
  return true;
}

bool ParseEntity(Cursor & cursor, Feed & feed)
{
  bool deleted = false;
  TripUpdate update;
  bool hasUpdate = false;
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return false;
    if (field == 2 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      deleted = value != 0;
    }
    else if (field == 3 && wire == 2)
    {
      std::string_view slice;
      if (!cursor.ReadSlice(slice))
        return false;
      Cursor nested(slice);
      if (!ParseTripUpdate(nested, update))
        return false;
      hasUpdate = true;
    }
    else if (!cursor.Skip(wire))
      return false;
  }
  if (hasUpdate && !deleted)
    feed.m_updates.push_back(std::move(update));
  return true;
}

bool ParseHeader(Cursor & cursor, Feed & feed)
{
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return false;
    if (field == 3 && wire == 0)
    {
      uint64_t value = 0;
      if (!cursor.ReadVarint(value))
        return false;
      feed.m_headerTimestamp = static_cast<int64_t>(value);
    }
    else if (!cursor.Skip(wire))
      return false;
  }
  return true;
}
}  // namespace

std::optional<Feed> ParseFeed(std::string_view bytes)
{
  if (bytes.empty() || bytes.front() == '<' || bytes.front() == '{' || bytes.front() == '[')
    return std::nullopt;

  Cursor cursor(bytes);
  Feed feed;
  while (!cursor.Eof())
  {
    uint32_t field = 0;
    uint32_t wire = 0;
    if (!cursor.ReadTag(field, wire))
      return std::nullopt;
    if (field == 1 && wire == 2)
    {
      std::string_view slice;
      if (!cursor.ReadSlice(slice))
        return std::nullopt;
      Cursor nested(slice);
      if (!ParseHeader(nested, feed))
        return std::nullopt;
    }
    else if (field == 2 && wire == 2)
    {
      std::string_view slice;
      if (!cursor.ReadSlice(slice))
        return std::nullopt;
      Cursor nested(slice);
      if (!ParseEntity(nested, feed))
        return std::nullopt;
    }
    else if (!cursor.Skip(wire))
      return std::nullopt;
  }
  return feed;
}
}  // namespace bus_live
