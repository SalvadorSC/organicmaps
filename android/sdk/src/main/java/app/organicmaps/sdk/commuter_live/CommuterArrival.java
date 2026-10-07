package app.organicmaps.sdk.commuter_live;

import androidx.annotation.Keep;

/** One upcoming FGC or Rodalies train. mEtaUnixSec is a unix timestamp in seconds. */
@Keep
public final class CommuterArrival
{
  public String mLine = "";
  public String mDestination = "";
  public long mEtaUnixSec;
}
