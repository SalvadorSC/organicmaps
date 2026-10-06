#include "bike_share/availability_service.hpp"

#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "base/logging.hpp"

#include <exception>
#include <optional>
#include <string>

namespace
{
void SetOptionalInt(JNIEnv * env, jobject object, jfieldID field, std::optional<int> const & value)
{
  env->SetIntField(object, field, value ? *value : -1);
}

bool FillAvailability(JNIEnv * env, jobject out, bike_share::Availability const & availability)
{
  jclass const clazz = env->GetObjectClass(out);
  if (clazz == nullptr)
    return false;

  jfieldID const bikes = env->GetFieldID(clazz, "mBikes", "I");
  jfieldID const ebikes = env->GetFieldID(clazz, "mEBikes", "I");
  jfieldID const mechanical = env->GetFieldID(clazz, "mMechanical", "I");
  jfieldID const docks = env->GetFieldID(clazz, "mDocks", "I");
  jfieldID const updated = env->GetFieldID(clazz, "mLastUpdatedSec", "J");
  jfieldID const stationName = env->GetFieldID(clazz, "mStationName", "Ljava/lang/String;");
  jfieldID const feedName = env->GetFieldID(clazz, "mFeedName", "Ljava/lang/String;");
  if (env->ExceptionCheck() || bikes == nullptr || ebikes == nullptr || mechanical == nullptr || docks == nullptr ||
      updated == nullptr || stationName == nullptr || feedName == nullptr)
  {
    env->ExceptionClear();
    return false;
  }

  env->SetIntField(out, bikes, availability.m_bikes);
  SetOptionalInt(env, out, ebikes, availability.m_ebikes);
  SetOptionalInt(env, out, mechanical, availability.m_mechanical);
  SetOptionalInt(env, out, docks, availability.m_docks);
  env->SetLongField(out, updated, static_cast<jlong>(availability.m_lastUpdated));

  jni::ScopedLocalRef<jstring> const station(env, jni::ToJavaString(env, availability.m_stationName));
  jni::ScopedLocalRef<jstring> const feed(env, jni::ToJavaString(env, availability.m_feedName));
  env->SetObjectField(out, stationName, station.get());
  env->SetObjectField(out, feedName, feed.get());
  return !env->ExceptionCheck();
}
}  // namespace

extern "C"
{
JNIEXPORT jboolean Java_app_organicmaps_sdk_bike_1share_BikeShare_nativeIsEnabled(JNIEnv *, jclass)
{
  return bike_share::IsAvailabilityEnabled() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void Java_app_organicmaps_sdk_bike_1share_BikeShare_nativeSetEnabled(JNIEnv *, jclass, jboolean enabled)
{
  bike_share::SetAvailabilityEnabled(enabled == JNI_TRUE);
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_bike_1share_BikeShare_nativeLookup(JNIEnv * env, jclass, jdouble lat,
                                                                               jdouble lon, jstring name, jstring ref,
                                                                               jobject out)
{
  if (out == nullptr)
    return JNI_FALSE;

  try
  {
    bike_share::MatchQuery query;
    query.m_point = ms::LatLon(lat, lon);
    if (name != nullptr)
      query.m_name = jni::ToNativeString(env, name);
    if (ref != nullptr)
      query.m_ref = jni::ToNativeString(env, ref);

    auto const availability = bike_share::LookupAvailability(query);
    if (!availability)
      return JNI_FALSE;
    return FillAvailability(env, out, *availability) ? JNI_TRUE : JNI_FALSE;
  }
  catch (std::exception const & exception)
  {
    LOG(LINFO, ("Bike-share lookup failed", exception.what()));
    if (env->ExceptionCheck())
      env->ExceptionClear();
    return JNI_FALSE;
  }
}
}
