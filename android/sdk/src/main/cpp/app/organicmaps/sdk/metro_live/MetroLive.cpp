#include "metro_live/service.hpp"

#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "transit/transit_graph_data.hpp"

#include "indexer/mwm_set.hpp"

#include "drape_frontend/metro_train_marker.hpp"

#include "drape/color.hpp"

#include "geometry/mercator.hpp"
#include "geometry/point2d.hpp"

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
void AppendOriented(std::vector<m2::PointD> & dst, std::vector<m2::PointD> const & src)
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

struct StopPair
{
  routing::transit::StopId m_from = 0;
  routing::transit::StopId m_to = 0;
  routing::transit::LineId m_line = 0;

  bool operator<(StopPair const & rhs) const
  {
    if (m_line != rhs.m_line)
      return m_line < rhs.m_line;
    if (m_from != rhs.m_from)
      return m_from < rhs.m_from;
    return m_to < rhs.m_to;
  }
};

// The colored subway stroke is the transit-scheme polyline (a curve through the
// route's stops), not the railway way. Walk each subway route and keep that curve.
std::vector<metro_live::MapTrack> TracksFromGraph(routing::transit::GraphData const & graph)
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

  std::map<StopPair, Edge const *> edges;
  for (auto const & edge : graph.GetEdges())
  {
    if (edge.GetTransfer() || edge.GetShapeIds().empty())
      continue;
    StopPair const forward{edge.GetStop1Id(), edge.GetStop2Id(), edge.GetLineId()};
    StopPair const backward{edge.GetStop2Id(), edge.GetStop1Id(), edge.GetLineId()};
    for (auto const & key : {forward, backward})
    {
      auto & slot = edges[key];
      if (slot == nullptr || edge.GetShapeIds().size() > slot->GetShapeIds().size())
        slot = &edge;
    }
  }

  std::vector<metro_live::MapTrack> tracks;
  for (auto const & line : graph.GetLines())
  {
    if (line.GetType() != "subway" || line.GetNumber().empty())
      continue;
    std::vector<m2::PointD> polyline;
    std::vector<ms::LatLon> anchors;
    for (auto const & range : line.GetStopIds())
    {
      if (range.empty())
        continue;
      if (auto const found = stops.find(range.front()); found != stops.end())
      {
        AppendOriented(polyline, {found->second->GetPoint()});
        anchors.push_back(mercator::ToLatLon(found->second->GetPoint()));
      }
      for (size_t i = 0; i + 1 < range.size(); ++i)
      {
        if (auto const found = stops.find(range[i + 1]); found != stops.end())
          anchors.push_back(mercator::ToLatLon(found->second->GetPoint()));
        auto const edgeIt = edges.find(StopPair{range[i], range[i + 1], line.GetId()});
        if (edgeIt == edges.end())
        {
          if (auto const found = stops.find(range[i + 1]); found != stops.end())
            AppendOriented(polyline, {found->second->GetPoint()});
          continue;
        }
        // Greedily chain the edge's shape pieces so a reversed list cannot
        // insert a chord across the city. Pieces that do not meet the cursor
        // stay out of the polyline.
        std::vector<std::vector<m2::PointD> const *> pending;
        for (auto const & shapeId : edgeIt->second->GetShapeIds())
        {
          auto const shapeIt = shapes.find(std::pair{shapeId.GetStop1Id(), shapeId.GetStop2Id()});
          if (shapeIt != shapes.end())
            pending.push_back(shapeIt->second);
        }
        double constexpr kJoinM = 200.0;
        double constexpr kMercatorPerMeter = 1.0 / 111320.0;
        double const kJoin2 = kJoinM * kMercatorPerMeter * kJoinM * kMercatorPerMeter;
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
          AppendOriented(polyline, *pending[best]);
          pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(best));
        }
      }
    }
    if (polyline.size() < 2)
      continue;
    metro_live::MapTrack track;
    track.m_ref = line.GetNumber();
    track.m_shape.reserve(polyline.size());
    for (auto const & point : polyline)
      track.m_shape.push_back(mercator::ToLatLon(point));
    track.m_stops = std::move(anchors);
    tracks.push_back(std::move(track));
  }
  return tracks;
}

