#include "bus_live/gtfs_csv.hpp"

#include "bus_live/stop_matcher.hpp"

#include "coding/reader.hpp"
#include "coding/zip_reader.hpp"

#include "base/exception.hpp"
#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include <utility>

namespace bus_live
{
namespace
{
bool NextRow(std::string_view & in, std::vector<std::string> & row)
{
  row.clear();
  if (in.empty())
    return false;

  std::string field;
  bool inQuotes = false;
  for (size_t i = 0; i < in.size(); ++i)
  {
    char const ch = in[i];
    if (inQuotes)
    {
      if (ch == '"')
        if (i + 1 < in.size() && in[i + 1] == '"')
        {
          field.push_back('"');
          ++i;
        }
        else
          inQuotes = false;
      else
        field.push_back(ch);
      continue;
    }
    if (ch == '"')
    {
      inQuotes = true;
      continue;
    }
    if (ch == ',')
    {
      row.push_back(std::move(field));
      field.clear();
      continue;
    }
    if (ch == '\n')
    {
      row.push_back(std::move(field));
      in.remove_prefix(i + 1);
      return true;
    }
    if (ch != '\r')
      field.push_back(ch);
  }
  row.push_back(std::move(field));
  in = {};
  return true;
}

std::string_view StripBom(std::string_view csv)
{
  if (csv.size() >= 3 && static_cast<unsigned char>(csv[0]) == 0xef && static_cast<unsigned char>(csv[1]) == 0xbb &&
      static_cast<unsigned char>(csv[2]) == 0xbf)
    csv.remove_prefix(3);
  return csv;
}

int Column(std::vector<std::string> const & header, std::string const & name)
{
  for (size_t i = 0; i < header.size(); ++i)
  {
    std::string cell = header[i];
    strings::Trim(cell);
    if (cell == name)
      return static_cast<int>(i);
  }
  return -1;
}

std::string Cell(std::vector<std::string> const & row, int index)
{
  if (index < 0 || static_cast<size_t>(index) >= row.size())
    return {};
  std::string value = row[static_cast<size_t>(index)];
  strings::Trim(value);
  return value;
}

template <typename RowFn>
bool ForEachRow(std::string_view csv, RowFn && onRow)
{
  csv = StripBom(csv);
  std::vector<std::string> header;
  if (!NextRow(csv, header))
    return false;
  std::vector<std::string> row;
  while (NextRow(csv, row))
  {
    if (row.size() == 1 && row[0].empty())
      continue;
    if (!onRow(header, row))
      return false;
  }
  return true;
}

std::string FindZipEntry(ZipFileReader::FileList const & files, std::string const & base)
{
  std::string const suffix = "/" + base;
  std::string nested;
  for (auto const & entry : files)
  {
    if (entry.first == base)
      return entry.first;
    if (nested.empty() && entry.first.size() >= suffix.size() && entry.first.ends_with(suffix))
      nested = entry.first;
  }
  return nested;
}

std::optional<std::string> ReadZipText(std::string const & zipPath, std::string const & entry)
{
  try
  {
    auto reader = ZipFileReader::CreateModelReader(zipPath, entry);
    if (!reader)
      return std::nullopt;
    std::string text;
    reader->ReadAsString(text);
    return text;
  }
  catch (RootException const & exception)
  {
    LOG(LINFO, ("Bus arrivals GTFS entry failed", entry, exception.Msg()));
    return std::nullopt;
  }
}
}  // namespace

std::optional<std::vector<TransitStop>> ParseStops(std::string_view csv)
{
  std::vector<TransitStop> stops;
  bool ok = ForEachRow(csv, [&](std::vector<std::string> const & header, std::vector<std::string> const & row)
  {
    int const idCol = Column(header, "stop_id");
    int const nameCol = Column(header, "stop_name");
    int const latCol = Column(header, "stop_lat");
    int const lonCol = Column(header, "stop_lon");
    if (idCol < 0 || latCol < 0 || lonCol < 0)
      return false;
    TransitStop stop;
    stop.m_id = Cell(row, idCol);
    if (stop.m_id.empty())
      return true;
    stop.m_name = Cell(row, nameCol);
    auto const code = Cell(row, Column(header, "stop_code"));
    stop.m_code = CanonicalCode(code.empty() ? stop.m_id : code);
    double lat = 0;
    double lon = 0;
    if (!strings::to_double(Cell(row, latCol), lat) || !strings::to_double(Cell(row, lonCol), lon))
      return true;
    stop.m_point = ms::LatLon(lat, lon);
    stops.push_back(std::move(stop));
    return true;
  });
  if (!ok || stops.empty())
    return std::nullopt;
  return stops;
}

std::optional<std::vector<GtfsRoute>> ParseRoutes(std::string_view csv)
{
  std::vector<GtfsRoute> routes;
  bool ok = ForEachRow(csv, [&](std::vector<std::string> const & header, std::vector<std::string> const & row)
  {
    int const idCol = Column(header, "route_id");
    if (idCol < 0)
      return false;
    GtfsRoute route;
    route.m_id = Cell(row, idCol);
    if (route.m_id.empty())
      return true;
    route.m_shortName = Cell(row, Column(header, "route_short_name"));
    route.m_longName = Cell(row, Column(header, "route_long_name"));
    routes.push_back(std::move(route));
    return true;
  });
  if (!ok)
    return std::nullopt;
  return routes;
}

std::optional<std::vector<GtfsTrip>> ParseTrips(std::string_view csv)
{
  std::vector<GtfsTrip> trips;
  bool ok = ForEachRow(csv, [&](std::vector<std::string> const & header, std::vector<std::string> const & row)
  {
    int const idCol = Column(header, "trip_id");
    int const routeCol = Column(header, "route_id");
    if (idCol < 0 || routeCol < 0)
      return false;
    GtfsTrip trip;
    trip.m_id = Cell(row, idCol);
    trip.m_routeId = Cell(row, routeCol);
    if (trip.m_id.empty())
      return true;
    trip.m_headsign = Cell(row, Column(header, "trip_headsign"));
    trips.push_back(std::move(trip));
    return true;
  });
  if (!ok)
    return std::nullopt;
  return trips;
}

std::optional<StaticIndex> LoadStaticIndexFromZip(std::string const & zipPath)
{
  try
  {
    ZipFileReader::FileList files;
    ZipFileReader::FilesList(zipPath, files);
    auto const stopsName = FindZipEntry(files, "stops.txt");
    auto const routesName = FindZipEntry(files, "routes.txt");
    auto const tripsName = FindZipEntry(files, "trips.txt");
    if (stopsName.empty() || routesName.empty() || tripsName.empty())
    {
      LOG(LINFO, ("Bus arrivals GTFS zip is missing stops, routes or trips"));
      return std::nullopt;
    }
    auto const stopsText = ReadZipText(zipPath, stopsName);
    auto const routesText = ReadZipText(zipPath, routesName);
    auto const tripsText = ReadZipText(zipPath, tripsName);
    if (!stopsText || !routesText || !tripsText)
      return std::nullopt;

    auto stops = ParseStops(*stopsText);
    auto routes = ParseRoutes(*routesText);
    auto trips = ParseTrips(*tripsText);
    if (!stops || !routes || !trips)
      return std::nullopt;

    StaticIndex index;
    index.m_stops = std::move(*stops);
    for (auto & route : *routes)
    {
      auto id = route.m_id;
      index.m_routes.emplace(std::move(id), std::move(route));
    }
    for (auto & trip : *trips)
    {
      auto id = trip.m_id;
      index.m_trips.insert_or_assign(std::move(id), std::move(trip));
    }
    return index;
  }
  catch (RootException const & exception)
  {
    LOG(LINFO, ("Bus arrivals GTFS zip failed", exception.Msg()));
    return std::nullopt;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Bus arrivals GTFS zip failed", exception.what()));
    return std::nullopt;
  }
}
}  // namespace bus_live
