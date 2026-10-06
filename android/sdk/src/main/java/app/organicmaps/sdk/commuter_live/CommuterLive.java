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

  private static native boolean nativeIsEnabled();

  private static native void nativeSetEnabled(boolean enabled);

  private static native void nativeSetTrains(@Nullable CommuterTrain[] trains);

  private static native boolean nativePoll(CommuterSnapshot snapshot);
}
