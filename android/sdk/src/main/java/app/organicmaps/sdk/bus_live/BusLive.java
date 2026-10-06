package app.organicmaps.sdk.bus_live;

import androidx.annotation.Nullable;

/** Opt-in Barcelona bus arrivals. The native lookup blocks, so call it off the UI thread. */
public final class BusLive
{
  private BusLive() {}

  public static boolean isEnabled()
  {
    return nativeIsEnabled();
  }

  public static void setEnabled(boolean enabled)
  {
    nativeSetEnabled(enabled);
  }

  public static String getTmbAppId()
  {
    final String value = nativeGetTmbAppId();
    return value == null ? "" : value;
  }

  public static String getTmbAppKey()
  {
    final String value = nativeGetTmbAppKey();
    return value == null ? "" : value;
  }

  public static void setTmbCredentials(@Nullable String appId, @Nullable String appKey)
  {
    nativeSetTmbCredentials(appId == null ? "" : appId, appKey == null ? "" : appKey);
  }

  @Nullable
  public static BusArrivals lookup(double lat, double lon, @Nullable String name, @Nullable String ref)
  {
    BusArrivals arrivals = new BusArrivals();
    if (!nativeLookup(lat, lon, name == null ? "" : name, ref == null ? "" : ref, arrivals))
      return null;
    return arrivals;
  }

  private static native boolean nativeIsEnabled();

  private static native void nativeSetEnabled(boolean enabled);

  private static native String nativeGetTmbAppId();

  private static native String nativeGetTmbAppKey();

  private static native void nativeSetTmbCredentials(String appId, String appKey);

  private static native boolean nativeLookup(double lat, double lon, String name, String ref, BusArrivals arrivals);
}
