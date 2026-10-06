package app.organicmaps.sdk.metro_live;

import androidx.annotation.Nullable;

/** Opt-in estimated Barcelona metro positions. Poll blocks, so call it off the UI thread. */
public final class MetroLive
{
  private MetroLive() {}

  public static boolean isEnabled()
  {
    return nativeIsEnabled();
  }

  public static void setEnabled(boolean enabled)
  {
    nativeSetEnabled(enabled);
  }

  /** Replaces the arrows drawn in the map pass. Null or empty clears them. */
  public static void setTrains(@Nullable MetroTrain[] trains)
  {
    nativeSetTrains(trains);
  }

  public static void setTapListener(boolean enabled)
  {
    nativeSetTapListener(enabled);
  }

  @Nullable
  public static MetroSnapshot poll()
  {
    MetroSnapshot snapshot = new MetroSnapshot();
    if (!nativePoll(snapshot))
      return null;
    if (snapshot.mLines == null)
      snapshot.mLines = new MetroLine[0];
    if (snapshot.mTrains == null)
      snapshot.mTrains = new MetroTrain[0];
    return snapshot;
  }

  private static native boolean nativeIsEnabled();

  private static native void nativeSetEnabled(boolean enabled);

  private static native void nativeSetTrains(@Nullable MetroTrain[] trains);

  private static native void nativeSetTapListener(boolean enabled);

  private static native boolean nativePoll(MetroSnapshot snapshot);
}