std::vector<metro_live::MapTrack> LoadSubwayTracks()
{
  static std::mutex mutex;
  static std::string cachedKey;
  static std::vector<metro_live::MapTrack> cached;
  if (!g_framework)
    return {};
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

  std::vector<metro_live::MapTrack> tracks;
  for (auto const & info : infos)
  {
    if (!info || !info->IsRegistered())
      continue;
    MwmSet::MwmHandle const handle = dataSource.GetMwmHandleById(MwmSet::MwmId(info));
    if (!handle.IsAlive() || handle.GetValue() == nullptr)
      continue;
    MwmValue const & value = *handle.GetValue();
    if (!value.m_cont.IsExist(TRANSIT_FILE_TAG))
      continue;
    try
    {
      auto const reader = value.m_cont.GetReader(TRANSIT_FILE_TAG);
      if (reader.GetPtr() == nullptr)
        continue;
      routing::transit::GraphData graph;
      graph.DeserializeAll(*reader.GetPtr());
      auto const found = TracksFromGraph(graph);
      tracks.insert(tracks.end(), found.begin(), found.end());
    }
    catch (RootException const & exception)
    {
      LOG(LWARNING, ("Skipping transit section", value.GetCountryFileName(), exception.Msg()));
    }
  }
  LOG(LINFO, ("Subway tracks for metro estimates", tracks.size()));
  std::lock_guard<std::mutex> const lock(mutex);
  cachedKey = std::move(key);
  cached = tracks;
  return cached;
}

jobjectArray ToLines(JNIEnv * env, std::vector<metro_live::LineSummary> const & lines)
{
  jclass const clazz = env->FindClass("app/organicmaps/sdk/metro_live/MetroLine");
  if (clazz == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  jmethodID const ctor = env->GetMethodID(clazz, "<init>", "()V");
  jfieldID const name = env->GetFieldID(clazz, "mName", "Ljava/lang/String;");
  jfieldID const color = env->GetFieldID(clazz, "mColor", "Ljava/lang/String;");
  if (ctor == nullptr || name == nullptr || color == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  jobjectArray array = env->NewObjectArray(static_cast<jsize>(lines.size()), clazz, nullptr);
  if (array == nullptr)
  {
    env->ExceptionClear();
    return nullptr;
  }
  for (size_t i = 0; i < lines.size(); ++i)
  {
    jobject item = env->NewObject(clazz, ctor);
    if (item == nullptr)
    {
      env->ExceptionClear();
      return nullptr;
    }
    jni::ScopedLocalRef<jstring> const nameText(env, jni::ToJavaString(env, lines[i].m_name));
    jni::ScopedLocalRef<jstring> const colorText(env, jni::ToJavaString(env, lines[i].m_color));
    env->SetObjectField(item, name, nameText.get());
    env->SetObjectField(item, color, colorText.get());
    env->SetObjectArrayElement(array, static_cast<jsize>(i), item);
    env->DeleteLocalRef(item);
  }
  return array;
}

jobjectArray ToTrains(JNIEnv * env, std::vector<metro_live::TrainEstimate> const & trains)
{
  jclass const clazz = env->FindClass("app/organicmaps/sdk/metro_live/MetroTrain");
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
  if (ctor == nullptr || line == nullptr || color == nullptr || destination == nullptr || nextStop == nullptr ||
      key == nullptr || lat == nullptr || lon == nullptr || heading == nullptr)
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
    env->SetObjectArrayElement(array, static_cast<jsize>(i), item);
    env->DeleteLocalRef(item);
  }
  return array;
}
}  // namespace

