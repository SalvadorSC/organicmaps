#include "bus_live/arrivals_service.hpp"

#include "bus_live/arrivals.hpp"
#include "bus_live/gtfs_csv.hpp"
#include "bus_live/stop_matcher.hpp"
#include "bus_live/tmb_parser.hpp"

#include "platform/http_client.hpp"
#include "platform/platform.hpp"
#include "platform/settings.hpp"

#include "coding/file_reader.hpp"
#include "coding/file_writer.hpp"

#include "base/exception.hpp"
#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace bus_live
{
namespace
{
double constexpr kMinLat = 41.26;
double constexpr kMaxLat = 41.55;
double constexpr kMinLon = 1.90;
double constexpr kMaxLon = 2.30;
double constexpr kTimeoutSec = 20.0;
double constexpr kZipTimeoutSec = 60.0;
std::string_view constexpr kUserAgent = "OrganicMaps";
std::chrono::seconds constexpr kFeedTtl{30};
std::chrono::seconds constexpr kStaticTtl{7 * 24 * 3600};
int64_t constexpr kStaticTtlSec = 7 * 24 * 3600;

std::string Encode(std::string_view text)
{
  char constexpr kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (unsigned char const ch : text)
  {
    bool const plain = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
                       ch == '_' || ch == '.' || ch == '~';
    if (plain)
      out.push_back(static_cast<char>(ch));
    else
    {
      out.push_back('%');
      out.push_back(kHex[ch >> 4]);
      out.push_back(kHex[ch & 0xf]);
    }
  }
  return out;
}

uint64_t Fingerprint(std::string_view text)
{
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char const ch : text)
  {
    hash ^= ch;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string TmbStopUrl(std::string const & code, Credentials const & credentials)
{
  return std::string(kTmbStopUrlPrefix) + Encode(code) + "?app_id=" + Encode(credentials.m_appId) +
         "&app_key=" + Encode(credentials.m_appKey);
}

std::string TmbCatalogRequestUrl(Credentials const & credentials)
{
  return std::string(kTmbCatalogUrl) + "?app_id=" + Encode(credentials.m_appId) +
         "&app_key=" + Encode(credentials.m_appKey);
}

bool UrlHasSecret(std::string const & url)
{
  return url.find("app_key=") != std::string::npos;
}

std::optional<std::string> HttpGet(std::string const & url, double timeoutSec)
{
  bool const secret = UrlHasSecret(url);
  try
  {
    platform::HttpClient request(url);
    request.SetTimeout(timeoutSec);
    request.SetRawHeader("User-Agent", std::string(kUserAgent));
    std::string body;
    if (!request.RunHttpRequest(body))
    {
      if (secret)
        LOG(LINFO, ("Bus arrivals request failed", request.ErrorCode()));
      else
        LOG(LINFO, ("Bus arrivals request failed", url, request.ErrorCode()));
      return std::nullopt;
    }
    return body;
  }
  catch (RootException const & exception)
  {
    if (secret)
      LOG(LINFO, ("Bus arrivals request failed", exception.Msg()));
    else
      LOG(LINFO, ("Bus arrivals request failed", url, exception.Msg()));
    return std::nullopt;
  }
  catch (std::exception const & exception)
  {
    if (secret)
      LOG(LINFO, ("Bus arrivals request failed", exception.what()));
    else
      LOG(LINFO, ("Bus arrivals request failed", url, exception.what()));
    return std::nullopt;
  }
}

bool DownloadToFile(std::string const & url, std::string const & path, double timeoutSec)
{
  try
  {
    platform::HttpClient request(url);
    request.SetTimeout(timeoutSec);
    request.SetRawHeader("User-Agent", std::string(kUserAgent));
    request.SetReceivedFile(path);
    if (!request.RunHttpRequest())
    {
      LOG(LINFO, ("Bus arrivals download failed", url, request.ErrorCode()));
      return false;
    }
    uint64_t size = 0;
    return Platform::GetFileSizeByFullPath(path, size) && size > 0;
  }
  catch (RootException const & exception)
  {
    LOG(LINFO, ("Bus arrivals download failed", url, exception.Msg()));
    return false;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Bus arrivals download failed", url, exception.what()));
    return false;
  }
}

bool IsFreshFile(std::string const & path, int64_t nowUnix)
{
  if (!Platform::IsFileExistsByFullPath(path))
    return false;
  time_t const modified = Platform::GetFileModificationTime(path);
  if (modified <= 0)
    return false;
  return nowUnix < static_cast<int64_t>(modified) || nowUnix - static_cast<int64_t>(modified) < kStaticTtlSec;
}

bool ReplaceFile(std::string const & from, std::string const & to)
{
  std::error_code error;
  std::filesystem::rename(from, to, error);
  if (!error)
    return true;
  Platform::RemoveFileIfExists(from);
  return false;
}

std::optional<StaticIndex> LoadAmbStatic()
{
  try
  {
    std::string const dir = base::JoinPath(GetPlatform().WritableDir(), "bus_live");
    if (!Platform::MkDirRecursively(dir))
      return std::nullopt;
    std::string const path = base::JoinPath(dir, "amb_gtfs.zip");
    int64_t const now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (!IsFreshFile(path, now))
    {
      std::string const temporary = path + ".download";
      Platform::RemoveFileIfExists(temporary);
      if (!DownloadToFile(std::string(kAmbGtfsUrl), temporary, kZipTimeoutSec) || !ReplaceFile(temporary, path))
        Platform::RemoveFileIfExists(temporary);
    }
    if (!Platform::IsFileExistsByFullPath(path))
      return std::nullopt;
    return LoadStaticIndexFromZip(path);
  }
  catch (RootException const & exception)
  {
    LOG(LINFO, ("Bus arrivals GTFS load failed", exception.Msg()));
    return std::nullopt;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Bus arrivals GTFS load failed", exception.what()));
    return std::nullopt;
  }
}

std::string CatalogPath(std::string const & appId)
{
  char hex[17];
  auto const hash = Fingerprint(appId);
  for (int i = 0; i < 16; ++i)
  {
    auto const nibble = (hash >> (60 - 4 * i)) & 0xf;
    hex[i] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
  }
  hex[16] = '\0';
  return base::JoinPath(GetPlatform().WritableDir(), "bus_live", std::string("tmb_parades_") + hex + ".json");
}

std::optional<std::string> ReadFile(std::string const & path)
{
  try
  {
    FileReader reader(path);
    std::string text;
    reader.ReadAsString(text);
    return text;
  }
  catch (RootException const &)
  {
    return std::nullopt;
  }
}

void WriteFile(std::string const & path, std::string const & body)
{
  FileWriter writer(path);
  writer.Write(body.data(), body.size());
}

std::optional<std::vector<TransitStop>> LoadTmbCatalog(Credentials const & credentials)
{
  if (credentials.Empty())
    return std::nullopt;
  try
  {
    std::string const dir = base::JoinPath(GetPlatform().WritableDir(), "bus_live");
    if (!Platform::MkDirRecursively(dir))
      return std::nullopt;
    // The file name is a hash of the app id only. The key is not stored in the path.
    std::string const path = CatalogPath(credentials.m_appId);
    int64_t const now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::optional<std::string> body;
    if (IsFreshFile(path, now))
      body = ReadFile(path);
    if (!body)
    {
      body = HttpGet(TmbCatalogRequestUrl(credentials), kTimeoutSec);
      if (body)
      {
        auto parsed = ParseTmbCatalog(*body);
        if (!parsed)
          return std::nullopt;
        WriteFile(path, *body);
        return parsed;
      }
      if (Platform::IsFileExistsByFullPath(path))
        body = ReadFile(path);
    }
    if (!body)
      return std::nullopt;
    return ParseTmbCatalog(*body);
  }
  catch (RootException const & exception)
  {
    LOG(LINFO, ("Bus arrivals TMB catalog failed", exception.Msg()));
    return std::nullopt;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Bus arrivals TMB catalog failed", exception.what()));
    return std::nullopt;
  }
}

std::string TrimmedSetting(std::string_view key)
{
  std::string value;
  if (!settings::Get(key, value))
    return {};
  strings::Trim(value);
  return value;
}

int64_t UnixNow(std::chrono::system_clock::time_point time)
{
  return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

ArrivalsService & SharedService()
{
  static ArrivalsService service([](std::string const & url) { return HttpGet(url, kTimeoutSec); }, []
  { return std::chrono::system_clock::now(); }, [] { return std::chrono::steady_clock::now(); }, [] {
    return GetTmbCredentials();
  }, [] { return LoadAmbStatic(); }, [](Credentials const & credentials) { return LoadTmbCatalog(credentials); });
  return service;
}
}  // namespace

bool InBarcelonaArea(ms::LatLon const & point)
{
  return point.m_lat >= kMinLat && point.m_lat <= kMaxLat && point.m_lon >= kMinLon && point.m_lon <= kMaxLon;
}

ArrivalsService::ArrivalsService(HttpGet httpGet, WallClock wall, SteadyClock steady, CredentialsFn credentials,
                                 StaticLoader loadStatic, CatalogLoader loadCatalog)
  : m_httpGet(std::move(httpGet))
  , m_wall(std::move(wall))
  , m_steady(std::move(steady))
  , m_credentials(std::move(credentials))
  , m_loadStatic(std::move(loadStatic))
  , m_loadCatalog(std::move(loadCatalog))
{}

ArrivalList ArrivalsService::Lookup(StopQuery const & query)
{
  std::lock_guard<std::mutex> const lock(m_mutex);
  ArrivalList result;
  if (!query.m_point.IsValid() || !InBarcelonaArea(query.m_point))
  {
    result.m_status = LookupStatus::NotApplicable;
    return result;
  }

  auto const nowSteady = m_steady ? m_steady() : std::chrono::steady_clock::now();
  int64_t const nowUnix = UnixNow(m_wall ? m_wall() : std::chrono::system_clock::now());

  if (!(nowSteady < m_staticExpires) && m_loadStatic)
  {
    auto loaded = m_loadStatic();
    bool const got = loaded.has_value();
    if (got)
      m_static = std::move(loaded);
    auto const loadedAt = m_steady ? m_steady() : nowSteady;
    m_staticExpires = loadedAt + (got ? kStaticTtl : kFeedTtl);
  }
  StaticIndex const * index = m_static && nowSteady < m_staticExpires ? &*m_static : nullptr;

  Credentials credentials = m_credentials ? m_credentials() : Credentials{};
  strings::Trim(credentials.m_appId);
  strings::Trim(credentials.m_appKey);
  bool const tmbEnabled = !credentials.Empty();
  uint64_t const signature = tmbEnabled ? Fingerprint(credentials.m_appId + "\n" + credentials.m_appKey) : 0;
  if (signature != m_tmbSig)
  {
    m_tmbSig = signature;
    m_catalog.reset();
    m_catalogExpires = {};
    m_tmbByStop.clear();
  }

  std::vector<TransitStop> const * catalog = nullptr;
  if (tmbEnabled)
  {
    if (nowSteady < m_catalogExpires)
      catalog = m_catalog ? &*m_catalog : nullptr;
    else if (m_loadCatalog)
    {
      auto loaded = m_loadCatalog(credentials);
      bool const got = loaded.has_value();
      if (got)
        m_catalog = std::move(loaded);
      auto const loadedAt = m_steady ? m_steady() : nowSteady;
      m_catalogExpires = loadedAt + (got ? kStaticTtl : kFeedTtl);
      catalog = m_catalog && nowSteady < m_catalogExpires ? &*m_catalog : nullptr;
    }
  }

  std::optional<StopMatch> ambMatch;
  if (index != nullptr)
    ambMatch = MatchStop(index->m_stops, query);
  std::optional<StopMatch> tmbMatch;
  if (catalog != nullptr)
    tmbMatch = MatchStop(*catalog, query);

  if (ambMatch)
  {
    if (!(nowSteady < m_feedExpires) && m_httpGet)
    {
      if (auto const body = m_httpGet(std::string(kAmbTripsUrl)))
      {
        if (auto parsed = ParseFeed(*body))
          m_feed = std::move(parsed);
      }
      m_feedExpires = (m_steady ? m_steady() : nowSteady) + kFeedTtl;
    }
  }

  std::vector<Arrival> ambArrivals;
  int64_t updated = 0;
  if (ambMatch && index != nullptr && m_feed && nowSteady < m_feedExpires)
  {
    ambArrivals = CollectAmbArrivals(*m_feed, *index, ambMatch->m_stop, nowUnix);
    updated = m_feed->m_headerTimestamp;
  }

  std::string tmbCode;
  if (!CanonicalCode(query.m_ref).empty())
    tmbCode = CanonicalCode(query.m_ref);
  else if (tmbMatch)
    tmbCode = tmbMatch->m_stop.m_code;
  else if (ambMatch)
    tmbCode = ambMatch->m_stop.m_code;

  std::vector<Arrival> tmbArrivals;
  if (tmbEnabled && !tmbCode.empty())
  {
    auto const cached = m_tmbByStop.find(tmbCode);
    bool const fresh = cached != m_tmbByStop.end() && nowSteady < cached->second.second;
    if (fresh)
    {
      if (cached->second.first.m_ok)
      {
        for (auto const & arrival : cached->second.first.m_arrivals)
          if (arrival.m_etaUnixSec >= nowUnix - kStaleSec)
            tmbArrivals.push_back(arrival);
        if (cached->second.first.m_updatedUnixSec != 0)
          updated = std::max(updated, cached->second.first.m_updatedUnixSec);
      }
    }
    else if (m_httpGet)
    {
      TmbCache entry;
      if (auto const body = m_httpGet(TmbStopUrl(tmbCode, credentials)))
      {
        if (auto parsed = ParseTmbArrivals(*body))
        {
          entry.m_ok = true;
          entry.m_updatedUnixSec = parsed->m_updatedUnixSec;
          for (auto & arrival : parsed->m_arrivals)
            if (arrival.m_etaUnixSec >= nowUnix - kStaleSec)
              entry.m_arrivals.push_back(std::move(arrival));
          if (entry.m_updatedUnixSec != 0)
            updated = std::max(updated, entry.m_updatedUnixSec);
          tmbArrivals = entry.m_arrivals;
        }
      }
      auto const expires = (m_steady ? m_steady() : nowSteady) + kFeedTtl;
      m_tmbByStop.insert_or_assign(tmbCode, std::make_pair(std::move(entry), expires));
    }
  }

  result.m_arrivals = MergeArrivals(std::move(ambArrivals), std::move(tmbArrivals));
  result.m_updatedUnixSec = updated != 0 ? updated : nowUnix;
  result.m_status = result.m_arrivals.empty() ? LookupStatus::NoData : LookupStatus::Ok;
  return result;
}

bool IsBusArrivalsEnabled()
{
  return settings::IsEnabled(kBusArrivalsEnabledSetting);
}

void SetBusArrivalsEnabled(bool enabled)
{
  settings::Set(kBusArrivalsEnabledSetting, enabled);
}

Credentials GetTmbCredentials()
{
  Credentials credentials;
  credentials.m_appId = TrimmedSetting(kTmbAppIdSetting);
  credentials.m_appKey = TrimmedSetting(kTmbAppKeySetting);
  return credentials;
}

void SetTmbCredentials(std::string appId, std::string appKey)
{
  strings::Trim(appId);
  strings::Trim(appKey);
  settings::Set(kTmbAppIdSetting, appId);
  settings::Set(kTmbAppKeySetting, appKey);
}

ArrivalList LookupArrivals(StopQuery const & query)
{
  if (!IsBusArrivalsEnabled())
  {
    ArrivalList hidden;
    hidden.m_status = LookupStatus::NotApplicable;
    return hidden;
  }
  return SharedService().Lookup(query);
}
}  // namespace bus_live
