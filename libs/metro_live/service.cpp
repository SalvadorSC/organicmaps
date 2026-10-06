#include "metro_live/service.hpp"

#include "metro_live/parser.hpp"

#include "platform/http_client.hpp"
#include "platform/platform.hpp"
#include "platform/settings.hpp"

#include "coding/file_reader.hpp"
#include "coding/file_writer.hpp"

#include "base/exception.hpp"
#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include <chrono>
#include <filesystem>

namespace metro_live
{
namespace
{
double constexpr kTimeoutSec = 20.0;
std::string_view constexpr kUserAgent = "OrganicMaps";
std::chrono::seconds constexpr kLiveTtl{20};
std::chrono::seconds constexpr kRetryTtl{30};
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

std::string WithCredentials(std::string_view url, Credentials const & credentials)
{
  return std::string(url) + "?app_id=" + Encode(credentials.m_appId) + "&app_key=" + Encode(credentials.m_appKey);
}

bool UrlHasSecret(std::string const & url)
{
  return url.find("app_key=") != std::string::npos;
}

std::optional<std::string> HttpGet(std::string const & url)
{
  bool const secret = UrlHasSecret(url);
  try
  {
    platform::HttpClient request(url);
    request.SetTimeout(kTimeoutSec);
    request.SetRawHeader("User-Agent", std::string(kUserAgent));
    std::string body;
    if (!request.RunHttpRequest(body))
    {
      if (secret)
        LOG(LINFO, ("Metro request failed", request.ErrorCode()));
      else
        LOG(LINFO, ("Metro request failed", url, request.ErrorCode()));
      return std::nullopt;
    }
    return body;
  }
  catch (RootException const & exception)
  {
    if (secret)
      LOG(LINFO, ("Metro request failed", exception.Msg()));
    else
      LOG(LINFO, ("Metro request failed", url, exception.Msg()));
    return std::nullopt;
  }
  catch (std::exception const & exception)
  {
    if (secret)
      LOG(LINFO, ("Metro request failed", exception.what()));
    else
      LOG(LINFO, ("Metro request failed", url, exception.what()));
    return std::nullopt;
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

std::string CachePath(std::string const & appId, char const * name)
{
  char hex[17];
  auto const hash = Fingerprint(appId);
  for (int i = 0; i < 16; ++i)
  {
    auto const nibble = (hash >> (60 - 4 * i)) & 0xf;
    hex[i] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
  }
  hex[16] = '\0';
  return base::JoinPath(GetPlatform().WritableDir(), "metro_live", std::string(name) + "_" + hex + ".json");
}

// Static catalogs only. The file name is a hash of the app id. The key is not stored.
std::optional<std::string> CachedGet(std::string const & url, std::string const & path)
{
  int64_t const now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  if (IsFreshFile(path, now))
    if (auto cached = ReadFile(path))
      return cached;
  auto body = HttpGet(url);
  if (!body)
  {
    if (Platform::IsFileExistsByFullPath(path))
      return ReadFile(path);
    return std::nullopt;
  }
  try
  {
    std::string const dir = base::JoinPath(GetPlatform().WritableDir(), "metro_live");
    if (Platform::MkDirRecursively(dir))
      WriteFile(path, *body);
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Metro catalog save failed", exception.what()));
  }
  return body;
}

std::optional<std::string> ProductionGet(std::string const & url)
{
  bool const catalog = url.find("/transit/linies/metro") != std::string::npos;
  if (!catalog)
    return HttpGet(url);
  auto const idStart = url.find("app_id=");
  auto const idEnd = url.find('&', idStart == std::string::npos ? 0 : idStart);
  if (idStart == std::string::npos)
    return HttpGet(url);
  std::string const appId = url.substr(idStart + 7, idEnd - (idStart + 7));
  bool const stations = url.find("/estacions") != std::string::npos;
  try
  {
    return CachedGet(url, CachePath(appId, stations ? "stations" : "lines"));
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Metro catalog load failed", exception.what()));
    return std::nullopt;
  }
}

int64_t UnixNow(std::chrono::system_clock::time_point time)
{
  return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

MetroService & SharedService()
{
  static MetroService service([](std::string const & url) { return ProductionGet(url); }, [] {
    return std::chrono::system_clock::now();
  }, [] { return std::chrono::steady_clock::now(); }, [] { return GetTmbCredentials(); });
  return service;
}
}  // namespace

MetroService::MetroService(HttpGet httpGet, WallClock wall, SteadyClock steady, CredentialsFn credentials)
  : m_httpGet(std::move(httpGet))
  , m_wall(std::move(wall))
  , m_steady(std::move(steady))
  , m_credentials(std::move(credentials))
{}

PollResult MetroService::Poll(std::vector<MapTrack> const & tracks)
{
  std::lock_guard<std::mutex> const lock(m_mutex);
  PollResult result;
  result.m_enabled = true;
  Credentials credentials = m_credentials ? m_credentials() : Credentials{};
  strings::Trim(credentials.m_appId);
  strings::Trim(credentials.m_appKey);
  if (credentials.Empty())
  {
    result.m_needsKey = true;
    return result;
  }

  auto const nowSteady = m_steady ? m_steady() : std::chrono::steady_clock::now();
  int64_t const nowUnix = UnixNow(m_wall ? m_wall() : std::chrono::system_clock::now());
  uint64_t const signature = Fingerprint(credentials.m_appId + "\n" + credentials.m_appKey);
  if (signature != m_signature)
  {
    m_signature = signature;
    m_hasNetwork = false;
    m_hasRows = false;
    m_network = {};
    m_rows.clear();
    m_networkExpires = {};
    m_rowsExpires = {};
  }

  if (!(nowSteady < m_networkExpires) && m_httpGet)
  {
    auto const stationsBody = m_httpGet(WithCredentials(kStationsUrl, credentials));
    auto const linesBody = m_httpGet(WithCredentials(kLinesUrl, credentials));
    bool got = false;
    if (stationsBody && linesBody)
    {
      auto stations = ParseStations(*stationsBody);
      auto lines = ParseLines(*linesBody);
      if (stations && lines)
      {
        m_network = BuildNetwork(*lines, *stations);
        m_hasNetwork = true;
        got = true;
      }
    }
    auto const loadedAt = m_steady ? m_steady() : nowSteady;
    m_networkExpires = loadedAt + (got ? kStaticTtl : kRetryTtl);
  }

  if (!(nowSteady < m_rowsExpires) && m_httpGet)
  {
    bool got = false;
    if (auto const body = m_httpGet(WithCredentials(kArrivalsUrl, credentials)))
    {
      if (auto feed = ParseArrivals(*body))
      {
        m_rows = std::move(feed->m_rows);
        m_hasRows = true;
        got = true;
      }
    }
    auto const loadedAt = m_steady ? m_steady() : nowSteady;
    // A failed live fetch retries soon and keeps the previous rows until then.
    if (!got && m_hasRows)
      m_rowsExpires = loadedAt + kRetryTtl;
    else
      m_rowsExpires = loadedAt + (got ? kLiveTtl : kRetryTtl);
    if (!got && !m_hasRows)
      m_rows.clear();
  }

  if (m_hasNetwork && nowSteady < m_networkExpires)
    for (auto const & line : m_network.m_lines)
      result.m_lines.push_back(LineSummary{line.m_name, line.m_color});
  if (m_hasNetwork && m_hasRows && nowSteady < m_networkExpires && nowSteady < m_rowsExpires)
  {
    if (tracks.empty())
    {
      result.m_trains = EstimateTrains(m_network, m_rows, nowUnix);
    }
    else
    {
      Network snapped = m_network;
      ApplyMapTracks(snapped, tracks);
      result.m_trains = EstimateTrains(snapped, m_rows, nowUnix);
    }
  }
  return result;
}

bool IsMetroLiveEnabled()
{
  return settings::IsEnabled(kMetroLiveEnabledSetting);
}

void SetMetroLiveEnabled(bool enabled)
{
  settings::Set(kMetroLiveEnabledSetting, enabled);
}

Credentials GetTmbCredentials()
{
  Credentials credentials;
  if (!settings::Get(kTmbAppIdSetting, credentials.m_appId))
    credentials.m_appId.clear();
  if (!settings::Get(kTmbAppKeySetting, credentials.m_appKey))
    credentials.m_appKey.clear();
  strings::Trim(credentials.m_appId);
  strings::Trim(credentials.m_appKey);
  return credentials;
}

PollResult PollMetro(std::vector<MapTrack> const & tracks)
{
  if (!IsMetroLiveEnabled())
    return {};
  return SharedService().Poll(tracks);
}
}  // namespace metro_live