dp::Color ParseMetroColor(std::string const & text)
{
  unsigned value = 0;
  int digits = 0;
  for (char const ch : text)
  {
    unsigned char const c = static_cast<unsigned char>(ch);
    int nibble = -1;
    if (c >= '0' && c <= '9')
      nibble = c - '0';
    else if (c >= 'a' && c <= 'f')
      nibble = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      nibble = c - 'A' + 10;
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

void OnMetroTrainTapped(std::string const & key)
{
  JNIEnv * env = jni::GetEnv();
  if (env == nullptr)
    return;
  jclass const clazz = env->FindClass("app/organicmaps/widget/MetroTrainOverlay");
  if (clazz == nullptr)
  {
    env->ExceptionClear();
    return;
  }
  jmethodID const method = env->GetStaticMethodID(clazz, "onTrainTapped", "(Ljava/lang/String;)V");
  if (method == nullptr)
  {
    env->ExceptionClear();
    return;
  }
  jni::ScopedLocalRef<jstring> const text(env, jni::ToJavaString(env, key));
  env->CallStaticVoidMethod(clazz, method, text.get());
  if (env->ExceptionCheck())
    env->ExceptionClear();
}

extern "C"
{
JNIEXPORT jboolean Java_app_organicmaps_sdk_metro_1live_MetroLive_nativeIsEnabled(JNIEnv *, jclass)
{
  return metro_live::IsMetroLiveEnabled() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void Java_app_organicmaps_sdk_metro_1live_MetroLive_nativeSetEnabled(JNIEnv *, jclass, jboolean enabled)
{
  metro_live::SetMetroLiveEnabled(enabled == JNI_TRUE);
}

JNIEXPORT void Java_app_organicmaps_sdk_metro_1live_MetroLive_nativeSetTrains(JNIEnv * env, jclass, jobjectArray trains)
{
  if (!g_framework)
    return;
  std::vector<df::MetroTrainMarker> markers;
  std::vector<std::string> keys;
  if (trains != nullptr)
  {
    jclass const clazz = env->FindClass("app/organicmaps/sdk/metro_live/MetroTrain");
    jfieldID const colorField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mColor", "Ljava/lang/String;");
    jfieldID const keyField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mKey", "Ljava/lang/String;");
    jfieldID const latField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mLat", "D");
    jfieldID const lonField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mLon", "D");
    jfieldID const headingField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mHeadingDeg", "D");
    if (clazz == nullptr || colorField == nullptr || keyField == nullptr || latField == nullptr ||
        lonField == nullptr || headingField == nullptr)
    {
      env->ExceptionClear();
      return;
    }
    double constexpr kDegToRad = 0.017453292519943295;
    jsize const count = env->GetArrayLength(trains);
    markers.reserve(static_cast<size_t>(count));
    keys.reserve(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i)
    {
      jobject const item = env->GetObjectArrayElement(trains, i);
      if (item == nullptr)
        continue;
      jni::ScopedLocalRef<jstring> const colorText(env, static_cast<jstring>(env->GetObjectField(item, colorField)));
      jni::ScopedLocalRef<jstring> const keyText(env, static_cast<jstring>(env->GetObjectField(item, keyField)));
      df::MetroTrainMarker marker;
      marker.m_mercator =
          mercator::FromLatLon(env->GetDoubleField(item, latField), env->GetDoubleField(item, lonField));
      marker.m_headingRad = static_cast<float>(env->GetDoubleField(item, headingField) * kDegToRad);
      marker.m_color =
          ParseMetroColor(colorText.get() == nullptr ? std::string() : jni::ToNativeString(env, colorText.get()));
      markers.push_back(marker);
      keys.push_back(keyText.get() == nullptr ? std::string() : jni::ToNativeString(env, keyText.get()));
      env->DeleteLocalRef(item);
    }
  }
  frm()->SetMetroTrains(std::move(markers), std::move(keys));
}

JNIEXPORT void Java_app_organicmaps_sdk_metro_1live_MetroLive_nativeSetTapListener(JNIEnv *, jclass, jboolean enabled)
{
  if (!g_framework)
    return;
  if (enabled == JNI_TRUE)
    frm()->SetMetroTrainTapHandler(&OnMetroTrainTapped);
  else
    frm()->SetMetroTrainTapHandler({});
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_metro_1live_MetroLive_nativePoll(JNIEnv * env, jclass, jobject out)
{
  if (out == nullptr)
    return JNI_FALSE;
  try
  {
    auto const result = metro_live::PollMetro(LoadSubwayTracks());
    jclass const clazz = env->GetObjectClass(out);
    if (clazz == nullptr)
      return JNI_FALSE;
    jfieldID const enabled = env->GetFieldID(clazz, "mEnabled", "Z");
    jfieldID const needsKey = env->GetFieldID(clazz, "mNeedsKey", "Z");
    jfieldID const lines = env->GetFieldID(clazz, "mLines", "[Lapp/organicmaps/sdk/metro_live/MetroLine;");
    jfieldID const trains = env->GetFieldID(clazz, "mTrains", "[Lapp/organicmaps/sdk/metro_live/MetroTrain;");
    if (enabled == nullptr || needsKey == nullptr || lines == nullptr || trains == nullptr)
    {
      env->ExceptionClear();
      return JNI_FALSE;
    }
    env->SetBooleanField(out, enabled, result.m_enabled ? JNI_TRUE : JNI_FALSE);
    env->SetBooleanField(out, needsKey, result.m_needsKey ? JNI_TRUE : JNI_FALSE);
    jobjectArray lineArray = ToLines(env, result.m_lines);
    jobjectArray trainArray = ToTrains(env, result.m_trains);
    if (lineArray == nullptr || trainArray == nullptr)
      return JNI_FALSE;
    env->SetObjectField(out, lines, lineArray);
    env->SetObjectField(out, trains, trainArray);
    return env->ExceptionCheck() ? JNI_FALSE : JNI_TRUE;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Metro poll failed", exception.what()));
    if (env->ExceptionCheck())
      env->ExceptionClear();
    return JNI_FALSE;
  }
}
}
