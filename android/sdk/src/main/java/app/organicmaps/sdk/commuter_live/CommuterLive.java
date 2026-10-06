package app.organicmaps.sdk.commuter_live;

import androidx.annotation.Nullable;

/** Opt-in live FGC and Rodalies positions. Poll blocks, so call it off the UI thread. */
public final class CommuterLive
{
  private CommuterLive() {}

  public static boolean isEnabled()
  {
    return nativeIsEnabled();
  }

  public static void setEnabled(boolean enabled)
  {
    nativeSetEnabled(enabled);
  }

  /** Replaces the commuter markers drawn in the map pass. Null or empty clears that layer only. */
  public static void setTrains(@Nullable CommuterTrain[] trains)
  {
    nativeSetTrains(trains);
  }

  @Nullable
  public static CommuterSnapshot poll()
  {
    CommuterSnapshot snapshot = new CommuterSnapshot();
    if (!nativePoll(snapshot))
      return null;
    if (snapshot.mTrains == null)
      snapshot.mTrains = new CommuterTrain[0];
    return snapshot;
  }

  /** Next trains at a station. Blocks, so call it off the UI thread. */
  @Nullable
  public static CommuterArrivals lookup(double lat, double lon, @Nullable String name)
  {
    CommuterArrivals arrivals = new CommuterArrivals();
    if (!nativeLookup(lat, lon, name == null ? "" : name, arrivals))
      return null;
    if (arrivals.mArrivals == null)
      arrivals.mArrivals = new CommuterArrival[0];
    return arrivals;
  }

  private static native boolean nativeIsEnabled();

  private static native void nativeSetEnabled(boolean enabled);

  private static native void nativeSetTrains(@Nullable CommuterTrain[] trains);

  private static native boolean nativePoll(CommuterSnapshot snapshot);

  private static native boolean nativeLookup(double lat, double lon, String name, CommuterArrivals arrivals);
}
