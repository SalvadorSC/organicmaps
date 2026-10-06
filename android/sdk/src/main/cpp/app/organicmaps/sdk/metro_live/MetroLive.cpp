#include "metro_live/service.hpp"

#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "base/logging.hpp"

#include <exception>
#include <string>

namespace
{
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
  if (ctor == nullptr || line == nullptr || color == nullptr || destination == nullptr || nextStop == nullptr ||
      key == nullptr || lat == nullptr || lon == nullptr)
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
    env->SetObjectArrayElement(array, static_cast<jsize>(i), item);
    env->DeleteLocalRef(item);
  }
  return array;
}
}  // namespace

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

JNIEXPORT jboolean Java_app_organicmaps_sdk_metro_1live_MetroLive_nativePoll(JNIEnv * env, jclass, jobject out)
{
  if (out == nullptr)
    return JNI_FALSE;
  try
  {
    auto const result = metro_live::PollMetro();
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
