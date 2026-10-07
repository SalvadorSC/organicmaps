#include "commuter_live/names.hpp"
#include "commuter_live/scheme_data.hpp"
#include "commuter_live/service.hpp"
#include "commuter_live/snap.hpp"

#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "transit/transit_graph_data.hpp"

#include "indexer/classificator.hpp"
#include "indexer/feature.hpp"
#include "indexer/mwm_set.hpp"
#include "indexer/scales.hpp"

#include "drape_frontend/metro_train_marker.hpp"

#include "drape/color.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"

#include "coding/reader.hpp"

#include "base/exception.hpp"
#include "base/logging.hpp"

#include "defines.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
dp::Color ParseCommuterColor(std::string const & text)
{
  auto hex = [](char ch) -> int
  {
    if (ch >= '0' && ch <= '9')
      return ch - '0';
    if (ch >= 'a' && ch <= 'f')
      return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
      return ch - 'A' + 10;
    return -1;
  };
  unsigned value = 0;
  int digits = 0;
  for (char const ch : text)
  {
    int const nibble = hex(ch);
    if (nibble < 0)
      continue;
    value = (value << 4) + static_cast<unsigned>(nibble);
    ++digits;
    if (digits == 6)
      break;
  }
  if (digits < 6)
    return dp::Color(128, 128, 128);
  return dp::Color(static_cast<uint8_t>((value >> 16) & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF),
                   static_cast<uint8_t>(value & 0xFF));
}

