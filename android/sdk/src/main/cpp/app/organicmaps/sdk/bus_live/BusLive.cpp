#include "bus_live/arrivals_service.hpp"

#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "base/logging.hpp"

#include <exception>
#include <string>

namespace
{
jobjectArray ToJavaArrivals(JNIEnv * env, std::vector<bus_live::Arrival> const & arrivals)
{
  jclass const clazz = env->FindClass("app/organicmaps/sdk/bus_live/BusArrival");
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

bool FillArrivals(JNIEnv * env, jobject out, bus_live::ArrivalList const & list)
{
  jclass const clazz = env->GetObjectClass(out);
  if (clazz == nullptr)
    return false;
  jfieldID const status = env->GetFieldID(clazz, "mStatus", "I");
  jfieldID const arrivals = env->GetFieldID(clazz, "mArrivals", "[Lapp/organicmaps/sdk/bus_live/BusArrival;");
  jfieldID const updated = env->GetFieldID(clazz, "mUpdatedUnixSec", "J");
  if (status == nullptr || arrivals == nullptr || updated == nullptr)
  {
    env->ExceptionClear();
    return false;
  }

  env->SetIntField(out, status, static_cast<jint>(list.m_status));
  env->SetLongField(out, updated, static_cast<jlong>(list.m_updatedUnixSec));
  jobjectArray array = ToJavaArrivals(env, list.m_arrivals);
  if (array == nullptr)
    return false;
  env->SetObjectField(out, arrivals, array);
  return !env->ExceptionCheck();
}
}  // namespace

extern "C"
{
JNIEXPORT jboolean Java_app_organicmaps_sdk_bus_1live_BusLive_nativeIsEnabled(JNIEnv *, jclass)
{
  return bus_live::IsBusArrivalsEnabled() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void Java_app_organicmaps_sdk_bus_1live_BusLive_nativeSetEnabled(JNIEnv *, jclass, jboolean enabled)
{
  bus_live::SetBusArrivalsEnabled(enabled == JNI_TRUE);
}

JNIEXPORT jstring Java_app_organicmaps_sdk_bus_1live_BusLive_nativeGetTmbAppId(JNIEnv * env, jclass)
{
  return jni::ToJavaString(env, bus_live::GetTmbCredentials().m_appId);
}

JNIEXPORT jstring Java_app_organicmaps_sdk_bus_1live_BusLive_nativeGetTmbAppKey(JNIEnv * env, jclass)
{
  return jni::ToJavaString(env, bus_live::GetTmbCredentials().m_appKey);
}

JNIEXPORT void Java_app_organicmaps_sdk_bus_1live_BusLive_nativeSetTmbCredentials(JNIEnv * env, jclass, jstring appId,
                                                                                  jstring appKey)
{
  std::string id;
  std::string key;
  if (appId != nullptr)
    id = jni::ToNativeString(env, appId);
  if (appKey != nullptr)
    key = jni::ToNativeString(env, appKey);
  bus_live::SetTmbCredentials(std::move(id), std::move(key));
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_bus_1live_BusLive_nativeLookup(JNIEnv * env, jclass, jdouble lat,
                                                                           jdouble lon, jstring name, jstring ref,
                                                                           jobject out)
{
  if (out == nullptr)
    return JNI_FALSE;
  try
  {
    bus_live::StopQuery query;
    query.m_point = ms::LatLon(lat, lon);
    if (name != nullptr)
      query.m_name = jni::ToNativeString(env, name);
    if (ref != nullptr)
      query.m_ref = jni::ToNativeString(env, ref);
    auto const list = bus_live::LookupArrivals(query);
    return FillArrivals(env, out, list) ? JNI_TRUE : JNI_FALSE;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Bus arrivals lookup failed", exception.what()));
    if (env->ExceptionCheck())
      env->ExceptionClear();
    return JNI_FALSE;
  }
}
}
