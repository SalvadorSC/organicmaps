package app.organicmaps.sdk.commuter_live;

import androidx.annotation.Keep;

/** Place-page arrivals. mStatus is NOT_APPLICABLE, NO_DATA or OK. */
@Keep
public final class CommuterArrivals
{
  public static final int NOT_APPLICABLE = 0;
  public static final int NO_DATA = 1;
  public static final int OK = 2;

  public int mStatus = NOT_APPLICABLE;
  public CommuterArrival[] mArrivals = new CommuterArrival[0];
}