jobjectArray CommuterToTrains(JNIEnv * env, std::vector<commuter_live::Train> const & trains)
{
  jclass const clazz = env->FindClass("app/organicmaps/sdk/commuter_live/CommuterTrain");
  if (clazz == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  jmethodID const ctor = env->GetMethodID(clazz, "<init>", "()V");
  jfieldID const line = env->GetFieldID(clazz, "mLine", "Ljava/lang/String;");
  jfieldID const color = env->GetFieldID(clazz, "mColor", "Ljava/lang/String;");
  jfieldID const destination = env->GetFieldID(clazz, "mDestination", "Ljava/lang/String;");
  jfieldID const nextStop = env->GetFieldID(clazz, "mNextStop", "Ljava/lang/String;");
  jfieldID const key = env->GetFieldID(clazz, "mKey", "Ljava/lang/String;");
  jfieldID const lat = env->GetFieldID(clazz, "mLat", "D");
  jfieldID const lon = env->GetFieldID(clazz, "mLon", "D");
  jfieldID const heading = env->GetFieldID(clazz, "mHeadingDeg", "D");
  jfieldID const directional = env->GetFieldID(clazz, "mDirectional", "Z");
  if (ctor == nullptr || line == nullptr || color == nullptr || destination == nullptr || nextStop == nullptr ||
      key == nullptr || lat == nullptr || lon == nullptr || heading == nullptr || directional == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  jobjectArray array = env->NewObjectArray(static_cast<jsize>(trains.size()), clazz, nullptr);
  if (array == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  for (size_t i = 0; i < trains.size(); ++i)
  {
    jobject item = env->NewObject(clazz, ctor);
    if (item == nullptr)
    {
      env->ExceptionClear();
      return nullptr;
    }
    auto setText = [&](jfieldID field, std::string const & value)
    {
      jni::ScopedLocalRef<jstring> const text(env, jni::ToJavaString(env, value));
      env->SetObjectField(item, field, text.get());
    };
    setText(line, trains[i].m_line);
    setText(color, trains[i].m_color);
    setText(destination, trains[i].m_destination);
    setText(nextStop, trains[i].m_nextStop);
    setText(key, trains[i].m_key);
    env->SetDoubleField(item, lat, trains[i].m_lat);
    env->SetDoubleField(item, lon, trains[i].m_lon);
    env->SetDoubleField(item, heading, trains[i].m_headingDeg);
    env->SetBooleanField(item, directional, trains[i].m_directional ? JNI_TRUE : JNI_FALSE);
    env->SetObjectArrayElement(array, static_cast<jsize>(i), item);
    env->DeleteLocalRef(item);
  }
  return array;
}

std::mutex g_trackMutex;
std::vector<commuter_live::RailTrack> g_tracks;

void CommuterAppendOriented(std::vector<m2::PointD> & dst, std::vector<m2::PointD> const & src)
{
  if (src.empty())
    return;
  bool reversed = false;
  if (!dst.empty())
  {
    double const toFront = dst.back().SquaredLength(src.front());
    double const toBack = dst.back().SquaredLength(src.back());
    reversed = toBack < toFront;
  }
  auto const push = [&](m2::PointD const & point)
  {
    if (!dst.empty() && dst.back().SquaredLength(point) < 1e-12)
      return;
    dst.push_back(point);
  };
  if (!reversed)
    for (auto const & point : src)
      push(point);
  else
    for (auto it = src.rbegin(); it != src.rend(); ++it)
      push(*it);
}

struct CommuterStopPair
{
  routing::transit::StopId m_from = 0;
  routing::transit::StopId m_to = 0;
  routing::transit::LineId m_line = 0;

  bool operator<(CommuterStopPair const & rhs) const
  {
    if (m_line != rhs.m_line)
      return m_line < rhs.m_line;
    if (m_from != rhs.m_from)
      return m_from < rhs.m_from;
    return m_to < rhs.m_to;
  }
};

std::vector<commuter_live::RailTrack> CommuterTracksFromGraph(routing::transit::GraphData const & graph)
{
  using namespace routing::transit;
  std::unordered_map<uint64_t, Stop const *> stops;
  stops.reserve(graph.GetStops().size());
  for (auto const & stop : graph.GetStops())
    stops.emplace(stop.GetId(), &stop);

  std::map<std::pair<uint64_t, uint64_t>, std::vector<m2::PointD> const *> shapes;
  for (auto const & shape : graph.GetShapes())
  {
    if (shape.GetPolyline().size() < 2)
      continue;
    shapes.emplace(std::pair{shape.GetId().GetStop1Id(), shape.GetId().GetStop2Id()}, &shape.GetPolyline());
  }

  std::map<CommuterStopPair, Edge const *> edges;
  for (auto const & edge : graph.GetEdges())
  {
    if (edge.GetTransfer() || edge.GetShapeIds().empty())
      continue;
    CommuterStopPair const forward{edge.GetStop1Id(), edge.GetStop2Id(), edge.GetLineId()};
    CommuterStopPair const backward{edge.GetStop2Id(), edge.GetStop1Id(), edge.GetLineId()};
    for (auto const & key : {forward, backward})
    {
      auto & slot = edges[key];
      if (slot == nullptr || edge.GetShapeIds().size() > slot->GetShapeIds().size())
        slot = &edge;
    }
  }

  std::vector<commuter_live::RailTrack> tracks;
  for (auto const & line : graph.GetLines())
  {
    if (line.GetNumber().empty())
      continue;
    std::vector<m2::PointD> polyline;
    for (auto const & range : line.GetStopIds())
    {
      if (range.empty())
        continue;
      if (auto const found = stops.find(range.front()); found != stops.end())
        CommuterAppendOriented(polyline, {found->second->GetPoint()});
      for (size_t i = 0; i + 1 < range.size(); ++i)
      {
        auto const edgeIt = edges.find(CommuterStopPair{range[i], range[i + 1], line.GetId()});
        if (edgeIt == edges.end())
        {
          if (auto const found = stops.find(range[i + 1]); found != stops.end())
            CommuterAppendOriented(polyline, {found->second->GetPoint()});
          continue;
        }
        std::vector<std::vector<m2::PointD> const *> pending;
        for (auto const & shapeId : edgeIt->second->GetShapeIds())
        {
          auto const shapeIt = shapes.find(std::pair{shapeId.GetStop1Id(), shapeId.GetStop2Id()});
          if (shapeIt != shapes.end())
            pending.push_back(shapeIt->second);
        }
        double constexpr kJoin2 = (200.0 / 111320.0) * (200.0 / 111320.0);
        while (!pending.empty())
        {
          size_t best = 0;
          double bestDist = 1e100;
          for (size_t p = 0; p < pending.size(); ++p)
          {
            double const dist = polyline.empty() ? 0.0
                                                 : std::min(polyline.back().SquaredLength(pending[p]->front()),
                                                            polyline.back().SquaredLength(pending[p]->back()));
            if (dist < bestDist)
            {
              bestDist = dist;
              best = p;
            }
          }
          if (!polyline.empty() && bestDist > kJoin2)
            break;
          CommuterAppendOriented(polyline, *pending[best]);
          pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(best));
        }
      }
    }
    if (polyline.size() < 2)
      continue;
    commuter_live::RailTrack track;
    track.m_ref = line.GetNumber();
    track.m_colored = line.GetType() == "subway";
    track.m_shape.reserve(polyline.size());
    for (auto const & point : polyline)
      track.m_shape.push_back(mercator::ToLatLon(point));
    tracks.push_back(std::move(track));
  }
  return tracks;
}

void AppendRailFeature(FeatureType & feature, std::vector<commuter_live::RailTrack> & tracks)
{
  bool subway = false;
  bool rail = false;
  feature.ForEachType([&](uint32_t type)
  {
    auto const path = classif().GetFullObjectNamePath(type);
    if (path.size() < 2 || path[0] != "railway")
      return;
    if (path[1] == "subway")
      subway = true;
    else if (path[1] == "rail" || path[1] == "light_rail" || path[1] == "narrow_gauge")
      rail = true;
  });
  if (!subway && !rail)
    return;
  int const scale = scales::GetUpperScale();
  feature.ParseGeometry(scale);
  if (feature.GetGeomType() != feature::GeomType::Line || feature.GetPointsCount() < 2)
    return;
  commuter_live::RailTrack track;
  track.m_colored = subway;
  m2::PointD last = feature.GetPoint(0);
  track.m_shape.push_back(mercator::ToLatLon(last));
  for (size_t i = 1; i < feature.GetPointsCount(); ++i)
  {
    m2::PointD const point = feature.GetPoint(i);
    bool const lastPoint = i + 1 == feature.GetPointsCount();
    if (!lastPoint && mercator::DistanceOnEarth(last, point) < 40.0)
      continue;
    last = point;
    track.m_shape.push_back(mercator::ToLatLon(point));
    if (track.m_shape.size() >= 400)
      break;
  }
  if (track.m_shape.size() >= 2)
    tracks.push_back(std::move(track));
}

std::vector<commuter_live::RailTrack> LoadCommuterTracks()
{
  static std::mutex mutex;
  static std::string cachedKey;
  static std::vector<commuter_live::RailTrack> cached;
  std::vector<commuter_live::RailTrack> scheme = commuter_live::SchemeTracks();
  if (!g_framework || g_framework->NativeFramework() == nullptr)
    return scheme;

  auto & dataSource = g_framework->NativeFramework()->GetDataSource();
  std::vector<std::shared_ptr<MwmInfo>> infos;
  dataSource.GetMwmsInfo(infos);
  std::string key;
  for (auto const & info : infos)
    if (info && info->IsRegistered())
      key.append(info->GetCountryName()).append(std::to_string(info->GetVersion())).push_back('\n');
  {
    std::lock_guard<std::mutex> const lock(mutex);
    if (key == cachedKey)
      return cached;
  }

  std::vector<commuter_live::RailTrack> fromMap;
  for (auto const & info : infos)
  {
    if (!info || !info->IsRegistered())
      continue;
    MwmSet::MwmHandle const handle = dataSource.GetMwmHandleById(MwmSet::MwmId(info));
    if (!handle.IsAlive() || handle.GetValue() == nullptr)
      continue;
    MwmValue const & value = *handle.GetValue();
    if (value.m_cont.IsExist(TRANSIT_FILE_TAG))
    {
      try
      {
        auto const reader = value.m_cont.GetReader(TRANSIT_FILE_TAG);
        if (reader.GetPtr() != nullptr)
        {
          routing::transit::GraphData graph;
          graph.DeserializeAll(*reader.GetPtr());
          auto const found = CommuterTracksFromGraph(graph);
          fromMap.insert(fromMap.end(), found.begin(), found.end());
        }
      }
      catch (RootException const & exception)
      {
        LOG(LWARNING, ("Skipping transit section", value.GetCountryFileName(), exception.Msg()));
      }
    }
  }

  m2::RectD box;
  box.Add(mercator::FromLatLon(41.15, 1.45));
  box.Add(mercator::FromLatLon(41.80, 2.80));
  try
  {
    dataSource.ForEachInRect([&](FeatureType & feature)
    {
      if (fromMap.size() > 8000)
        return;
      try
      {
        AppendRailFeature(feature, fromMap);
      }
      catch (RootException const &)
      {}
    }, box, scales::GetUpperScale());
  }
  catch (RootException const & exception)
  {
    LOG(LWARNING, ("Commuter rail scan failed", exception.Msg()));
  }

  // Keep scheme shapes even when the transit graph already paints the line.
  // SharedStrokes prefers the uncoloured scheme and falls back to a coloured
  // subway shape, which is how FGC routes get a stroke.
  std::vector<commuter_live::RailTrack> tracks;
  tracks.reserve(scheme.size() + fromMap.size());
  tracks.insert(tracks.end(), std::make_move_iterator(scheme.begin()), std::make_move_iterator(scheme.end()));
  tracks.insert(tracks.end(), std::make_move_iterator(fromMap.begin()), std::make_move_iterator(fromMap.end()));
  LOG(LINFO, ("Commuter rail tracks", tracks.size()));
  std::lock_guard<std::mutex> const lock(mutex);
  cachedKey = std::move(key);
  cached = tracks;
  return cached;
}

ms::LatLon LerpLatLon(ms::LatLon const & a, ms::LatLon const & b, double t)
{
  return {a.m_lat + (b.m_lat - a.m_lat) * t, a.m_lon + (b.m_lon - a.m_lon) * t};
}

ms::LatLon PointAt(std::vector<ms::LatLon> const & shape, std::vector<double> const & cum, double dist)
{
  if (dist <= 0)
    return shape.front();
  if (dist >= cum.back())
    return shape.back();
  auto const it = std::lower_bound(cum.begin(), cum.end(), dist);
  size_t const index = static_cast<size_t>(it - cum.begin());
  if (index == 0)
    return shape.front();
  double const span = cum[index] - cum[index - 1];
  double const t = span < 1e-3 ? 0 : (dist - cum[index - 1]) / span;
  return LerpLatLon(shape[index - 1], shape[index], t);
}

// Equal bands along the stroke: 50/50, thirds, and so on. Long corridors repeat
// the cycle so every line stays visible instead of owning one half of the city.
void AppendStriped(std::vector<df::MetroTrainStroke> & strokes, std::vector<ms::LatLon> const & shape,
                   std::vector<dp::Color> const & colors)
{
  if (shape.size() < 2 || colors.empty())
    return;
  std::vector<double> cum(shape.size(), 0);
  for (size_t i = 1; i < shape.size(); ++i)
    cum[i] = cum[i - 1] + ms::DistanceOnEarth(shape[i - 1], shape[i]);
  double const total = cum.back();
  if (total < 1)
    return;
  double constexpr kStripeM = 400.0;
  double const stripe =
      total < kStripeM * static_cast<double>(colors.size()) ? total / static_cast<double>(colors.size()) : kStripeM;
  size_t colorIndex = 0;
  for (double start = 0; start < total - 0.5; start += stripe, ++colorIndex)
  {
    double const end = std::min(total, start + stripe);
    df::MetroTrainStroke stroke;
    stroke.m_color = colors[colorIndex % colors.size()];
    stroke.m_mercator.push_back(mercator::FromLatLon(PointAt(shape, cum, start)));
    for (size_t i = 1; i + 1 < shape.size(); ++i)
      if (cum[i] > start && cum[i] < end)
        stroke.m_mercator.push_back(mercator::FromLatLon(shape[i]));
    stroke.m_mercator.push_back(mercator::FromLatLon(PointAt(shape, cum, end)));
    if (stroke.m_mercator.size() >= 2)
      strokes.push_back(std::move(stroke));
  }
}

std::vector<df::MetroTrainStroke> StrokesFor(std::vector<std::string> const & lines)
{
  std::vector<commuter_live::RailTrack> tracks;
  {
    std::lock_guard<std::mutex> const lock(g_trackMutex);
    tracks = g_tracks;
  }
  std::vector<df::MetroTrainStroke> strokes;
  for (auto const & corridor : commuter_live::SharedStrokes(tracks, lines))
  {
    std::vector<dp::Color> colors;
    colors.reserve(corridor.m_lines.size());
    for (auto const & line : corridor.m_lines)
      colors.push_back(ParseCommuterColor(commuter_live::LineColor(line)));
    AppendStriped(strokes, corridor.m_shape, colors);
  }
  return strokes;
}

jobjectArray CommuterToArrivals(JNIEnv * env, std::vector<commuter_live::Arrival> const & arrivals)
{
  jclass const clazz = env->FindClass("app/organicmaps/sdk/commuter_live/CommuterArrival");
  if (clazz == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  jmethodID const ctor = env->GetMethodID(clazz, "<init>", "()V");
  jfieldID const line = env->GetFieldID(clazz, "mLine", "Ljava/lang/String;");
  jfieldID const destination = env->GetFieldID(clazz, "mDestination", "Ljava/lang/String;");
  jfieldID const eta = env->GetFieldID(clazz, "mEtaUnixSec", "J");
  if (ctor == nullptr || line == nullptr || destination == nullptr || eta == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  jobjectArray array = env->NewObjectArray(static_cast<jsize>(arrivals.size()), clazz, nullptr);
  if (array == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  for (size_t i = 0; i < arrivals.size(); ++i)
  {
    jobject item = env->NewObject(clazz, ctor);
    if (item == nullptr)
    {
      env->ExceptionClear();
      return nullptr;
    }
    jni::ScopedLocalRef<jstring> const lineText(env, jni::ToJavaString(env, arrivals[i].m_line));
    jni::ScopedLocalRef<jstring> const destinationText(env, jni::ToJavaString(env, arrivals[i].m_destination));
    env->SetObjectField(item, line, lineText.get());
    env->SetObjectField(item, destination, destinationText.get());
    env->SetLongField(item, eta, static_cast<jlong>(arrivals[i].m_etaUnixSec));
    env->SetObjectArrayElement(array, static_cast<jsize>(i), item);
    env->DeleteLocalRef(item);
  }
  return array;
}

bool FillCommuterArrivals(JNIEnv * env, jobject out, commuter_live::ArrivalList const & list)
{
  jclass const clazz = env->GetObjectClass(out);
  if (clazz == nullptr)
    return false;
  jfieldID const status = env->GetFieldID(clazz, "mStatus", "I");
  jfieldID const arrivals = env->GetFieldID(clazz, "mArrivals", "[Lapp/organicmaps/sdk/commuter_live/CommuterArrival;");
  if (status == nullptr || arrivals == nullptr)
  {
    env->ExceptionClear();
    return false;
  }
  env->SetIntField(out, status, static_cast<jint>(list.m_status));
  jobjectArray array = CommuterToArrivals(env, list.m_arrivals);
  if (array == nullptr)
    return false;
  env->SetObjectField(out, arrivals, array);
  return !env->ExceptionCheck();
}
}  // namespace

extern "C"
{
JNIEXPORT jboolean Java_app_organicmaps_sdk_commuter_1live_CommuterLive_nativeIsEnabled(JNIEnv *, jclass)
{
  return commuter_live::IsCommuterLiveEnabled() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void Java_app_organicmaps_sdk_commuter_1live_CommuterLive_nativeSetEnabled(JNIEnv *, jclass, jboolean enabled)
{
  commuter_live::SetCommuterLiveEnabled(enabled == JNI_TRUE);
}

JNIEXPORT void Java_app_organicmaps_sdk_commuter_1live_CommuterLive_nativeSetTrains(JNIEnv * env, jclass,
                                                                                    jobjectArray trains)
{
  if (!g_framework)
    return;
  std::vector<df::MetroTrainMarker> markers;
  std::vector<std::string> keys;
  if (trains != nullptr)
  {
    jclass const clazz = env->FindClass("app/organicmaps/sdk/commuter_live/CommuterTrain");
    jfieldID const lineField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mLine", "Ljava/lang/String;");
    jfieldID const colorField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mColor", "Ljava/lang/String;");
    jfieldID const keyField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mKey", "Ljava/lang/String;");
    jfieldID const latField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mLat", "D");
    jfieldID const lonField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mLon", "D");
    if (clazz == nullptr || lineField == nullptr || colorField == nullptr || keyField == nullptr ||
        latField == nullptr || lonField == nullptr)
    {
      env->ExceptionClear();
      return;
    }
    jsize const count = env->GetArrayLength(trains);
    markers.reserve(static_cast<size_t>(count));
    keys.reserve(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i)
    {
      jobject const item = env->GetObjectArrayElement(trains, i);
      if (item == nullptr)
        continue;
      jni::ScopedLocalRef<jstring> const lineText(env, static_cast<jstring>(env->GetObjectField(item, lineField)));
      jni::ScopedLocalRef<jstring> const colorText(env, static_cast<jstring>(env->GetObjectField(item, colorField)));
      jni::ScopedLocalRef<jstring> const keyText(env, static_cast<jstring>(env->GetObjectField(item, keyField)));
      df::MetroTrainMarker marker;
      marker.m_mercator =
          mercator::FromLatLon(env->GetDoubleField(item, latField), env->GetDoubleField(item, lonField));
      marker.m_label = lineText.get() == nullptr ? std::string() : jni::ToNativeString(env, lineText.get());
      marker.m_color =
          ParseCommuterColor(colorText.get() == nullptr ? std::string() : jni::ToNativeString(env, colorText.get()));
      markers.push_back(std::move(marker));
      keys.push_back(keyText.get() == nullptr ? std::string() : jni::ToNativeString(env, keyText.get()));
      env->DeleteLocalRef(item);
    }
  }
  std::vector<std::string> lines;
  lines.reserve(markers.size());
  for (auto const & marker : markers)
    if (!marker.m_label.empty())
      lines.push_back(marker.m_label);
  if (auto * framework = frm())
  {
    framework->SetCommuterTrains(std::move(markers), std::move(keys));
    framework->SetCommuterStrokes(StrokesFor(lines));
  }
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_commuter_1live_CommuterLive_nativePoll(JNIEnv * env, jclass, jobject out)
{
  if (out == nullptr)
    return JNI_FALSE;
  try
  {
    auto tracks = LoadCommuterTracks();
    {
      std::lock_guard<std::mutex> const lock(g_trackMutex);
      g_tracks = tracks;
    }
    auto const result = commuter_live::PollCommuter(tracks);
    jclass const clazz = env->GetObjectClass(out);
    if (clazz == nullptr)
      return JNI_FALSE;
    jfieldID const enabled = env->GetFieldID(clazz, "mEnabled", "Z");
    jfieldID const trains = env->GetFieldID(clazz, "mTrains", "[Lapp/organicmaps/sdk/commuter_live/CommuterTrain;");
    if (enabled == nullptr || trains == nullptr)
    {
      env->ExceptionClear();
      return JNI_FALSE;
    }
    env->SetBooleanField(out, enabled, result.m_enabled ? JNI_TRUE : JNI_FALSE);
    jobjectArray trainArray = CommuterToTrains(env, result.m_trains);
    if (trainArray == nullptr)
      return JNI_FALSE;
    env->SetObjectField(out, trains, trainArray);
    return env->ExceptionCheck() ? JNI_FALSE : JNI_TRUE;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Commuter poll failed", exception.what()));
    if (env->ExceptionCheck())
      env->ExceptionClear();
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_commuter_1live_CommuterLive_nativeLookup(JNIEnv * env, jclass, jdouble lat,
                                                                                     jdouble lon, jstring name,
                                                                                     jobject out)
{
  if (out == nullptr)
    return JNI_FALSE;
  try
  {
    std::string place;
    if (name != nullptr)
      place = jni::ToNativeString(env, name);
    auto const list = commuter_live::LookupCommuterArrivals(lat, lon, place);
    return FillCommuterArrivals(env, out, list) ? JNI_TRUE : JNI_FALSE;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Commuter arrivals lookup failed", exception.what()));
    if (env->ExceptionCheck())
      env->ExceptionClear();
    return JNI_FALSE;
  }
}
}
