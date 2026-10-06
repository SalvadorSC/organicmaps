package app.organicmaps.sdk.metro_live;

/** Result of one metro poll. Filled from native code. */
public final class MetroSnapshot
{
  public boolean mEnabled;
  public boolean mNeedsKey;
  public MetroLine[] mLines;
  public MetroTrain[] mTrains;
}
