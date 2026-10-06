#include "commuter_live/service.hpp"

#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "drape_frontend/metro_train_marker.hpp"

#include "drape/color.hpp"

#include "geometry/mercator.hpp"

#include "base/logging.hpp"

#include <exception>
#include <string>
#include <vector>

namespace
{
::Framework * frm()
{
  if (!g_framework)
    return nullptr;
  return g_framework->NativeFramework();
}

dp::Color ParseColor(std::string const & text)
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

jobjectArray ToTrains(JNIEnv * env, std::vector<commuter_live::Train> const & trains)
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
    jfieldID const colorField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mColor", "Ljava/lang/String;");
    jfieldID const keyField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mKey", "Ljava/lang/String;");
    jfieldID const latField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mLat", "D");
    jfieldID const lonField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mLon", "D");
    jfieldID const headingField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mHeadingDeg", "D");
    jfieldID const directionalField = clazz == nullptr ? nullptr : env->GetFieldID(clazz, "mDirectional", "Z");
    if (clazz == nullptr || colorField == nullptr || keyField == nullptr || latField == nullptr ||
        lonField == nullptr || headingField == nullptr || directionalField == nullptr)
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
      marker.m_directional = env->GetBooleanField(item, directionalField) == JNI_TRUE;
      marker.m_color =
          ParseColor(colorText.get() == nullptr ? std::string() : jni::ToNativeString(env, colorText.get()));
      markers.push_back(marker);
      keys.push_back(keyText.get() == nullptr ? std::string() : jni::ToNativeString(env, keyText.get()));
      env->DeleteLocalRef(item);
    }
  }
  frm()->SetCommuterTrains(std::move(markers), std::move(keys));
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_commuter_1live_CommuterLive_nativePoll(JNIEnv * env, jclass, jobject out)
{
  if (out == nullptr)
    return JNI_FALSE;
  try
  {
    auto const result = commuter_live::PollCommuter();
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
    jobjectArray trainArray = ToTrains(env, result.m_trains);
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
}
