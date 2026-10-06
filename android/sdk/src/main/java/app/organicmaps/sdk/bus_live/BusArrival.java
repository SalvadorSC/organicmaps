package app.organicmaps.sdk.bus_live;

import androidx.annotation.Keep;

/** One upcoming bus. mEtaUnixSec is a unix timestamp in seconds. */
@Keep
public final class BusArrival
{
  public String mLine = "";
  public String mDestination = "";
  public long mEtaUnixSec;
}
