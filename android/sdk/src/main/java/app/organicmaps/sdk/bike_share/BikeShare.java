package app.organicmaps.sdk.bike_share;

import androidx.annotation.Nullable;

/** Opt-in GBFS lookup. The native call blocks, so use it off the UI thread. */
public final class BikeShare
{
  private BikeShare() {}

  public static boolean isEnabled()
  {
    return nativeIsEnabled();
  }

  public static void setEnabled(boolean enabled)
  {
    nativeSetEnabled(enabled);
  }

  @Nullable
  public static BikeShareAvailability lookup(double lat, double lon, @Nullable String name, @Nullable String ref)
  {
    BikeShareAvailability availability = new BikeShareAvailability();
    if (!nativeLookup(lat, lon, name == null ? "" : name, ref == null ? "" : ref, availability))
      return null;
    return availability;
  }

  private static native boolean nativeIsEnabled();

  private static native void nativeSetEnabled(boolean enabled);

  private static native boolean nativeLookup(double lat, double lon, String name, String ref,
                                             BikeShareAvailability availability);
}
