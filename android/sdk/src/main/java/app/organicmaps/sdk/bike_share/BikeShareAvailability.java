package app.organicmaps.sdk.bike_share;

import androidx.annotation.Keep;

/** Live counts for one matched dock. Negative optional counts mean the feed did not provide them. */
@Keep
public final class BikeShareAvailability
{
  public int mBikes;
  /** -1 when the feed has no e-bike split. */
  public int mEBikes = -1;
  /** -1 when the feed has no e-bike split. */
  public int mMechanical = -1;
  /** -1 when the feed does not report free docks. */
  public int mDocks = -1;
  public long mLastUpdatedSec;
  public String mStationName = "";
  public String mFeedName = "";
}
